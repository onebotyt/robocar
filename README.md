# 🚗 NovaX V2 - Autonomous & Remote Robot Car System

> **Production-grade mobile robotics platform featuring an ESP32 V2 firmware architecture, Capacitor Android Cockpit, RFC 6455 WebSocket real-time communication, and autonomous obstacle avoidance.**

[![Live Web Cockpit](https://img.shields.io/badge/Live%20Demo-GitHub%20Pages-00bfa5?style=for-the-badge&logo=googlechrome)](https://onebotyt.github.io/robocar/)
[![Download Android APK](https://img.shields.io/badge/Download-NovaX--Controller.apk-brightgreen?style=for-the-badge&logo=android)](https://github.com/onebotyt/robocar/raw/main/apk/NovaX-Controller.apk)
[![Build NovaX Android APK](https://github.com/onebotyt/robocar/actions/workflows/build-apk.yml/badge.svg)](https://github.com/onebotyt/robocar/actions/workflows/build-apk.yml)
[![WebSocket Protocol](https://img.shields.io/badge/Protocol-RFC%206455%20WebSocket%20(:81)-blue?style=for-the-badge)](docs/WEBSOCKET_PROTOCOL.md)
[![Hardware Version](https://img.shields.io/badge/Hardware-ESP32--V2-orange?style=for-the-badge)](docs/HARDWARE.md)

---

### 🌐 Quick Links & Live Demos
- **📊 [Project Presentation & Slides Deck](PRESENTATION.md)**
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

## 2. Complete Hardware Pin Layout & Wiring Guide

All components connect to the ESP32 (30-pin / 38-pin DevKit V1) according to this finalized, conflict-free pin map:

### 2.1 Master Quick-Reference Pinout

| Component | Component Pin | ESP32 Pin | Voltage / Logic | Function & Wiring Notes |
| :--- | :--- | :--- | :--- | :--- |
| **OV7670 (Non-FIFO)** | **3V3** | **3.3V** | 3.3V Power | Main camera sensor core power |
| | **GND** | **GND** | 0V Ground | Common system ground |
| | **SIOC** (SCL) | **GPIO 22** | 3.3V I2C | SCCB Clock (Shared with MPU6050, 4.7kΩ pull-up) |
| | **SIOD** (SDA) | **GPIO 21** | 3.3V I2C | SCCB Data (Shared with MPU6050, 4.7kΩ pull-up) |
| | **VSYNC** | **GPIO 4** | 3.3V Input | Frame vertical sync interrupt |
| | **HREF** | **GPIO 17** | 3.3V Input | Horizontal reference line signal |
| | **PCLK** | **GPIO 16** | 3.3V Input | Pixel byte clock (I2S DMA slave input) |
| | **XCLK** | **GPIO 27** | 3.3V Output | Master clock (10 MHz generated by ESP32 LEDC) |
| | **D7** | **GPIO 26** | 3.3V Input | Pixel data bit 7 (MSB) |
| | **D6** | **GPIO 25** | 3.3V Input | Pixel data bit 6 |
| | **D5** | **GPIO 33** | 3.3V Input | Pixel data bit 5 |
| | **D4** | **GPIO 32** | 3.3V Input | Pixel data bit 4 |
| | **D3** | **GPIO 35** | 3.3V Input | Pixel data bit 3 (Input-only pin) |
| | **D2** | **GPIO 34** | 3.3V Input | Pixel data bit 2 (Input-only pin) |
| | **D1** | **GPIO 39** | 3.3V Input | Pixel data bit 1 (SENSOR_VN, Input-only) |
| | **D0** | **GPIO 36** | 3.3V Input | Pixel data bit 0 (SENSOR_VP, Input-only) |
| | **RESET** | **3.3V** | 3.3V High | Connect directly to 3.3V (Active LOW reset) |
| | **PWDN** | **GND** | 0V Low | Connect directly to GND (Active HIGH power down) |
| **Dual Motor Driver** | **IN1** | **GPIO 13** | 3.3V PWM | Left Motor Forward (PWM channel 0) |
| *(MX1508 / L298N)* | **IN2** | **GPIO 14** | 3.3V PWM | Left Motor Reverse (PWM channel 1) |
| | **IN3** | **GPIO 18** | 3.3V PWM | Right Motor Forward (PWM channel 2) |
| | **IN4** | **GPIO 19** | 3.3V PWM | Right Motor Reverse (PWM channel 3) |
| | **VM / VCC** | **Battery (+)** | 6.0V–8.4V | Power directly from 2S 18650 Li-ion battery pack |
| | **GND** | **Battery (-)** | 0V Ground | Common Ground with ESP32 |
| **HC-SR04 Ultrasonic** | **VCC** | **5V (VIN)** | 5.0V Power | Sensor requires 5V rail for transducer power |
| | **GND** | **GND** | 0V Ground | Common system ground |
| | **TRIG** | **GPIO 23** | 3.3V Output | 10 µs trigger pulse output |
| | **ECHO** | **GPIO 3** | **3.3V Input** | **MUST use 1kΩ / 2kΩ voltage divider from 5V ECHO!** |
| **SG90 Micro Servo** | **Signal** (Orange) | **GPIO 2** | 3.3V PWM | 50 Hz PWM panning control (0°–180°) |
| | **VCC** (Red) | **5V (VIN)** | 5.0V Power | Power from 5V rail (DO NOT draw from ESP32 3.3V) |
| | **GND** (Brown) | **GND** | 0V Ground | Common system ground |
| **MPU6050 6-Axis IMU** | **VCC** | **3.3V** | 3.3V Power | Sensor logic power |
| | **GND** | **GND** | 0V Ground | Common system ground |
| | **SDA** | **GPIO 21** | 3.3V I2C | Shared I2C Data bus (`0x68`) |
| | **SCL** | **GPIO 22** | 3.3V I2C | Shared I2C Clock bus (`0x68`) |
| | **AD0** | **GND** | 0V Low | Selects default I2C address `0x68` |
| | **INT** | *NC* | None | Not connected |
| **74HC595 LED Shift** | **SER** (Data) | **GPIO 5** | 3.3V Output | Serial data input (Pin 14) |
| | **SRCLK** (Clock) | **GPIO 12** | 3.3V Output | Shift register clock input (Pin 11) |
| | **RCLK** (Latch) | **GPIO 15** | 3.3V Output | Storage latch register clock input (Pin 12) |
| | **OE** (Enable) | **GND** | 0V Low | Active LOW output enable (Pin 13) |
| | **MR** (Reset) | **3.3V** | 3.3V High | Active LOW master reset (Pin 10) |
| | **VCC** | **3.3V** | 3.3V Power | Logic supply (Pin 16) |
| | **GND** | **GND** | 0V Ground | Logic ground (Pin 8) |
| | **Q0–Q7** | **LEDs** | 3.3V Output | To 220Ω–330Ω resistors → 8x LEDs → GND |

---

### 2.2 Component Wiring & Electrical Details

#### 📷 OV7670 (18-Pin Non-FIFO Module)
The blue non-FIFO OV7670 module has 18 header pins arranged in two rows of 9:
```
           +-----------------------------+
           |     OV7670 (Non-FIFO)       |
           |          [LENS]             |
           +-----------------------------+
               3V3   [ 1]   [ 2]  GND
              SIOC   [ 3]   [ 4]  SIOD
             VSYNC   [ 5]   [ 6]  HREF
              PCLK   [ 7]   [ 8]  XCLK
                D7   [ 9]   [10]  D6
                D5   [11]   [12]  D4
                D3   [13]   [14]  D2
                D1   [15]   [16]  D0
             RESET   [17]   [18]  PWDN
```
* **Wiring Rules**:
  * `RESET` (Pin 17) -> Connect to **3.3V** (Keeps camera active).
  * `PWDN` (Pin 18) -> Connect to **GND** (Keeps power-down mode disabled).
  * `SIOD` (Pin 4) & `SIOC` (Pin 3) share the I2C bus with the MPU6050. Ensure both lines have 4.7kΩ pull-up resistors to 3.3V.
  * `D0–D3` are connected to ESP32 input-only pins (GPIO 36, 39, 34, 35) to conserve general-purpose outputs for motors and peripherals.

#### 🦇 HC-SR04 Ultrasonic ECHO Voltage Divider
The HC-SR04 operates at 5V and outputs 5V logic pulses on its `ECHO` pin. Connecting `ECHO` directly to ESP32 GPIO 3 will damage the microcontroller. You **must** wire a simple two-resistor voltage divider:

```
  HC-SR04                       ESP32
  +--------+                  +-------+
  |  ECHO  |───[ 1kΩ Resistor ]──┬───>| GPIO 3|
  +--------+                     │    +-------+
                          [ 2kΩ Resistor ]
                                 │
                                GND
```
* **Calculation**: $V_{out} = 5\text{V} \times \frac{2\text{k}\Omega}{1\text{k}\Omega + 2\text{k}\Omega} = 3.33\text{V}$ (Safe for ESP32).

#### 🔋 Power Supply & Anti-Brownout Architecture
Motors and servos draw rapid current spikes that can cause the ESP32's Wi-Fi core to brown out and reset. Always separate the power rails:
```
  [2S Li-ion Battery 7.4V - 8.4V]
            │
            ├───────────────> Motor Driver VM / VCC (High current)
            │
            ▼
    [LM2596 / Buck 5V 3A]
            │
            ├───────────────> ESP32 5V (VIN) pin
            ├───────────────> SG90 Servo VCC (Red wire)
            └───────────────> HC-SR04 VCC (5V pin)

  * IMPORTANT: Connect ALL Grounds together (Battery GND = Buck GND = ESP32 GND = Motor GND).
```

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

## 7. ESP32 OTA Firmware Update (Permanent Wi-Fi Support)

> [!NOTE]
> **Does the new firmware have OTA update support? YES, 100%!**
> The main firmware (`NovaX_V2.ino`) has an embedded, permanent OTA Web Engine (`/update`, `/ota/update`, and `/ota/status`). Once flashed with `NovaX-Firmware.bin`, **all future firmware updates can be installed over Wi-Fi directly from your phone without ever needing a USB cable again.**

### Method 1: 1-Tap Offline Flashing (Pre-Downloaded Firmware — Recommended)
This solves the classic problem where connecting to the car's Wi-Fi disconnects your phone from the Internet:
1. While connected to **home Wi-Fi or mobile data**, open the NovaX App.
2. Open **Settings ⚙️** → Scroll to **ESP32 Firmware OTA**.
3. Tap **"📥 Pre-Download"**. The app fetches `NovaX-Firmware.bin` from GitHub and caches it in your phone's local storage.
4. Now switch your phone's Wi-Fi back to **`NovaX-Car`**.
5. Tap **"⚡ Flash Pre-Downloaded Firmware"**!
6. The app transmits the firmware over Wi-Fi directly to the ESP32, validates the partition, and reboots the car!

### Method 2: In-App Cloud OTA from GitHub
If the ESP32 is connected to your home router in **STA mode**:
1. Open **Settings ⚙️** → **ESP32 Firmware OTA**.
2. Tap **"☁️ Flash Firmware from GitHub"**.
3. The app instructs the ESP32 to fetch `NovaX-Firmware.bin` directly from GitHub, flash itself, and reboot.

### Method 3: Direct Web Browser Flashing (Bootstrap or Web Page)
If you are running the `NovaX_OTA_Bootstrap.ino` or accessing via a laptop:
1. Connect to **`NovaX-Car`** Wi-Fi.
2. Open your web browser to **`http://192.168.4.1/update`** or **`http://192.168.4.1`**.
3. Choose the binary: `firmware/NovaX-Firmware.bin`.
4. Click **Update Firmware**. The ESP32 flashes the binary and reboots into NovaX V2.

*For complete token security and endpoint documentation, consult [OTA.md](docs/OTA.md).*

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
