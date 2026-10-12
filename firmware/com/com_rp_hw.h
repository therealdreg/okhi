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

#ifndef __COM_RP_HW__
#define __COM_RP_HW__

#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/regs/io_qspi.h"
#include "hardware/spi.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "com.h"
#include "com_rp.h"
#include "com_rp_pins.h"

#define BP() __asm("bkpt #1");

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 256
#endif
#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE 4096
#endif
#define FLASH_TOTAL_SIZE (16 * 1024 * 1024)

#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define SPI_BAUD 5000000
#define SPI_ID spi1

/*
Attempting to achieve the minimum necessary delay for the ESP Slave SPI CS signal
-
90 NOP at 125 MHz = 0.72 us. Our SPI runs at ~5 MHz, so 0.72 us is a delay of approximately 3.6 SPI clock cycles.
Overclocking CPU frequency to 250 MHz reduces NOP execution time to 0.36 us,
corresponding to approximately 1.8 SPI clock cycles.
-
https://github.com/espressif/esp-idf/blob/v5.2.2/examples/peripherals/spi_slave/sender/main/app_main.c
spi_device_interface_config_t devcfg = {
...
        .cs_ena_posttrans = 3,
...
Keep the CS low 3 cycles after transaction,
to stop slave from missing the last bit when CS has less propagation delay than CLK
*/
#define delay_cs()                                                                                                     \
    asm volatile("nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t"           \
                 "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t");

#ifndef delay_cs_pre
#define delay_cs_pre() delay_cs();
#endif

#ifndef delay_cs_pos
#define delay_cs_pos() delay_cs();
#endif

#define CS_LOW()                                                                                                       \
    asm volatile("nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop");                                                  \
    gpio_put(SPI_CS_PIN, false);                                                                                       \
    delay_cs_pre();
#define CS_HIGH()                                                                                                      \
    delay_cs_pos();                                                                                                    \
    gpio_put(SPI_CS_PIN, true);                                                                                        \
    asm volatile("nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop");

typedef enum
{
    VERSION_00 = 0,
    VERSION_01,
    VERSION_10,
    VERSION_11,
    VERSION_FF,
    VERSION_0F,
    VERSION_1F,
    VERSION_F0,
    VERSION_F1,
    VERSION_UNKNOWN
} hw_version_t;

typedef enum
{
    PIN_STATE_LOW = 0,
    PIN_STATE_HIGH,
    PIN_STATE_FLOATING
} pin_state_t;

extern char __flash_binary_end;

static volatile char *hwver_name = "UNKNOWN";
static volatile hw_version_t hwver = VERSION_UNKNOWN;

__attribute__((section(".uninitialized_data"))) uint32_t wait_20;

static pin_state_t get_pin_state(uint gpio)
{
    gpio_init(gpio);
    gpio_pull_up(gpio);
    sleep_ms(5);
    bool pull_up_state = gpio_get(gpio);

    gpio_pull_down(gpio);
    sleep_ms(5);
    bool pull_down_state = gpio_get(gpio);

    gpio_deinit(gpio);

    if (pull_up_state && !pull_down_state)
    {
        return PIN_STATE_FLOATING;
    }

    return pull_up_state ? PIN_STATE_HIGH : PIN_STATE_LOW;
}

static hw_version_t detect_hw_version(void)
{
    pin_state_t state_a = get_pin_state(GPIO_A);
    pin_state_t state_b = get_pin_state(GPIO_B);

    if (state_a == PIN_STATE_FLOATING && state_b == PIN_STATE_FLOATING)
    {
        return VERSION_FF;
    }
    else if (state_a == PIN_STATE_LOW && state_b == PIN_STATE_FLOATING)
    {
        return VERSION_0F;
    }
    else if (state_a == PIN_STATE_HIGH && state_b == PIN_STATE_FLOATING)
    {
        return VERSION_1F;
    }
    else if (state_a == PIN_STATE_FLOATING && state_b == PIN_STATE_LOW)
    {
        return VERSION_F0;
    }
    else if (state_a == PIN_STATE_FLOATING && state_b == PIN_STATE_HIGH)
    {
        return VERSION_F1;
    }
    else if (state_a == PIN_STATE_LOW && state_b == PIN_STATE_LOW)
    {
        return VERSION_00;
    }
    else if (state_a == PIN_STATE_LOW && state_b == PIN_STATE_HIGH)
    {
        return VERSION_01;
    }
    else if (state_a == PIN_STATE_HIGH && state_b == PIN_STATE_LOW)
    {
        return VERSION_10;
    }
    else if (state_a == PIN_STATE_HIGH && state_b == PIN_STATE_HIGH)
    {
        return VERSION_11;
    }
    else
    {
        return VERSION_UNKNOWN;
    }
}

