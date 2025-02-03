# okhi - Open Keylogger Hardware Implant

![](stuff/images/insidekeyboard.png)

okhi is an implant that can be utilized to log keystrokes from a USB/PS2 keyboard. The implant is designed to be  easily concealable within a keyboard, laptop, or tower case. It is powered by the keyboard cable itself. The implant can be accessed via WiFi and enables real-time viewing of keystrokes.

It is based on the RP2040 + ESP chip. The RP2040 is responsible for sniffing & parsing the keyboard data, while the ESP chip is used to transmit the data over WiFi.

The **RP2040** features a dual-core Arm Cortex-M0+ processor, making it highly efficient for handling multiple tasks simultaneously (PIO rlz!).

okhi leverages the **ESP32-C2**, a new chip from Espressif, specifically the **ESP8684**. This chip includes a RISC-V single-core CPU, which is known for its small size. It is important to note that the ESP8266, ESP8285, and ESP8654 are different chips and should not be confused with the **ESP8684**.

----

![](stuff/images/wifiweb.jpg)

----

okhi is designed to be a proof of concept and educational tool. It is not intended to be used for malicious purposes.

okhi consumes ~120ma of current, the target & cable must be able to supply this amount, otherwise, the implant will not work.

Note that the keyboard itself consumes power as well. The maximum current for USB 2.0 is 500mA. Never exceed this limit.

Just connect VBUS, GND and data lines to okhi, and you are ready to go.

You can sniff keyboard data by connecting okhi to the end of the cable or to a through-hole pin connector inside the tower. It can also be connected to accessible pads or traces. If the keyboard data is present, okhi will be able to sniff it.

okhi connected using copper wires to keyboard PCB-pads:

![](stuff/images/withcables.jpg)

DIY-Keylogger POC:

https://github.com/therealdreg/pico-usb-sniffer-lite

# Appearance

Front (black or green)

![](stuff/images/front.png)

Back (black or green)

![](stuff/images/back.png)

# Size (mm)

![](stuff/images/size.jpg)

Check size before buying, maybe it is too big for your target. Or maybe you need drill some space inside the keyboard.

# Features

- PS2 and USB keyboard support
- WiFi + web support
- Real-time viewing of keystrokes
- Open Hardware
- Open Source (MIT License)
- Community support

# Where to buy

Early Stage project, so you can purchase it directly from me. Please send an email to dreg@rootkit.es for further inquiries.

In a few weeks, I will provide a public link to buy it.

Gerber, Pick and Place files and BOM will be available soon.

----

 At this moment only Windows is documented. Linux and Mac will be documented soon. I am only one person, so please be patient....

 ----

# What a mess of names!

The okhi board integrates two main processors: the RP2040 and the ESP8684-MINI-1-H4. The RP2040 is a dual-core Arm Cortex-M0+ processor running at up to 133 MHz, equipped with 264 KB of on-chip SRAM. While the RP2040 does not include flash memory, an additional 16 MB of external flash has been added, enhancing its storage capabilities. Additionally, the RP2040 is well-suited for overclocking. This processor is also the foundation for the popular Raspberry Pi Pico microcontroller board.

Complementing the RP2040, the ESP8684-MINI-1-H4 module operates at speeds up to 120 MHz and includes 4 MB of internal flash memory. It features a built-in PCB antenna, eliminating the need for an external one, and is housed within a PCB-to-PCB module that incorporates the ESP8684 chip along with other essential components.

The ESP8684 itself is a single-core RISC-V processor and serves as the core of the ESP32-C2 modules (ESP32-C2 is a generic name). The ESP32-C2 offers a cost-effective solution with Wi-Fi 4 and Bluetooth 5 (LE) connectivity, outperforming the older ESP8266 in both size and performance. It is built around a 32-bit single-core RISC-V processor, featuring 272 KB of SRAM (with 16 KB dedicated to cache) and 576 KB of ROM.

The ESP8684-DevKitM-1 provides a dedicated development board tailored for the ESP8684-MINI-1-H4 module.


# ESP8684-MINI-1-H4 module (ESP32-C2)

okhi uses ESP8684-MINI-1-H4 module (ESP32-C2), which is known for its small size.

![](stuff/images/miniesp.jpg)

