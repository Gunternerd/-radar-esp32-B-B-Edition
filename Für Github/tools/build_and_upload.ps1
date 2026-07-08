# PowerShell helper to build and upload the ESP32 firmware using PlatformIO
# Usage:
# 1) Open an elevated or normal PowerShell depending on your environment.
# 2) Edit the $ComPort variable below to match your ESP32 (e.g. COM3).
# 3) Run this script: .\build_and_upload.ps1

param(
  [string]$ComPort = "COM18",
  [string]$EnvName = "esp32-wrover-mgn4r8",
  [string]$SecretPath = ""
)

$ProjectPath = Convert-Path "..\" -ErrorAction SilentlyContinue
if (-not $ProjectPath) { $ProjectPath = Get-Location }
Set-Location -Path $PSScriptRoot\..\

Write-Host "Project path:" (Get-Location)
Write-Host "Using COM port:" $ComPort
if ($SecretPath -ne "") { Write-Host "Secret path provided:" $SecretPath }

# Build
if ($SecretPath -ne "") {
    if (-Not (Test-Path $SecretPath)) { Write-Error "Secret file not found: $SecretPath"; exit 4 }
    Copy-Item -Path $SecretPath -Destination src\secret.h -Force
    Write-Host "Copied secret file into src\secret.h"
}

if (Get-Command pio -ErrorAction SilentlyContinue) {
    Write-Host "Using 'pio' from PATH: running: pio run -e $EnvName"
    pio run -e $EnvName
    if ($LASTEXITCODE -ne 0) { Write-Error "Build failed (pio)"; exit 1 }

    # Upload
    Write-Host "Uploading to $ComPort (pio)"
    pio run -e $EnvName -t upload --upload-port $ComPort
    if ($LASTEXITCODE -ne 0) { Write-Error "Upload failed (pio)"; exit 2 }

    # Open monitor
    Write-Host "Opening serial monitor on $ComPort at 115200 (pio)"
    pio device monitor --port $ComPort -b 115200
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    Write-Host "'pio' not found, using 'python -m platformio' as a fallback"
    Write-Host "Running: python -m platformio run -e $EnvName"
    python -m platformio run -e $EnvName
    if ($LASTEXITCODE -ne 0) { Write-Error "Build failed (python -m platformio)"; exit 1 }

    Write-Host "Uploading to $ComPort (python -m platformio)"
    python -m platformio run -e $EnvName -t upload --upload-port $ComPort
    if ($LASTEXITCODE -ne 0) { Write-Error "Upload failed (python -m platformio)"; exit 2 }

    Write-Host "Opening serial monitor on $ComPort at 115200 (python -m platformio)"
    python -m platformio device monitor --port $ComPort -b 115200
} else {
    Write-Error "Neither 'pio' nor 'python' were found. Install PlatformIO (VSCode extension) or Python + platformio package. See README_UPLOAD.md for instructions."
    exit 3
}
