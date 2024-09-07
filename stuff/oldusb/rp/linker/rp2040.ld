/*
SPDX-License-Identifier: BSD-3-Clause
Copyright (c) 2021, Alex Taradov <alex@taradov.com>. All rights reserved.
*/

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

MEMORY
{
  boot  (rx) : ORIGIN = 0x10000000, LENGTH = 256
  flash (rx) : ORIGIN = 0x10000100, LENGTH = 2048K - 256
  ram  (rwx) : ORIGIN = 0x20000000, LENGTH = 264K
}

ENTRY(boot_entry)

SECTIONS
{
  .boot : ALIGN(4)
  {
    KEEP(*(.boot.entry))
    *(.boot*)
    . = 256-4;
    LONG(0xcccccccc) /* CRC */
  } > boot

  .text : ALIGN(4)
  {
    _text = .;
    KEEP(*(.vectors))
    *(.text*)
    *(.rodata)
    *(.rodata.*)
    . = ALIGN(4);
    _etext = .;
  } > ram AT > flash

  .firam : ALIGN(4)
  {
    *(.firam)
  } > ram

  .fflash :
  {
    *(.fflash*)
    . = ALIGN(4);
  } > flash

  .data : ALIGN(4)
  {
    _data = .;
    *(.ramfunc .ramfunc.*);
    *(vtable)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > ram AT > flash

  .bss : ALIGN(4)
  {
    _bss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > ram

  .flash_end : {
      __flash_binary_end = .;
  } > flash

  PROVIDE(_end = .);
  PROVIDE(_text_start = ORIGIN(flash));
  PROVIDE(_stack_top = ORIGIN(ram) + LENGTH(ram));
}
