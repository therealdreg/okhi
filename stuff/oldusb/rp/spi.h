#ifndef _SPI_H_
#define _SPI_H_

/*
Dreg's note:

I've modified this project https://github.com/ataradov/usb-sniffer-lite 
for my own needs for the OKHI keylogger. I've added a lot of code that's 
pretty much junk and it kind of works... I'll improve it in the future.

Copyright (c) [2024] by David Reguera Garcia aka Dreg 
https://github.com/therealdreg/okhi
https://www.rootkit.es
X @therealdreg
dreg@rootkit.es

Thx Alex Taradov <alex@taradov.com> for the original work!
*/

#include <stdint.h>
#include <stdbool.h>
#include "hal_gpio.h"
#include "conf.h"

#define _REG_(x)
#define _u(x) x ## u
#define REG_ALIAS_SET_BITS (0x2u << 12u)
#define REG_ALIAS_CLR_BITS (0x3u << 12u)
#define REG_ALIAS_XOR_BITS (0x1u << 12u)
#define RESETS_RESET_SPI1_RESET  _u(0x1)
#define RESETS_RESET_SPI1_BITS   _u(0x00020000)
#define RESETS_RESET_SPI1_MSB    _u(17)
#define RESETS_RESET_SPI1_LSB    _u(17)
#define RESETS_RESET_SPI1_ACCESS "RW"
#define RESETS_RESET_SPI0_RESET  _u(0x1)
#define RESETS_RESET_SPI0_BITS   _u(0x00010000)
#define RESETS_RESET_SPI0_MSB    _u(16)
#define RESETS_RESET_SPI0_LSB    _u(16)
#define RESETS_RESET_SPI0_ACCESS "RW"
#define SPI_SSPCR1_SSE_RESET  _u(0x0)
#define SPI_SSPCR1_SSE_BITS   _u(0x00000002)
#define SPI_SSPCR1_SSE_MSB    _u(1)
#define SPI_SSPCR1_SSE_LSB    _u(1)
#define SPI_SSPCR1_SSE_ACCESS "RW"
#define SPI_SSPCR0_SCR_RESET  _u(0x00)
#define SPI_SSPCR0_SCR_BITS   _u(0x0000ff00)
#define SPI_SSPCR0_SCR_MSB    _u(15)
#define SPI_SSPCR0_SCR_LSB    _u(8)
#define SPI_SSPCR0_SCR_ACCESS "RW"
#define SPI_SSPCR0_DSS_RESET  _u(0x0)
#define SPI_SSPCR0_DSS_BITS   _u(0x0000000f)
#define SPI_SSPCR0_DSS_MSB    _u(3)
#define SPI_SSPCR0_DSS_LSB    _u(0)
#define SPI_SSPCR0_DSS_ACCESS "RW"
#define SPI_SSPCR0_SPO_RESET  _u(0x0)
#define SPI_SSPCR0_SPO_BITS   _u(0x00000040)
#define SPI_SSPCR0_SPO_MSB    _u(6)
#define SPI_SSPCR0_SPO_LSB    _u(6)
#define SPI_SSPCR0_SPO_ACCESS "RW"
#define SPI_SSPCR0_SPH_RESET  _u(0x0)
#define SPI_SSPCR0_SPH_BITS   _u(0x00000080)
#define SPI_SSPCR0_SPH_MSB    _u(7)
#define SPI_SSPCR0_SPH_LSB    _u(7)
#define SPI_SSPCR0_SPH_ACCESS "RW"
#define SPI_SSPDMACR_TXDMAE_RESET  _u(0x0)
#define SPI_SSPDMACR_TXDMAE_BITS   _u(0x00000002)
#define SPI_SSPDMACR_TXDMAE_MSB    _u(1)
#define SPI_SSPDMACR_TXDMAE_LSB    _u(1)
#define SPI_SSPDMACR_TXDMAE_ACCESS "RW"
#define SPI_SSPDMACR_RXDMAE_RESET  _u(0x0)
#define SPI_SSPDMACR_RXDMAE_BITS   _u(0x00000001)
#define SPI_SSPDMACR_RXDMAE_MSB    _u(0)
#define SPI_SSPDMACR_RXDMAE_LSB    _u(0)
#define SPI_SSPDMACR_RXDMAE_ACCESS "RW"
#define NUM_BANK0_GPIOS _u(30)
#define PADS_BANK0_GPIO0_IE_RESET  _u(0x1)
#define PADS_BANK0_GPIO0_IE_BITS   _u(0x00000040)
#define PADS_BANK0_GPIO0_IE_MSB    _u(6)
#define PADS_BANK0_GPIO0_IE_LSB    _u(6)
#define PADS_BANK0_GPIO0_IE_ACCESS "RW"
#define PADS_BANK0_GPIO0_OD_RESET  _u(0x0)
#define PADS_BANK0_GPIO0_OD_BITS   _u(0x00000080)
#define PADS_BANK0_GPIO0_OD_MSB    _u(7)
#define PADS_BANK0_GPIO0_OD_LSB    _u(7)
#define PADS_BANK0_GPIO0_OD_ACCESS "RW"
#define IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB                              _u(0)
#define SPI_SSPSR_TNF_RESET  _u(0x1)
#define SPI_SSPSR_TNF_BITS   _u(0x00000002)
#define SPI_SSPSR_TNF_MSB    _u(1)
#define SPI_SSPSR_TNF_LSB    _u(1)
#define SPI_SSPSR_TNF_ACCESS "RO"
#define SPI_SSPSR_RNE_RESET  _u(0x0)
#define SPI_SSPSR_RNE_BITS   _u(0x00000004)
#define SPI_SSPSR_RNE_MSB    _u(2)
#define SPI_SSPSR_RNE_LSB    _u(2)
#define SPI_SSPSR_RNE_ACCESS "RO"
#define hw_set_alias_untyped(addr) ((void *)(REG_ALIAS_SET_BITS | hw_alias_check_addr(addr)))
#define hw_clear_alias_untyped(addr) ((void *)(REG_ALIAS_CLR_BITS | hw_alias_check_addr(addr)))
#define hw_xor_alias_untyped(addr) ((void *)(REG_ALIAS_XOR_BITS | hw_alias_check_addr(addr)))
#define hw_alias_check_addr(addr) ((uintptr_t)(addr))

