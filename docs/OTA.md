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

### Step 1: Export Compiled Firmware in Arduino IDE
1. Open `esp32/NovaX_V2.ino` in the Arduino IDE.
2. Select **Board**: `ESP32 Dev Module`.
3. In the top menu, click **Sketch -> Export Compiled Binary** (`Ctrl+Alt+S`).
4. Look in the `build/` subdirectory next to your sketch file for:
   `NovaX_V2.ino.bin`.

### Step 2: Flash via Phone / Cockpit
1. Connect your phone or laptop to `NovaX-Car` (or local station network).
2. Open the **NovaX Controller** app or browser cockpit.
3. Tap **Settings (⚙️)** in the top navigation bar.
4. Scroll to **ESP32 Firmware OTA**.
5. Select your exported `.bin` file.
6. Enter your OTA Security Token (default: `NovaX-OTA-ChangeMe`).
7. Tap **Upload Firmware**.
8. A progress bar tracks the upload. Once 100% is reached, the ESP32 writes to flash and automatically restarts into AP mode with the new firmware!
