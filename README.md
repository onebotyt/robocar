# 🚗 NovaX V2 - Autonomous & Remote Robot Car System

> **Production-grade mobile robotics platform featuring an ESP32 V2 firmware architecture, Capacitor Android Cockpit, RFC 6455 WebSocket real-time communication, and autonomous obstacle avoidance.**

[![Live Web Cockpit](https://img.shields.io/badge/Live%20Demo-GitHub%20Pages-00bfa5?style=for-the-badge&logo=googlechrome)](https://onebotyt.github.io/robocar/)
[![Download Android APK](https://img.shields.io/badge/Download-NovaX--Controller.apk-brightgreen?style=for-the-badge&logo=android)](https://github.com/onebotyt/robocar/raw/main/apk/NovaX-Controller.apk)
[![Build NovaX Android APK](https://github.com/onebotyt/robocar/actions/workflows/build-apk.yml/badge.svg)](https://github.com/onebotyt/robocar/actions/workflows/build-apk.yml)
[![WebSocket Protocol](https://img.shields.io/badge/Protocol-RFC%206455%20WebSocket%20(:81)-blue?style=for-the-badge)](docs/WEBSOCKET_PROTOCOL.md)
[![Hardware Version](https://img.shields.io/badge/Hardware-ESP32--V2-orange?style=for-the-badge)](docs/HARDWARE.md)

---

### 🌐 Quick Links & Live Demos
- **🎮 [Live Web Cockpit (Browser Preview)](https://onebotyt.github.io/robocar/)**
- **📲 [Download Android APK Directly](https://github.com/onebotyt/robocar/raw/main/apk/NovaX-Controller.apk)**
- **📑 [Detailed WebSocket Protocol](docs/WEBSOCKET_PROTOCOL.md)**
- **🔌 [REST API Specification](docs/API.md)**
- **📶 [Wi-Fi AP/STA Architecture Guide](docs/WIFI.md)**
- **⚡ [OTA Firmware Flashing Guide](docs/OTA.md)**
- **🛠️ [Hardware Schematics & Wiring Map](docs/HARDWARE.md)**

---

## 📑 Table of Contents
1. [Hardware Overview & System Architecture](#1-hardware-overview--system-architecture)
2. [Finalized GPIO Mapping](#2-finalized-gpio-mapping)
3. [ESP32 Firmware Build & Flash](#3-esp32-firmware-build--flash)
4. [Capacitor Android App Build](#4-capacitor-android-app-build)
5. [Wi-Fi Setup & AP/STA Switching](#5-wi-fi-setup--apsta-switching)
6. [WebSocket Protocol & Motor Safety Watchdog](#6-websocket-protocol--motor-safety-watchdog)
7. [ESP32 OTA Firmware Update](#7-esp32-ota-firmware-update)
8. [GitHub App Update System (Integrity & Fallback)](#8-github-app-update-system-integrity--fallback)
9. [Troubleshooting & FAQs](#9-troubleshooting--faqs)

---

## 1. Hardware Overview & System Architecture

NovaX V2 divides responsibilities cleanly between high-level mobile computing and low-level deterministic embedded execution:

```
                            GitHub
                          /        \
                         /          \
              Android App            ESP32 Firmware
             (Cockpit & AI)         (Real-time Control)
                   |                        |
                   |      Wi-Fi Link        |
                   +==== WebSocket (:81) ===+
                   |  (Dual HTTP Fallback)  |
                                            |
                         +------------------+------------------+
                         |                  |                  |
                   MX1508 Motors      HC-SR04 & SG90      OV7670 Cam
                  (Watchdog Safe)     (180° Radar)        (QQVGA HUD)
                         |                  |
                     MPU6050 IMU       74HC595 LEDs
                     (Gyro Yaw)       (Shift Register)
```

- **Phone (Android / Capacitor App)**:
  - Cockpit UI (D-pad, Draw Mode canvas, center live camera HUD, 180° radar, rotation triggers, LED pattern selectors).
  - High-level decision making, path planning, computer vision capture, and speech integration.
- **ESP32 Microcontroller**:
  - Deterministic PWM motor control with a strict **400ms safety watchdog**.
  - Sensor acquisition: HC-SR04 ultrasonic distance, SG90 servo panning, MPU6050 gyroscope yaw drift filtering, and OV7670 image acquisition.
  - 74HC595 shift register LED animations.
  - Standalone Autonomous Avoidance Mode (Auto Mode runs locally without requiring continuous phone commands).
  - WebServer (HTTP :80) for configuration, Wi-Fi management, and OTA updates.
  - RFC 6455 WebSocket Server (:81) for real-time drive and telemetry streaming.

---

## 2. Finalized GPIO Mapping

All pins adhere strictly to the finalized NovaX V2 hardware design:

| Component | Pin Function | ESP32 GPIO | Electrical Notes |
| :--- | :--- | :--- | :--- |
| **OV7670 Camera** | D0 .. D7 | GPIO 36, 39, 34, 35, 32, 33, 25, 26 | 8-bit parallel digital video bus |
| | XCLK | GPIO 27 | 10–20 MHz system clock output |
| | PCLK | GPIO 16 | Pixel clock input |
| | HREF | GPIO 17 | Horizontal reference signal |
| | VSYNC | GPIO 4 | Vertical synchronization interrupt |
| | SIOD (SDA) | GPIO 21 | Shared I2C data bus (4.7kΩ pull-up to 3.3V) |
| | SIOC (SCL) | GPIO 22 | Shared I2C clock bus (4.7kΩ pull-up to 3.3V) |
| | RESET / PWDN | 3.3V / GND | RESET tied to 3.3V; PWDN tied to GND |
| **MX1508 Motors** | IN1 (Left FWD) | GPIO 13 | PWM capable (LEDC channel 0) |
| | IN2 (Left REV) | GPIO 14 | PWM capable (LEDC channel 1) |
| | IN3 (Right FWD) | GPIO 18 | PWM capable (LEDC channel 2) |
| | IN4 (Right REV) | GPIO 19 | PWM capable (LEDC channel 3) |
| **HC-SR04 Sonar** | TRIG | GPIO 23 | 10µs trigger pulse output |
| | **ECHO** | **GPIO 3** | **CRITICAL: 1kΩ / 2kΩ voltage divider to 3.3V** |
| **SG90 Servo** | Signal (PWM) | GPIO 2 | 50Hz PWM output; Servo powered from 5V rail |
| **74HC595 LEDs** | DATA (SER) | GPIO 5 | Serial data in |
| | CLOCK (SRCLK) | GPIO 12 | Shift register clock |
| | LATCH (RCLK) | GPIO 15 | Storage register clock; VCC=3.3V, OE=GND, MR=3.3V |
| **MPU6050 IMU** | SDA | GPIO 21 | Shared I2C with OV7670 (I2C Addr: `0x68`) |
| | SCL | GPIO 22 | Shared I2C with OV7670 (I2C Addr: `0x68`) |

> [!IMPORTANT]
> **Voltage Divider on GPIO 3**: The HC-SR04 operates at 5V. Connecting ECHO directly to ESP32 GPIO 3 will damage the chip. Connect HC-SR04 ECHO -> 1kΩ resistor -> GPIO 3 -> 2kΩ resistor -> GND to safely step down the 5V pulse to 3.3V.

---

## 3. ESP32 Firmware Build & Flash

The complete firmware is located at [`esp32/NovaX_V2.ino`](esp32/NovaX_V2.ino).

### Prerequisites
- [Arduino IDE 2.x](https://www.arduino.cc/en/software) or [PlatformIO](https://platformio.org/)
- **Board Package**: `esp32` by Espressif Systems (v2.0.14 or later)
- **Board Selection**: `ESP32 Dev Module`

### Required Libraries
Install the following libraries via Arduino Library Manager (`Ctrl+Shift+I`):
1. **ESP32Servo** (by Kevin Harrington / John K.)
2. **Adafruit MPU6050** (by Adafruit)
3. **Adafruit Unified Sensor** (by Adafruit)

*(The RFC 6455 WebSocket engine uses the ESP32's built-in `WiFiServer` and `mbedtls` cryptographic primitives — no external third-party WebSocket libraries are required).*

### Build Configuration Settings in Arduino IDE
- **Board**: `ESP32 Dev Module`
- **CPU Frequency**: `240MHz (WiFi/BT)`
- **Flash Frequency**: `80MHz`
- **Flash Mode**: `QIO`
- **Flash Size**: `4MB (32Mb)`
- **Partition Scheme**: `Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)` or `Huge APP (3MB No OTA/1MB SPIFFS)`
- **Upload Speed**: `921600`
- **Port**: Select your USB-to-UART COM port

Press **Upload**. Upon boot, the ESP32 will immediately initialize all hardware peripherals and launch the default AP network.

---

## 4. Capacitor Android App Build

The native Android app package is configured as **`com.novax.controller`**.

### Option A: Install Built APK Directly (No Setup)
Download the latest pre-compiled debug APK from the repository:
```bash
apk/NovaX-Controller.apk
```
Or download from GitHub: [Download NovaX-Controller.apk](https://github.com/onebotyt/robocar/raw/main/apk/NovaX-Controller.apk).

### Option B: Build via GitHub Actions (Automated CI/CD)
The `.github/workflows/build-apk.yml` workflow automatically builds `NovaX-Controller.apk` on every push to `main` using Ubuntu, Node 22, Java 21, and Android SDK 34, then commits the APK back to `apk/` and attaches it as a GitHub release artifact.

### Option C: Build Locally with Android Studio
```bash
# 1. Install dependencies
npm install

# 2. Sync web cockpit assets to Capacitor Android project
npm run sync

# 3. Open in Android Studio
npm run open:android

# 4. Or build APK from command line
npm run build:apk
```
The compiled APK will be generated at:
`android/app/build/outputs/apk/debug/app-debug.apk`

---

## 5. Wi-Fi Setup & AP/STA Switching

NovaX V2 employs an **AP-First, Zero-Bluetooth architecture**:

```
[ Power On ]
      ↓
Starts in AP Mode (NovaX-Car / 12345678 @ 192.168.4.1)
      ↓
User Connects Phone to "NovaX-Car"
      ↓
Cockpit Scans Nearby Wi-Fi (GET /wifi/scan)
      ↓
User Inputs Password & Saves Profile (POST /wifi/save)
(Credentials saved to NVS — does NOT auto-connect)
      ↓
User Explicitly Clicks "Switch to STA"
      ↓
ESP32 Connects to Router & Outputs Assigned IP (e.g., 192.168.1.120)
      ↓
User Reconnects Phone to Router & Operates at Assigned IP
      ↓
(User can click "Switch to AP" at any time to revert to 192.168.4.1)
```

### Wi-Fi REST API Reference
- `GET /wifi/scan`: Scans and returns nearby SSIDs with RSSI.
- `GET /wifi/saved`: Returns saved profiles (SSIDs only, passwords are never returned).
- `POST /wifi/save`: Saves credentials to ESP32 Non-Volatile Storage (up to 5 profiles).
- `POST /wifi/select?index=N`: Sets active profile index.
- `POST /wifi/delete?index=N`: Removes profile from NVS.
- `POST /wifi/switchSta`: Transitions ESP32 from AP mode into station mode.
- `POST /wifi/switchAp`: Reverts ESP32 into Access Point mode.

---

## 6. WebSocket Protocol & Motor Safety Watchdog

WebSocket is the **primary real-time control link** operating on port **81** (`ws://<ip>:81/`).

### Motor Safety Watchdog (ESP32 Autonomous Safety)
- **400ms Hardware Command Timeout**: The ESP32 enforces an internal timer. If no valid directional command is received within 400ms, all motors immediately stop.
- **WebSocket Disconnect Safety**: When the WebSocket drops or the phone goes out of range, the ESP32 halts both motors instantly.
- **Emergency STOP**: Clicking STOP sends an immediate WebSocket `{ "type": "stop" }` frame and dual-fires the HTTP `/stop` endpoint.
- **Dual Fallback**: If the WebSocket is temporarily reconnecting, the controller automatically routes commands via HTTP REST `/move?d=...`.

### Sample WebSocket Payloads

#### Client → ESP32 Drive Command:
```json
{
  "type": "move",
  "dir": "F",
  "speed": 180
}
```
*Directions: `"F"` (Forward), `"B"` (Reverse), `"L"` (Left), `"R"` (Right), `"S"` (Stop).*

#### Client → ESP32 Mode Toggle:
```json
{
  "type": "mode",
  "value": "auto"
}
```

#### ESP32 → Client Telemetry Stream (10 Hz):
```json
{
  "type": "telemetry",
  "distance": 42,
  "heading": 183.5,
  "battery": 3.92,
  "mode": "manual",
  "wifiMode": "AP",
  "ip": "192.168.4.1"
}
```

#### ESP32 → Client Radar Sweep Data:
```json
{
  "type": "radar",
  "angle": 30,
  "distance": 86
}
```

*For complete message schemas, consult [WEBSOCKET_PROTOCOL.md](docs/WEBSOCKET_PROTOCOL.md).*

---

## 7. ESP32 OTA Firmware Update (Cloud & Local)

Update the ESP32 firmware wirelessly over Wi-Fi without USB cables:

### Method A: 1-Tap Cloud OTA from GitHub (Recommended)
Since your firmware code is hosted on GitHub, you can flash directly from GitHub:
1. Ensure your phone is connected to **"NovaX-Car"** Wi-Fi.
2. Open NovaX Cockpit **⚙️ Settings**.
3. Under **ESP32 Firmware OTA**, see the current installed version vs latest GitHub version.
4. Tap **☁️ Flash Firmware from GitHub**.
5. The app automatically stops robot motors, downloads `firmware/NovaX-Firmware.bin` from your repository, streams it directly into the ESP32 OTA flash partition over Wi-Fi, and reboots the car!

### Method B: Manual Local .bin File
1. In Arduino IDE, compile your sketch and click **Sketch -> Export Compiled Binary**.
2. Open the NovaX Cockpit Settings modal (**⚙️ Settings**).
3. Scroll to **ESP32 Firmware OTA (Cloud & Local)**.
4. Enter the OTA security token (Default: `NovaX-OTA-ChangeMe`).
5. Choose the local `.bin` file and tap **Upload Local File**.
6. The robot stops all motors, validates the binary, writes to the OTA partition, and reboots cleanly back into AP mode.

*For complete security details, consult [OTA.md](docs/OTA.md).*

---

## 8. GitHub App Update & Version Control System

NovaX V2 includes an automated **Version Counter** and **In-App Hot-Update System** for both the phone UI and ESP32 firmware:

- **Automated Version Counter**: Every push to GitHub runs the CI workflow, auto-incrementing the version number (e.g. `v2.3.1`, `v2.3.2`...) across `version.json`, `android/app/build.gradle`, and `firmware/version.json`.
- **Instant Live UI Sync (Hot Update)**: The mobile app checks GitHub on startup. If a newer commit exists, it shows an update banner. Tapping **Update Now** downloads the updated UI structure (`index.html`), styles (`style.css`), and logic (`app.js`) without needing to reinstall the APK!
- **Force Refresh**: In Settings, tap **Force Refresh** at any time to immediately pull the latest UI layout from GitHub with zero caching delay.
- **Fail-Safe Fallback**: If an update encounters an evaluation error, it automatically purges local hot cache and reloads the factory APK bundle. Tap **"Reset Bundle"** in Settings at any time to manually revert.

---

## 9. Troubleshooting & FAQs

### Q: Why do the motors stop after ~400ms when I hold a button?
**A:** The ESP32 enforces a 400ms safety watchdog. Ensure your phone remains connected to the car's Wi-Fi. The controller sends repeat pulses every 100ms while holding a button. If the Wi-Fi connection drops packets, the watchdog safely halts the car to prevent collisions.

### Q: Why is my HC-SR04 ultrasonic distance reading 0 or 400cm constantly?
**A:** Check the voltage divider on GPIO 3. Verify that the 1kΩ and 2kΩ resistors are correctly positioned between ECHO, GPIO 3, and GND. Ensure Trig is connected to GPIO 23 and the sensor receives 5V.

### Q: The camera stream is black or fails to initialize.
**A:** OV7670 requires 3.3V logic and stable connections. Verify that SIOD (GPIO 21) and SIOC (GPIO 22) have 4.7kΩ pull-up resistors to 3.3V. Ensure RESET is pulled to 3.3V and PWDN is tied to GND.

### Q: Can I use Bluetooth instead of Wi-Fi?
**A:** No. Bluetooth cannot support concurrent high-speed OV7670 camera streaming and real-time telemetry alongside motor control. NovaX V2 uses dedicated Wi-Fi AP/STA networking.

### Q: How do I recover if I enter an incorrect Wi-Fi password in STA mode?
**A:** Power-cycle the ESP32. In NovaX V2, the ESP32 always boots in AP mode (`NovaX-Car` @ `192.168.4.1`) on power-on. You can connect directly to the AP and adjust your network credentials in Settings.

---

## 📄 License & Credits
Developed for the **NovaX Mobile Robotics Platform**. Built with Capacitor, Vanilla CSS/JS, and Espressif ESP32 Core.
