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

#ifndef __COM_COM__
#define __COM_COM__

// uint8_t, for the sequence helpers below.
#include <stdint.h>

#define OKHI_VARIANT_PS2 "ps2"
#define OKHI_VARIANT_USB "usb"

#define OKHI_PACKAGE_TAG_PS2 "PS2"
#define OKHI_PACKAGE_TAG_USB "USB"

#define SPI_FRAME_SIZE 256
#define SPI_FRAME_VERSION 3

#define SPI_FRAME_TYPE_STATUS 1
#define SPI_FRAME_TYPE_DATA 2
#define SPI_FRAME_TYPE_BENCH 3
#define SPI_FRAME_TYPE_UART_STATS 4

#define SPI_CTRL_TYPE_POLL 1
#define SPI_CTRL_TYPE_REQUEST_BLOCK 2
#define SPI_CTRL_TYPE_BENCH 3
#define SPI_CTRL_TYPE_BENCH_RESULT 4
#define SPI_CTRL_TYPE_UART_SETUP 5
#define SPI_CTRL_TYPE_SELFTEST 6

#define SPI_FRAME_MAGIC_BYTES 'O', 'K', 'H', 'I'
#define SPI_CTRL_MAGIC_BYTES 'O', 'K', 'H', 'C'

// What the USB variant's RP sees attached to its capture port, in byte 7 of its control frames
// (a benchmark frame has its step there; older firmware and the PS/2 variant leave it at 0). WIDE
// is a full speed keyboard whose descriptors the RP did not see and that has sent a report longer
// than 8 bytes since its last bus reset; an ESP that predates WIDE reads it as unknown.
#define SPI_CTRL_USB_BUS_OFF 7
#define USB_BUS_UNKNOWN 0
#define USB_BUS_LOW 1
#define USB_BUS_FULL 2
#define USB_BUS_NONE 3
#define USB_BUS_INVALID 4
#define USB_BUS_WIDE 5

// Crystal calibration modes, in which the RP stops capturing and uses the two sniff pins instead:
// GPIO21 carries its crystal clock out, or measures one coming in. The ESP asks for a mode in its
// status frame behind a fresh random token, so the RP never acts on a request twice. The RP
// reports its mode and its console lines in its POLL frames, each line with a sequence: the RP
// sends a line until the ESP acknowledges that sequence, and the ESP stores no line twice.
#define CLOCK_MODE_OFF 0
#define CLOCK_MODE_OUT 1
#define CLOCK_MODE_METER 2
#define SPI_CLOCK_TOKEN_OFF 68
#define SPI_CLOCK_MODE_OFF 72
#define SPI_CLOCK_ACK_OFF 73
#define SPI_CLOCK_OPTS_OFF 74
#define CLOCK_OPT_REF 0x01
#define SPI_CLOCK_GATE_OFF 76
#define SPI_CLOCK_TARGET_OFF 80
#define SPI_CTRL_CLOCK_MODE_OFF 96
#define SPI_CTRL_CLOCK_SEQ_OFF 97
#define SPI_CTRL_CLOCK_LINE_OFF 98
#define CLOCK_LINE_MAX 120

#define SPI_FRAME_LEAD 16
#define SPI_PAYLOAD_FROM_MAGIC 96
#define SPI_BLOCK_PAYLOAD 128
#define SPI_PAYLOAD_OFFSET (SPI_FRAME_LEAD + SPI_PAYLOAD_FROM_MAGIC)
#define SPI_NO_BLOCK 0xFFFFFFFFu

// CRC-32 of a DATA frame's block payload, in bytes 64 to 67 from the magic, so the RP fetches
// again only the block that arrived corrupt, instead of failing the whole image.
#define SPI_BLOCK_CRC_OFF 64

/*
 * Sequenced capture records, so a resend cannot duplicate a keystroke.
 *
 * The RP is the master for capture records and clocks them at an ESP that may have no transaction
 * armed. Nothing detects that: spi_write_blocking() succeeds whether or not anyone is listening,
 * and with no transaction the ESP runs no callback either. Sending again is the only cure, but a
 * blind resend duplicates whatever did land, and the SLAVEREADY handshake cannot tell the two
 * apart: its falling edge says that a transaction ended, not which one. Measured on hardware,
 * tuning the handshake's timeout only trades losses for duplicates.
 *
 * So each record carries its own sequence and the ESP drops a repeat: the RP can send a record
 * again without knowing whether the first copy landed.
 *
 * Wire format of a capture record:
 *
 *     [0] SPI_RECORD_MARKER
 *     [1] sequence, 1..255, never 0
 *     [2..] the record text, ending in CRLF
 *
 * The marker is 0x01 because a record's text starts with a printable character. The ESP drops a
 * frame that does not start with the marker and end with CRLF (spi_garbled): two frames run
 * together, or one shifted. The sequence skips 0 because spi_payload_length() measures to the
 * first NUL, and a 0 there would cut the frame down to its marker.
 */
