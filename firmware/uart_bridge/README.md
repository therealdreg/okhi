Raspberry Pi Pico USB-UART Bridge
=================================

This program bridges the Raspberry Pi Pico HW UARTs to two independent USB CDC serial devices in order to behave like any other USB-to-UART Bridge controllers.

okhi fork
---------

Modified by Dreg for okhi, the Open Keylogger Hardware Implant. It used to live in `stuff/pico-uart-bridge-dregmod`, built out of tree on Linux against a `pico-sdk` git submodule. It is now a normal okhi firmware project: it builds with the same Pico SDK 2.3.0 toolchain as `firmware/usb/rp` and `firmware/ps2/rp`, and works with the official Raspberry Pi Pico VS Code extension.

**This is a keylogger specific fork, not a general purpose USB-UART bridge.** Everything below assumes okhi wiring. If you want the generic, unmodified flasher, take it from https://github.com/therealdreg/pico-esp-flasher

What the fork changes:

- A single USB CDC interface (`CFG_TUD_CDC 1`), so one COM port instead of two. Only UART0 is bridged.
- The CDC is serviced unconditionally instead of behind `tud_cdc_n_connected()`. On Windows, esptool opens the port with DTR=0, which made the original filter drop traffic. See https://github.com/espressif/esptool/issues/1119
- `tud_cdc_line_state_cb()` maps the CDC control lines onto the ESP32: RTS drives CHIP_PU (GPIO28), DTR drives EBOOT (GPIO14). ELOG is GPIO15.
- BOOTSEL is polled at startup and jumps to `reset_usb_boot()`, so the board can be reflashed without unplugging it.

The build is deliberately left off `PICO_DEOPTIMIZED_DEBUG`, unlike the okhi RP projects: the UART FIFOs are disabled, so RX takes one interrupt per byte, and flashing runs at 921600 baud.

`uart_bridge.uf2` is a release input. `make_release.bat` copies it out of `build/`, so the build output stays versioned like the other firmware artifacts.

Raspberry Pi Pico Pinout
------------------------

| Raspberry Pi Pico GPIO | Function |
|:----------------------:|:--------:|
| GPIO16 (Pin 21)        | UART0 TX |
| GPIO17 (Pin 22)        | UART0 RX |

Building
--------

Same as any other okhi RP target. From VS Code, open this folder with the Raspberry Pi Pico extension. From the command line:

```powershell
& "C:\Users\regue\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" -G Ninja `
  -S "c:\Users\regue\Desktop\okhi\firmware\uart_bridge" `
  -B "c:\Users\regue\Desktop\okhi\firmware\uart_bridge\build" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_MAKE_PROGRAM="C:/Users/regue/.pico-sdk/ninja/v1.13.2/ninja.exe"

& "C:\Users\regue\.pico-sdk\ninja\v1.13.2\ninja.exe" -C "c:\Users\regue\Desktop\okhi\firmware\uart_bridge\build"
```

Disclaimer
----------

This software is provided without warranty, according to the MIT License, and should therefore not be used where it may endanger life, financial stakes, or cause discomfort and inconvenience to others.
