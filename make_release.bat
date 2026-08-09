@echo off
setlocal EnableExtensions EnableDelayedExpansion

echo By Dreg
cd /d "%~dp0"

set "timestamp="

rem --- 1) Try WMIC (older Windows) ---
for /F "tokens=2 delims==" %%A in ('wmic os get localdatetime /value 2^>nul') do set "datetime=%%A"
if defined datetime set "timestamp=%datetime:~0,4%%datetime:~4,2%%datetime:~6,2%%datetime:~8,2%%datetime:~10,2%%datetime:~12,2%"

rem --- 2) Fallback to PowerShell (Windows 11, no WMIC) ---
if not defined timestamp for /F "usebackq delims=" %%A in (`powershell -NoProfile -Command "Get-Date -Format yyyyMMddHHmmss"`) do set "timestamp=%%A"

if not defined timestamp (
  echo [ERROR] Could not get a timestamp from WMIC or PowerShell.
  pause >nul
  exit /b 1
)
set "out=%timestamp%"

echo Generating %timestamp% folder

set "ERRORS=0"
set "VERBOSE=1"

call :stage ps2
call :stage usb

echo Copying last_esptool...
xcopy /E /I /Y "stuff\last_esptool" "%out%\last_esptool" >nul
if errorlevel 1 (
  echo [COPY FAIL] stuff\last_esptool -^> %out%\last_esptool
  set /a ERRORS+=1
) else (
  if defined VERBOSE echo [OK] stuff\last_esptool ^> %out%\last_esptool
)

echo.
if %ERRORS% gtr 0 (
  echo =============================
  echo FAILURES: %ERRORS% issue^(s^)
  echo =============================
  echo Press Enter to exit.
  pause >nul
  exit /b 1
) else (
  echo All copies verified successfully.
  echo Done. Press Enter to exit.
  pause >nul
  exit /b 0
)

:: -------------------------
:stage
set "tgt=%~1"
set "dest=%out%\%tgt%"
mkdir "%dest%" 2>nul

call :copy_checked "firmware\%tgt%\esp\esptool.exe" "%dest%"
call :copy_checked "firmware\%tgt%\esp\upload_firmware.bat" "%dest%"
call :copy_checked "firmware\%tgt%\esp\build\okhi.bin" "%dest%"
call :copy_checked "firmware\%tgt%\esp\build\bootloader\bootloader.bin" "%dest%"
call :copy_checked "firmware\%tgt%\esp\build\partition_table\partition-table.bin" "%dest%"
call :copy_checked "firmware\%tgt%\esp\build\storage.bin" "%dest%"

call :copy_checked "firmware\%tgt%\rp\build\okhi.uf2" "%dest%"
call :copy_checked "firmware\%tgt%\PROGRAM_INSTRUCTIONS.txt" "%dest%"
call :copy_checked "stuff\okhi_reset_flash.uf2" "%dest%"
call :copy_checked "stuff\pico-uart-bridge-dregmod\build\uart_bridge.uf2" "%dest%"
exit /b

:: -------------------------
:copy_checked
set "SRC=%~1"
set "DSTDIR=%~2"
set "BASE=%~nx1"
set "DST=%DSTDIR%\%BASE%"

if not exist "%SRC%" (
  echo [MISSING] %SRC%
  set /a ERRORS+=1
  goto :eof
)

if not exist "%DSTDIR%" mkdir "%DSTDIR%" 2>nul

copy /y "%SRC%" "%DSTDIR%\" >nul
if errorlevel 1 (
  echo [COPY FAIL] %SRC% -> %DSTDIR%
  set /a ERRORS+=1
  goto :eof
)

if not exist "%DST%" (
  echo [NO DEST] %DST%
  set /a ERRORS+=1
  goto :eof
)

fc /b "%SRC%" "%DST%" >nul
if errorlevel 1 (
  echo [VERIFY FAIL] %SRC% != %DST%
  set /a ERRORS+=1
  goto :eof
)

if defined VERBOSE echo [OK] %SRC% ^> %DST%
goto :eof