typedef volatile uint32_t io_rw_32;
typedef const volatile uint32_t io_ro_32;
typedef struct spi_inst spi_inst_t;
typedef unsigned int uint;

enum clock_index {
    clk_gpout0 = 0,     ///< GPIO Muxing 0
    clk_gpout1,         ///< GPIO Muxing 1
    clk_gpout2,         ///< GPIO Muxing 2
    clk_gpout3,         ///< GPIO Muxing 3
    clk_ref,            ///< Watchdog and timers reference clock
    clk_sys,            ///< Processors, bus fabric, memory, memory mapped registers
    clk_peri,           ///< Peripheral clock for UART and SPI
    clk_usb,            ///< USB clock
    clk_adc,            ///< ADC clock
    clk_rtc,            ///< Real time clock
    CLK_COUNT
};

typedef enum {
    SPI_CPOL_0 = 0,
    SPI_CPOL_1 = 1
} spi_cpol_t;

typedef enum {
    SPI_LSB_FIRST = 0,
    SPI_MSB_FIRST = 1
} spi_order_t;

typedef enum {
    SPI_CPHA_0 = 0,
    SPI_CPHA_1 = 1
} spi_cpha_t;

enum gpio_function {
    GPIO_FUNC_XIP = 0,
    GPIO_FUNC_SPI = 1,
    GPIO_FUNC_UART = 2,
    GPIO_FUNC_I2C = 3,
    GPIO_FUNC_PWM = 4,
    GPIO_FUNC_SIO = 5,
    GPIO_FUNC_PIO0 = 6,
    GPIO_FUNC_PIO1 = 7,
    GPIO_FUNC_GPCK = 8,
    GPIO_FUNC_USB = 9,
    GPIO_FUNC_NULL = 0x1f,
};

