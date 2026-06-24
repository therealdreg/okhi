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
#include "hardware/pwm.h"
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
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico_clock_calc.pio.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USSEL_PIN 8
#define USOE_PIN 9

#define FW_VERSION "1"

// TEST en GPIO21 -> PWM slice 2 canal B (2B)
#define PIN_TEST_IN 21
#define SLICE_TEST 2

#define GATE_TIME_MS 5000

static int gate_time_ms = GATE_TIME_MS;

static volatile uint32_t ovf_test = 0;

static void __isr __time_critical_func(pwm_wrap_isr)(void)
{
    uint32_t mask = pwm_get_irq_status_mask();
    if (mask & (1u << SLICE_TEST))
    {
        pwm_clear_irq(SLICE_TEST);
        ovf_test++;
    }
}

static void measure_test(double *f_test)
{
    irq_set_enabled(PWM_IRQ_WRAP, false);
    ovf_test = 0;
    pwm_set_counter(SLICE_TEST, 0);
    pwm_clear_irq(SLICE_TEST);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    absolute_time_t t0 = get_absolute_time();
    sleep_ms(gate_time_ms);
    irq_set_enabled(PWM_IRQ_WRAP, false);

    uint16_t low_test = pwm_get_counter(SLICE_TEST);
    uint32_t of_test = ovf_test;

    irq_set_enabled(PWM_IRQ_WRAP, true);
    absolute_time_t t1 = get_absolute_time();

    uint64_t ticks_test = ((uint64_t)of_test << 16) | low_test;
    double elapsed_s = absolute_time_diff_us(t0, t1) / 1e6;

    *f_test = ticks_test / elapsed_s;
}

static bool bootsel_pressed_safely(void)
{
    const uint CS_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[CS_INDEX].ctrl, GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    for (volatile int i = 0; i < 1000; ++i)
    {
        tight_loop_contents();
    }

    bool pressed = !(sio_hw->gpio_hi_in & (1u << CS_INDEX));

    hw_write_masked(&ioqspi_hw->io[CS_INDEX].ctrl, GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);
    return pressed;
}

static void boot_press(void)
{
    int x = 0;
    for (int i = 0; i < 500; i++)
    {
        if (bootsel_pressed_safely())
        {
            x++;
        }
    }
    if (x > 90)
    {
        reset_usb_boot(0, 0);
    }
}

int main(void)
{
    boot_press();
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

    stdio_init_all();
    sleep_ms(2000);
    stdio_usb_init();

    printf("\npico_freq_meter_tcxo_ref for OKHI v%s by Dreg\n"
           "https://github.com/therealdreg/pico-freq-meter-tcxo-ref\n"
           "https://github.com/therealdreg/okhi\n"
           "https://www.rootkit.es\n"
           "X @therealdreg\n\n",
           FW_VERSION);

    printf("The OKHI WITH TCXO is 12MHz ATXAIG-H12-F-12.000MHz-F25 with +-2.5ppm error\n");

    gpio_put(USOE_PIN, false);

    printf(" Gate = %d ms\n", GATE_TIME_MS);
    printf(" Test signal -> GPIO %d (sniff '-') slice %d, channel B)\n", PIN_TEST_IN, SLICE_TEST);
    printf(" Timebase   -> Internal 12 MHz TCXO (XOSC)\n\n");

    gpio_set_function(PIN_TEST_IN, GPIO_FUNC_PWM);
    gpio_pull_down(PIN_TEST_IN);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_RISING); // B rising edge -> +1
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, 0xFFFF);

    pwm_init(SLICE_TEST, &cfg, false);

    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_isr);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    pwm_clear_irq(SLICE_TEST);
    pwm_set_irq_enabled(SLICE_TEST, true);

    pwm_set_enabled(SLICE_TEST, true);

    char gate_buf[16];
    int gpos = 0;
    printf("Enter gate time in ms (ENTER for default %d ms): ", GATE_TIME_MS);
    fflush(stdout);
    memset(gate_buf, 0, sizeof(gate_buf));
    while (1)
    {
        int c = getchar();
        if (c == '\r' || c == '\n' || c == EOF)
        {
            break;
        }
        if (c >= '0' && c <= '9' && gpos < (int)(sizeof(gate_buf) - 1))
        {
            gate_buf[gpos++] = (char)c;
        }
        putchar(c);
    }
    gate_buf[gpos] = '\0';
    putchar('\n');
    if (gpos > 0)
    {
        int gv = atoi(gate_buf);
        if (gv > 0)
        {
            gate_time_ms = gv;
        }
    }

    double target_freq = 12000000.0;
    char input_buf[32];
    int pos = 0;
    printf("Enter target frequency in Hz (Ex: 15000005, ENTER for 12 MHz): ");
    fflush(stdout);
    memset(input_buf, 0, sizeof(input_buf));
    while (1)
    {
        int c = getchar();
        if (c == '\r' || c == '\n' || c == EOF)
        {
            break;
        }
        if (c >= '0' && c <= '9')
        {
            if (pos < (int)(sizeof(input_buf) - 1))
            {
                input_buf[pos++] = (char)c;
            }
            putchar(c);
        }
    }
    input_buf[pos] = '\0';
    putchar('\n');

    if (pos > 0)
    {
        unsigned long long uv = strtoull(input_buf, NULL, 10);
        if (uv > 0)
        {
            target_freq = (double)uv;
        }
    }

    printf("Target frequency: %.0f Hz\n", target_freq);
    printf("Gate time: %d ms\n", gate_time_ms);
    putchar('\n');

    while (true)
    {
        double f_test;
        measure_test(&f_test);
        double ppm = 1000000.0 * (f_test - target_freq) / target_freq;
        double err_hz = f_test - target_freq;
        printf("Measured: Test=%.6f Hz | Delta=%+0.3f Hz | Error: %+0.2f ppm\n", f_test, err_hz, ppm);
    }
    return 0;
}
