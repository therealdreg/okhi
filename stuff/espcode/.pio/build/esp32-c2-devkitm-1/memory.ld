/**
 *                    ESP32-C2 Linker Script Memory Layout
 * This file describes the memory layout (memory blocks) by virtual memory addresses.
 * This linker script is passed through the C preprocessor to include configuration options.
 * Please use preprocessor features sparingly!
 * Restrict to simple macros with numeric values, and/or #if/#endif blocks.
 */
/*

 * Automatically generated file. DO NOT EDIT.

 * Espressif IoT Development Framework (ESP-IDF) 5.1.4 Configuration Header

 */
       
/* List of deprecated options */
/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* CPU instruction prefetch padding size for flash mmap scenario */
_esp_flash_mmap_prefetch_pad_size = 16;
/* CPU instruction prefetch padding size for memory protection scenario */
_esp_memprot_prefetch_pad_size = 0;
/* Memory alignment size for PMS */
_esp_memprot_align_size = 0;
_esp_mmu_block_size = (0x10000);
MEMORY
{
  /**
   *  All these values assume the flash cache is on, and have the blocks this uses subtracted from the length
   *  of the various regions. The 'data access port' dram/drom regions map to the same iram/irom regions but
   *  are connected to the data port of the CPU and eg allow byte-wise access.
   */
  /* IRAM for PRO CPU. */
  iram0_0_seg (RX) : org = (0x4037C000 + 0x4000), len = 0x403AEB70 - (0x4037C000 - 0x3FCA0000 + 0x4000) - (0x3FCA0000)
  /* Flash mapped instruction data */
  iram0_2_seg (RX) : org = 0x42000020, len = 0x400000-0x20
  /**
   * (0x20 offset above is a convenience for the app binary image generation.
   * Flash cache has 64KB pages. The .bin file which is flashed to the chip
   * has a 0x18 byte file header, and each segment has a 0x08 byte segment
   * header. Setting this offset makes it simple to meet the flash cache MMU's
   * constraint that (paddr % 64KB == vaddr % 64KB).)
   */
  /**
   * Shared data RAM, excluding memory reserved for ROM bss/data/stack.
   * Enabling Bluetooth & Trace Memory features in menuconfig will decrease the amount of RAM available.
   */
  dram0_0_seg (RW) : org = (0x3FCA0000), len = 0x403AEB70 - (0x4037C000 - 0x3FCA0000 + 0x4000) - (0x3FCA0000)
  /* Flash mapped constant data */
  drom0_0_seg (R) : org = 0x3C000020, len = 0x400000-0x20
  /* (See iram0_2_seg for meaning of 0x20 offset in the above.) */
}
/* Heap ends at top of dram0_0_seg */
_heap_end = 0x40000000;
  REGION_ALIAS("default_code_seg", iram0_2_seg);
  REGION_ALIAS("default_rodata_seg", drom0_0_seg);
/**
 *  If rodata default segment is placed in `drom0_0_seg`, then flash's first rodata section must
 *  also be first in the segment.
 */
  ASSERT(_flash_rodata_dummy_start == ORIGIN(default_rodata_seg),
         ".flash_rodata_dummy section must be placed at the beginning of the rodata segment.")