ESP8684-MINI-1-H4 module vs ESP8684-WROOM-02C-H4 module:

![](stuff/images/ESP8684-MINI-1-H4_DIFFS_ESP8684-WROOM-02C-H4.jpg)

With WIFI speeds up to 72Mbps (9MB/s), this module is ideal for a physical keylogger. However, in real-world scenarios, the WIFI speed is typically much lower, but still sufficient for a web interface displaying keystrokes.

 # Starter pack

 Currently, the only way to buy okhi is with the starter pack. The starter pack includes:

- 1 okhi implant

- 1 auxboard: it allows you to program the implant. It is also used to test the implant out of the keyboard (man in the middle USB).

To test implant (USB): insert the sniff-male-pins (GND must coincide with G pin) into aux board sniff-female-pins, connect keyboard to USB female and connect the auxboard to the computer. You can see the keystrokes in the web interface.

![](stuff/images/auxboardmitm.jpg)

To program the implant: connect the implant prog-male-pins to auxboard PROG-female-pins (GND must coincide with G pin), press BOOT button and connect the auxboard to the computer. **Keyboard must be disconnected** from the auxboard!!

![](stuff/images/auxboardphoto.jpg)

**NOTE: Future versions will include a USB male connector to give more power to the implant if needed.**

- 1 ps2 implant probe board: It is also used to test the implant out of the PS2 keyboard (man in the middle PS2). This board converts PS2 signals to 3v3 for okhi.

![](stuff/images/mintps2.jpg)

To test implant (PS2): insert the sniff-male-pins (GND must coincide with G pin) into sniff-female-pins, connect keyboard to PS2 connector and connect the implant probe to your computer PS2/or PS2<->USB adapter. You can see the keystrokes in the web interface.

![](stuff/images/ps2near.jpg)

**NOTE: Future versions will include a USB male connector to give more power to the implant if needed.**

- 1 PS2 adapter board for okhi, okhi inputs only allow 3v3, so this board is necessary to convert PS2 signals to 3v3 (use only when you need install okhi inside the PS2 keyboard/tower....).

![](stuff/images/ps2adapter.jpg)

Just connect okhi-male-SNIFF-pins to the female pins (GND with G) and ps2adapter-male-pins to the PS2 keyboard.

You need to buy by yourself:
- 1 PS2 male to PS2 male cable (MINI-DIN 6P)
- 1 PS2<->USB adapter
- 1 USB extension cable (optional)
- 1 USB Keyboard (optional)
- 1 PS2 Keyboard (Lenovo on Aliexpress is OK) (optional)

# Firmware update

It is necessary to update the firmware before using okhi. The firmware is divided into two parts: RP2040 and ESP32-C2.

RP2040 chip is the responsible for programming the ESP32-C2 chip.

So first, you need burn a temporal firmware to the RP2040 chip to program the ESP32-C2 chip. After that, you need to burn the ESP firmware to the ESP32-C2 chip via RP2040 usb port.

Finally, you need to burn the final firmware to the RP2040 chip, thats all. Can be done in a few minutes, and this task is only necessary when you need to update the firmware.

Follow these steps to update the firmware:

- Download the latest firmware from the releases section: https://github.com/therealdreg/okhi/releases/latest

- Connect the implant to auxboard PROG pins (R female-pin must match with the R male-pin of the implant):

![](stuff/images/auxboardprogpins.png)

