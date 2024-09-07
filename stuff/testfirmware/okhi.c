/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/clocks.h"

#define F_REF      12000000
#define F_CPU      120000000
#define F_PER      120000000
#define F_RTC      (F_REF / 256)
#define F_TICK     1000000
#define STATUS_TIMEOUT     500000 // us

#define UART_ID uart0
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

// We are using pins 0 and 1, but see the GPIO function select table in the
// datasheet for information on which other pins can be used.
#define UART_TX_PIN 0
#define UART_RX_PIN 1


#include "hardware/pio.h"
#include "hardware/spi.h"

void func(void)
{
    PIO pio; 
    uint sm;
    
    pio = pio0;
    sm = 0;
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);

    pio = pio1;
    sm = 0;
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
}

spi_inst_t *spi = spi1;

void myspininit(void)
{
    
//  uint32_t baud = spi_init(spi, 5000000); // ~4.6 mhz
  uint32_t baud = spi_init(spi, 1000000); // ~4.6 mhz
  gpio_set_function(10, GPIO_FUNC_SPI);
  gpio_set_function(11, GPIO_FUNC_SPI);
  gpio_set_function(12, GPIO_FUNC_SPI);
  //gpio_set_function(13, GPIO_FUNC_SPI);
  
  gpio_init(13);
  gpio_set_dir(13, GPIO_OUT);
  gpio_put(13, 1);
  //printf("spi enabled at %d\n", baud);
}

int main() {
    //func();
    stdio_init_all();

    // Set up our UART with a basic baud rate.
    uart_init(UART_ID, BAUD_RATE);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_hw_flow(UART_ID, false, false);

    // Set our data format
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);

    // Turn off FIFO's - we want to do this character by character
    uart_set_fifo_enabled(UART_ID, false);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(UART_ID, false, false);

    uart_puts(UART_ID, "\nHello, uart interrupts\n");


    /**
    while (1)
    {
        unsigned int number = 3;
        puts("enter number!");
        //scanf("%d", &number);
        printf("wopa: %d\n", number);
        tight_loop_contents();
    }
    */

       printf("SPI master example\n");

    myspininit();
  
     while (1)
     {
        unsigned char buff[128] = { 0x41, 0x42, 0x43, 0x00 };
        // Write the output buffer to MOSI, and at the same time read from MISO.
        //spi_write_read_blocking(spi, buff, buff, 3);
        gpio_put(13,false); 
        spi_write_blocking(spi, buff, sizeof(buff));
        // Sleep for ten seconds so you get a chance to read the output.
        gpio_put(13,true); 
        sleep_ms(1000);
    }
}

/// \end:uart_advanced[]
