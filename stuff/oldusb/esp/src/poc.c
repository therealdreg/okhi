
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "poc.h"

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
#define GPIO_HANDSHAKE 2
#define GPIO_MOSI 12
#define GPIO_MISO 13
#define GPIO_SCLK 15
#define GPIO_CS 14

#elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C2
#define GPIO_HANDSHAKE 3
#define GPIO_MOSI 7
#define GPIO_MISO 2
#define GPIO_SCLK 6
#define GPIO_CS 10

#elif CONFIG_IDF_TARGET_ESP32C6
#define GPIO_HANDSHAKE 15
#define GPIO_MOSI 19
#define GPIO_MISO 20
#define GPIO_SCLK 18
#define GPIO_CS 9

#elif CONFIG_IDF_TARGET_ESP32H2
#define GPIO_HANDSHAKE 2
#define GPIO_MOSI 5
#define GPIO_MISO 0
#define GPIO_SCLK 4
#define GPIO_CS 1

#elif CONFIG_IDF_TARGET_ESP32S3
#define GPIO_HANDSHAKE 2
#define GPIO_MOSI 11
#define GPIO_MISO 13
#define GPIO_SCLK 12
#define GPIO_CS 10

#endif //CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2

#ifdef CONFIG_IDF_TARGET_ESP32
#define RCV_HOST    HSPI_HOST

#else
#define RCV_HOST    SPI2_HOST

#endif

// Buffer para almacenar datos y variables para manejar el buffer
volatile unsigned char buffer[BUFFER_SIZE];
volatile unsigned int bufferIndex = 0;

void spi_task(void *pvParameters)
{
    int n = 0;
    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_MOSI,
        .miso_io_num = GPIO_MISO,
        .sclk_io_num = GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = GPIO_CS,
        .queue_size = 3,
        .flags = 0,
        .post_setup_cb = NULL,  // Callbacks eliminados
        .post_trans_cb = NULL
    };

    gpio_set_pull_mode(GPIO_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_SCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_CS, GPIO_PULLUP_ONLY);

    ret = spi_slave_initialize(RCV_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    assert(ret == ESP_OK);

    WORD_ALIGNED_ATTR unsigned char sendbuf[256] = "";
    WORD_ALIGNED_ATTR unsigned char recvbuf[256] = "";
    spi_slave_transaction_t t;

    while (1) {
        memset(recvbuf, 0x69, sizeof(recvbuf));
        //sprintf(sendbuf, "This is the receiver, sending data for transmission number %04d.", n);

        memset(sendbuf, 0xFF, sizeof(sendbuf));
        t.length = 256 * 8;
        t.tx_buffer = sendbuf;
        t.rx_buffer = recvbuf;
        ret = spi_slave_transmit(RCV_HOST, &t, portMAX_DELAY);

        /*
        printf("\r\nReceived: %s\n", recvbuf);
        for (int i = 0; i < 256; i++)
        {
            printf("%02X ", recvbuf[i]);
        }
        */
        int i;
        for (i = 0; i < 256; i++)
        {
            if (recvbuf[i] >= 0x20 && recvbuf[i] <= 0x7E)
            {
                buffer[(bufferIndex + i) % sizeof(buffer)] = recvbuf[i];
            }
            else
            {
                break;
            }
        }
        bufferIndex += i;
        n++;
    }
}