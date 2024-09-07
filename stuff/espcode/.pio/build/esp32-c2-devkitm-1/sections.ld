/* Automatically generated file; DO NOT EDIT */
/* Espressif IoT Development Framework Linker Script */
/* Generated from: C:\Users\dreg\.platformio\packages\framework-espidf\components\esp_system\ld\esp32c2\sections.ld.in */

/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Default entry point */
ENTRY(call_start_cpu0);

SECTIONS
{
  .iram0.text :
  {
    _iram_start = ABSOLUTE(.);
    /* Vectors go to start of IRAM */
    ASSERT(ABSOLUTE(.) % 0x100 == 0, "vector address must be 256 byte aligned");
    KEEP(*(.exception_vectors.text));
    . = ALIGN(4);

    _invalid_pc_placeholder = ABSOLUTE(.);

    /* Code marked as running out of IRAM */
    _iram_text_start = ABSOLUTE(.);

    *(.iram1 .iram1.*)
    *libapp_trace.a:app_trace.*(.literal .literal.* .text .text.*)
    *libapp_trace.a:app_trace_util.*(.literal .literal.* .text .text.*)
    *libapp_trace.a:port_uart.*(.literal .literal.* .text .text.*)
    *libdriver.a:gptimer.*(.literal.gptimer_default_isr .text.gptimer_default_isr)
    *libesp_event.a:default_event_loop.*(.literal.esp_event_isr_post .text.esp_event_isr_post)
    *libesp_event.a:esp_event.*(.literal.esp_event_isr_post_to .text.esp_event_isr_post_to)
    *libesp_hw_support.a:cpu.*(.literal.esp_cpu_compare_and_set .text.esp_cpu_compare_and_set)
    *libesp_hw_support.a:cpu.*(.literal.esp_cpu_reset .text.esp_cpu_reset)
    *libesp_hw_support.a:cpu.*(.literal.esp_cpu_stall .text.esp_cpu_stall)
    *libesp_hw_support.a:cpu.*(.literal.esp_cpu_unstall .text.esp_cpu_unstall)
    *libesp_hw_support.a:cpu.*(.literal.esp_cpu_wait_for_intr .text.esp_cpu_wait_for_intr)
    *libesp_hw_support.a:esp_gpio_reserve.*(.literal.esp_gpio_is_pin_reserved .text.esp_gpio_is_pin_reserved)
    *libesp_hw_support.a:esp_gpio_reserve.*(.literal.esp_gpio_reserve_pins .text.esp_gpio_reserve_pins)
    *libesp_hw_support.a:esp_memory_utils.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:periph_ctrl.*(.literal.periph_module_reset .text.periph_module_reset)
    *libesp_hw_support.a:periph_ctrl.*(.literal.wifi_module_disable .text.wifi_module_disable)
    *libesp_hw_support.a:periph_ctrl.*(.literal.wifi_module_enable .text.wifi_module_enable)
    *libesp_hw_support.a:rtc_clk.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:rtc_sleep.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:rtc_time.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:sar_periph_ctrl.*(.literal.sar_periph_ctrl_power_enable .text.sar_periph_ctrl_power_enable)
    *libesp_hw_support.a:systimer.*(.literal .literal.* .text .text.*)
    *libesp_mm.a:esp_cache.*(.literal .literal.* .text .text.*)
    *libesp_ringbuf.a:(.literal .literal.* .text .text.*)
    *libesp_rom.a:esp_rom_spiflash.*(.literal .literal.* .text .text.*)
    *libesp_rom.a:esp_rom_systimer.*(.literal .literal.* .text .text.*)
    *libesp_rom.a:esp_rom_tlsf.*(.literal .literal.* .text .text.*)
    *libesp_rom.a:esp_rom_wdt.*(.literal .literal.* .text .text.*)
    *libesp_system.a:esp_err.*(.literal .literal.* .text .text.*)
    *libesp_system.a:esp_system_chip.*(.literal.esp_system_abort .text.esp_system_abort)
    *libesp_system.a:ubsan.*(.literal .literal.* .text .text.*)
    *libesp_wifi.a:esp_adapter.*(.literal.coex_pti_get_wrapper .text.coex_pti_get_wrapper)
    *libesp_wifi.a:wifi_netif.*(.literal.wifi_sta_receive .text.wifi_sta_receive)
    *libesp_wifi.a:wifi_netif.*(.literal.wifi_transmit_wrap .text.wifi_transmit_wrap)
    *libfreertos.a:(EXCLUDE_FILE(*libfreertos.a:app_startup.* *libfreertos.a:idf_additions.*) .literal EXCLUDE_FILE(*libfreertos.a:app_startup.* *libfreertos.a:idf_additions.*) .literal.* EXCLUDE_FILE(*libfreertos.a:app_startup.* *libfreertos.a:idf_additions.*) .text EXCLUDE_FILE(*libfreertos.a:app_startup.* *libfreertos.a:idf_additions.*) .text.*)
    *libgcc.a:_divsf3.*(.literal .literal.* .text .text.*)
    *libgcc.a:lib2funcs.*(.literal .literal.* .text .text.*)
    *libgcc.a:save-restore.*(.literal .literal.* .text .text.*)
    *libgcov.a:(.literal .literal.* .text .text.*)
    *libhal.a:cache_hal.*(.literal .literal.* .text .text.*)
    *libhal.a:i2c_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:ledc_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:mmu_hal.*(.literal .literal.* .text .text.*)
    *libhal.a:spi_flash_encrypt_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:spi_flash_hal_gpspi.*(.literal .literal.* .text .text.*)
    *libhal.a:spi_flash_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:spi_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:spi_slave_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:timer_hal.*(.literal.timer_hal_capture_and_get_counter_value .text.timer_hal_capture_and_get_counter_value)
    *libheap.a:multi_heap.*(.literal._multi_heap_lock .text._multi_heap_lock)
    *libheap.a:multi_heap.*(.literal._multi_heap_unlock .text._multi_heap_unlock)
    *libheap.a:multi_heap.*(.literal.multi_heap_in_rom_init .text.multi_heap_in_rom_init)
    *liblog.a:log.*(.literal.esp_log_write .text.esp_log_write)
    *liblog.a:log_freertos.*(.literal.esp_log_early_timestamp .text.esp_log_early_timestamp)
    *liblog.a:log_freertos.*(.literal.esp_log_impl_lock .text.esp_log_impl_lock)
    *liblog.a:log_freertos.*(.literal.esp_log_impl_lock_timeout .text.esp_log_impl_lock_timeout)
    *liblog.a:log_freertos.*(.literal.esp_log_impl_unlock .text.esp_log_impl_unlock)
    *liblog.a:log_freertos.*(.literal.esp_log_timestamp .text.esp_log_timestamp)
    *libnet80211.a:(.wifi0iram .wifi0iram.*)
    *libnet80211.a:(.wifirxiram .wifirxiram.*)
    *libnet80211.a:(.wifislprxiram .wifislprxiram.*)
    *libnewlib.a:abort.*(.literal .literal.* .text .text.*)
    *libnewlib.a:assert.*(.literal .literal.* .text .text.*)
    *libnewlib.a:heap.*(.literal .literal.* .text .text.*)
    *libnewlib.a:port_stdatomic.*(.literal .literal.* .text .text.*)
    *libnewlib.a:stdatomic.*(.literal .literal.* .text .text.*)
    *libpp.a:(.wifi0iram .wifi0iram.*)
    *libpp.a:(.wifiorslpiram .wifiorslpiram.*)
    *libpp.a:(.wifirxiram .wifirxiram.*)
    *libpp.a:(.wifislprxiram .wifislprxiram.*)
    *libriscv.a:interrupt.*(.text .text._global_interrupt_handler .text.esprv_intc_get_interrupt_unmask .text.esprv_intc_int_get_priority .text.esprv_intc_int_get_type .text.intr_handler_get_arg .text.intr_matrix_route)
    *libriscv.a:vectors.*(.literal .literal.* .text .text.*)
    *librtc.a:(.literal .literal.* .text .text.*)
    *libsoc.a:lldesc.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:flash_brownout_hook.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:memspi_host_driver.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_boya.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_gd.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_generic.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_issi.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_mxic.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_th.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_winbond.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_wrap.*(.literal .literal.* .text .text.*)

  } > iram0_0_seg

  /**
   * This section is required to skip .iram0.text area because iram0_0_seg and
   * dram0_0_seg reflect the same address space on different buses.
   */
  .dram0.dummy (NOLOAD):
  {
    . = ORIGIN(dram0_0_seg) + _iram_end - _iram_start;
  } > dram0_0_seg

  /**
   * This section MUST be placed at the beginning of the DRAM0,
   * which will be released along with iram0_bt.text when Bluetooth is no longer in use
   */
 .dram0.bt.data :
  {
    _data_start = ABSOLUTE(.);
    . = ALIGN(8);
  } > dram0_0_seg

  .dram0.bt.bss (NOLOAD) :
  {
    . = ALIGN (8);
    _bss_bt_start = ABSOLUTE(.);
    _bss_bt_end = ABSOLUTE(.);
  } > dram0_0_seg

  .dram0.data :
  {
    *(.gnu.linkonce.d.*)
    *(.data1)
    __global_pointer$ = . + 0x800;
    *(.sdata)
    *(.sdata.*)
    *(.gnu.linkonce.s.*)
    *(.gnu.linkonce.s2.*)
    *(.jcr)

    *(.data .data.*)
    *(.dram1 .dram1.*)
    _coredump_dram_start = ABSOLUTE(.);
    *(.dram2.coredump .dram2.coredump.*)
    _coredump_dram_end = ABSOLUTE(.);
    *libapp_trace.a:app_trace.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libapp_trace.a:app_trace_util.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libapp_trace.a:port_uart.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:esp_memory_utils.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:rtc_clk.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:systimer.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_mm.a:esp_cache.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_spiflash.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_systimer.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_tlsf.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_wdt.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_system.a:esp_err.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_system.a:ubsan.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libfreertos.a:FreeRTOS-openocd.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libgcc.a:_divsf3.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libgcc.a:save-restore.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libgcov.a:(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:cache_hal.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:i2c_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:ledc_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:mmu_hal.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_flash_encrypt_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_flash_hal_gpspi.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_flash_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_slave_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:abort.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:assert.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:heap.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:port_stdatomic.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:stdatomic.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libphy.a:(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libsoc.a:lldesc.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:flash_brownout_hook.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:memspi_host_driver.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_boya.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_gd.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_generic.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_issi.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_mxic.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_th.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_winbond.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_wrap.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)

    _data_end = ABSOLUTE(.);
    . = ALIGN(4);
  } > dram0_0_seg

  /**
   * This section holds data that should not be initialized at power up.
   * The section located in Internal SRAM memory region. The macro _NOINIT
   * can be used as attribute to place data into this section.
   * See the "esp_attr.h" file for more information.
   */
  .noinit (NOLOAD):
  {
    . = ALIGN(4);
    _noinit_start = ABSOLUTE(.);
    *(.noinit .noinit.*)
    . = ALIGN(4) ;
    _noinit_end = ABSOLUTE(.);
  } > dram0_0_seg

  /* Shared RAM */
  .dram0.bss (NOLOAD) :
  {
    . = ALIGN (8);
    _bss_start = ABSOLUTE(.);

    *(.bss .bss.*)
    *(.dynbss .dynsbss .gnu.linkonce.b .gnu.linkonce.b.* .gnu.linkonce.sb .gnu.linkonce.sb.* .gnu.linkonce.sb2 .gnu.linkonce.sb2.* .sbss .sbss.* .sbss2 .sbss2.* .scommon .share.mem)
    *(.ext_ram.bss .ext_ram.bss.*)
    *(COMMON)

    *(.dynsbss)
    *(.sbss)
    *(.sbss.*)
    *(.gnu.linkonce.sb.*)
    *(.scommon)
    *(.sbss2)
    *(.sbss2.*)
    *(.gnu.linkonce.sb2.*)
    *(.dynbss)
    *(.share.mem)
    *(.gnu.linkonce.b.*)

    . = ALIGN (8);
    _bss_end = ABSOLUTE(.);
  } > dram0_0_seg

  ASSERT(((_bss_end - ORIGIN(dram0_0_seg)) <= LENGTH(dram0_0_seg)), "DRAM segment data does not fit.")

  .flash.text :
  {
    _stext = .;
    _instruction_reserved_start = ABSOLUTE(.);  /* This is a symbol marking the flash.text start, this can be used for mmu driver to maintain virtual address */
    _text_start = ABSOLUTE(.);

    *(EXCLUDE_FILE(*libesp_ringbuf.a *libfreertos.a *libgcov.a *librtc.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libdriver.a:gptimer.* *libesp_event.a:default_event_loop.* *libesp_event.a:esp_event.* *libesp_hw_support.a:cpu.* *libesp_hw_support.a:esp_gpio_reserve.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:periph_ctrl.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:rtc_sleep.* *libesp_hw_support.a:rtc_time.* *libesp_hw_support.a:sar_periph_ctrl.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_tlsf.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:esp_system_chip.* *libesp_system.a:ubsan.* *libesp_wifi.a:esp_adapter.* *libesp_wifi.a:wifi_netif.* *libgcc.a:_divsf3.* *libgcc.a:lib2funcs.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libhal.a:timer_hal.* *libheap.a:multi_heap.* *liblog.a:log.* *liblog.a:log_freertos.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:port_stdatomic.* *libnewlib.a:stdatomic.* *libriscv.a:interrupt.* *libriscv.a:vectors.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .literal EXCLUDE_FILE(*libesp_ringbuf.a *libfreertos.a *libgcov.a *librtc.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libdriver.a:gptimer.* *libesp_event.a:default_event_loop.* *libesp_event.a:esp_event.* *libesp_hw_support.a:cpu.* *libesp_hw_support.a:esp_gpio_reserve.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:periph_ctrl.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:rtc_sleep.* *libesp_hw_support.a:rtc_time.* *libesp_hw_support.a:sar_periph_ctrl.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_tlsf.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:esp_system_chip.* *libesp_system.a:ubsan.* *libesp_wifi.a:esp_adapter.* *libesp_wifi.a:wifi_netif.* *libgcc.a:_divsf3.* *libgcc.a:lib2funcs.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libhal.a:timer_hal.* *libheap.a:multi_heap.* *liblog.a:log.* *liblog.a:log_freertos.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:port_stdatomic.* *libnewlib.a:stdatomic.* *libriscv.a:interrupt.* *libriscv.a:vectors.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .literal.* EXCLUDE_FILE(*libesp_ringbuf.a *libfreertos.a *libgcov.a *librtc.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libdriver.a:gptimer.* *libesp_event.a:default_event_loop.* *libesp_event.a:esp_event.* *libesp_hw_support.a:cpu.* *libesp_hw_support.a:esp_gpio_reserve.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:periph_ctrl.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:rtc_sleep.* *libesp_hw_support.a:rtc_time.* *libesp_hw_support.a:sar_periph_ctrl.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_tlsf.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:esp_system_chip.* *libesp_system.a:ubsan.* *libesp_wifi.a:esp_adapter.* *libesp_wifi.a:wifi_netif.* *libgcc.a:_divsf3.* *libgcc.a:lib2funcs.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libhal.a:timer_hal.* *libheap.a:multi_heap.* *liblog.a:log.* *liblog.a:log_freertos.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:port_stdatomic.* *libnewlib.a:stdatomic.* *libriscv.a:interrupt.* *libriscv.a:vectors.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .text EXCLUDE_FILE(*libesp_ringbuf.a *libfreertos.a *libgcov.a *librtc.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libdriver.a:gptimer.* *libesp_event.a:default_event_loop.* *libesp_event.a:esp_event.* *libesp_hw_support.a:cpu.* *libesp_hw_support.a:esp_gpio_reserve.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:periph_ctrl.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:rtc_sleep.* *libesp_hw_support.a:rtc_time.* *libesp_hw_support.a:sar_periph_ctrl.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_tlsf.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:esp_system_chip.* *libesp_system.a:ubsan.* *libesp_wifi.a:esp_adapter.* *libesp_wifi.a:wifi_netif.* *libgcc.a:_divsf3.* *libgcc.a:lib2funcs.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libhal.a:timer_hal.* *libheap.a:multi_heap.* *liblog.a:log.* *liblog.a:log_freertos.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:port_stdatomic.* *libnewlib.a:stdatomic.* *libriscv.a:interrupt.* *libriscv.a:vectors.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .text.*)
    *(EXCLUDE_FILE(*libnet80211.a *libpp.a) .wifi0iram EXCLUDE_FILE(*libnet80211.a *libpp.a) .wifi0iram.*)
    *(.wifiextrairam .wifiextrairam.*)
    *(EXCLUDE_FILE(*libpp.a) .wifiorslpiram EXCLUDE_FILE(*libpp.a) .wifiorslpiram.*)
    *(EXCLUDE_FILE(*libnet80211.a *libpp.a) .wifirxiram EXCLUDE_FILE(*libnet80211.a *libpp.a) .wifirxiram.*)
    *(.wifislpiram .wifislpiram.*)
    *(EXCLUDE_FILE(*libnet80211.a *libpp.a) .wifislprxiram EXCLUDE_FILE(*libnet80211.a *libpp.a) .wifislprxiram.*)
    *libdriver.a:gptimer.*(.text .text.gptimer_del_timer .text.gptimer_destroy .text.gptimer_disable .text.gptimer_enable .text.gptimer_get_captured_count .text.gptimer_get_raw_count .text.gptimer_get_resolution .text.gptimer_new_timer .text.gptimer_register_event_callbacks .text.gptimer_release_group_handle .text.gptimer_set_alarm_action .text.gptimer_set_raw_count .text.gptimer_start .text.gptimer_stop)
    *libesp_event.a:default_event_loop.*(.text .text.esp_event_handler_instance_register .text.esp_event_handler_instance_unregister .text.esp_event_handler_register .text.esp_event_handler_unregister .text.esp_event_loop_create_default .text.esp_event_loop_delete_default .text.esp_event_post)
    *libesp_event.a:esp_event.*(.text .text.base_node_add_handler .text.esp_event_dump .text.esp_event_handler_instance_register_with .text.esp_event_handler_instance_unregister_with .text.esp_event_handler_register_with .text.esp_event_handler_register_with_internal .text.esp_event_handler_unregister_with .text.esp_event_handler_unregister_with_internal .text.esp_event_loop_create .text.esp_event_loop_delete .text.esp_event_loop_run .text.esp_event_loop_run_task .text.esp_event_post_to .text.handler_instances_add .text.handler_instances_remove .text.handler_instances_remove_all .text.loop_node_add_handler)
    *libesp_hw_support.a:cpu.*(.text .text.esp_cpu_clear_breakpoint .text.esp_cpu_clear_watchpoint .text.esp_cpu_intr_get_desc .text.esp_cpu_set_breakpoint .text.esp_cpu_set_watchpoint .text.semihosting_call_noerrno)
    *libesp_hw_support.a:esp_gpio_reserve.*(.text)
    *libesp_hw_support.a:periph_ctrl.*(.text .text.periph_ll_get_clk_en_mask .text.periph_ll_get_rst_en_mask.constprop.0 .text.periph_ll_get_rst_en_reg .text.periph_module_disable .text.periph_module_enable)
    *libesp_hw_support.a:sar_periph_ctrl.*(.text .text.sar_periph_ctrl_adc_oneshot_power_acquire .text.sar_periph_ctrl_adc_oneshot_power_release .text.sar_periph_ctrl_init .text.sar_periph_ctrl_power_disable .text.sar_periph_ctrl_pwdet_power_acquire .text.sar_periph_ctrl_pwdet_power_release .text.unlikely.sar_periph_ctrl_adc_continuous_power_acquire .text.unlikely.sar_periph_ctrl_adc_continuous_power_release)
    *libesp_system.a:esp_system_chip.*(.text .text.esp_get_free_heap_size .text.esp_get_free_internal_heap_size .text.esp_get_idf_version .text.esp_get_minimum_free_heap_size)
    *libesp_wifi.a:esp_adapter.*(.text .text.clear_intr_wrapper .text.coex_deinit_wrapper .text.coex_disable_wrapper .text.coex_enable_wrapper .text.coex_init_wrapper .text.coex_register_start_cb_wrapper .text.coex_schm_curr_period_get_wrapper .text.coex_schm_curr_phase_get_wrapper .text.coex_schm_interval_get_wrapper .text.coex_schm_process_restart_wrapper .text.coex_schm_register_cb_wrapper .text.coex_schm_status_bit_clear_wrapper .text.coex_schm_status_bit_set_wrapper .text.coex_wifi_channel_set_wrapper .text.coex_wifi_request_wrapper .text.disable_intr_wrapper .text.enable_intr_wrapper .text.esp_event_post_wrapper .text.esp_log_write_wrapper .text.esp_log_writev_wrapper .text.esp_phy_disable_wrapper .text.esp_phy_enable_wrapper .text.esp_read_mac_wrapper .text.event_group_wait_bits_wrapper .text.get_time_wrapper .text.mutex_create_wrapper .text.mutex_delete_wrapper .text.nvs_open_wrapper .text.queue_create_wrapper .text.queue_recv_wrapper .text.queue_send_to_back_wrapper .text.queue_send_to_front_wrapper .text.queue_send_wrapper .text.recursive_mutex_create_wrapper .text.set_intr_wrapper .text.set_isr_wrapper .text.task_create_pinned_to_core_wrapper .text.task_create_wrapper .text.task_get_max_priority_wrapper .text.wifi_clock_disable_wrapper .text.wifi_clock_enable_wrapper .text.wifi_create_queue .text.wifi_create_queue_wrapper .text.wifi_delete_queue .text.wifi_delete_queue_wrapper .text.wifi_reset_mac_wrapper .text.wifi_thread_semphr_free .text.wifi_thread_semphr_get_wrapper)
    *libesp_wifi.a:wifi_netif.*(.text .text.esp_wifi_create_if_driver .text.esp_wifi_destroy_if_driver .text.esp_wifi_get_if_mac .text.esp_wifi_is_if_ready_when_started .text.esp_wifi_register_if_rxcb .text.wifi_ap_receive .text.wifi_driver_start .text.wifi_free .text.wifi_transmit)
    *libfreertos.a:app_startup.*(.literal .literal.* .text .text.*)
    *libfreertos.a:idf_additions.*(.literal .literal.* .text .text.*)
    *libhal.a:timer_hal.*(.text .text.timer_hal_deinit .text.timer_hal_init .text.timer_hal_set_counter_value)
    *libheap.a:multi_heap.*(.text)
    *liblog.a:log.*(.text .text.esp_log_level_get .text.esp_log_level_set .text.esp_log_set_vprintf .text.esp_log_writev .text.heap_bubble_down .text.s_log_level_get_and_unlock)
    *liblog.a:log_freertos.*(.text .text.esp_log_system_timestamp)
    *libriscv.a:interrupt.*(.literal.intr_handler_get .text.intr_handler_get)
    *libriscv.a:interrupt.*(.literal.intr_handler_set .text.intr_handler_set)

    *(.stub .gnu.warning .gnu.linkonce.literal.* .gnu.linkonce.t.*.literal .gnu.linkonce.t.*)
    *(.irom0.text) /* catch stray ICACHE_RODATA_ATTR */
    *(.fini.literal)
    *(.fini)
    *(.gnu.version)

    /** CPU will try to prefetch up to 16 bytes of
      * of instructions. This means that any configuration (e.g. MMU, PMS) must allow
      * safe access to up to 16 bytes after the last real instruction, add
      * dummy bytes to ensure this
      */
    . += _esp_flash_mmap_prefetch_pad_size;

    _text_end = ABSOLUTE(.);
    _instruction_reserved_end = ABSOLUTE(.);  /* This is a symbol marking the flash.text end, this can be used for mmu driver to maintain virtual address */
    _etext = .;

    /**
     * Similar to _iram_start, this symbol goes here so it is
     * resolved by addr2line in preference to the first symbol in
     * the flash.text segment.
     */
    _flash_cache_start = ABSOLUTE(0);
  } > default_code_seg

  /**
   * This dummy section represents the .flash.text section but in default_rodata_seg.
   * Thus, it must have its alignment and (at least) its size.
   */
  .flash_rodata_dummy (NOLOAD):
  {
    _flash_rodata_dummy_start = .;
    /* Start at the same alignment constraint than .flash.text */
    . = ALIGN(ALIGNOF(.flash.text));
    /* Create an empty gap as big as .flash.text section */
    . = . + SIZEOF(.flash.text);
    /* Prepare the alignment of the section above. Few bytes (0x20) must be
     * added for the mapping header. */
    . = ALIGN(_esp_mmu_block_size) + 0x20;
  } > default_rodata_seg

  .flash.appdesc : ALIGN(0x10)
  {
    _rodata_reserved_start = ABSOLUTE(.);  /* This is a symbol marking the flash.rodata start, this can be used for mmu driver to maintain virtual address */
    _rodata_start = ABSOLUTE(.);

    *(.rodata_desc .rodata_desc.*)               /* Should be the first.  App version info.        DO NOT PUT ANYTHING BEFORE IT! */
    *(.rodata_custom_desc .rodata_custom_desc.*) /* Should be the second. Custom app version info. DO NOT PUT ANYTHING BEFORE IT! */

    /* Create an empty gap within this section. Thanks to this, the end of this
     * section will match .flash.rodata's begin address. Thus, both sections
     * will be merged when creating the final bin image. */
    . = ALIGN(ALIGNOF(.flash.rodata));
  } >default_rodata_seg

  .flash.rodata : ALIGN(0x10)
  {
    _flash_rodata_start = ABSOLUTE(.);

    *(EXCLUDE_FILE(*libgcov.a *libphy.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_tlsf.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:ubsan.* *libfreertos.a:FreeRTOS-openocd.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:port_stdatomic.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .rodata EXCLUDE_FILE(*libgcov.a *libphy.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_tlsf.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:ubsan.* *libfreertos.a:FreeRTOS-openocd.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:port_stdatomic.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .rodata.* EXCLUDE_FILE(*libgcov.a *libphy.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_tlsf.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:ubsan.* *libfreertos.a:FreeRTOS-openocd.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:port_stdatomic.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .sdata2 EXCLUDE_FILE(*libgcov.a *libphy.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_tlsf.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:ubsan.* *libfreertos.a:FreeRTOS-openocd.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:port_stdatomic.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .sdata2.* EXCLUDE_FILE(*libgcov.a *libphy.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_tlsf.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:ubsan.* *libfreertos.a:FreeRTOS-openocd.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:port_stdatomic.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .srodata EXCLUDE_FILE(*libgcov.a *libphy.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_tlsf.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:ubsan.* *libfreertos.a:FreeRTOS-openocd.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:port_stdatomic.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .srodata.*)
    *(.rodata_wlog_error .rodata_wlog_error.*)
    *(.rodata_wlog_info .rodata_wlog_info.*)
    *(.rodata_wlog_warning .rodata_wlog_warning.*)

    *(.irom1.text) /* catch stray ICACHE_RODATA_ATTR */
    *(.gnu.linkonce.r.*)
    *(.rodata1)
    __XT_EXCEPTION_TABLE_ = ABSOLUTE(.);
    *(.xt_except_table)
    *(.gcc_except_table .gcc_except_table.*)
    *(.gnu.linkonce.e.*)
    *(.gnu.version_r)
    . = (. + 7) & ~ 3;
    /*
     * C++ constructor and destructor tables
     * Don't include anything from crtbegin.o or crtend.o, as IDF doesn't use toolchain crt.
     *
     * RISC-V gcc is configured with --enable-initfini-array so it emits an .init_array section instead.
     * But the init_priority sections will be sorted for iteration in ascending order during startup.
     * The rest of the init_array sections is sorted for iteration in descending order during startup, however.
     * Hence a different section is generated for the init_priority functions which is iterated in
     * ascending order during startup. The corresponding code can be found in startup.c.
     */
    __init_priority_array_start = ABSOLUTE(.);
    KEEP (*(EXCLUDE_FILE (*crtend.* *crtbegin.*) .init_array.*))
    __init_priority_array_end = ABSOLUTE(.);
    __init_array_start = ABSOLUTE(.);
    KEEP (*(EXCLUDE_FILE (*crtend.* *crtbegin.*) .init_array))
    __init_array_end = ABSOLUTE(.);
    KEEP (*crtbegin.*(.dtors))
    KEEP (*(EXCLUDE_FILE (*crtend.*) .dtors))
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))
    /* C++ exception handlers table: */
    __XT_EXCEPTION_DESCS_ = ABSOLUTE(.);
    *(.xt_except_desc)
    *(.gnu.linkonce.h.*)
    __XT_EXCEPTION_DESCS_END__ = ABSOLUTE(.);
    *(.xt_except_desc_end)
    *(.dynamic)
    *(.gnu.version_d)
    /* Addresses of memory regions reserved via SOC_RESERVE_MEMORY_REGION() */
    soc_reserved_memory_region_start = ABSOLUTE(.);
    KEEP (*(.reserved_memory_address))
    soc_reserved_memory_region_end = ABSOLUTE(.);
    /* System init functions registered via ESP_SYSTEM_INIT_FN */
    _esp_system_init_fn_array_start = ABSOLUTE(.);
    KEEP (*(SORT_BY_INIT_PRIORITY(.esp_system_init_fn.*)))
    _esp_system_init_fn_array_end = ABSOLUTE(.);
    _rodata_end = ABSOLUTE(.);
    /* Literals are also RO data. */
    _lit4_start = ABSOLUTE(.);
    *(*.lit4)
    *(.lit4.*)
    *(.gnu.linkonce.lit4.*)
    _lit4_end = ABSOLUTE(.);
    . = ALIGN(4);
    _thread_local_start = ABSOLUTE(.);
    *(.tdata)
    *(.tdata.*)
    *(.tbss)
    *(.tbss.*)
    _thread_local_end = ABSOLUTE(.);
    . = ALIGN(ALIGNOF(.eh_frame));
  } > default_rodata_seg

  /* Keep this section shall be at least aligned on 4 */
  .eh_frame : ALIGN(8)
  {
    __eh_frame = ABSOLUTE(.);
    KEEP (*(.eh_frame))
    __eh_frame_end = ABSOLUTE(.);
    /* Guarantee that this section and the next one will be merged by making
     * them adjacent. */
    . = ALIGN(ALIGNOF(.eh_frame_hdr));
  } > default_rodata_seg

  /* To avoid any exception in C++ exception frame unwinding code, this section
   * shall be aligned on 8. */
  .eh_frame_hdr : ALIGN(8)
  {
    __eh_frame_hdr = ABSOLUTE(.);
    KEEP (*(.eh_frame_hdr))
    __eh_frame_hdr_end = ABSOLUTE(.);
  } > default_rodata_seg

  /*
    This section is a place where we dump all the rodata which aren't used at runtime,
    so as to avoid binary size increase
  */
  .flash.rodata_noload (NOLOAD) :
  {
    /*
      This is a symbol marking the flash.rodata end, this can be used for mmu driver to maintain virtual address
      We don't need to include the noload rodata in this section
    */
    _rodata_reserved_end = ABSOLUTE(.);
    . = ALIGN (4);
    *(.rodata_wlog_debug .rodata_wlog_debug.*)
    *(.rodata_wlog_verbose .rodata_wlog_verbose.*)
  } > default_rodata_seg

  /* Marks the end of IRAM code segment */
  .iram0.text_end (NOLOAD) :
  {
    /* ESP32-C2 memprot requires 16B padding for possible CPU prefetch and 512B alignment for PMS split lines */
    . += _esp_memprot_prefetch_pad_size;
    . = ALIGN(_esp_memprot_align_size);
    /* iram_end_test section exists for use by memprot unit tests only */
    *(.iram_end_test)
    _iram_text_end = ABSOLUTE(.);
  } > iram0_0_seg

  .iram0.data :
  {
    . = ALIGN(16);
    _iram_data_start = ABSOLUTE(.);

    *(.iram.data .iram.data.*)
    _coredump_iram_start = ABSOLUTE(.);
    *(.iram2.coredump .iram2.coredump.*)
    _coredump_iram_end = ABSOLUTE(.);

    _iram_data_end = ABSOLUTE(.);
  } > iram0_0_seg

  .iram0.bss (NOLOAD) :
  {
    . = ALIGN(16);
    _iram_bss_start = ABSOLUTE(.);

    *(.iram.bss .iram.bss.*)

    _iram_bss_end = ABSOLUTE(.);
  } > iram0_0_seg

  /**
   * This section needs to be placed at the end of the IRAM0,
   * which will be released along with dram0_bt_data and dram0_bt_bss when Bluetooth is no longer in use
   */
  .iram0.bt.text :
  {
    . = ALIGN(16);
    _iram_bt_text_start = ABSOLUTE(.);

    . = ALIGN(16);
    _iram_end = ABSOLUTE(.);
  } > iram0_0_seg

  /* Marks the end of data, bss and possibly rodata  */
  .dram0.heap_start (NOLOAD) :
  {
    . = ALIGN (16);
    _heap_start = ABSOLUTE(.);
  } > dram0_0_seg
}

ASSERT(((_iram_end - ORIGIN(iram0_0_seg)) <= LENGTH(iram0_0_seg)),
          "IRAM0 segment data does not fit.")

ASSERT(((_heap_start - ORIGIN(dram0_0_seg)) <= LENGTH(dram0_0_seg)),
          "DRAM segment data does not fit.")