typedef struct {
    _REG_(SPI_SSPCR0_OFFSET) // SPI_SSPCR0
    // Control register 0, SSPCR0 on page 3-4
    // 0x0000ff00 [15:8]  : SCR (0): Serial clock rate
    // 0x00000080 [7]     : SPH (0): SSPCLKOUT phase, applicable to Motorola SPI frame format only
    // 0x00000040 [6]     : SPO (0): SSPCLKOUT polarity, applicable to Motorola SPI frame format only
    // 0x00000030 [5:4]   : FRF (0): Frame format: 00 Motorola SPI frame format
    // 0x0000000f [3:0]   : DSS (0): Data Size Select: 0000 Reserved, undefined operation
    io_rw_32 cr0;

    _REG_(SPI_SSPCR1_OFFSET) // SPI_SSPCR1
    // Control register 1, SSPCR1 on page 3-5
    // 0x00000008 [3]     : SOD (0): Slave-mode output disable
    // 0x00000004 [2]     : MS (0): Master or slave mode select
    // 0x00000002 [1]     : SSE (0): Synchronous serial port enable: 0 SSP operation disabled
    // 0x00000001 [0]     : LBM (0): Loop back mode: 0 Normal serial port operation enabled
    io_rw_32 cr1;

    _REG_(SPI_SSPDR_OFFSET) // SPI_SSPDR
    // Data register, SSPDR on page 3-6
    // 0x0000ffff [15:0]  : DATA (0): Transmit/Receive FIFO: Read Receive FIFO
    io_rw_32 dr;

    _REG_(SPI_SSPSR_OFFSET) // SPI_SSPSR
    // Status register, SSPSR on page 3-7
    // 0x00000010 [4]     : BSY (0): PrimeCell SSP busy flag, RO: 0 SSP is idle
    // 0x00000008 [3]     : RFF (0): Receive FIFO full, RO: 0 Receive FIFO is not full
    // 0x00000004 [2]     : RNE (0): Receive FIFO not empty, RO: 0 Receive FIFO is empty
    // 0x00000002 [1]     : TNF (1): Transmit FIFO not full, RO: 0 Transmit FIFO is full
    // 0x00000001 [0]     : TFE (1): Transmit FIFO empty, RO: 0 Transmit FIFO is not empty
    io_ro_32 sr;

    _REG_(SPI_SSPCPSR_OFFSET) // SPI_SSPCPSR
    // Clock prescale register, SSPCPSR on page 3-8
    // 0x000000ff [7:0]   : CPSDVSR (0): Clock prescale divisor
    io_rw_32 cpsr;

    _REG_(SPI_SSPIMSC_OFFSET) // SPI_SSPIMSC
    // Interrupt mask set or clear register, SSPIMSC on page 3-9
    // 0x00000008 [3]     : TXIM (0): Transmit FIFO interrupt mask: 0 Transmit FIFO half empty or less condition interrupt is masked
    // 0x00000004 [2]     : RXIM (0): Receive FIFO interrupt mask: 0 Receive FIFO half full or less condition interrupt is masked
    // 0x00000002 [1]     : RTIM (0): Receive timeout interrupt mask: 0 Receive FIFO not empty and no read prior to timeout...
    // 0x00000001 [0]     : RORIM (0): Receive overrun interrupt mask: 0 Receive FIFO written to while full condition...
    io_rw_32 imsc;

    _REG_(SPI_SSPRIS_OFFSET) // SPI_SSPRIS
    // Raw interrupt status register, SSPRIS on page 3-10
    // 0x00000008 [3]     : TXRIS (1): Gives the raw interrupt state, prior to masking, of the SSPTXINTR interrupt
    // 0x00000004 [2]     : RXRIS (0): Gives the raw interrupt state, prior to masking, of the SSPRXINTR interrupt
    // 0x00000002 [1]     : RTRIS (0): Gives the raw interrupt state, prior to masking, of the SSPRTINTR interrupt
    // 0x00000001 [0]     : RORRIS (0): Gives the raw interrupt state, prior to masking, of the SSPRORINTR interrupt
    io_ro_32 ris;

    _REG_(SPI_SSPMIS_OFFSET) // SPI_SSPMIS
    // Masked interrupt status register, SSPMIS on page 3-11
    // 0x00000008 [3]     : TXMIS (0): Gives the transmit FIFO masked interrupt state, after masking, of the SSPTXINTR interrupt
    // 0x00000004 [2]     : RXMIS (0): Gives the receive FIFO masked interrupt state, after masking, of the SSPRXINTR interrupt
    // 0x00000002 [1]     : RTMIS (0): Gives the receive timeout masked interrupt state, after masking, of the SSPRTINTR interrupt
    // 0x00000001 [0]     : RORMIS (0): Gives the receive over run masked interrupt status, after masking, of the...
    io_ro_32 mis;

    _REG_(SPI_SSPICR_OFFSET) // SPI_SSPICR
    // Interrupt clear register, SSPICR on page 3-11
    // 0x00000002 [1]     : RTIC (0): Clears the SSPRTINTR interrupt
    // 0x00000001 [0]     : RORIC (0): Clears the SSPRORINTR interrupt
    io_rw_32 icr;

    _REG_(SPI_SSPDMACR_OFFSET) // SPI_SSPDMACR
    // DMA control register, SSPDMACR on page 3-12
    // 0x00000002 [1]     : TXDMAE (0): Transmit DMA Enable
    // 0x00000001 [0]     : RXDMAE (0): Receive DMA Enable
    io_rw_32 dmacr;
} spi_hw_t;