static int init_ver(void)
{
    hwver = detect_hw_version();

    switch (hwver)
    {
        case VERSION_00:
            hwver_name = "00";
            printf("Hardware version: 00\n");
            break;

        case VERSION_01:
            hwver_name = "01";
            printf("Hardware version: 01\n");
            break;

        case VERSION_10:
            hwver_name = "10";
            printf("Hardware version: 10\n");
            break;

        case VERSION_11:
            hwver_name = "11";
            printf("Hardware version: 11\n");
            break;

        case VERSION_FF:
            hwver_name = "FF";
            printf("Hardware version: FF (both floating)\n");
            break;

        case VERSION_0F:
            hwver_name = "0F";
            printf("Hardware version: 0F (A low, B floating)\n");
            break;

        case VERSION_1F:
            hwver_name = "1F";
            printf("Hardware version: 1F (A high, B floating)\n");
            break;

        case VERSION_F0:
            hwver_name = "F0";
            printf("Hardware version: F0 (A floating, B low)\n");
            break;

        case VERSION_F1:
            hwver_name = "F1";
            printf("Hardware version: F1 (A floating, B high)\n");
            break;

        default:
            hwver_name = "UK";
            printf("Hardware version: Unknown\n");
            break;
    }

    return 0;
}


#define RP_FAULT_MAGIC 0xFA017EDCu

typedef struct
{
    uint32_t magic;
    uint32_t pc;
    uint32_t lr;
    uint32_t psr;
    uint32_t sp;
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t core;
    uint32_t count;
} rp_fault_t;

static rp_fault_t rp_fault __attribute__((section(".uninitialized_data")));

static void rp_fault_putc(char c)
{
#ifdef uart_default
    if ((uart_get_hw(uart_default)->cr & UART_UARTCR_UARTEN_BITS) == 0)
    {
        return;
    }

    while ((uart_get_hw(uart_default)->fr & UART_UARTFR_TXFF_BITS) != 0)
    {
        tight_loop_contents();
    }

    uart_get_hw(uart_default)->dr = (uint32_t)(uint8_t)c;
#else
    (void)c;
#endif
}

static void rp_fault_puts(const char *text)
{
    while (*text != '\0')
    {
        rp_fault_putc(*text++);
    }
}

static void rp_fault_hex(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    rp_fault_puts("0x");

    for (int shift = 28; shift >= 0; shift -= 4)
    {
        rp_fault_putc(digits[(value >> shift) & 0xf]);
    }
}

static void rp_fault_line(const char *name, uint32_t value)
{
    rp_fault_puts(name);
    rp_fault_hex(value);
    rp_fault_puts("\r\n");
}

void okhi_fault_report(uint32_t *frame, uint32_t exc_return)
{
    rp_fault.magic = RP_FAULT_MAGIC;
    rp_fault.r0 = frame[0];
    rp_fault.r1 = frame[1];
    rp_fault.r2 = frame[2];
    rp_fault.r3 = frame[3];
    rp_fault.r12 = frame[4];
    rp_fault.lr = frame[5];
    rp_fault.pc = frame[6];
    rp_fault.psr = frame[7];
    rp_fault.sp = (uint32_t)frame;
    rp_fault.core = get_core_num();
    rp_fault.count++;

    (void)exc_return;

    rp_fault_puts("\r\n*** HARD FAULT on core ");
    rp_fault_putc((char)('0' + (rp_fault.core & 1)));
    rp_fault_puts(" ***\r\n");
    rp_fault_line("  pc  ", rp_fault.pc);
    rp_fault_line("  lr  ", rp_fault.lr);
    rp_fault_line("  psr ", rp_fault.psr);
    rp_fault_line("  sp  ", rp_fault.sp);
    rp_fault_puts("rebooting\r\n");

    watchdog_reboot(0, 0, 0);

    while (1)
    {
        tight_loop_contents();
    }
}

