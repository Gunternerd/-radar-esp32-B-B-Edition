# Upload & Diagnostic Instructions

Use these instructions to build and upload the ESP32 firmware and run the SD card diagnostic.

## Prerequisites
- Install Python and PlatformIO Core (CLI): `python -m pip install --user -U platformio`
- Or install PlatformIO extension in VSCode.
- If using Arduino CLI: install `arduino-cli`.
- Ensure your ESP32 board is connected and note its COM port (e.g. `COM3`).

## Quick PowerShell helper
A helper script is provided at `tools/build_and_upload.ps1`.

Open PowerShell in this project directory and run (replace COM3 if needed):

```powershell
.\tools\build_and_upload.ps1 -ComPort COM3
```

This script will:
- build with PlatformIO
- upload to the given COM port
- open the serial monitor at 115200

## Upload sd_diagnose sketch (alternative)
If you just want to test the SD card quickly using Arduino IDE or Arduino CLI, use `sd_test/sd_diagnose.ino`.

Arduino CLI example:
```powershell
arduino-cli compile --fqbn esp32:esp32:esp32wrover Y:\nur_radar\sd_test\sd_diagnose.ino
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32wrover Y:\nur_radar\sd_test\sd_diagnose.ino
arduino-cli monitor -p COM3 -b 115200
```

## SD card notes
- Format card as FAT32 (not exFAT). For cards >32GB use SD Association Formatter or tools that can force FAT32.
- CS pin is GPIO5; VSPI default pins: SCK=18, MISO=19, MOSI=23.
- Ensure SD runs at 3.3V and the module has sufficient power.

## If Upload fails
- Check drivers (CP210x/CH340).
- Check COM port and use manual BOOT/EN if needed.
- Post serial log here and I'll help diagnose.