typedef struct {
    _REG_(RESETS_RESET_OFFSET) // RESETS_RESET
    // Reset control
    // 0x01000000 [24]    : usbctrl (1)
    // 0x00800000 [23]    : uart1 (1)
    // 0x00400000 [22]    : uart0 (1)
    // 0x00200000 [21]    : timer (1)
    // 0x00100000 [20]    : tbman (1)
    // 0x00080000 [19]    : sysinfo (1)
    // 0x00040000 [18]    : syscfg (1)
    // 0x00020000 [17]    : spi1 (1)
    // 0x00010000 [16]    : spi0 (1)
    // 0x00008000 [15]    : rtc (1)
    // 0x00004000 [14]    : pwm (1)
    // 0x00002000 [13]    : pll_usb (1)
    // 0x00001000 [12]    : pll_sys (1)
    // 0x00000800 [11]    : pio1 (1)
    // 0x00000400 [10]    : pio0 (1)
    // 0x00000200 [9]     : pads_qspi (1)
    // 0x00000100 [8]     : pads_bank0 (1)
    // 0x00000080 [7]     : jtag (1)
    // 0x00000040 [6]     : io_qspi (1)
    // 0x00000020 [5]     : io_bank0 (1)
    // 0x00000010 [4]     : i2c1 (1)
    // 0x00000008 [3]     : i2c0 (1)
    // 0x00000004 [2]     : dma (1)
    // 0x00000002 [1]     : busctrl (1)
    // 0x00000001 [0]     : adc (1)
    io_rw_32 reset;

    _REG_(RESETS_WDSEL_OFFSET) // RESETS_WDSEL
    // Watchdog select
    // 0x01000000 [24]    : usbctrl (0)
    // 0x00800000 [23]    : uart1 (0)
    // 0x00400000 [22]    : uart0 (0)
    // 0x00200000 [21]    : timer (0)
    // 0x00100000 [20]    : tbman (0)
    // 0x00080000 [19]    : sysinfo (0)
    // 0x00040000 [18]    : syscfg (0)
    // 0x00020000 [17]    : spi1 (0)
    // 0x00010000 [16]    : spi0 (0)
    // 0x00008000 [15]    : rtc (0)
    // 0x00004000 [14]    : pwm (0)
    // 0x00002000 [13]    : pll_usb (0)
    // 0x00001000 [12]    : pll_sys (0)
    // 0x00000800 [11]    : pio1 (0)
    // 0x00000400 [10]    : pio0 (0)
    // 0x00000200 [9]     : pads_qspi (0)
    // 0x00000100 [8]     : pads_bank0 (0)
    // 0x00000080 [7]     : jtag (0)
    // 0x00000040 [6]     : io_qspi (0)
    // 0x00000020 [5]     : io_bank0 (0)
    // 0x00000010 [4]     : i2c1 (0)
    // 0x00000008 [3]     : i2c0 (0)
    // 0x00000004 [2]     : dma (0)
    // 0x00000002 [1]     : busctrl (0)
    // 0x00000001 [0]     : adc (0)
    io_rw_32 wdsel;

    _REG_(RESETS_RESET_DONE_OFFSET) // RESETS_RESET_DONE
    // Reset done
    // 0x01000000 [24]    : usbctrl (0)
    // 0x00800000 [23]    : uart1 (0)
    // 0x00400000 [22]    : uart0 (0)
    // 0x00200000 [21]    : timer (0)
    // 0x00100000 [20]    : tbman (0)
    // 0x00080000 [19]    : sysinfo (0)
    // 0x00040000 [18]    : syscfg (0)
    // 0x00020000 [17]    : spi1 (0)
    // 0x00010000 [16]    : spi0 (0)
    // 0x00008000 [15]    : rtc (0)
    // 0x00004000 [14]    : pwm (0)
    // 0x00002000 [13]    : pll_usb (0)
    // 0x00001000 [12]    : pll_sys (0)
    // 0x00000800 [11]    : pio1 (0)
    // 0x00000400 [10]    : pio0 (0)
    // 0x00000200 [9]     : pads_qspi (0)
    // 0x00000100 [8]     : pads_bank0 (0)
    // 0x00000080 [7]     : jtag (0)
    // 0x00000040 [6]     : io_qspi (0)
    // 0x00000020 [5]     : io_bank0 (0)
    // 0x00000010 [4]     : i2c1 (0)
    // 0x00000008 [3]     : i2c0 (0)
    // 0x00000004 [2]     : dma (0)
    // 0x00000002 [1]     : busctrl (0)
    // 0x00000001 [0]     : adc (0)
    io_ro_32 reset_done;
} resets_hw_t;

typedef struct {
    _REG_(PADS_BANK0_VOLTAGE_SELECT_OFFSET) // PADS_BANK0_VOLTAGE_SELECT
    // Voltage select
    // 0x00000001 [0]     : VOLTAGE_SELECT (0)
    io_rw_32 voltage_select;

    _REG_(PADS_BANK0_GPIO0_OFFSET) // PADS_BANK0_GPIO0
    // (Description copied from array index 0 register PADS_BANK0_GPIO0 applies similarly to other array indexes)
    //
    // Pad control register
    // 0x00000080 [7]     : OD (0): Output disable
    // 0x00000040 [6]     : IE (1): Input enable
    // 0x00000030 [5:4]   : DRIVE (1): Drive strength
    // 0x00000008 [3]     : PUE (0): Pull up enable
    // 0x00000004 [2]     : PDE (1): Pull down enable
    // 0x00000002 [1]     : SCHMITT (1): Enable schmitt trigger
    // 0x00000001 [0]     : SLEWFAST (0): Slew rate control
    io_rw_32 io[NUM_BANK0_GPIOS]; // 30
} padsbank0_hw_t;

typedef struct {
    _REG_(IO_BANK0_GPIO0_STATUS_OFFSET) // IO_BANK0_GPIO0_STATUS
    // GPIO status
    // 0x04000000 [26]    : IRQTOPROC (0): interrupt to processors, after override is applied
    // 0x01000000 [24]    : IRQFROMPAD (0): interrupt from pad before override is applied
    // 0x00080000 [19]    : INTOPERI (0): input signal to peripheral, after override is applied
    // 0x00020000 [17]    : INFROMPAD (0): input signal from pad, before override is applied
    // 0x00002000 [13]    : OETOPAD (0): output enable to pad after register override is applied
    // 0x00001000 [12]    : OEFROMPERI (0): output enable from selected peripheral, before register override is applied
    // 0x00000200 [9]     : OUTTOPAD (0): output signal to pad after register override is applied
    // 0x00000100 [8]     : OUTFROMPERI (0): output signal from selected peripheral, before register override is applied
    io_ro_32 status;

    _REG_(IO_BANK0_GPIO0_CTRL_OFFSET) // IO_BANK0_GPIO0_CTRL
    // GPIO control including function select and overrides
    // 0x30000000 [29:28] : IRQOVER (0)
    // 0x00030000 [17:16] : INOVER (0)
    // 0x00003000 [13:12] : OEOVER (0)
    // 0x00000300 [9:8]   : OUTOVER (0)
    // 0x0000001f [4:0]   : FUNCSEL (0x1f): 0-31 -> selects pin function according to the gpio table
    io_rw_32 ctrl;
} iobank0_status_ctrl_hw_t;

