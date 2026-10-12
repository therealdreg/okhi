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

#ifndef _COM_RP_CLOCK_H__
#define _COM_RP_CLOCK_H__

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Crystal calibration, the web's two clock menus. The implant leaves capture and turns its two
// sniff pins into a clock tool, the console firmwares stuff/clock and stuff/pico_clock_calc:
//
//   CLOCK_MODE_OUT    GPIO21 carries the crystal (XOSC through GPOUT0, divisor 1): on a board with
//                     the 12 MHz TCXO, a clean reference. GPIO20 stays high impedance.
//   CLOCK_MODE_METER  GPIO21 counts rising edges (PWM slice 2, channel B) over a gate timed by the
//                     crystal: on a TCXO board, it calibrates another board's crystal fed to GPIO21.
//                     Only when asked (CLOCK_OPT_REF), GPIO20 drives 12 MHz from the same crystal
//                     (clk_sys / 10), so bridging GPIO20 to GPIO21 must read the target exactly: the
//                     meter checking itself. Otherwise GPIO20 stays high impedance, never driving
//                     against whatever is wired to it.
//
// A mode is entered and left by rebooting, so capture never runs alongside it and every mode starts
// from a clean chip. It lives in RAM the boot leaves alone: a reboot keeps it, a power cut always
// brings the keylogger back. The ESP asks behind a random token; the last token acted on is kept
// with the mode, so a request is never acted on twice.

#define CLOCK_PIN_IO 21
#define CLOCK_PIN_REF 20
#define CLOCK_SYS_KHZ 120000
#define CLOCK_BOOT_MAGIC 0xC10C4B07u
#define CLOCK_QUEUE 16
#define CLOCK_GATE_MIN_MS 100
#define CLOCK_GATE_MAX_MS 600000
#define CLOCK_GATE_DEFAULT_MS 5000
#define CLOCK_TARGET_DEFAULT_HZ 12000000u
#define CLOCK_OUT_PERIOD_US 1000000u
#define CLOCK_WATCHDOG_MS 4000
#define CLOCK_ESP_BOOT_MS 2000
#define CLOCK_GATE_LEAD_US 5000u
#define CLOCK_LINK_GIVEUP_US 60000000u

typedef struct
{
    uint32_t magic;
    uint32_t mode;
    uint32_t gate_ms;
    uint32_t target_hz;
    uint32_t token;
    uint32_t opts;
    uint32_t check;
} clock_boot_t;

__attribute__((section(".uninitialized_data"))) static clock_boot_t clock_boot;

static uint8_t clock_mode_now = CLOCK_MODE_OFF;

static volatile uint32_t esp_clock_token;
static volatile uint8_t esp_clock_mode;
static volatile uint8_t esp_clock_ack;
static volatile uint8_t esp_clock_opts;
static volatile uint32_t esp_clock_gate_ms;
static volatile uint32_t esp_clock_target_hz;

static char clock_queue[CLOCK_QUEUE][CLOCK_LINE_MAX];
static uint8_t clock_queue_seq[CLOCK_QUEUE];
static uint32_t clock_queue_head;
static uint32_t clock_queue_tail;
static uint8_t clock_seq_last;
static uint32_t clock_lines_dropped;

static uint clock_slice;
static volatile uint32_t clock_wraps;

static uint32_t clock_boot_sum(const clock_boot_t *b)
{
    return b->magic ^ b->mode ^ b->gate_ms ^ b->target_hz ^ b->token ^ (b->opts << 7) ^ 0x5A5AA5A5u;
}

static bool clock_boot_valid(void)
{
    return clock_boot.magic == CLOCK_BOOT_MAGIC && clock_boot.check == clock_boot_sum(&clock_boot) &&
           clock_boot.mode <= CLOCK_MODE_METER;
}

static void clock_boot_store(uint32_t mode, uint32_t gate_ms, uint32_t target_hz, uint32_t token, uint32_t opts)
{
    clock_boot.magic = CLOCK_BOOT_MAGIC;
    clock_boot.mode = mode;
    clock_boot.gate_ms = gate_ms;
    clock_boot.target_hz = target_hz;
    clock_boot.token = token;
    clock_boot.opts = opts;
    clock_boot.check = clock_boot_sum(&clock_boot);
}

