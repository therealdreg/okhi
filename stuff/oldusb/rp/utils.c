// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, Alex Taradov <alex@taradov.com>. All rights reserved.

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
#include "utils.h"

/*- Implementations ---------------------------------------------------------*/

unsigned int msprintf(char *buf, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char *out = buf;
    while (*fmt) {
        if (*fmt == '%') {
            fmt++; 
            switch (*fmt) {
                case 'c': {
                    char c = (char) va_arg(args, int); 
                    *out++ = c;
                    break;
                }
                case 'd': {
                    int d = va_arg(args, int);
                    char numStr[20];
                    mitoa(d, numStr, 10);
                    char *ptr = numStr;
                    while (*ptr) {
                        *out++ = *ptr++;
                    }
                    break;
                }
                case 'u': {
                    unsigned int u = va_arg(args, unsigned int);
                    char numStr[20];
                    muitoa(u, numStr, 10);
                    char *ptr = numStr;
                    while (*ptr) {
                        *out++ = *ptr++;
                    }
                    break;
                }
                case 'x': {
                    unsigned int x = va_arg(args, unsigned int);
                    char numStr[20];
                    muitoa(x, numStr, 16);
                    char *ptr = numStr;
                    while (*ptr) {
                        *out++ = *ptr++;
                    }
                    break;
                }
                case 's': {
                    char *s = va_arg(args, char *);
                    while (*s) {
                        *out++ = *s++;
                    }
                    break;
                }
                default:
                    *out++ = '%';  
                    *out++ = *fmt; 
            }
        } else {
            *out++ = *fmt;
        }
        fmt++;
    }
    *out = '\0';  
    va_end(args);

    return out - buf; 
}


//-----------------------------------------------------------------------------
void hw_divmod_u32(uint32_t dividend, uint32_t divisor, uint32_t *quotient, uint32_t *remainder)
{
  asm volatile (R"asm(
    movs       r6, #0xd0
    lsl        r6, r6, #24 // r2 = SIO_BASE
    str        %[dividend], [r6, #0x60] // SIO_DIV_UDIVIDEND
    str        %[divisor],  [r6, #0x64] // SIO_DIV_UDIVISOR
    b          1f
1:  b          1f
1:  b          1f
1:  b          1f
1:  ldr        r7, [r6, #0x74] // SIO_DIV_REMAINDER
    str        r7, [%[remainder]]
    ldr        r7, [r6, #0x70] // SIO_DIV_QUOTIENT
    str        r7, [%[quotient]]
    )asm"
    : [quotient] "+r" (quotient), [remainder] "+r" (remainder)
    : [dividend] "r" (dividend), [divisor] "r" (divisor)
    : "r6", "r7"
  );
}

//-----------------------------------------------------------------------------
void hw_divmod_s32(int32_t dividend, int32_t divisor, int32_t *quotient, int32_t *remainder)
{
  asm volatile (R"asm(
    movs       r6, #0xd0
    lsl        r6, r6, #24 // r2 = SIO_BASE
    str        %[dividend], [r6, #0x68] // SIO_DIV_SDIVIDEND
    str        %[divisor],  [r6, #0x6c] // SIO_DIV_SDIVISOR
    b          1f
1:  b          1f
1:  b          1f
1:  b          1f
1:  ldr        r7, [r6, #0x74] // SIO_DIV_REMAINDER
    str        r7, [%[remainder]]
    ldr        r7, [r6, #0x70] // SIO_DIV_QUOTIENT
    str        r7, [%[quotient]]
    )asm"
    : [quotient] "+r" (quotient), [remainder] "+r" (remainder)
    : [dividend] "r" (dividend), [divisor] "r" (divisor)
    : "r6", "r7"
  );
}

//-----------------------------------------------------------------------------
void format_hex(char *buf, uint32_t v, int size)
{
  static const char hex[] = "0123456789abcdef";

  for (int i = 0; i < size; i++)
  {
    int offs = ((size-1) - i) * 4;
    buf[i] = hex[(v >> offs) & 0xf];
  }

  buf[size] = 0;
}

//-----------------------------------------------------------------------------
void format_dec(char *buf, uint32_t v, int size)
{
  uint32_t remainder;
  int len = 0;

  do
  {
    hw_divmod_u32(v, 10, &v, &remainder);
    buf[len++] = '0' + remainder;
    size--;
  } while (v);

  while (size > 0)
  {
    buf[len++] = ' ';
    size--;
  }

  for (int i = 0; i < len/2; i++)
  {
    int t = buf[i];
    buf[i] = buf[len-1 - i];
    buf[len-1 - i] = t;
  }

  buf[len] = 0;
}