#define SPI_RECORD_MARKER 0x01
#define SPI_RECORD_SEQ_MIN 1
#define SPI_RECORD_SEQ_MAX 255
#define SPI_RECORD_HEADER 2

/*
 * The sequence of the last record the ESP accepted, echoed in every STATUS frame so the RP can
 * reconcile.
 *
 * The sequence alone stops a resend from duplicating a keystroke, but not from losing one: the RP
 * still has to guess whether a frame landed, and the SLAVEREADY handshake misleads both ways. Its
 * falling edge can come from the previous transaction when the RP's frame was never taken, so the
 * RP moves on and the record is lost. Measured with the sequence alone: 0.28% still lost.
 *
 * With this byte the RP stops guessing. It compares what it has sent with what the ESP says it
 * holds, rewinds its ring cursor over the difference and sends again; the resend costs nothing,
 * since the ESP drops repeats. Byte 63 is the byte just before the block CRC at SPI_BLOCK_CRC_OFF.
 */
#define SPI_STATUS_RECORD_SEQ_OFF 63

/*
 * Sequence arithmetic, shared so both ends agree on what "newer" means. The sequence ring is
 * 1..255, 255 values; 0 means "none yet", and no record carries it.
 *
 * SPI_SEQ_FORWARD_MAX splits the ring in half: a delta of 1..127 is a record the ESP has not seen,
 * 0 the one it holds and 128..254 one it has passed, the RP rewinding and sending again. So the ESP
 * recognises a whole run sent again as duplicates, not only the single record it holds.
 *
 * SPI_SEQ_RESYNC_AFTER is the way out that lets the ESP be strict. The ESP refuses to skip
 * forward and holds its position so the RP can rewind and fill the hole; with no limit, two ends
 * that lost each other would leave the ESP dropping everything for ever, far worse than the loss
 * it prevents. After this many refusals in a row it gives up and resynchronises, counting what it
 * skipped (spi_gaps). The RP reconciles at every poll, at most 4 records apart (USB_SEND_WINDOW,
 * PS2_SEND_WINDOW) and every 50 ms when idle (SPI_POLL_INTERVAL_US), so in normal operation this
 * never fires; it bounds the damage when the RP is old firmware, or wedged.
 */
#define SPI_SEQ_FORWARD_MAX 127
#define SPI_SEQ_RESYNC_AFTER 64

static inline uint8_t spi_seq_next(uint8_t seq)
{
    return (seq >= SPI_RECORD_SEQ_MAX) ? (uint8_t)SPI_RECORD_SEQ_MIN : (uint8_t)(seq + 1);
}

/* Steps forward from a to b around the 1..255 ring, 0..254. */
static inline uint8_t spi_seq_delta(uint8_t a, uint8_t b)
{
    int d = (int)b - (int)a;

    if (d < 0)
    {
        d += SPI_RECORD_SEQ_MAX;
    }

    return (uint8_t)d;
}

/*
 * Do not raise SPI_MAGIC_SEARCH_MAX. The RP looks for the magic up to 32 bytes into the frame,
 * and ota_fetch_block() reads up to SPI_PAYLOAD_FROM_MAGIC + SPI_BLOCK_PAYLOAD bytes past it: the
 * worst case is 32 + 96 + 128 = 256, exactly SPI_FRAME_SIZE. A larger window lets a block read run
 * past the end of the receive buffer.
 */
#define SPI_MAGIC_SEARCH_MAX 32

#define SPI_IDENTITY_OFFSET 16
#define SPI_IDENTITY_MAX 64

#define SPI_FLAG_BENCH_ARMED 0x01
#define SPI_FLAG_SELFTEST_ARMED 0x08
#define SPI_FLAG_RP_IMAGE_READY 0x02
#define SPI_FLAG_RP_COMMIT 0x04
#define SPI_FLAG_RP_RESET 0x10
#define SPI_FLAG_RP_BOOTSEL 0x20