void __attribute__((naked)) isr_hardfault(void)
{
    __asm volatile("mov  r1, lr                     \n"
                   "movs r0, #4                     \n"
                   "tst  r0, r1                     \n"
                   "bne  1f                         \n"
                   "mrs  r0, msp                    \n"
                   "b    2f                         \n"
                   "1:                              \n"
                   "mrs  r0, psp                    \n"
                   "2:                              \n"
                   "ldr  r2, =okhi_fault_report     \n"
                   "bx   r2                         \n");
}

static void report_last_fault(void)
{
    if (rp_fault.magic != RP_FAULT_MAGIC)
    {
        return;
    }

    printf("\r\n*** the previous boot ended in a HARD FAULT on core %u, crash %u ***\r\n",
           (unsigned)(rp_fault.core & 1), (unsigned)rp_fault.count);
    printf("    pc %08x  lr %08x  psr %08x  sp %08x\r\n", (unsigned)rp_fault.pc, (unsigned)rp_fault.lr,
           (unsigned)rp_fault.psr, (unsigned)rp_fault.sp);
    printf("    r0 %08x  r1 %08x  r2 %08x  r3 %08x  r12 %08x\r\n", (unsigned)rp_fault.r0, (unsigned)rp_fault.r1,
           (unsigned)rp_fault.r2, (unsigned)rp_fault.r3, (unsigned)rp_fault.r12);

    rp_fault.magic = 0;
}

static int my_spi_write_blocking(const uint8_t *src, size_t len)
{
    CS_LOW();
    int retf = spi_write_blocking(SPI_ID, src, len);
    CS_HIGH();

    return retf;
}

static int my_spi_read_blocking(uint8_t *dst, size_t len)
{
    CS_LOW();
    // repeated_tx_data is output repeatedly on TX as data is read in from RX. Generally this can be 0
    int retf = spi_read_blocking(SPI_ID, 0, dst, len);
    CS_HIGH();

    return retf;
}

static int my_spi_write_read_blocking(const uint8_t *src, uint8_t *dst, size_t len)
{
    CS_LOW();
    int retf = spi_write_read_blocking(SPI_ID, src, dst, len);
    CS_HIGH();

    return retf;
}

static unsigned char *get_base_flash_space_addr(void)
{
    return (unsigned char *)XIP_BASE;
}

static uint32_t get_start_free_flash_space_addr(void)
{
    return (((uint32_t)&__flash_binary_end) + (FLASH_PAGE_SIZE - 1)) & ~(uint32_t)(FLASH_PAGE_SIZE - 1);
}

static uint32_t get_flash_end_address(void)
{
    return ((((((uint32_t)XIP_BASE)) + (PICO_FLASH_SIZE_BYTES - 1)) + (FLASH_PAGE_SIZE - 1)) & ~(FLASH_PAGE_SIZE - 1));
}

static uint32_t get_free_flash_space(void)
{
    return get_flash_end_address() - get_start_free_flash_space_addr();
}

static void erase_flash(void)
{
    flash_range_erase(0, PICO_FLASH_SIZE_BYTES);
    reset_usb_boot(0, 0);
}

static void report_flash_layout(void)
{
    printf("flash free space addr: 0x%08x\r\n"
           "flash end addr: 0x%08x\r\n"
           "flash free space size: 0x%08x bytes\r\n",
           get_start_free_flash_space_addr(), get_flash_end_address(), get_free_flash_space());
}

static void free_all_pio_state_machines(PIO pio)
{
    for (int sm = 0; sm < 4; sm++)
    {
        if (pio_sm_is_claimed(pio, sm))
        {
            pio_sm_unclaim(pio, sm);
        }
    }
}

static void pio_destroy(void)
{
    free_all_pio_state_machines(pio0);
    free_all_pio_state_machines(pio1);
    pio_clear_instruction_memory(pio0);
    pio_clear_instruction_memory(pio1);
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

    // FIX BY DREG: 90 of 500 (18%) let a floating or noisy pin drop the board into the bootloader
    // at power up instead of running. Require a strong majority, a genuinely held button.
    if (x > 450)
    {
        reset_usb_boot(0, 0);
    }
}

