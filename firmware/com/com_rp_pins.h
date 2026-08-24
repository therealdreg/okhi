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
 * RP2040 board pin map. Nothing but macros, so it stays free of Pico SDK
 * includes and any project can pull it in on its own. com_rp.h and com_rp_hw.h
 * both include it, and so does firmware/uart_bridge, which needs the pins
 * without the rest of the okhi hardware layer.
 */

#ifndef __COM_RP_PINS__
#define __COM_RP_PINS__

#define RP_LED_GPIO 26 // From PCB v5

#define GPIO_A 4
#define GPIO_B 5

#define USSEL_PIN 8
#define USOE_PIN 9

#define SPI_SCK_PIN 10
#define SPI_MOSI_PIN 11
#define SPI_MISO_PIN 12
#define SPI_CS_PIN 13

#define DP_SNIFF_GPIO 20
#define DM_SNIFF_GPIO 21

#define EBOOT_MASTERDATAREADY_GPIO 14
#define ELOG_SLAVEREADY_GPIO 15
#define ESP_RESET_GPIO 28

#define ESP_UART_ID uart0
#define ESP_UART_TX_PIN 16
#define ESP_UART_RX_PIN 17

#endif // __COM_RP_PINS__
