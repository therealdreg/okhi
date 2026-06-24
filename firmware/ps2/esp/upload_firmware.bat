echo "By Dreg"

cd "%~dp0"

echo uart_bridge.uf2 prior to firmware version v5 require using esptool version (v4.7.0) included in release package. Using other versions may cause issues^! Refer to this link for more details: https://github.com/espressif/esptool/issues/1119
echo Starting from firmware version v5, uart_bridge.uf2 is compatible with the latest version of esptool without any issues.

echo Available COM ports:
for /F "tokens=2 delims==" %%A in ('wmic path Win32_SerialPort get DeviceID /value') do echo %%A

:askCOM
set /p COMPORT=Please enter the COM port (including full name e.g., COM3): 
if "%COMPORT%"=="" goto askCOM

:loopez
esptool.exe --port "%COMPORT%" --chip esp32c2 --baud 921600  write-flash -z --flash-mode dio --flash-freq 60m --flash-size 4MB 0x0000 bootloader.bin 0x8000 partition-table.bin 0x10000 okhi.bin 0x300000 storage.bin

echo PRESS ENTER TO EXIT

PAUSE