#define SPI_BENCH_MAX_STEPS 40
#define SPI_BENCH_MIN_KHZ 200
#define SPI_BENCH_MAX_KHZ 65000
#define SPI_BENCH_MIN_MS 50
#define SPI_BENCH_MAX_MS 2000
#define SPI_BENCH_MIN_PASS_PERMIL 999

#define SPI_BENCH_MIN_SOAK_MS 2000
#define SPI_BENCH_MAX_SOAK_MS 10800000
#define SPI_BENCH_SOAK_CHUNK_MS 2000
#define SPI_BENCH_NO_STEP 0xFF

#define SPI_BENCH_REFINE_KHZ 1000
#define SPI_BENCH_MAX_STEPDOWNS 8
#define SPI_BENCH_ROWS_PER_REPLY 12

#define SPI_BENCH_PHASE_SWEEP 0
#define SPI_BENCH_PHASE_REFINE 1
#define SPI_BENCH_PHASE_CONFIRM 2

#define SPI_BENCH_KIND_SPI 0
#define SPI_BENCH_KIND_UART 1

#define UART_BENCH_MAGIC0 'O'
#define UART_BENCH_MAGIC1 'K'
#define UART_BENCH_MAGIC2 'U'
#define UART_BENCH_MAGIC3 'B'

#define UART_BENCH_PAYLOAD 48
#define UART_BENCH_SEQ_OFF 4
#define UART_BENCH_DATA_OFF 8
#define UART_BENCH_CRC_OFF (UART_BENCH_DATA_OFF + UART_BENCH_PAYLOAD)
#define UART_BENCH_FRAME_LEN (UART_BENCH_CRC_OFF + 4)
#define UART_BENCH_CRC_FROM UART_BENCH_SEQ_OFF
#define UART_BENCH_CRC_LEN (UART_BENCH_PAYLOAD + 4)

#define UART_BENCH_MIN_BAUD 9600
#define UART_BENCH_MAX_BAUD 3000000
#define UART_BENCH_SETUP_ATTEMPTS 150
#define UART_BENCH_STATS_POLLS 24
#define UART_BENCH_EDGE_SLACK 3

#define SPI_UART_SETUP_BAUD_OFF 80
#define SPI_UART_SETUP_SEED_OFF 84
#define SPI_UART_SETUP_TOKEN_OFF 88
#define SPI_UART_SETUP_FLAGS_OFF 89

#define SPI_UART_STATS_TOKEN_OFF 8
#define SPI_UART_STATS_READY_OFF 9
#define SPI_UART_STATS_RX_OK_OFF 32
#define SPI_UART_STATS_RX_BAD_OFF 36
#define SPI_UART_STATS_RESYNC_OFF 40
#define SPI_UART_STATS_TX_OFF 44
#define SPI_UART_STATS_BAUD_OFF 48

#define SPI_BENCH_STATUS_RUN_OFF 26
#define SPI_BENCH_STATUS_STEPS_OFF 27
#define SPI_BENCH_STATUS_MINKHZ_OFF 28
#define SPI_BENCH_STATUS_MAXKHZ_OFF 30
#define SPI_BENCH_STATUS_STATE_OFF 32
#define SPI_BENCH_STATUS_ACK_OFF 33
#define SPI_BENCH_STATUS_VERDICT_OFF 34
#define SPI_BENCH_STATUS_RACK_OFF 35
#define SPI_BENCH_STATUS_SOAKMS_OFF 36
#define SPI_BENCH_STATUS_DOWN_OFF 40
#define SPI_BENCH_STATUS_OC_OFF 41
#define SPI_BENCH_STATUS_KIND_OFF 42
#define SPI_SELFTEST_STATUS_RUN_OFF 43
#define SPI_SELFTEST_STATUS_OPTS_OFF 44
#define SPI_SELFTEST_STATUS_BLINK_OFF 45
#define SPI_SELFTEST_STATUS_EBOOT_OFF 46

// Byte 46 carries the live MASTERDATAREADY level in bit 0 and a latch of both levels the
// ESP has seen since the self test was armed. The self test reads the latch: the live bit
// alone is always low, because the RP releases the line before it clocks the frame that
// reports it.
#define EBOOT_SEEN_HIGH 0x02
#define EBOOT_SEEN_LOW 0x04