typedef struct {
    _REG_(IO_BANK0_PROC0_INTE0_OFFSET) // IO_BANK0_PROC0_INTE0
    // (Description copied from array index 0 register IO_BANK0_PROC0_INTE0 applies similarly to other array indexes)
    //
    // Interrupt Enable for proc0
    // 0x80000000 [31]    : GPIO7_EDGE_HIGH (0)
    // 0x40000000 [30]    : GPIO7_EDGE_LOW (0)
    // 0x20000000 [29]    : GPIO7_LEVEL_HIGH (0)
    // 0x10000000 [28]    : GPIO7_LEVEL_LOW (0)
    // 0x08000000 [27]    : GPIO6_EDGE_HIGH (0)
    // 0x04000000 [26]    : GPIO6_EDGE_LOW (0)
    // 0x02000000 [25]    : GPIO6_LEVEL_HIGH (0)
    // 0x01000000 [24]    : GPIO6_LEVEL_LOW (0)
    // 0x00800000 [23]    : GPIO5_EDGE_HIGH (0)
    // 0x00400000 [22]    : GPIO5_EDGE_LOW (0)
    // 0x00200000 [21]    : GPIO5_LEVEL_HIGH (0)
    // 0x00100000 [20]    : GPIO5_LEVEL_LOW (0)
    // 0x00080000 [19]    : GPIO4_EDGE_HIGH (0)
    // 0x00040000 [18]    : GPIO4_EDGE_LOW (0)
    // 0x00020000 [17]    : GPIO4_LEVEL_HIGH (0)
    // 0x00010000 [16]    : GPIO4_LEVEL_LOW (0)
    // 0x00008000 [15]    : GPIO3_EDGE_HIGH (0)
    // 0x00004000 [14]    : GPIO3_EDGE_LOW (0)
    // 0x00002000 [13]    : GPIO3_LEVEL_HIGH (0)
    // 0x00001000 [12]    : GPIO3_LEVEL_LOW (0)
    // 0x00000800 [11]    : GPIO2_EDGE_HIGH (0)
    // 0x00000400 [10]    : GPIO2_EDGE_LOW (0)
    // 0x00000200 [9]     : GPIO2_LEVEL_HIGH (0)
    // 0x00000100 [8]     : GPIO2_LEVEL_LOW (0)
    // 0x00000080 [7]     : GPIO1_EDGE_HIGH (0)
    // 0x00000040 [6]     : GPIO1_EDGE_LOW (0)
    // 0x00000020 [5]     : GPIO1_LEVEL_HIGH (0)
    // 0x00000010 [4]     : GPIO1_LEVEL_LOW (0)
    // 0x00000008 [3]     : GPIO0_EDGE_HIGH (0)
    // 0x00000004 [2]     : GPIO0_EDGE_LOW (0)
    // 0x00000002 [1]     : GPIO0_LEVEL_HIGH (0)
    // 0x00000001 [0]     : GPIO0_LEVEL_LOW (0)
    io_rw_32 inte[4];

    _REG_(IO_BANK0_PROC0_INTF0_OFFSET) // IO_BANK0_PROC0_INTF0
    // (Description copied from array index 0 register IO_BANK0_PROC0_INTF0 applies similarly to other array indexes)
    //
    // Interrupt Force for proc0
    // 0x80000000 [31]    : GPIO7_EDGE_HIGH (0)
    // 0x40000000 [30]    : GPIO7_EDGE_LOW (0)
    // 0x20000000 [29]    : GPIO7_LEVEL_HIGH (0)
    // 0x10000000 [28]    : GPIO7_LEVEL_LOW (0)
    // 0x08000000 [27]    : GPIO6_EDGE_HIGH (0)
    // 0x04000000 [26]    : GPIO6_EDGE_LOW (0)
    // 0x02000000 [25]    : GPIO6_LEVEL_HIGH (0)
    // 0x01000000 [24]    : GPIO6_LEVEL_LOW (0)
    // 0x00800000 [23]    : GPIO5_EDGE_HIGH (0)
    // 0x00400000 [22]    : GPIO5_EDGE_LOW (0)
    // 0x00200000 [21]    : GPIO5_LEVEL_HIGH (0)
    // 0x00100000 [20]    : GPIO5_LEVEL_LOW (0)
    // 0x00080000 [19]    : GPIO4_EDGE_HIGH (0)
    // 0x00040000 [18]    : GPIO4_EDGE_LOW (0)
    // 0x00020000 [17]    : GPIO4_LEVEL_HIGH (0)
    // 0x00010000 [16]    : GPIO4_LEVEL_LOW (0)
    // 0x00008000 [15]    : GPIO3_EDGE_HIGH (0)
    // 0x00004000 [14]    : GPIO3_EDGE_LOW (0)
    // 0x00002000 [13]    : GPIO3_LEVEL_HIGH (0)
    // 0x00001000 [12]    : GPIO3_LEVEL_LOW (0)
    // 0x00000800 [11]    : GPIO2_EDGE_HIGH (0)
    // 0x00000400 [10]    : GPIO2_EDGE_LOW (0)
    // 0x00000200 [9]     : GPIO2_LEVEL_HIGH (0)
    // 0x00000100 [8]     : GPIO2_LEVEL_LOW (0)
    // 0x00000080 [7]     : GPIO1_EDGE_HIGH (0)
    // 0x00000040 [6]     : GPIO1_EDGE_LOW (0)
    // 0x00000020 [5]     : GPIO1_LEVEL_HIGH (0)
    // 0x00000010 [4]     : GPIO1_LEVEL_LOW (0)
    // 0x00000008 [3]     : GPIO0_EDGE_HIGH (0)
    // 0x00000004 [2]     : GPIO0_EDGE_LOW (0)
    // 0x00000002 [1]     : GPIO0_LEVEL_HIGH (0)
    // 0x00000001 [0]     : GPIO0_LEVEL_LOW (0)
    io_rw_32 intf[4];

    _REG_(IO_BANK0_PROC0_INTS0_OFFSET) // IO_BANK0_PROC0_INTS0
    // (Description copied from array index 0 register IO_BANK0_PROC0_INTS0 applies similarly to other array indexes)
    //
    // Interrupt status after masking & forcing for proc0
    // 0x80000000 [31]    : GPIO7_EDGE_HIGH (0)
    // 0x40000000 [30]    : GPIO7_EDGE_LOW (0)
    // 0x20000000 [29]    : GPIO7_LEVEL_HIGH (0)
    // 0x10000000 [28]    : GPIO7_LEVEL_LOW (0)
    // 0x08000000 [27]    : GPIO6_EDGE_HIGH (0)
    // 0x04000000 [26]    : GPIO6_EDGE_LOW (0)
    // 0x02000000 [25]    : GPIO6_LEVEL_HIGH (0)
    // 0x01000000 [24]    : GPIO6_LEVEL_LOW (0)
    // 0x00800000 [23]    : GPIO5_EDGE_HIGH (0)
    // 0x00400000 [22]    : GPIO5_EDGE_LOW (0)
    // 0x00200000 [21]    : GPIO5_LEVEL_HIGH (0)
    // 0x00100000 [20]    : GPIO5_LEVEL_LOW (0)
    // 0x00080000 [19]    : GPIO4_EDGE_HIGH (0)
    // 0x00040000 [18]    : GPIO4_EDGE_LOW (0)
    // 0x00020000 [17]    : GPIO4_LEVEL_HIGH (0)
    // 0x00010000 [16]    : GPIO4_LEVEL_LOW (0)
    // 0x00008000 [15]    : GPIO3_EDGE_HIGH (0)
    // 0x00004000 [14]    : GPIO3_EDGE_LOW (0)
    // 0x00002000 [13]    : GPIO3_LEVEL_HIGH (0)
    // 0x00001000 [12]    : GPIO3_LEVEL_LOW (0)
    // 0x00000800 [11]    : GPIO2_EDGE_HIGH (0)
    // 0x00000400 [10]    : GPIO2_EDGE_LOW (0)
    // 0x00000200 [9]     : GPIO2_LEVEL_HIGH (0)
    // 0x00000100 [8]     : GPIO2_LEVEL_LOW (0)
    // 0x00000080 [7]     : GPIO1_EDGE_HIGH (0)
    // 0x00000040 [6]     : GPIO1_EDGE_LOW (0)
    // 0x00000020 [5]     : GPIO1_LEVEL_HIGH (0)
    // 0x00000010 [4]     : GPIO1_LEVEL_LOW (0)
    // 0x00000008 [3]     : GPIO0_EDGE_HIGH (0)
    // 0x00000004 [2]     : GPIO0_EDGE_LOW (0)
    // 0x00000002 [1]     : GPIO0_LEVEL_HIGH (0)
    // 0x00000001 [0]     : GPIO0_LEVEL_LOW (0)
    io_ro_32 ints[4];
} io_irq_ctrl_hw_t;

