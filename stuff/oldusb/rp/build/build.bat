:loopez
echo "By Dreg"
cd "%~dp0"
@RD /S /Q "build\"
@del /S /Q "*.uf2"
@del /S /Q crc_output.txt
call "C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\pico-env.cmd"
set "CURRENT_DIR=%CD%"
echo %PATH% | findstr /C:"%CURRENT_DIR%" > nul
if %ERRORLEVEL% == 1 (
    set "PATH=%CURRENT_DIR%;%PATH%"
)
mkdir "build\"
make
for /r %%p in (*.uf2) do copy %%p "."
bin2uf2.exe -s -u -i build/okhi.bin -o build/okhi_crc.bin > crc_output.txt
hexup.exe crc_output.txt build/okhi.elf build/okhi_crc.elf
where python >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Python is installed, running the script...
    python print_sections_ram_usage.py
) else (
    echo Python is not installed. Please install Python to run this script.
)
REM echo press enter to rebuild
REM pause
REM goto loopez