#define SPI_WIFI_MODE_OFF 47
#define SPI_WIFI_IP_OFF 48
#define SPI_UART_MIN_BAUD_OFF 52
#define SPI_UART_MAX_BAUD_OFF 56

// Which sniff line the page asks the RP to probe with a ground wire: 1 for the D+/DATA
// side, 2 for the D-/CLK side, 0 for nothing pending. The ESP clears it when the result
// row comes back or the probe times out, so the RP acts only when the byte leaves 0.
#define SPI_SELFTEST_STATUS_PROBE_OFF 62

// When the ESP re-arms a benchmark that a reset cut short: the kHz to resume the
// descending confirmation from (16 bits, low byte first), so the RP skips the sweep
// and drops straight below the clock that caused the reset. 0 means a fresh run.
#define SPI_BENCH_STATUS_RESUME_OFF 60

#define WIFI_LINK_AP 0
#define WIFI_LINK_STA 1
#define WIFI_LINK_CONNECTING 2

#define SPI_BENCH_STATE_IDLE 0
#define SPI_BENCH_STATE_ARMED 1
#define SPI_BENCH_STATE_RUNNING 2
#define SPI_BENCH_STATE_DONE 3
#define SPI_BENCH_STATE_TIMEOUT 4
#define SPI_BENCH_STATE_ABORTED 5

#define SPI_BENCH_MISO_SEQ_OFF 8
#define SPI_BENCH_MISO_ACK_OFF 12
#define SPI_BENCH_MISO_RUN_OFF 13
#define SPI_BENCH_MISO_VERDICT_OFF 14
#define SPI_BENCH_MISO_RACK_OFF 15
#define SPI_BENCH_MISO_CRC_OFF 16
#define SPI_BENCH_MISO_PATTERN_OFF 24

#define SPI_BENCH_MOSI_STEP_OFF 7
#define SPI_BENCH_MOSI_SEQ_OFF 8
#define SPI_BENCH_MOSI_CRC_OFF 12
#define SPI_BENCH_MOSI_RUN_OFF 79
#define SPI_BENCH_MOSI_PATTERN_OFF 80

#define SPI_BENCH_MOSI_PATTERN_LEN 176
#define SPI_BENCH_MISO_PATTERN_LEN 200
#define SPI_BENCH_TX_VARIANTS 8

#define SPI_BENCH_MOSI_CRC_A_OFF 16
#define SPI_BENCH_MOSI_CRC_A_LEN 208
#define SPI_BENCH_MOSI_CRC_B_OFF 4
#define SPI_BENCH_MOSI_CRC_B_LEN 8

#define SPI_BENCH_MISO_CRC_A_OFF 20
#define SPI_BENCH_MISO_CRC_A_LEN 204
#define SPI_BENCH_MISO_CRC_B_OFF 0
#define SPI_BENCH_MISO_CRC_B_LEN 16


#define SPI_BENCH_RESULT_FLAGS_OFF 8
#define SPI_BENCH_RESULT_TOTAL_OFF 9
#define SPI_BENCH_RESULT_SEQ_OFF 10
#define SPI_BENCH_RESULT_PHASE_OFF 11

#define SPI_BENCH_RESULT_ACTUAL_OFF 80
#define SPI_BENCH_RESULT_TARGET_OFF 84
#define SPI_BENCH_RESULT_FRAMES_OFF 88
#define SPI_BENCH_RESULT_MISO_OK_OFF 92
#define SPI_BENCH_RESULT_MISO_BAD_OFF 96
#define SPI_BENCH_RESULT_XFER_FAIL_OFF 100
#define SPI_BENCH_RESULT_ELAPSED_MS_OFF 104
#define SPI_BENCH_RESULT_OFFMIN_OFF 108
#define SPI_BENCH_RESULT_OFFMAX_OFF 110
#define SPI_BENCH_RESULT_CLKPERI_OFF 112
#define SPI_BENCH_RESULT_STALE_OFF 116
#define SPI_BENCH_RESULT_NATIVE_OFF 120
#define SPI_BENCH_RESULT_FBDIV_OFF 124
#define SPI_BENCH_RESULT_PD1_OFF 125
#define SPI_BENCH_RESULT_PD2_OFF 126
#define SPI_BENCH_RESULT_AUXSRC_OFF 127
#define SPI_BENCH_RESULT_SYSCLK_OFF 128
#define SPI_BENCH_RESULT_MOSI_OK_OFF 136
#define SPI_BENCH_RESULT_MOSI_BAD_OFF 140
#define SPI_BENCH_RESULT_PEER_TX_OFF 144
#define SPI_BENCH_RESULT_KIND_OFF 148

