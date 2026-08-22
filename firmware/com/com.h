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

#define SPI_FRAME_LEAD 16
#define SPI_PAYLOAD_FROM_MAGIC 96
#define SPI_BLOCK_PAYLOAD 128
#define SPI_PAYLOAD_OFFSET (SPI_FRAME_LEAD + SPI_PAYLOAD_FROM_MAGIC)
#define SPI_NO_BLOCK 0xFFFFFFFFu

/*
 * SPI_MAGIC_SEARCH_MAX CANNOT BE RAISED. ota_fetch_block() copies
 * SPI_PAYLOAD_FROM_MAGIC + up to SPI_BLOCK_PAYLOAD bytes measured from the
 * magic, so the worst case is 32 + 96 + 128 = 256, exactly SPI_FRAME_SIZE.
 * A larger window lets a block read run past the end of the receive buffer.
 */
#define SPI_MAGIC_SEARCH_MAX 32

#define SPI_IDENTITY_OFFSET 16
#define SPI_IDENTITY_MAX 64

#define SPI_FLAG_BENCH_ARMED 0x01
#define SPI_FLAG_SELFTEST_ARMED 0x08
#define SPI_FLAG_RP_IMAGE_READY 0x02
#define SPI_FLAG_RP_COMMIT 0x04
#define SPI_FLAG_RP_RESET 0x10

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

#define SPI_WIFI_MODE_OFF 47
#define SPI_WIFI_IP_OFF 48
#define SPI_UART_MIN_BAUD_OFF 52
#define SPI_UART_MAX_BAUD_OFF 56

// When the ESP re-arms a benchmark after a reset it lost mid-run, this carries
// the kHz to resume the descending confirmation from, so the RP skips the sweep
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
