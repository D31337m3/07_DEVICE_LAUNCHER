# ESP32-S3 Monitor Script
# Open serial monitor to view debug output

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "ESP32-S3 Device Launcher - Serial Monitor" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

# Configuration
$PROJECT_DIR = $PSScriptRoot
$COM_PORT = "COM6"

Write-Host "Opening serial monitor on $COM_PORT..." -ForegroundColor Yellow
Write-Host "Press Ctrl+] to exit" -ForegroundColor Yellow
Write-Host ""

Push-Location $PROJECT_DIR

& cmd /c @"
set PATH=C:\DevTools\Python313;C:\DevTools\Python313\Scripts;%PATH%
call C:\Espressif\frameworks\esp-idf-v5.5.1\export.bat >nul 2>&1
idf.py -p $COM_PORT monitor
"@

Pop-Location