#define SPI_SELFTEST_ID_OFF 152
#define SPI_SELFTEST_STATUS_OFF 153
#define SPI_SELFTEST_FLAGS_OFF 154
#define SPI_SELFTEST_A_OFF 156
#define SPI_SELFTEST_B_OFF 160
#define SPI_SELFTEST_TEXT_OFF 164
#define SPI_SELFTEST_TEXT_MAX 36

#define SPI_SELFTEST_FLAG_LAST 0x01
#define SPI_SELFTEST_FLAG_ABORTED 0x02

#define SPI_SELFTEST_MAX_RESULTS 48
#define SPI_SELFTEST_ROWS_PER_REPLY 8

#define SELFTEST_PASS 0
#define SELFTEST_FAIL 1
#define SELFTEST_WARN 2
#define SELFTEST_SKIP 3
#define SELFTEST_INFO 4
#define SELFTEST_ASK 5

#define SELFTEST_OPT_FLASH 0x01
#define SELFTEST_OPT_LEDS 0x02
#define SELFTEST_OPT_SWITCH 0x04

#define ST_RP_FIRMWARE 0x01
#define ST_RP_HWVER 0x02
#define ST_RP_FLASH_ID 0x03
#define ST_RP_FLASH_SIZE 0x04
#define ST_RP_FLASH_RW 0x05
#define ST_RP_CLK_SYS 0x06
#define ST_RP_CLK_PERI 0x07
#define ST_RP_CLK_USB 0x08
#define ST_RP_CLK_ADC 0x09
#define ST_RP_XOSC 0x0A
#define ST_RP_ROSC 0x0B
#define ST_RP_ADC_PIN 0x0C
#define ST_RP_TEMP 0x0D
#define ST_RP_GPIO_DRIVE 0x0E
#define ST_RP_GPIO_PULL 0x0F
#define ST_RP_GPIO_SHORT 0x10
#define ST_RP_USB_MUX 0x11
#define ST_RP_SNIFF 0x12
#define ST_RP_OTA_META 0x13
#define ST_RP_LED 0x14
#define ST_RP_PIN_STATE 0x15
#define ST_RP_SW_DP 0x16
#define ST_RP_SW_DM 0x17

#define ST_X_EBOOT 0x40
#define ST_X_ELOG 0x41
#define ST_X_SPI_LINK 0x42
#define ST_X_UART_LINK 0x43
#define ST_X_ESP_RESET 0x44

#define ST_ESP_FIRMWARE 0x80
#define ST_ESP_CHIP 0x81
#define ST_ESP_MAC 0x82
#define ST_ESP_FLASH 0x83
#define ST_ESP_HEAP 0x84
#define ST_ESP_RESET_REASON 0x85
#define ST_ESP_PARTITIONS 0x86
#define ST_ESP_SPIFFS 0x87
#define ST_ESP_WIFI 0x88
#define ST_ESP_SPI_COUNTERS 0x89
#define ST_ESP_LED 0x8A
#define ST_ESP_WIFI_SPEED 0x8B

#define SPI_BENCH_RESULT_CRC_OFF 200
#define SPI_BENCH_RESULT_CRC_FROM 4
#define SPI_BENCH_RESULT_CRC_LEN 196




#define SPI_BENCH_RESULT_FLAG_LAST 0x01
#define SPI_BENCH_RESULT_FLAG_ABORTED 0x02
#define SPI_BENCH_RESULT_FLAG_PROGRESS 0x04
#define SPI_BENCH_RESULT_FLAG_SOAK_BEGIN 0x08

#define RP_UART_BAUD 74880

static void spi_bench_fill_pattern(unsigned char *dst, unsigned int len, unsigned int seed)
{
    unsigned int state = seed * 2654435761u + 0x9e3779b9u;

    if (state == 0)
    {
        state = 0x9e3779b9u;
    }

    for (unsigned int i = 0; i < len; ++i)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        dst[i] = (unsigned char)(state >> 24);
    }
}

#endif // __COM_COM__