- Connect the auxboard to the computer using a USB cable (make sure the auxboard's USB female connector is not connected)

- Entering Program Mode for the okhi Module: Before connecting the okhi module to a USB port, ensure you press and hold the programming button (BOOT auxboard).

- Uploading the UART Bridge Firmware: Transfer the uart_bridge.uf2 file to the okhi module, which will appear as a MASS STORAGE DEVICE on your computer (e.g., drive E:). The device will automatically eject once the file transfer completes.

- Switching to Normal Mode: To operate the okhi module in normal mode, connect it to a USB port without pressing any buttons.

- Programming the ESP Module: Run the upload_firmware.bat script to start programming the ESP module. This script automates the firmware installation process.

- Selecting the COM Port: Input the COM port number where the okhi module is connected (e.g., COM3). If you are uncertain of the correct COM port, the batch file will display a list of available COM ports. You may need to try each one sequentially to find the correct connection.

- Completing the Programming Process: Wait for the script to finish running. Once complete, the ESP module will be programmed and ready for use.

- Re-entering Program Mode: Disconnect the okhi module from the USB, then reconnect it in program mode by pressing the programming button before plugging it back into the USB.

- Programming the RP2040 Chip: Copy the okhi.uf2 file to the okhi module, now recognized again as a MASS STORAGE DEVICE (e.g., drive E:). The device will automatically eject after the file transfer.

- Finalizing Setup for RP2040: The RP2040 chip is now programmed and ready for operational use.

- Reconnecting for Regular Use: Disconnect the okhi module from USB and then reconnect it without pressing the programming button for normal use.

- Connecting to the ESP WiFi Network: An ESP WiFi network will become available with the password '0123456789'. Connect to this network and open a web browser to access the web interface at: http://192.168.4.1/

# PS2<->USB adapters working with okhi

Note: Some adapters use purple for the keyboard, others use green for the keyboard... wtf! If it doesn't work in one, try the other.

- 1 (adapter from hell, it works, but not recommended) (Amazon)

![](stuff/images/hellpsadapter.png)

- 2 Aliexpress (OK)

![](stuff/images/ps2adapter2.png)

- 3 Aliexpress (OK)

![](stuff/images/ps2adapter3.png)

- 4 Aliexpress (OK)

![](stuff/images/ps2adapter4.png)

- 5 Aliexpress (OK)

![](stuff/images/ps2adapter5.png)

# NEVER BUY THIS PS2<->USB

![](stuff/images/neverbuy.png)

# Reporting issues

If you have a PS2-USB adapter that doesn't work with the sniffer, please send me some captures of the device using a logic analyzer or the pico-ps2-diagnostic-tool. This tool is designed to capture and replay signals on a PS/2 interface, specifically targeting the DATA and CLOCK lines:

https://github.com/therealdreg/pico-ps2-diagnostic-tool

![](stuff/images/originalvsre.png)

 A short pulse was captured on the original CLOCK signal, and the replayed signal successfully reproduced it.

# Developers setup

 - Install pico-sdk SDK **v1.5.1**: https://github.com/raspberrypi/pico-setup-windows/releases/latest/download/pico-setup-windows-x64-standalone.exe

**WARNING: Do not install pico-sdk 2.0 or higher (including PICO VSCODE extensions), as it is not supported yet.**

 - Clone this repo or download the zip file: https://github.com/therealdreg/okhi/archive/refs/heads/main.zip

 - Open the "Pico - Visual Studio Code" shortcut (it is installed with the pico-setup-windows). Never use a normal Visual Studio Code!!

 ![](stuff/images/picoapp.png)

- Open the okhi project, File -> Open Folder...

![](stuff/images/vsopenfolder.png)

- Select oki folder: firmware\ps2\rp

- Click Select Folder

- Click on "Trust the authors" if you see this message

![](stuff/images/selectfolder.png)

- Press shift+ctrl+p, type cmake cache and Select: "CMake: Delete Cache and Reconfigure"

![](stuff/images/cmakecache.png)

- Press shift+ctrl+p, type cmake build and Select: "CMake: Build"

![](stuff/images/cmakebuild.png)

- If everything is ok, you will see a message like this in OUTPUT window (bottom):

![](stuff/images/outputwindow.png)

- Now a new firmware\ps2\rp\build\okhi.uf2 file is created and ready to be uploaded to okhi

- Install PlatformIO IDE in "Pico - Visual Studio Code", go to Extensions and search for PlatformIO and install it

![](stuff/images/extensionicon.png)

![](stuff/images/installio.png)

- Wait for the installation to finish, it will take a few minutes...

- Click on Reload Now

![](stuff/images/reloadnow.png)

- Close all instances of the "Pico - Visual Studio Code" and open it again

- Open the okhi project, File -> Open Folder...

![](stuff/images/vsopenfolder.png)

- Select oki folder: firmware\ps2\esp

![](stuff/images/selectespfol.png)

- Wait while PlatformIO is configuring the project, download the dependencies, etc... it will take a few minutes...

![](stuff/images/espconfproject.png)

- Click on PlatformIO icon (left side), select esp32-c2-devkitm-1 and click on Build, it will take a few minutes... if everything is ok, a message like this will appear in TERMINAL window (bottom):

![](stuff/images/successioesp.png)

- A new .pio\build\esp32-c2-devkitm-1\firmware.bin file is created and ready to be uploaded to okhi

- Execute the following file to generate a new release to upload a new ESP firmware: make_release.bat

-----

To compile USB firmware, follow the same steps. Just select the firmware\usb\rp folder and firmware\usb\esp folder.

Note: I modified the original usb-sniffer-lite project by Alex Taradov (porting to pico-sdk). I am not a RP2040 expert, so I am learning a lot with this project.

# Developers web

The web is located in webps2\index.html for PS2 firmware and webusb\index.html for USB firmware. You can modify the web as you want, adding more keyboard layouts, improving keyboard protocol parsing (javascript)...

Installing node.js on Windows
```
https://github.com/coreybutler/nvm-windows
```

Open cmd

```
nvm.exe install latest
```

Execute "nvm use" with the version installed, Example
```
nvm.exe use 22.7.0
```

Check node version
```
node.exe -v
v22.7.0
```

**WARNING: type "node.exe" instead of "node"**

Install deps

```
npm.exe install html-minifier --no-audit
```

Just modify the index.html, run node.js script "webps2\node.js" or "webusb\node.js" and compile ESP firmware again.

```
node.exe node.js
```

This script will generate a new "web buffer data" for ESP firmware, also it reduces the size of the web (minify).

Please, keep in mind that the web is very simple, it is only a proof of concept, and we must conserve the ESP flash memory for other features, so do not add tons of code, just the necessary for useful features.

# Developers notes for PS2 PIO

As you noticed, the PS2 firmware is based on PIO (Programmable IO) of RP2040. The PIO is a very powerful tool to sniff and parse PS2 data.

Currently, PS2 firmware can looks unnecessary complex, but it is the only-reliable-and-stable way to parse PS2 data because:

- 1 Adapter from hell (AMAZON). Please avoid this adapter!

![](stuff/images/hellpsadapter.png)

This PS2->USB adapter is a little nightmare. Behaves very strange and the PIO code + main cores must be very robust to avoid problems.

PIO code for PS2 should be very, just simple waiting from idle to a start state (KEYBOARD to PC / PC to KEYBOARD), and then parse the data.

## IDLE behavior (adapter from hell)

Normal PS2 behavior, from idle (CLK+DAT HIGH) to start state, and finish state return to idle:

![](stuff/images/normalps2cap.png)

Now, the IDLE state for adapter from hell:

![](stuff/images/helladapterindle.png)

So, yes, the IDLE state for this adapter means a DAT HIGH (its ok), but.... wtf, a CLOCK pulse every time? as you can see this CLOCK pulse is longer than normal CLOCK pulses for PS2 protocol...

Another PS2 to USB Adapter from Aliexpress with a fast 1.8us low pulse (CLOCK LINE):

![](stuff/images/lowpulse1p8us.jpg)

NOTE: These types of "glitches" do not affect the functionality of the PS2 keyboard and the adapter in any way, but they are a problem when creating a generic sniffer.

So, PIO code cant wait for a normal IDLE state like the ps2kbd library does:

https://github.com/lurk101/ps2kbd-lib/blob/master/ps2kbd.pio

```
.program ps2kbd

    wait 0 pin 1     ; skip start bit
    wait 1 pin 1

    set x, 7         ; 8 bit counter
bitloop:
    wait 0 pin 1 [1] ; wait negative clock edge
    in pins, 1       ; sample data
    wait 1 pin 1     ; wait for positive edge
    jmp x-- bitloop

    wait 0 pin 1     ; skip parity and stop bits
    wait 1 pin 1
    wait 0 pin 1
    wait 1 pin 1
```

And one more thing... sometimes a wild very-short-pulse appears in the CLOCK line, look:

![](stuff/images/shortpulse.png)

A ~8us pulse! so we have two main problems: a weird long CLOCK every time and short pulses before the real packet....

So, PIO code must be compatible with this adapter, and also with normal PS2 adapters. PIO code + main cores code will be more complex than expected, but it is the only way to make it work with all adapters.

PIO code tasks:
- Track the IDLE state for all adapters (including adapter from hell)
- Track the start state for all adapters
- Track host to keyboard and keyboard to host states & data
- Helper to discard short pulses and long clock pulses

[Current PIO code for PS2](firmware/ps2/rp/okhi.pio)

Note: I just code (very fast) the necessary for the adapter from hell + others. So, the PIO code and main cores code will be improved in the future. And not all PIO code tasks are implemented yet (maybe will be not necessary).

I tested the current PIO + main cores code with different PS2 adapters and it works fine.

# Developers hardware pack

Programming using the implant can be a pain, so I made some hardware PCBs to make it easier.

Pack includes:
- 1 USB man in the middle board for developers:

![](stuff/images/usbdevboard.jpg)

- 1 PS2 man in the middle board for developers (this board converts PS2 signals to 3v3 for Raspberry Pi Pico):

![](stuff/images/ps2devboard.jpg)

You need buy by yourself:

- 1 RASPBERRY PI PICO (soldered version) + USB cable
- 1 Raspberry Debug probe (debugger) + USB cable
- 1 ESP8684-DevKitM-1 (soldered version) + USB cable
- 1 kit Dupont cables (Female-Female, Male-Male, Male-Female)
- 1 Cheap logic analyzer ~8$ (compatible with Saleae software if possible)
- 1 PS2<->USB adapter
- 1 PS2 male to PS2 male cable (MINI-DIN 6P)
- 1 USB extension cable (optional)
- 1 USB Keyboard (optional)
- 1 PS2 Keyboard (Lenovo on Aliexpress is OK) (optional)

So, this is basically the okhi implant in big format! the PCBs just allow an easy man in the middle stuff and interconnection between Raspberry Pi Pico and ESP32-C2 and the keyboard.

With this setup, you can debug, test, and develop the implant firmware in a more comfortable way.

## Flash PI PICO PS2 firmware using Raspberry Pi Debug Probe

- Connect the raspberry pi debug probe to the raspberry pi pico:

![](stuff/images/debugprobepico.png)

Connect the debug probe to the computer using a USB cable. Also connect raspberry pi pico to the computer using a USB cable.

Open Visual Studio Code, open okhi ps2 project, shift+ctrl+p, type ">Tasks: Run Task", press enter, select "Flash", press enter. Done!

## Debugging PI PICO PS2 firmware using Raspberry Pi Debug Probe

- The same as flashing, but go to Run and Debug icon (shift+ctrl+d). Select "Pico Debug (Cortex Debug)" and press the green arrow. Done!

![](stuff/images/debuggingcortex.png)

The debugger stops on platform_entry function, At this point, you can set a breakpoint in the main() code, etc...

## PI PICO Debug UART Console

Connect GND from USB to 3v3 UART adapter to GND PIN of the Raspberry Pi Pico.

Connect a USB to 3v3 UART adapter -> RX From adapter to the TX PIN (GPIO 4) of the Raspberry Pi Pico:

![](stuff/images/uartdebug.png)

You can use Raspberry Pi Debug probe to debug UART because it has a USB to UART connector:

![](stuff/images/uartcon.png)

Note: Raspberry Pi Pico is 3v3, so you need a 3v3 USB to UART adapter.

Open a terminal (putty, teraterm, etc...) and select the COM port of the USB to UART adapter. Set the baudrate to 921600 and done! you can see the debug messages in the terminal.

![](stuff/images/teratermconfig.png)

![](stuff/images/mroedebug.png)

## Flash PS2 ESP32-C2 firmware using ESP32-C2-DevKitM-1

Install CP210x Universal Windows Driver from Silicon Labs: https://www.silabs.com/documents/public/software/CP210x_Universal_Windows_Driver.zip

Connect the ESP32-C2-DevKitM-1 to the computer using a USB cable.

Open Visual Studio Code, open okhi esp project, open platformio.ini file, change the upload_port & monitor_port to the COM port of the ESP32-C2-DevKitM-1 (check it on Windows Device Manager), save the file.

Click on PlatformIO icon (left side), select esp32-c2-devkitm-1 and click on Upload, it will take a few minutes... if everything is ok, a message like this will appear in TERMINAL window (bottom):

![](stuff/images/platformupload.png)


```
Writing at 0x00070fa3... (85 %)
Writing at 0x00077235... (90 %)
Writing at 0x0007d5a6... (95 %)
Writing at 0x00082fd6... (100 %)
Wrote 485968 bytes (321297 compressed) at 0x00010000 in 7.7 seconds (effective 503.2 kbit/s)...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
================================= [SUCCESS] Took 18.55 seconds =================================
 *  Terminal will be reused by tasks, press any key to close it.
```

# PS2 Captures 

Using the Saleae logic software, PulseView, and the pico-ps2-diagnostic-tool, I captured data from a variety of USB-to-PS2 adapters and real motherboards. This data illustrates the differences among these devices, showing that the PIO code must be compatible with them all. The diagnostic tool also allows you to replay the data to the implant to verify its functionality.

All captures are stored in the **stuff/ps2caps** folder. Each capture contains the following keystroke events:

drg[Caps Lock]

Additionally, by pressing [Caps Lock from another keyboard], the PS2 keyboard receives the LED SET event.

## ASUS 970 PRO GAMING/AURA ACPI BIOS Revision 1001

[Download Captures: ASUS 970 PRO GAMING/AURA ACPI BIOS Revision 1001](stuff/ps2caps/asus_970_pro_gaming_aura_acpi_bios_revision_1001.zip)

This board features dual PS2 ports, supporting both a PS2 keyboard and a PS2 mouse:

![](stuff/images/ps2conn.jpeg)

## Adapter from Hell (AMAZON)

[Download Captures: Adapter from Hell](stuff/ps2caps/adapter_from_hell.zip)

This adapter is notoriously difficult to work with due to its erratic behavior. The PIO code and main firmware must be carefully designed to accommodate its quirks.

![](stuff/images/hellpsadapter.png)

## Adapter 2 from Aliexpress

[Download Captures: Adapter 2 from Aliexpress](stuff/ps2caps/adapter2_aliexpress.zip)

Although this adapter is somewhat easier to parse, it still presents challenges. Saleae’s Logic software PS2 parser may struggle to decode certain events.

![](stuff/images/ps2adapter2.png)

## Adapter 5 from Aliexpress

[Download Captures: Adapter 5 from Aliexpress](stuff/ps2caps/adapter5_aliexpress.zip)

This adapter is one of the most reliable, as it is the easiest to parse. Saleae’s Logic software successfully decodes all events.

![](stuff/images/ps2adapter5.png)

# Developers doc

- https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf

- https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf

- https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf

- https://docs.espressif.com/projects/esp-idf/en/latest/esp32c2/get-started/index.html

- https://www.espressif.com/sites/default/files/documentation/esp8684_datasheet_en.pdf

- https://www.espressif.com/sites/default/files/documentation/esp8684_technical_reference_manual_en.pdf

- https://esp32.com/

- https://forums.raspberrypi.com/

- https://www.usbmadesimple.co.uk/

- https://www.beyondlogic.org/usbnutshell/usb1.shtml

Take a look at the [stuff](stuff) folder, there are some useful documents.

- [PS2 Keyboard - Adam Chapweske.pdf](stuff/PS2_Keyboard.pdf)

- [USB Documentation - Christopher D Leary and Devrin Talen.pdf](stuff/USB_doc.pdf)

# Related

- https://github.com/therealdreg/pico-usb-sniffer-lite

- https://github.com/therealdreg/pico-ps2-sniffer

- https://github.com/therealdreg/pico-ps2-diagnostic-tool

# Schematic

![](stuff/images/schematic.png)


# Credits

- Juan M Martinez Casais: my friend who is a - GOD mode -  electronic engineer

- Alex Taradov (PIO USB & CODE): https://github.com/ataradov/usb-sniffer-lite

# Developers

- Main developer: Dreg

# Sponsors, donators, and early adopters

Thanks to all of you for your support! We are continuously working hard to improve this project every day.

Early adopters are the first individuals who purchase our products.

Here is a list of our sponsors, donors, and early adopters. If you would like to be added to this list, please contact me.

- ...
