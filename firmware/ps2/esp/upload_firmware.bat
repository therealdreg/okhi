echo "By Dreg"

cd "%~dp0"

echo Available COM ports:
for /F "tokens=2 delims==" %%A in ('wmic path Win32_SerialPort get DeviceID /value') do echo %%A

:askCOM
set /p COMPORT=Please enter the COM port (e.g., COM3): 
if "%COMPORT%"=="" goto askCOM

:loopez
esptool.exe --chip esp32c2 --port "%COMPORT%" --baud 921600 --before no_reset --after no_reset write_flash -z --flash_mode dio --flash_freq 60m --flash_size detect 0x0000 .pio\build\esp32-c2-devkitm-1\bootloader.bin 0x8000 .pio\build\esp32-c2-devkitm-1\partitions.bin 0xe000 boot_app0.bin 0x10000 .pio\build\esp32-c2-devkitm-1\firmware.bin

echo PRESS ENTER TO EXIT