typedef struct {
    iobank0_status_ctrl_hw_t io[NUM_BANK0_GPIOS]; // 30

    _REG_(IO_BANK0_INTR0_OFFSET) // IO_BANK0_INTR0
    // (Description copied from array index 0 register IO_BANK0_INTR0 applies similarly to other array indexes)
    //
    // Raw Interrupts
    // 0x80000000 [31]    : GPIO7_EDGE_HIGH (0)
    // 0x40000000 [30]    : GPIO7_EDGE_LOW (0)
    // 0x20000000 [29]    : GPIO7_LEVEL_HIGH (0)
    // 0x10000000 [28]    : GPIO7_LEVEL_LOW (0)
    // 0x08000000 [27]    : GPIO6_EDGE_HIGH (0)
    // 0x04000000 [26]    : GPIO6_EDGE_LOW (0)
    // 0x02000000 [25]    : GPIO6_LEVEL_HIGH (0)
    // 0x01000000 [24]    : GPIO6_LEVEL_LOW (0)
    // 0x00800000 [23]    : GPIO5_EDGE_HIGH (0)
    // 0x00400000 [22]    : GPIO5_EDGE_LOW (0)
    // 0x00200000 [21]    : GPIO5_LEVEL_HIGH (0)
    // 0x00100000 [20]    : GPIO5_LEVEL_LOW (0)
    // 0x00080000 [19]    : GPIO4_EDGE_HIGH (0)
    // 0x00040000 [18]    : GPIO4_EDGE_LOW (0)
    // 0x00020000 [17]    : GPIO4_LEVEL_HIGH (0)
    // 0x00010000 [16]    : GPIO4_LEVEL_LOW (0)
    // 0x00008000 [15]    : GPIO3_EDGE_HIGH (0)
    // 0x00004000 [14]    : GPIO3_EDGE_LOW (0)
    // 0x00002000 [13]    : GPIO3_LEVEL_HIGH (0)
    // 0x00001000 [12]    : GPIO3_LEVEL_LOW (0)
    // 0x00000800 [11]    : GPIO2_EDGE_HIGH (0)
    // 0x00000400 [10]    : GPIO2_EDGE_LOW (0)
    // 0x00000200 [9]     : GPIO2_LEVEL_HIGH (0)
    // 0x00000100 [8]     : GPIO2_LEVEL_LOW (0)
    // 0x00000080 [7]     : GPIO1_EDGE_HIGH (0)
    // 0x00000040 [6]     : GPIO1_EDGE_LOW (0)
    // 0x00000020 [5]     : GPIO1_LEVEL_HIGH (0)
    // 0x00000010 [4]     : GPIO1_LEVEL_LOW (0)
    // 0x00000008 [3]     : GPIO0_EDGE_HIGH (0)
    // 0x00000004 [2]     : GPIO0_EDGE_LOW (0)
    // 0x00000002 [1]     : GPIO0_LEVEL_HIGH (0)
    // 0x00000001 [0]     : GPIO0_LEVEL_LOW (0)
    io_rw_32 intr[4];

    io_irq_ctrl_hw_t proc0_irq_ctrl;

    io_irq_ctrl_hw_t proc1_irq_ctrl;

    io_irq_ctrl_hw_t dormant_wake_irq_ctrl;
} iobank0_hw_t;

