#ifndef _UART_H_
#define _UART_H_

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

HAL_GPIO_PIN(UART_TX,  0,  0, uart0_tx)
HAL_GPIO_PIN(UART_RX,  0,  1, uart0_rx)

HAL_GPIO_PIN(UART1_TX,  0,  4, uart1_tx)
HAL_GPIO_PIN(UART1_RX,  0,  5, uart1_rx)


//-----------------------------------------------------------------------------
INLINE void uart_putc(char c)
{
  while (UART0->UARTFR_b.TXFF);
  UART0->UARTDR = c;
}

//-----------------------------------------------------------------------------
INLINE bool uart_getc(char *c)
{
  if (0 == UART0->UARTFR_b.RXFE)
  {
    *c = UART0->UARTDR;
    return true;
  }

  return false;
}

//-----------------------------------------------------------------------------
INLINE void uart_puts(char *s)
{
  while (*s)
    uart_putc(*s++);
}

INLINE void uart_put_uint32_hex(uint32_t num) 
{
    char buffer[11]; 
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[10] = '\0';

    for (int i = 0; i < 8; i++) {
        uint8_t digit = (num >> (4 * i)) & 0xF; 
        if (digit < 10)
            buffer[9 - i] = '0' + digit; 
        else
            buffer[9 - i] = 'A' + (digit - 10); 
    }

    uart_puts(buffer); 
}

INLINE void uart_put_uint32_dec(uint32_t num) 
{
    char buffer[12]; // 10 digits for 32-bit integer + 1 for null terminator
    int i = 10;
    buffer[11] = '\0'; // Null terminator

    if (num == 0) {
        buffer[10] = '0';
        i = 9;
    } else {
        while (num > 0) {
            buffer[i] = '0' + (num % 10);
            num /= 10;
            i--;
        }
    }

    uart_puts(&buffer[i + 1]); // Print the buffer from the first non-zero digit
}

INLINE void uart_put_uint16_dec(uint16_t num) 
{
    char buffer[6]; // 5 digits for 16-bit integer + 1 for null terminator
    int i = 4;
    buffer[5] = '\0'; // Null terminator

    if (num == 0) {
        buffer[4] = '0';
        i = 3;
    } else {
        while (num > 0) {
            buffer[i] = '0' + (num % 10);
            num /= 10;
            i--;
        }
    }

    uart_puts(&buffer[i + 1]); // Print the buffer from the first non-zero digit
}

INLINE void uart_put_uint8_dec(uint8_t num) 
{
    char buffer[4]; // 3 digits for 8-bit integer + 1 for null terminator
    int i = 2;
    buffer[3] = '\0'; // Null terminator

    if (num == 0) {
        buffer[2] = '0';
        i = 1;
    } else {
        while (num > 0) {
            buffer[i] = '0' + (num % 10);
            num /= 10;
            i--;
        }
    }

    uart_puts(&buffer[i + 1]); // Print the buffer from the first non-zero digit
}

INLINE void uart_print_uint32_binary(uint32_t num) {
    for (int i = 31; i >= 0; i--) { 
        char bit = (num & (1u << i)) ? '1' : '0';
        uart_putc(bit); 

        if (i % 4 == 0 && i != 0) { 
            uart_putc(' ');
        }
    }
}

INLINE void uart_print_uint16_binary(uint16_t num) {
    for (int i = 15; i >= 0; i--) { 
        char bit = (num & (1u << i)) ? '1' : '0';
        uart_putc(bit); 

        if (i % 4 == 0 && i != 0) {
            uart_putc(' ');
        }
    }
}

INLINE void uart_print_uint8_binary(uint8_t num) {
    for (int i = 7; i >= 0; i--) { 
        char bit = (num & (1u << i)) ? '1' : '0';
        uart_putc(bit); 

        if (i % 4 == 0 && i != 0) { 
            uart_putc(' ');
        }
    }
}

INLINE void uart_put_uint16_hex(uint16_t num) 
{
    char buffer[7];  
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[6] = '\0';  

    for (int i = 0; i < 4; i++) {  
        uint8_t digit = (num >> (4 * i)) & 0xF; 
        if (digit < 10)
            buffer[5 - i] = '0' + digit;
        else
            buffer[5 - i] = 'A' + (digit - 10); 
    }

    uart_puts(buffer); 
}

