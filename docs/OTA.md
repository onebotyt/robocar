# 🚀 NovaX V2 - Over-The-Air (OTA) Firmware Update Guide

## 1. Two Separate Update Systems

It is essential to distinguish between the two independent update systems in NovaX:

| Update Target | What is Updated | Delivery Mechanism |
| :--- | :--- | :--- |
| **Android Controller App** | User interface, touchscreen controls, radar visualizer | GitHub Releases (`NovaX-Controller.apk`) or GitHub in-app hot-updater (`version.json`) |
| **ESP32 Firmware** | C++ code running on the microcontroller, motor driver, sensors | Over-The-Air (`firmware.bin` via `POST /ota/update`) or USB cable |

---

## 2. Safety Interlocks & Security

Flashing microcontrollers while motors are energized is hazardous. NovaX enforces strict hardware safety during updates:

1. **Motor Disarm**: As soon as an OTA update begins (`UPLOAD_FILE_START`), the ESP32 sets both motor bridges to zero PWM (`stopCar()`) and disables the motor timer.
2. **Watchdog Suspension**: Prevents the software watchdog from triggering a reboot mid-flash.
3. **Token Authorization**:
   OTA uploads must include the security header:
   ```http
   X-NovaX-OTA: NovaX-OTA-ChangeMe
   ```
   *(Configure your secret token in firmware `OTA_TOKEN`)*.
4. **Validation Check**: If the binary is corrupted, truncated, or incompatible, `Update.end()` rejects the flash and aborts safely.

---

## 3. Step-by-Step OTA Upload Workflow

### Method A: 1-Tap Cloud OTA Flash from GitHub (Recommended)
Because the ESP32 firmware is version-controlled and hosted on GitHub:
1. Connect your phone to the car's **NovaX-Car** Wi-Fi.
2. Open the **NovaX Controller** app and tap **Settings (⚙️)**.
3. Under **ESP32 Firmware OTA (Cloud & Local)**, check the installed ESP32 version vs the latest version on GitHub.
4. Tap **☁️ Flash Firmware from GitHub**.
5. The mobile app automatically:
   - Signals the car to stop all motors.
   - Downloads `NovaX-Firmware.bin` directly from GitHub.
   - Uploads the binary to the car via `/ota/update` using your `X-NovaX-OTA` security token.
   - The car verifies the binary, writes to the OTA partition, and automatically reboots into AP mode.

### Method B: Manual .bin File Upload
1. In Arduino IDE, open `esp32/NovaX_V2.ino` with board `ESP32 Dev Module`.
2. Click **Sketch -> Export Compiled Binary** (`Ctrl+Alt+S`).
3. Connect your phone or laptop to `NovaX-Car` Wi-Fi.
4. Open Settings in the app, scroll to **ESP32 Firmware OTA**.
5. Tap **Choose File**, select `NovaX_V2.ino.bin`, and tap **Upload Local File**.
6. The ESP32 flashes the binary and restarts.
