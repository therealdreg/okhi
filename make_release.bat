setlocal

echo "By Dreg"

cd "%~dp0"

for /F "tokens=2 delims==" %%A in ('"wmic os get localdatetime /value"') do set datetime=%%A

set timestamp=%datetime:~0,4%%datetime:~4,2%%datetime:~6,2%%datetime:~8,2%%datetime:~10,2%%datetime:~12,2%

mkdir "%timestamp%"

mkdir "%timestamp%\ps2"
copy "firmware\ps2\esp\upload_firmware.bat" "%timestamp%\ps2"
copy "firmware\ps2\esp\esptool.exe" "%timestamp%\ps2"
copy "firmware\ps2\esp\boot_app0.bin" "%timestamp%\ps2"
xcopy "firmware\ps2\esp\.pio" "%timestamp%\ps2\.pio" /E /I /H
for /R "%timestamp%\ps2\.pio" %%F in (*.*) do (
    if /I not "%%~xF"==".bin" del "%%F"
)
for /F "delims=" %%D in ('dir "%timestamp%\ps2\.pio" /AD /B /S 2^>nul ^| sort /R') do (
    rd "%%D" 2>nul
)
copy "firmware\ps2\rp\build\okhi.uf2" "%timestamp%\ps2"
copy "firmware\ps2\PROGRAM_INSTRUCTIONS.txt" "%timestamp%\ps2"
copy "stuff\okhi_reset_flash.uf2" "%timestamp%\ps2"
copy "stuff\pico-uart-bridge-dregmod\build\uart_bridge.uf2" "%timestamp%\ps2"


mkdir "%timestamp%\usb"
copy "firmware\usb\esp\upload_firmware.bat" "%timestamp%\usb"
copy "firmware\usb\esp\esptool.exe" "%timestamp%\usb"
copy "firmware\usb\esp\boot_app0.bin" "%timestamp%\usb"
xcopy "firmware\usb\esp\.pio" "%timestamp%\usb\.pio" /E /I /H
for /R "%timestamp%\usb\.pio" %%F in (*.*) do (
    if /I not "%%~xF"==".bin" del "%%F"
)
for /F "delims=" %%D in ('dir "%timestamp%\usb\.pio" /AD /B /S 2^>nul ^| sort /R') do (
    rd "%%D" 2>nul
)
copy "firmware\usb\rp\build\okhi.uf2" "%timestamp%\usb"
copy "firmware\usb\PROGRAM_INSTRUCTIONS.txt" "%timestamp%\usb"
copy "stuff\okhi_reset_flash.uf2" "%timestamp%\usb"
copy "stuff\pico-uart-bridge-dregmod\build\uart_bridge.uf2" "%timestamp%\usb"


endlocal
echo "PRESS ENTER TO EXIT"
pause