INLINE void uart_put_uint8_hex(uint8_t num) 
{
    char buffer[5];  
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[4] = '\0';  

    for (int i = 0; i < 2; i++) { 
        uint8_t digit = (num >> (4 * i)) & 0xF; 
        if (digit < 10)
            buffer[3 - i] = '0' + digit; 
        else
            buffer[3 - i] = 'A' + (digit - 10); 
    }

    uart_puts(buffer); 
}

INLINE void uart_print_buffer_hex(uint8_t *buffer, size_t size, bool new_newline, bool space) {
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = buffer[i];
        char hex[3]; 

        uint8_t nibble = (byte >> 4) & 0x0F;
        if (nibble < 10)
            hex[0] = '0' + nibble;
        else
            hex[0] = 'A' + (nibble - 10);

        nibble = byte & 0x0F;
        if (nibble < 10)
            hex[1] = '0' + nibble;
        else
            hex[1] = 'A' + (nibble - 10);

        hex[2] = '\0'; 

        uart_puts(hex);  
        if (space)
          uart_putc(' ');   

        if (new_newline)
        {
          if ((i + 1) % 16 == 0 || i + 1 == size) {
              uart_puts("\r\n");
          }
        }
    }
}

INLINE void uart_print_buffer_binary(uint8_t *buffer, size_t size, bool new_newline, bool space) {
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = buffer[i];

        for (int j = 7; j >= 0; j--) { 
            char bit = (byte & (1 << j)) ? '1' : '0';
            uart_putc(bit);
        }

        if (space) {
            uart_putc(' '); 
        }

        if (new_newline) {
            if ((i + 1) % 8 == 0 || i + 1 == size) { 
                uart_puts("\r\n");
            }
        }
    }
}

INLINE void uart_print_hexdump(uint8_t *buffer, size_t size) {
    char ascii[17]; 
    ascii[16] = '\0';

    for (size_t i = 0; i < size; i++) {
        uint8_t byte = buffer[i];
        char hex[3]; 

        uint8_t nibble = (byte >> 4) & 0x0F;
        hex[0] = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);

        nibble = byte & 0x0F;
        hex[1] = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);

        hex[2] = '\0'; 

        uart_puts(hex);  
        uart_putc(' ');  

        ascii[i % 16] = (byte >= 32 && byte <= 126) ? byte : '.';

        if ((i + 1) % 16 == 0 || i + 1 == size) {
            if ((i + 1) % 16 != 0) {
                size_t remaining = 16 - ((i + 1) % 16);
                for (size_t j = 0; j < remaining; j++) {
                    uart_puts("   "); 
                }
            }

            uart_puts(" | ");
            ascii[(i % 16) + 1] = '\0';
            uart_puts(ascii);
            uart_puts("\r\n");
        }
    }
}

INLINE void uart_print_bindump(uint8_t *buffer, size_t size) {
    char ascii[5]; 
    ascii[4] = '\0'; 

    for (size_t i = 0; i < size; i++) {
        uint8_t byte = buffer[i];

        for (int j = 7; j >= 0; j--) {
            uart_putc((byte & (1 << j)) ? '1' : '0');
        }
        uart_putc(' '); 

        ascii[i % 4] = (byte >= 32 && byte <= 126) ? byte : '.';

        if ((i + 1) % 4 == 0 || i + 1 == size) {
            if ((i + 1) % 4 != 0) {
                size_t remaining = 4 - ((i + 1) % 4);
                for (size_t j = 0; j < remaining; j++) {
                    uart_puts("         "); 
                }
            }

            uart_puts(" | ");
            ascii[(i % 4) + 1] = '\0'; 
            uart_puts(ascii);
            uart_puts("\r\n");
        }
    }
}

//-----------------------------------------------------------------------------

INLINE void uart_init(uint32_t baud)
{
  RESETS_CLR->RESET = RESETS_RESET_uart0_Msk;
  while (0 == RESETS->RESET_DONE_b.uart0);

  baud = (F_PER * 4) / baud;

  UART0->UARTIBRD = baud / 64;
  UART0->UARTFBRD = baud % 64;

  UART0->UARTLCR_H = (3/*8 bits*/ << UART0_UARTLCR_H_WLEN_Pos) | UART0_UARTLCR_H_FEN_Msk;
  UART0->UARTCR = UART0_UARTCR_UARTEN_Msk | UART0_UARTCR_RXE_Msk | UART0_UARTCR_TXE_Msk;

  HAL_GPIO_UART_TX_init();
  HAL_GPIO_UART_RX_init();

  delay_ms(50); // a little delay is necessary for the Raspberry Pi Debug Probe to work properly
}


