# flash.ps1
# Automatically flashes the latest compiled Arduino binary to CH32V003

# Set the path to your minichlink folder
$MinichlinkDir = "C:\Users\AVS\Documents\CompressorProtect\ch32fun-master\minichlink"

# Set the path to the compiled binary
$BinFile = "C:\Users\AVS\Documents\CompressorProtect\firmware_b\build\WCH.ch32v.CH32V00x_EVT\firmware_b.ino.bin"
if (-Not (Test-Path $BinFile)) {
    $BinFile = "C:\Users\AVS\AppData\Local\arduino\sketches\01E11CF33B49A9F583C7CD936C4FD946\firmware_b.ino.bin"
}

# 1. Check if the compiled file exists
if (-Not (Test-Path $BinFile)) {
    Write-Host "ERROR: Compiled .bin file not found!" -ForegroundColor Red
    Write-Host "Please compile the sketch in Arduino IDE first (Ctrl+R)." -ForegroundColor Yellow
    exit 1
}

Write-Host "Found binary: $BinFile" -ForegroundColor Cyan
Write-Host "Starting flash process..." -ForegroundColor Cyan

# 2. Change directory to minichlink
Set-Location $MinichlinkDir

# 3. Execute the flash command (2>&1 tells PowerShell to treat stderr as normal text)
.\minichlink -w $BinFile 0x08000000 -b 2>&1 | Write-Host

# 4. Check if it succeeded
if ($LASTEXITCODE -eq 0) {
    Write-Host "`nSUCCESS! Firmware flashed and rebooting." -ForegroundColor Green
} else {
    Write-Host "`nFLASH FAILED. Check your SWIO/GND/3V3 connections." -ForegroundColor Red
}

# 5. Return to the project folder
Set-Location "C:\Users\AVS\Documents\CompressorProtect"