#define spi0_hw ((spi_hw_t *)SPI0_BASE)
#define spi1_hw ((spi_hw_t *)SPI1_BASE)
#define spi0 ((spi_inst_t *)spi0_hw)
#define spi1 ((spi_inst_t *)spi1_hw)
#define resets_hw ((resets_hw_t *)RESETS_BASE)
#define padsbank0_hw ((padsbank0_hw_t *)PADS_BANK0_BASE)
#define iobank0_hw ((iobank0_hw_t *)IO_BANK0_BASE)

static uint32_t configured_freq[CLK_COUNT] = {0, 0, 0, 0, 12000000, 125000000, 125000000, 48000000, 48000000, 46875};


INLINE void hw_set_bits(io_rw_32 *addr, uint32_t mask) {
    *(io_rw_32 *) hw_set_alias_untyped((volatile void *) addr) = mask;
}

INLINE void reset_block(uint32_t bits) {
    hw_set_bits(&resets_hw->reset, bits);
}

INLINE void spi_reset(spi_inst_t *spi) {
    reset_block(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);
}

INLINE void tight_loop_contents(void) {}

INLINE void hw_clear_bits(io_rw_32 *addr, uint32_t mask) {
    *(io_rw_32 *) hw_clear_alias_untyped((volatile void *) addr) = mask;
}

INLINE void unreset_block_wait(uint32_t bits) {
    hw_clear_bits(&resets_hw->reset, bits);
    while (~resets_hw->reset_done & bits)
        tight_loop_contents();
}

INLINE void spi_unreset(spi_inst_t *spi) {
    unreset_block_wait(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);
}

INLINE uint32_t clock_get_hz(enum clock_index clk_index) {
    return configured_freq[clk_index];
}

INLINE uint spi_get_index(const spi_inst_t *spi) {
    return spi == spi1 ? 1 : 0;
}

INLINE spi_hw_t *spi_get_hw(spi_inst_t *spi) {
    spi_get_index(spi); // check it is a hw spi
    return (spi_hw_t *)spi;
}

INLINE void hw_xor_bits(io_rw_32 *addr, uint32_t mask) {
    *(io_rw_32 *) hw_xor_alias_untyped((volatile void *) addr) = mask;
}