// UART 1


//-----------------------------------------------------------------------------
INLINE void uart1_putc(char c)
{
  while (UART1->UARTFR_b.TXFF);
  UART1->UARTDR = c;
}

//-----------------------------------------------------------------------------
INLINE bool uart1_getc(char *c)
{
  if (0 == UART1->UARTFR_b.RXFE)
  {
    *c = UART1->UARTDR;
    return true;
  }

  return false;
}

//-----------------------------------------------------------------------------
INLINE void uart1_puts(char *s)
{
  while (*s)
    uart1_putc(*s++);
}



INLINE void uart1_put_uint32_hex(uint32_t num) 
{
    char buffer[11]; 
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[10] = '\0';

    for (int i = 0; i < 8; i++) {
        uint8_t digit = (num >> (4 * i)) & 0xF; 
        if (digit < 10)
            buffer[9 - i] = '0' + digit; 
        else
            buffer[9 - i] = 'A' + (digit - 10); 
    }

    uart1_puts(buffer); 
}

INLINE void uart1_put_uint32_dec(uint32_t num) 
{
    char buffer[12]; // 10 digits for 32-bit integer + 1 for null terminator
    int i = 10;
    buffer[11] = '\0'; // Null terminator

    if (num == 0) {
        buffer[10] = '0';
        i = 9;
    } else {
        while (num > 0) {
            buffer[i] = '0' + (num % 10);
            num /= 10;
            i--;
        }
    }

    uart1_puts(&buffer[i + 1]); // Print the buffer from the first non-zero digit
}

INLINE void uart1_put_uint16_dec(uint16_t num) 
{
    char buffer[6]; // 5 digits for 16-bit integer + 1 for null terminator
    int i = 4;
    buffer[5] = '\0'; // Null terminator

    if (num == 0) {
        buffer[4] = '0';
        i = 3;
    } else {
        while (num > 0) {
            buffer[i] = '0' + (num % 10);
            num /= 10;
            i--;
        }
    }

    uart1_puts(&buffer[i + 1]); // Print the buffer from the first non-zero digit
}

INLINE void uart1_put_uint8_dec(uint8_t num) 
{
    char buffer[4]; // 3 digits for 8-bit integer + 1 for null terminator
    int i = 2;
    buffer[3] = '\0'; // Null terminator

    if (num == 0) {
        buffer[2] = '0';
        i = 1;
    } else {
        while (num > 0) {
            buffer[i] = '0' + (num % 10);
            num /= 10;
            i--;
        }
    }

    uart1_puts(&buffer[i + 1]); // Print the buffer from the first non-zero digit
}

INLINE void uart1_print_uint32_binary(uint32_t num) {
    for (int i = 31; i >= 0; i--) { 
        char bit = (num & (1u << i)) ? '1' : '0';
        uart1_putc(bit); 

        if (i % 4 == 0 && i != 0) { 
            uart1_putc(' ');
        }
    }
}

INLINE void uart1_print_uint16_binary(uint16_t num) {
    for (int i = 15; i >= 0; i--) { 
        char bit = (num & (1u << i)) ? '1' : '0';
        uart1_putc(bit); 

        if (i % 4 == 0 && i != 0) {
            uart1_putc(' ');
        }
    }
}

INLINE void uart1_print_uint8_binary(uint8_t num) {
    for (int i = 7; i >= 0; i--) { 
        char bit = (num & (1u << i)) ? '1' : '0';
        uart1_putc(bit); 

        if (i % 4 == 0 && i != 0) { 
            uart1_putc(' ');
        }
    }
}

INLINE void uart1_put_uint16_hex(uint16_t num) 
{
    char buffer[7];  
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[6] = '\0';  

    for (int i = 0; i < 4; i++) {  
        uint8_t digit = (num >> (4 * i)) & 0xF; 
        if (digit < 10)
            buffer[5 - i] = '0' + digit;
        else
            buffer[5 - i] = 'A' + (digit - 10); 
    }

    uart1_puts(buffer); 
}

INLINE void uart1_put_uint8_hex(uint8_t num) 
{
    char buffer[5];  
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[4] = '\0';  

    for (int i = 0; i < 2; i++) { 
        uint8_t digit = (num >> (4 * i)) & 0xF; 
        if (digit < 10)
            buffer[3 - i] = '0' + digit; 
        else
            buffer[3 - i] = 'A' + (digit - 10); 
    }

    uart1_puts(buffer); 
}

