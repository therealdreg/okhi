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

// I'm still a novice with the Pico SDK & RP2040, so please bear with me if there are unnecessary things ;-)

// This project assumes that copy_to_ram is enabled, so ALL code is running from RAM

#include "clock.pio.h"
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
#include "hardware/xosc.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define USSEL_PIN 8
#define USOE_PIN 9

int main(void)
{
    const uint gpio_clock_out = 21;
    const float divisor = 1.0f;
    clock_gpio_init(gpio_clock_out, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, divisor);

    /*
    gpio_init(20);
    gpio_set_dir(20, GPIO_OUT);
    gpio_put(20, false);
*/
    gpio_init(USSEL_PIN);
    gpio_set_dir(USSEL_PIN, GPIO_OUT);
    gpio_put(USSEL_PIN, false);
    sleep_ms(500);

    gpio_init(USSEL_PIN);
    gpio_set_dir(USSEL_PIN, GPIO_OUT);
    gpio_put(USSEL_PIN, false);

    sleep_ms(500);

    gpio_init(USOE_PIN);
    gpio_set_dir(USOE_PIN, GPIO_OUT);
    gpio_put(USOE_PIN, true);
    sleep_ms(500);

    gpio_put(USSEL_PIN, true);
    sleep_ms(500);

    gpio_put(USOE_PIN, false);

    sleep_ms(500);
    stdio_init_all();
    sleep_ms(1000);
    printf("\n[clock_out_gpio21] XOSC / %.1f --> %.3f MHz en GPIO %d\n", divisor, 12.0f / divisor, gpio_clock_out);

    while (true)
    {
        tight_loop_contents();
    }
}