static void clock_emit(const char *fmt, ...)
{
    char line[CLOCK_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    printf("%s\r\n", line);

    if (clock_queue_head - clock_queue_tail >= CLOCK_QUEUE)
    {
        clock_lines_dropped++;
        return;
    }

    uint32_t slot = clock_queue_head % CLOCK_QUEUE;
    clock_seq_last = (uint8_t)(clock_seq_last == 255 ? 1 : clock_seq_last + 1);
    memcpy(clock_queue[slot], line, CLOCK_LINE_MAX);
    clock_queue_seq[slot] = clock_seq_last;
    clock_queue_head++;
}

static void clock_control_fill(uint8_t *tx)
{
    tx[SPI_CTRL_CLOCK_MODE_OFF] = clock_mode_now;

    if (clock_queue_tail != clock_queue_head)
    {
        uint32_t slot = clock_queue_tail % CLOCK_QUEUE;
        tx[SPI_CTRL_CLOCK_SEQ_OFF] = clock_queue_seq[slot];
        memcpy(tx + SPI_CTRL_CLOCK_LINE_OFF, clock_queue[slot], CLOCK_LINE_MAX);
        tx[SPI_CTRL_CLOCK_LINE_OFF + CLOCK_LINE_MAX - 1] = 0;
    }
}

static void clock_status_parse(const uint8_t *head)
{
    esp_clock_token = spi_get_u32(head + SPI_CLOCK_TOKEN_OFF);
    esp_clock_mode = head[SPI_CLOCK_MODE_OFF];
    esp_clock_ack = head[SPI_CLOCK_ACK_OFF];
    esp_clock_opts = head[SPI_CLOCK_OPTS_OFF];
    esp_clock_gate_ms = spi_get_u32(head + SPI_CLOCK_GATE_OFF);
    esp_clock_target_hz = spi_get_u32(head + SPI_CLOCK_TARGET_OFF);

    if (clock_queue_tail != clock_queue_head && esp_clock_ack == clock_queue_seq[clock_queue_tail % CLOCK_QUEUE])
    {
        clock_queue_tail++;
    }
}

static void clock_request_check(void)
{
    uint32_t token = esp_clock_token;
    uint32_t last = clock_boot_valid() ? clock_boot.token : 0;

    if (token == 0 || token == last || esp_clock_mode > CLOCK_MODE_METER)
    {
        return;
    }

    uint32_t mode = esp_clock_mode;
    uint32_t gate_ms = esp_clock_gate_ms;
    uint32_t target_hz = esp_clock_target_hz;

    if (gate_ms < CLOCK_GATE_MIN_MS || gate_ms > CLOCK_GATE_MAX_MS)
    {
        gate_ms = CLOCK_GATE_DEFAULT_MS;
    }

    if (target_hz == 0)
    {
        target_hz = CLOCK_TARGET_DEFAULT_HZ;
    }

    clock_boot_store(mode, gate_ms, target_hz, token, esp_clock_opts & CLOCK_OPT_REF);

    if (mode == clock_mode_now && mode == CLOCK_MODE_OFF)
    {
        return;
    }

    printf("web requested clock mode %u, rebooting into it\r\n", (unsigned)mode);
    fflush(stdout);
    watchdog_reboot(0, 0, 0);

    while (1)
    {
        tight_loop_contents();
    }
}

static void __isr clock_wrap_isr(void)
{
    if (pwm_get_irq_status_mask() & (1u << clock_slice))
    {
        pwm_clear_irq(clock_slice);
        clock_wraps++;
    }
}

// The edge count at the instant the microsecond timer ticks, and that tick. A gate between two
// snapshots is a whole number of crystal microseconds with no rounding, and both ends take the
// same path, so the few cycles from tick to counter read cancel out: what is left is one edge of
// quantization (0.017 ppm on 12 MHz over 5 s). Timing the gate with time_us_64() reads, as the
// console tool did, rounded each end to the microsecond: +-0.2 ppm of noise at 5 s, 1 ppm at 1 s.
static uint32_t __not_in_flash_func(clock_snapshot)(uint64_t *edges)
{
    uint32_t irq = save_and_disable_interrupts();
    uint32_t prev = timer_hw->timerawl;
    uint32_t tick;

    while ((tick = timer_hw->timerawl) == prev)
    {
    }

    uint32_t low = pwm_hw->slice[clock_slice].ctr;
    uint32_t wraps = clock_wraps;

    // A wrap the handler has not counted yet: the counter has just restarted from zero.
    if ((pwm_hw->intr & (1u << clock_slice)) && low < 0x8000)
    {
        wraps++;
    }

    restore_interrupts(irq);
    *edges = ((uint64_t)wraps << 16) | low;
    return tick;
}

// The snapshot on tick `when` exactly, so a gate lasts exactly the gate time the user asked for.
// Called a few milliseconds early; if the loop was held up past `when`, the gate is just longer and
// the frequency still divides by its true length.
static uint32_t __not_in_flash_func(clock_snapshot_at)(uint32_t when, uint64_t *edges)
{
    while ((int32_t)(timer_hw->timerawl - (when - 1)) < 0)
    {
    }

    return clock_snapshot(edges);
}

static void clock_ref_start(void)
{
    static uint16_t insns[2];
    insns[0] = (uint16_t)(pio_encode_set(pio_pins, 1) | pio_encode_delay(4));
    insns[1] = (uint16_t)(pio_encode_set(pio_pins, 0) | pio_encode_delay(4));
    static const pio_program_t prog = {.instructions = insns, .length = 2, .origin = -1};

    uint offset = pio_add_program(pio0, &prog);
    uint sm = pio_claim_unused_sm(pio0, true);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_set_pins(&c, CLOCK_PIN_REF, 1);
    sm_config_set_wrap(&c, offset, offset + 1);
    sm_config_set_clkdiv(&c, 1.0f);
    pio_gpio_init(pio0, CLOCK_PIN_REF);
    pio_sm_set_consecutive_pindirs(pio0, sm, CLOCK_PIN_REF, 1, true);
    pio_sm_init(pio0, sm, offset, &c);
    pio_sm_set_enabled(pio0, sm, true);
}

static void clock_banner(const char *title)
{
    clock_emit("%s for OKHI v1 by Dreg", title);
    clock_emit("https://github.com/therealdreg/pico-freq-meter-tcxo-ref");
    clock_emit("https://github.com/therealdreg/okhi");
    clock_emit("https://www.rootkit.es");
    clock_emit("X @therealdreg");
}

static void clock_mode_run(void)
{
    clock_mode_now = (uint8_t)clock_boot.mode;

    set_sys_clock_khz(CLOCK_SYS_KHZ, true);
    delay_boot_if_esp_reset_detected();
    rp_board_boot_init();
    rp_spi_master_init();
    gpio_put(USOE_PIN, false);

    printf("\r\nokhi %s clock mode %u, keystroke capture is off\r\nBuild Date %s %s\r\n", RP_VARIANT,
           (unsigned)clock_mode_now, __DATE__, __TIME__);

    // The ESP reads its boot straps (GPIO9 = EBOOT, GPIO8 = ELOG) as it leaves reset: nothing may be
    // driven on them until it has booted, or it comes up in download mode and never answers.
    gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
    gpio_pull_up(ESP_RESET_GPIO);
    sleep_ms(CLOCK_ESP_BOOT_MS);
    esp_link_uart_init();
    esp_link_master_init();

    uint64_t next = 0;
    uint64_t edges0 = 0;
    uint32_t tick0 = 0;
    double target = (double)clock_boot.target_hz;
    uint32_t gate_us = clock_boot.gate_ms * 1000u;

    if (clock_mode_now == CLOCK_MODE_OUT)
    {
        gpio_init(CLOCK_PIN_REF);
        gpio_disable_pulls(CLOCK_PIN_REF);
        clock_gpio_init(CLOCK_PIN_IO, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, 1.0f);
        clock_banner("CLOCK OUTPUT");
    }
    else
    {
        clock_slice = pwm_gpio_to_slice_num(CLOCK_PIN_IO);
        gpio_set_function(CLOCK_PIN_IO, GPIO_FUNC_PWM);
        gpio_pull_down(CLOCK_PIN_IO);
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_RISING);
        pwm_config_set_clkdiv(&cfg, 1.0f);
        pwm_config_set_wrap(&cfg, 0xFFFF);
        pwm_init(clock_slice, &cfg, false);
        irq_set_exclusive_handler(PWM_IRQ_WRAP, clock_wrap_isr);
        pwm_clear_irq(clock_slice);
        pwm_set_irq_enabled(clock_slice, true);
        irq_set_enabled(PWM_IRQ_WRAP, true);
        pwm_set_enabled(clock_slice, true);

        bool ref = (clock_boot.opts & CLOCK_OPT_REF) != 0;

        if (ref)
        {
            clock_ref_start();
        }
        else
        {
            gpio_init(CLOCK_PIN_REF);
            gpio_disable_pulls(CLOCK_PIN_REF);
        }

        clock_banner("pico_freq_meter_tcxo_ref");
        clock_emit("The OKHI WITH TCXO is 12MHz ATXAIG-H12-F-12.000MHz-F25 with +-2.5ppm error");
        clock_emit(" Gate = %u ms", (unsigned)clock_boot.gate_ms);
        clock_emit(" Test signal -> GPIO %d (sniff '-') slice %u, channel B", CLOCK_PIN_IO, clock_slice);
        clock_emit(" Timebase   -> Internal 12 MHz TCXO (XOSC)");

        if (ref)
        {
            clock_emit(" Reference  -> GPIO %d (sniff '+') 12 MHz from XOSC, bridge it to GPIO %d to check the meter",
                       CLOCK_PIN_REF, CLOCK_PIN_IO);
        }
        else
        {
            clock_emit(" GPIO %d (sniff '+') -> high impedance", CLOCK_PIN_REF);
        }
        clock_emit("Target frequency: %.0f Hz", target);
        clock_emit("Gate time: %u ms", (unsigned)clock_boot.gate_ms);
        tick0 = clock_snapshot(&edges0);
    }

    watchdog_enable(CLOCK_WATCHDOG_MS, 0);
    uint64_t link_seen = time_us_64();

    while (1)
    {
        watchdog_update();
        poll_esp_if_due();

        uint64_t now = time_us_64();

        // The web is the only way out of a mode. Without the ESP for a minute nothing can stop it,
        // so go back to capture rather than leave the implant stranded.
        if (esp_link_up)
        {
            link_seen = now;
        }
        else if (now - link_seen > CLOCK_LINK_GIVEUP_US)
        {
            printf("no ESP for %u s in clock mode, back to keystroke capture\r\n", CLOCK_LINK_GIVEUP_US / 1000000u);
            clock_boot_store(CLOCK_MODE_OFF, clock_boot.gate_ms, clock_boot.target_hz, clock_boot.token, 0);
            watchdog_reboot(0, 0, 0);

            while (1)
            {
                tight_loop_contents();
            }
        }

        if (clock_mode_now == CLOCK_MODE_OUT)
        {
            if (now >= next)
            {
                next = now + CLOCK_OUT_PERIOD_US;
                clock_emit("[clock_out_gpio%d] XOSC / 1.0 --> 12.000 MHz GPIO %d (sniff '-')", CLOCK_PIN_IO,
                           CLOCK_PIN_IO);
            }
        }
        else if (timer_hw->timerawl - tick0 >= gate_us - CLOCK_GATE_LEAD_US)
        {
            // Back to back gates: the end of one is the start of the next, nothing is missed between.
            uint64_t edges1;
            uint32_t tick1 = clock_snapshot_at(tick0 + gate_us, &edges1);
            uint64_t edges = edges1 - edges0;
            uint32_t us = tick1 - tick0;
            edges0 = edges1;
            tick0 = tick1;

            double f = (double)edges * 1e6 / (double)us;
            double ppm = 1e6 * (f - target) / target;
            clock_emit("Measured: Test=%.6f Hz | Delta=%+0.3f Hz | Error: %+0.2f ppm", f, f - target, ppm);

            if (edges == 0)
            {
                clock_emit("No edges on GPIO %d: check the signal wire and the ground", CLOCK_PIN_IO);
            }
            else if (ppm > 1000.0 || ppm < -1000.0)
            {
                clock_emit("Over 1000 ppm from the target: check the target frequency, the wiring and the ground");
            }
        }
    }
}

static void clock_mode_boot_check(void)
{
    if (clock_boot_valid() && clock_boot.mode != CLOCK_MODE_OFF)
    {
        clock_mode_run();
    }
}

#endif // _COM_RP_CLOCK_H__
