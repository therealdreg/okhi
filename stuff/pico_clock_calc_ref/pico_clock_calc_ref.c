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
#include "pico_clock_calc_ref.pio.h"
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include <stdio.h>

#define PIN_TEST_IN 1 // GPIO1 (slice 0 channel B)
#define PIN_REF_IN 3  // GPIO3 (slice 1 channel B)
#define SLICE_TEST 0
#define SLICE_REF 1
#define GATE_TIME_MS 5000

static volatile uint32_t ovf_test = 0;
static volatile uint32_t ovf_ref = 0;

static void __isr __time_critical_func(pwm_wrap_isr)()
{
    uint32_t mask = pwm_get_irq_status_mask();
    if (mask & (1u << SLICE_TEST))
    {
        pwm_clear_irq(SLICE_TEST);
        ovf_test++;
    }
    if (mask & (1u << SLICE_REF))
    {
        pwm_clear_irq(SLICE_REF);
        ovf_ref++;
    }
}

static void measure_both(double *f_test, double *f_ref)
{
    irq_set_enabled(PWM_IRQ_WRAP, false);
    ovf_test = ovf_ref = 0;
    pwm_set_counter(SLICE_TEST, 0);
    pwm_set_counter(SLICE_REF, 0);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    absolute_time_t t0 = get_absolute_time();
    sleep_ms(GATE_TIME_MS);
    irq_set_enabled(PWM_IRQ_WRAP, false);
    uint16_t low_test = pwm_get_counter(SLICE_TEST);
    uint16_t low_ref = pwm_get_counter(SLICE_REF);
    uint32_t of_test = ovf_test;
    uint32_t of_ref = ovf_ref;
    irq_set_enabled(PWM_IRQ_WRAP, true);
    absolute_time_t t1 = get_absolute_time();

    uint64_t ticks_test = ((uint64_t)of_test << 16) | low_test;
    uint64_t ticks_ref = ((uint64_t)of_ref << 16) | low_ref;
    double elapsed_s = absolute_time_diff_us(t0, t1) / 1e6;

    *f_test = ticks_test / elapsed_s;
    *f_ref = ticks_ref / elapsed_s;
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);
    stdio_usb_init();

    gpio_set_function(PIN_TEST_IN, GPIO_FUNC_PWM);
    gpio_pull_down(PIN_TEST_IN);
    gpio_set_function(PIN_REF_IN, GPIO_FUNC_PWM);
    gpio_pull_down(PIN_REF_IN);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_RISING);
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, 0xFFFF);

    pwm_init(SLICE_TEST, &cfg, false);
    pwm_init(SLICE_REF, &cfg, false);

    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_isr);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    pwm_clear_irq(SLICE_TEST);
    pwm_clear_irq(SLICE_REF);
    pwm_set_irq_enabled(SLICE_TEST, true);
    pwm_set_irq_enabled(SLICE_REF, true);

    pwm_set_enabled(SLICE_TEST, true);
    pwm_set_enabled(SLICE_REF, true);

    printf("\npico-Freq-Meter with TXCO 12Mhz ref by Dreg\n");
    printf(" Gate = %d ms\n", GATE_TIME_MS);
    printf(" Test signal -> GPIO %d (slice %d)\n", PIN_TEST_IN, SLICE_TEST);
    printf(" Ref  signal -> GPIO %d (slice %d)\n\n", PIN_REF_IN, SLICE_REF);

    const double TXCO_FREQ = 12e6;

    while (true)
    {
        double f_test, f_ref;
        measure_both(&f_test, &f_ref);

        double f_corr = f_test * (TXCO_FREQ / f_ref);

        double ppm = (f_corr - TXCO_FREQ) / TXCO_FREQ * 1e6;

        printf("Measured: Test=%.0f Hz  Ref=%.0f Hz  ", f_test, f_ref);
        printf("Adjusted=%.0f Hz  (%.2f ppm)\n", f_corr, ppm);
    }
    return 0;
}