void gpio_callback(uint gpio, uint32_t events)
{
    // For devboard :D
    if (gpio == ESP_RESET_GPIO)
    {
        gpio_init(ESP_RESET_GPIO);
        gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
        gpio_init(EBOOT_MASTERDATAREADY_GPIO);
        gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_IN);
        gpio_init(ELOG_SLAVEREADY_GPIO);
        gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);

        wait_20 = 0x69699696;
        puts("\r\nexternal ESP-RESET detected!\r\nrebooting in 50 secs!!!\r\n");
        watchdog_reboot(0, 0, 0);
    }
}

static void delay_boot_if_esp_reset_detected(void)
{
    if (wait_20 == 0x69699696)
    {
        stdio_init_all();
        puts("\r\nwaiting 50 secs...\r\n");
        wait_20 = 0;
        sleep_ms(50000);
    }
}

static void rp_board_boot_init(void)
{
    gpio_init(ESP_RESET_GPIO);
    gpio_set_dir(ESP_RESET_GPIO, GPIO_OUT);
    gpio_put(ESP_RESET_GPIO, false);

    gpio_init(EBOOT_MASTERDATAREADY_GPIO);
    gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_IN);
    gpio_init(ELOG_SLAVEREADY_GPIO);
    gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);

    gpio_init(USSEL_PIN);
    gpio_set_dir(USSEL_PIN, GPIO_OUT);
    gpio_put(USSEL_PIN, false);

    gpio_init(USOE_PIN);
    gpio_set_dir(USOE_PIN, GPIO_OUT);
    gpio_put(USOE_PIN, true);

    init_ver();

    // uart init must be called after init_ver(), because on devboard the same pins are used for UART
    stdio_init_all();

    gpio_put(USSEL_PIN, true);

    sleep_ms(100);
}

static uint32_t rp_spi_master_init(void)
{
    uint32_t baud = spi_init(SPI_ID, SPI_BAUD);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MISO_PIN, GPIO_FUNC_SPI);
    // The CS pin is controlled manually
    gpio_init(SPI_CS_PIN);
    gpio_set_dir(SPI_CS_PIN, GPIO_OUT);
    gpio_put(SPI_CS_PIN, true);
    // SPI mode 0: 8 data bits, MSB first, CPOL=0, CPHA=0
    spi_set_format(SPI_ID, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    printf("Firmware version: v%s\r\n", FIRMV_STR);

    return baud;
}

static void esp_link_uart_init(void)
{
    gpio_set_irq_enabled_with_callback(ESP_RESET_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    uart_init(ESP_UART_ID, RP_UART_BAUD);
    gpio_set_function(ESP_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(ESP_UART_RX_PIN, GPIO_FUNC_UART);
    // UART 8N1: 1 start bit, 8 data bits, no parity bit, 1 stop bit
    uart_set_hw_flow(ESP_UART_ID, false, false);
    uart_set_format(ESP_UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(ESP_UART_ID, false);
    uart_set_irq_enables(ESP_UART_ID, false, false);

    gpio_init(ELOG_SLAVEREADY_GPIO);
    gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);
    gpio_pull_up(ELOG_SLAVEREADY_GPIO);
}

static void esp_link_master_init(void)
{
    gpio_init(EBOOT_MASTERDATAREADY_GPIO);
    gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_OUT);
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
}

static void report_packets_sent(unsigned int total_packets_sended)
{
    // FIX BY DREG: line[32] overflowed once the hex counter reached 8 digits (22 fixed + 2 hwver +
    // 8 + NUL), corrupting the statics below it. Wider buffer, bounded snprintf, and no trailing
    // NUL on the wire (the ESP splits frames by content, a NUL only desynced its parser).
    static unsigned char line[64] = {0};
    static unsigned int last_sended = 0;
    static unsigned int g = 0;
    static unsigned int z = 90000000 + 1;

    if (last_sended != total_packets_sended && g++ > 20000000)
    {
        z = 0;
        g = 0;
        last_sended = total_packets_sended;
        snprintf((char *)line, sizeof(line), "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
        uart_write_blocking(ESP_UART_ID, line, strlen((char *)line));
        puts((char *)line);
    }
    else if (z++ > 90000000)
    {
        z = 0;
        g = 0;
        snprintf((char *)line, sizeof(line), "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
        uart_write_blocking(ESP_UART_ID, line, strlen((char *)line));
        puts((char *)line);
    }
}

#endif // __COM_RP_HW__