INLINE void hw_write_masked(io_rw_32 *addr, uint32_t values, uint32_t write_mask) {
    hw_xor_bits(addr, (*addr ^ values) & write_mask);
}

INLINE uint spi_set_baudrate(spi_inst_t *spi, uint baudrate) {
    uint freq_in = clock_get_hz(clk_peri);
    uint prescale, postdiv;

    // Disable the SPI
    uint32_t enable_mask = spi_get_hw(spi)->cr1 & SPI_SSPCR1_SSE_BITS;
    hw_clear_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS);

    // Find smallest prescale value which puts output frequency in range of
    // post-divide. Prescale is an even number from 2 to 254 inclusive.
    for (prescale = 2; prescale <= 254; prescale += 2) {
        if (freq_in < (prescale + 2) * 256 * (uint64_t) baudrate)
            break;
    }

    // Find largest post-divide which makes output <= baudrate. Post-divide is
    // an integer in the range 1 to 256 inclusive.
    for (postdiv = 256; postdiv > 1; --postdiv) {
        if (freq_in / (prescale * (postdiv - 1)) > baudrate)
            break;
    }

    spi_get_hw(spi)->cpsr = prescale;
    hw_write_masked(&spi_get_hw(spi)->cr0, (postdiv - 1) << SPI_SSPCR0_SCR_LSB, SPI_SSPCR0_SCR_BITS);

    // Re-enable the SPI
    hw_set_bits(&spi_get_hw(spi)->cr1, enable_mask);

    // Return the frequency we were able to achieve
    return freq_in / (prescale * postdiv);
}

INLINE void spi_set_format(spi_inst_t *spi, uint data_bits, spi_cpol_t cpol, spi_cpha_t cpha, spi_order_t order) {
    // Disable the SPI
    uint32_t enable_mask = spi_get_hw(spi)->cr1 & SPI_SSPCR1_SSE_BITS;
    hw_clear_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS);

    hw_write_masked(&spi_get_hw(spi)->cr0,
                    ((uint)(data_bits - 1)) << SPI_SSPCR0_DSS_LSB |
                    ((uint)cpol) << SPI_SSPCR0_SPO_LSB |
                    ((uint)cpha) << SPI_SSPCR0_SPH_LSB,
        SPI_SSPCR0_DSS_BITS |
        SPI_SSPCR0_SPO_BITS |
        SPI_SSPCR0_SPH_BITS);

    // Re-enable the SPI
    hw_set_bits(&spi_get_hw(spi)->cr1, enable_mask);
}

INLINE uint spi_init(spi_inst_t *spi, uint baudrate) {
    spi_reset(spi);
    
    spi_unreset(spi);

    uint baud = spi_set_baudrate(spi, baudrate);

    spi_set_format(spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    // Always enable DREQ signals -- harmless if DMA is not listening
   
    hw_set_bits(&spi_get_hw(spi)->dmacr, SPI_SSPDMACR_TXDMAE_BITS | SPI_SSPDMACR_RXDMAE_BITS);

    // Finally enable the SPI
    hw_set_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS); 

    return baud;

}

INLINE void check_gpio_param(uint gpio) {
}

INLINE void gpio_set_function(uint gpio, enum gpio_function fn) {
    check_gpio_param(gpio);
    // Set input enable on, output disable off
    hw_write_masked(&padsbank0_hw->io[gpio],
                   PADS_BANK0_GPIO0_IE_BITS,
                   PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS
    );
    // Zero all fields apart from fsel; we want this IO to do what the peripheral tells it.
    // This doesn't affect e.g. pullup/pulldown, as these are in pad controls.
    iobank0_hw->io[gpio].ctrl = fn << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
}

INLINE const spi_hw_t *spi_get_const_hw(const spi_inst_t *spi) {
    spi_get_index(spi);  // check it is a hw spi
    return (const spi_hw_t *)spi;
}

INLINE bool spi_is_writable(const spi_inst_t *spi) {
    return (spi_get_const_hw(spi)->sr & SPI_SSPSR_TNF_BITS);
}

INLINE bool spi_is_readable(const spi_inst_t *spi) {
    return (spi_get_const_hw(spi)->sr & SPI_SSPSR_RNE_BITS);
}

INLINE int spi_write_read_blocking(spi_inst_t *spi, const uint8_t *src, uint8_t *dst, size_t len) {
    // Never have more transfers in flight than will fit into the RX FIFO,
    // else FIFO will overflow if this code is heavily interrupted.
    const size_t fifo_depth = 8;
    size_t rx_remaining = len, tx_remaining = len;

    while (rx_remaining || tx_remaining) {
        if (tx_remaining && spi_is_writable(spi) && rx_remaining < tx_remaining + fifo_depth) {
            spi_get_hw(spi)->dr = (uint32_t) *src++;
            --tx_remaining;
        }
        if (rx_remaining && spi_is_readable(spi)) {
            *dst++ = (uint8_t) spi_get_hw(spi)->dr;
            --rx_remaining;
        }
    }

    return (int)len;
}


#endif /* _SPI_H_ */