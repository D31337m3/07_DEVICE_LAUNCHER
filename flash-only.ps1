# ESP32-S3 Flash Only Script
# Flash previously built firmware without rebuilding

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "ESP32-S3 Device Launcher - Flash Only" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

# Configuration
$PROJECT_DIR = $PSScriptRoot
$COM_PORT = "COM6"

Write-Host "Flashing firmware to $COM_PORT..." -ForegroundColor Yellow
Write-Host ""

Push-Location $PROJECT_DIR

$cmdScript = @"
set PATH=C:\DevTools\Python313;C:\DevTools\Python313\Scripts;%PATH%
call C:\Espressif\frameworks\esp-idf-v5.5.1\export.bat >nul 2>&1
idf.py -p $COM_PORT flash
"@

$result = & cmd /c $cmdScript
$success = $LASTEXITCODE -eq 0

Pop-Location

if ($success) {
    Write-Host "`n✓ Flash successful!" -ForegroundColor Green
    Write-Host "`nDevice is ready!" -ForegroundColor Cyan
} else {
    Write-Host "`n✗ Flash failed!" -ForegroundColor Red
    exit 1
}

Write-Host "`n======================================" -ForegroundColor Cyan
Write-Host "Done!" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