INLINE void uart1_print_buffer_hex(uint8_t *buffer, size_t size, bool new_newline, bool space) {
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = buffer[i];
        char hex[3]; 

        uint8_t nibble = (byte >> 4) & 0x0F;
        if (nibble < 10)
            hex[0] = '0' + nibble;
        else
            hex[0] = 'A' + (nibble - 10);

        nibble = byte & 0x0F;
        if (nibble < 10)
            hex[1] = '0' + nibble;
        else
            hex[1] = 'A' + (nibble - 10);

        hex[2] = '\0'; 

        uart1_puts(hex);  
        if (space)
          uart1_putc(' ');   

        if (new_newline)
        {
          if ((i + 1) % 16 == 0 || i + 1 == size) {
              uart1_puts("\r\n");
          }
        }
    }
}

INLINE void uart1_print_buffer_binary(uint8_t *buffer, size_t size, bool new_newline, bool space) {
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = buffer[i];

        for (int j = 7; j >= 0; j--) { 
            char bit = (byte & (1 << j)) ? '1' : '0';
            uart1_putc(bit);
        }

        if (space) {
            uart1_putc(' '); 
        }

        if (new_newline) {
            if ((i + 1) % 8 == 0 || i + 1 == size) { 
                uart1_puts("\r\n");
            }
        }
    }
}

INLINE void uart1_print_hexdump(uint8_t *buffer, size_t size) {
    char ascii[17]; 
    ascii[16] = '\0';

    for (size_t i = 0; i < size; i++) {
        uint8_t byte = buffer[i];
        char hex[3]; 

        uint8_t nibble = (byte >> 4) & 0x0F;
        hex[0] = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);

        nibble = byte & 0x0F;
        hex[1] = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);

        hex[2] = '\0'; 

        uart1_puts(hex);  
        uart1_putc(' ');  

        ascii[i % 16] = (byte >= 32 && byte <= 126) ? byte : '.';

        if ((i + 1) % 16 == 0 || i + 1 == size) {
            if ((i + 1) % 16 != 0) {
                size_t remaining = 16 - ((i + 1) % 16);
                for (size_t j = 0; j < remaining; j++) {
                    uart1_puts("   "); 
                }
            }

            uart1_puts(" | ");
            ascii[(i % 16) + 1] = '\0';
            uart1_puts(ascii);
            uart1_puts("\r\n");
        }
    }
}

INLINE void uart1_print_bindump(uint8_t *buffer, size_t size) {
    char ascii[5]; 
    ascii[4] = '\0'; 

    for (size_t i = 0; i < size; i++) {
        uint8_t byte = buffer[i];

        for (int j = 7; j >= 0; j--) {
            uart1_putc((byte & (1 << j)) ? '1' : '0');
        }
        uart1_putc(' '); 

        ascii[i % 4] = (byte >= 32 && byte <= 126) ? byte : '.';

        if ((i + 1) % 4 == 0 || i + 1 == size) {
            if ((i + 1) % 4 != 0) {
                size_t remaining = 4 - ((i + 1) % 4);
                for (size_t j = 0; j < remaining; j++) {
                    uart1_puts("         "); 
                }
            }

            uart1_puts(" | ");
            ascii[(i % 4) + 1] = '\0'; 
            uart1_puts(ascii);
            uart1_puts("\r\n");
        }
    }
}


INLINE void uart1_init(uint32_t baud)
{
  RESETS_CLR->RESET = RESETS_RESET_uart1_Msk;
  while (0 == RESETS->RESET_DONE_b.uart1);

  baud = (F_PER * 4) / baud;

  UART1->UARTIBRD = baud / 64;
  UART1->UARTFBRD = baud % 64;

  UART1->UARTLCR_H = (3/*8 bits*/ << UART0_UARTLCR_H_WLEN_Pos) | UART0_UARTLCR_H_FEN_Msk;
  UART1->UARTCR = UART0_UARTCR_UARTEN_Msk | UART0_UARTCR_RXE_Msk | UART0_UARTCR_TXE_Msk;

  HAL_GPIO_UART1_TX_init();
  HAL_GPIO_UART1_RX_init();

  delay_ms(50); // a little delay is necessary for the Raspberry Pi Debug Probe to work properly
}



#endif /*_UART_H_ */
