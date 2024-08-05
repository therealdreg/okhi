// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, Alex Taradov <alex@taradov.com>. All rights reserved.

#ifndef _UTILS_H_
#define _UTILS_H_

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

/*- Includes ----------------------------------------------------------------*/
#include <stdint.h>
#include <stdarg.h>
#include "conf.h"

/*- Definitions -------------------------------------------------------------*/
#define PACK           __attribute__((packed))
#define WEAK           __attribute__((weak))
#define INLINE         static inline __attribute__((always_inline))
#define LIMIT(a, b)    (((int)(a) > (int)(b)) ? (int)(b) : (int)(a))

/*- Definitions -------------------------------------------------------------*/
#define BP()   __asm("bkpt #1"); // breakpoint via software macro
// In this project, all code is located in RAM by default (all .text sections reside in RAM, look at the linker script rp2040.ld)
#define FORCE_RAM_ATTR __attribute__((section(".firam"))) // force a function to be located in RAM
#define FORCE_FLASH_ATTR __attribute__((section(".fflash"))) // force a function to be located in FLASH

extern char __flash_binary_end;

#define END_FLASH_ADDR ((uintptr_t) &__flash_binary_end)

void hw_divmod_u32(uint32_t dividend, uint32_t divisor, uint32_t *quotient, uint32_t *remainder);
void hw_divmod_s32(int32_t dividend, int32_t divisor, int32_t *quotient, int32_t *remainder);
void format_hex(char *buf, uint32_t v, int size);
void format_dec(char *buf, uint32_t v, int size);


INLINE void mitoa(int num, char *str, int base) {
    int i = 0;
    int isNegative = 0;

    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (num < 0 && base == 10) {
        isNegative = 1;
        num = -num;
    }

    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    if (isNegative) {
        str[i++] = '-';
    }

    str[i] = '\0';

    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}


INLINE void muitoa(unsigned int num, char *str, int base) {
    int i = 0;

    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    while (num != 0) {
        unsigned int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    str[i] = '\0';

    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

unsigned int msprintf(char *buf, const char *fmt, ...); 

INLINE char lower(char c)
{
  if ('A' <= c && c <= 'Z')
    return c - ('A' - 'a');
  return c;
}

INLINE char upper(char c)
{
  if ('a' <= c && c <= 'z')
    return c + ('A' - 'a');
  return c;
}

INLINE void string_to_lower(char *str)
{
  if (str == 0) return; 
  while (*str)
  {
    *str = lower(*str); 
    str++;
  }
}

INLINE void string_to_upper(char *str)
{
  if (str == 0) return; 
  while (*str)
  {
    *str = upper(*str); 
    str++;
  }
}

INLINE void delay_ms(int ms)
{
  uint32_t cycles = ms * F_CPU / 3 / 1000;

  asm volatile (
    "1: sub %[cycles], %[cycles], #1 \n"
    "   bne 1b \n"
    : [cycles] "+r"(cycles)
  );
}

INLINE void delay_s(int s)
{
    uint32_t cycles = s * F_CPU / 3;  

    asm volatile (
        "1: sub %[cycles], %[cycles], #1 \n"
        "   bne 1b \n"
        : [cycles] "+r"(cycles)
    );
}

INLINE void delay_us(int us)
{
    uint32_t cycles = (us * F_CPU / 1000000) / 3;  // Ajuste para microsegundos

    asm volatile (
        "1: sub %[cycles], %[cycles], #1 \n"
        "   bne 1b \n"
        : [cycles] "+r"(cycles)
    );
}

INLINE void delay_min(int min)
{
    uint32_t cycles = (min * F_CPU * 60) / 3; 

    asm volatile (
        "1: sub %[cycles], %[cycles], #1 \n"
        "   bne 1b \n"
        : [cycles] "+r"(cycles)
    );
}

#endif // _UTILS_H_
