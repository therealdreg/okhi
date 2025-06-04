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
#include "pico_clock_calc.pio.h"
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include <stdio.h>

#define COUNT_SLICE 0
#define PIN_COUNT_IN 1
#define GATE_TIME_MS 1000

static volatile uint32_t g_overflows = 0;

static void __isr __time_critical_func(pwm_wrap_isr)(void)
{
    uint32_t mask = pwm_get_irq_status_mask();
    if (mask & (1u << COUNT_SLICE))
    {
        pwm_clear_irq(COUNT_SLICE);
        g_overflows++;
    }
}

static double measure_frequency(void)
{
    irq_set_enabled(PWM_IRQ_WRAP, false);
    g_overflows = 0;
    pwm_set_counter(COUNT_SLICE, 0);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    absolute_time_t t_start = get_absolute_time();

    sleep_ms(GATE_TIME_MS);

    irq_set_enabled(PWM_IRQ_WRAP, false);
    uint16_t low16 = pwm_get_counter(COUNT_SLICE);
    uint32_t ovfl = g_overflows;
    irq_set_enabled(PWM_IRQ_WRAP, true);

    absolute_time_t t_end = get_absolute_time();

    uint64_t total_ticks = ((uint64_t)ovfl << 16) | low16;
    double gate_sec = absolute_time_diff_us(t_start, t_end) / 1e6;

    return total_ticks / gate_sec;
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);
    stdio_usb_init();

    gpio_set_function(PIN_COUNT_IN, GPIO_FUNC_PWM);
    gpio_pull_down(PIN_COUNT_IN);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_RISING);
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, 0xFFFF);

    pwm_init(COUNT_SLICE, &cfg, false);

    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_isr);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    pwm_clear_irq(COUNT_SLICE);
    pwm_set_irq_enabled(COUNT_SLICE, true);

    pwm_set_enabled(COUNT_SLICE, true);

    printf("\nRP2040 Frequency Meter ready  gate = %d ms\n", GATE_TIME_MS);
    printf("Connect the signal to GPIO%d (PWM slice %d, channel B).\n\n", PIN_COUNT_IN, COUNT_SLICE);

    while (true)
    {
        double f = measure_frequency();
        double ppm = (f - 12e6) / 12e6 * 1e6;
        printf("Freq = %.3f Hz  (%.3f ppm vs 12 MHz)\n", f, ppm);
    }
}
