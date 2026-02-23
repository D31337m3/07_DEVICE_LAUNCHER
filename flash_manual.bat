@echo off
REM Manual Flash Script for ESP32-S3 Touch AMOLED
REM 
REM INSTRUCTIONS:
REM 1. Unplug USB cable from device
REM 2. Press and HOLD the BOOT button on the board
REM 3. While holding BOOT, plug in the USB cable
REM 4. Release the BOOT button
REM 5. Run this script immediately
REM

setlocal
set PATH=C:\DevTools\Python313;C:\DevTools\Python313\Scripts;%PATH%

call C:\Espressif\frameworks\esp-idf-v5.5.1\export.bat

cd /d "%~dp0build"

echo.
echo ============================================================
echo  ESP32-S3 Manual Flash Tool
echo ============================================================
echo.
echo Make sure device is in bootloader mode before continuing!
echo.
echo BOOTLOADER MODE STEPS:
echo  1. Unplug USB
echo  2. Hold BOOT button
echo  3. Plug in USB while holding BOOT
echo  4. Release BOOT
echo.
pause

echo.
echo Flashing firmware...
echo.

esptool.py --chip esp32s3 --port COM10 --before no_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB 0x0 bootloader\bootloader.bin 0x8000 partition_table\partition-table.bin 0xf000 ota_data_initial.bin 0x20000 device_launcher.bin

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ============================================================
    echo  Flash successful!
    echo ============================================================
    echo.
) else (
    echo.
    echo ============================================================
    echo  Flash FAILED - Try entering bootloader mode again
    echo ============================================================
    echo.
)

pause
