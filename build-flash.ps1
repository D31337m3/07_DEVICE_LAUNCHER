# ESP32-S3 Build and Flash Script
# Quick rebuild and flash for the Device Launcher firmware

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "ESP32-S3 Device Launcher - Build & Flash" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

# Configuration
$PROJECT_DIR = $PSScriptRoot
$COM_PORT = "COM6"

Write-Host "Building and flashing firmware..." -ForegroundColor Yellow
Write-Host ""

# Change to project directory and run idf.py through cmd with proper environment
Push-Location $PROJECT_DIR

$cmdScript = @"
set PATH=C:\DevTools\Python313;C:\DevTools\Python313\Scripts;%PATH%
call C:\Espressif\frameworks\esp-idf-v5.5.1\export.bat >nul 2>&1
idf.py build
if errorlevel 1 exit /b 1
idf.py -p $COM_PORT flash
if errorlevel 1 exit /b 1
"@

$result = & cmd /c $cmdScript
$success = $LASTEXITCODE -eq 0

Pop-Location

if ($success) {
    Write-Host "`n✓ Build and flash successful!" -ForegroundColor Green
    Write-Host "`nDevice should now show 'Firmware Updated Successfully' screen!" -ForegroundColor Cyan
    Write-Host ""
} else {
    Write-Host "`n✗ Build or flash failed!" -ForegroundColor Red
    Write-Host "Check output above for errors" -ForegroundColor Red
    exit 1
}

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "Done!" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
