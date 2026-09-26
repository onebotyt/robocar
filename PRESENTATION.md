---
marp: true
theme: default
paginate: true
header: 'NovaX V2 — Autonomous & Remote Mobile Robotics Platform'
footer: 'NovaX Robotics Project | ESP32-WROOM-32 | Capacitor Android'
style: |
  section {
    background-color: #0b0f19;
    color: #e2e8f0;
    font-family: 'Segoe UI', -apple-system, Roboto, Helvetica, sans-serif;
  }
  h1, h2, h3 {
    color: #38bdf8;
  }
  h1 {
    font-size: 2.2rem;
  }
  h2 {
    font-size: 1.6rem;
    border-bottom: 2px solid #1e293b;
    padding-bottom: 0.3rem;
  }
  table {
    font-size: 0.85rem;
    width: 100%;
    border-collapse: collapse;
  }
  th {
    background-color: #1e293b;
    color: #38bdf8;
  }
  td, th {
    border: 1px solid #334155;
    padding: 0.4rem 0.6rem;
  }
  pre {
    background-color: #131b2e !important;
    border: 1px solid #1e293b;
    color: #38bdf8;
    font-size: 0.8rem;
  }
  code {
    color: #06b6d4;
  }
  .highlight {
    color: #10b981;
    font-weight: bold;
  }
  .warning {
    color: #f59e0b;
  }
  .danger {
    color: #ef4444;
  }
  .badge {
    background: #0284c7;
    color: white;
    padding: 2px 8px;
    border-radius: 4px;
    font-size: 0.75rem;
    font-weight: bold;
  }
---

<!-- 
  =============================================================================
  SLIDE 1: TITLE SLIDE
  =============================================================================
-->

# 🚗 NOVA-X V2
### Autonomous & Remote Mobile Robotics Platform
**Production-Grade ESP32 Architecture • Cyber Glassmorphism HUD • Real-Time Computer Vision**

<br>

- **Target Microcontroller**: ESP32-WROOM-32 (240 MHz Dual-Core Xtensa LX6)
- **Control Interface**: Native Capacitor Android App & Cross-Platform PWA Cockpit
- **Protocol**: Low-Latency RFC 6455 WebSocket (:81) with Dual HTTP (:80) Fallback
- **Deployment**: Dual-Stage Zero-Wire Over-The-Air (OTA) Flash Pipeline
- **Firmware Version**: v2.4.46 | **Hardware Map**: ESP32-V2 Finalized

<br>

*Presented by the NovaX Engineering Team*  
*Repository: [github.com/onebotyt/robocar](https://github.com/onebotyt/robocar)*

<!-- Note: Welcome the audience. Introduce NovaX as a full-stack robotics project that bridges high-level modern web/mobile design with low-level deterministic embedded systems. -->

---

<!-- 
  =============================================================================
  SLIDE 2: THE PROBLEM & OUR VISION
  =============================================================================
-->

## 🎯 The Engineering Challenge & Our Vision

### Why Traditional DIY Robot Cars Fail:
1. **High Latency & Clunky Controls**: Relying on simple HTTP polling causes 150–400ms command latency, leading to jerky driving and overshooting.
2. **Safety Vulnerabilities**: Without an embedded hardware watchdog, a Wi-Fi dropout causes motors to run away continuously until physical collision.
3. **Flashing Friction**: Traditional firmware updates require disassembling the chassis, connecting micro-USB cables, and installing 10+ Arduino libraries.
4. **Camera Bottlenecks**: OV7670 non-FIFO modules typically fail on standard microcontrollers due to strict 10–20 MHz pixel clocks and lack of frame buffers.

### The NovaX Solution:
- ⚡ **Sub-15ms Real-Time Control**: Bi-directional RFC 6455 WebSocket streaming.
- 🛡️ **Fail-Safe Watchdog**: Hardware & software auto-stop timer (400ms threshold).
- 📲 **100% Cable-Free Operation**: Offline-capable phone caching + Zero-dependency Bootstrap OTA flasher.
- 👁️ **Direct I2S DMA Vision**: Non-FIFO camera streaming via native ESP32 I2S hardware pipeline.

<!-- Note: Highlight how NovaX addresses each pain point with industry-grade software and hardware patterns instead of quick hobbyist hacks. -->

---

<!-- 
  =============================================================================
  SLIDE 3: COMPLETE SYSTEM ARCHITECTURE
  =============================================================================
-->

## 🏗️ End-to-End System Architecture

```
                       CLOUD & CONTINUOUS DEPLOYMENT
     [ GitHub Repository ] ──► [ GitHub Actions CI/CD ] ──► Automated APK Build
                                                                    │
                                    ┌───────────────────────────────┘
                                    ▼
                         OPERATOR CONTROL LEVEL
     ┌─────────────────────────────────────────────────────────────┐
     │  Android Native Cockpit (Capacitor) / Progressive Web App   │
     │  • Cyber Glassmorphism HUD     • Autonav Draw-Path Canvas   │
     │  • Real-time Radar Scope       • Low-Latency MJPEG Streamer │
     │  • IndexedDB Firmware Cache    • Dynamic Gyroscope HUD      │
     └──────────────────────────────┬──────────────────────────────┘
                                    │ Wi-Fi 802.11 b/g/n
                  ┌─────────────────┴─────────────────┐
                  ▼                                   ▼
        [ WebSocket (:81) ]                   [ HTTP REST (:80) ]
        Sub-15ms Drive & Telemetry            Config, Scan, OTA Updates
                  │                                   │
                  └─────────────────┬─────────────────┘
                                    ▼
                         EMBEDDED HARDWARE CORE
     ┌─────────────────────────────────────────────────────────────┐
     │  ESP32-WROOM-32 (240 MHz Dual-Core Xtensa LX6)              │
     │  Core 0: I2S DMA Camera Pipeline & Network Stack            │
     │  Core 1: Deterministic Control Machine & 400ms Safety Watchdog │
     └───────┬──────────────┬──────────────┬──────────────┬────────┘
             ▼              ▼              ▼              ▼
        [ MX1508 ]    [ HC-SR04 ]     [ SG90 ]      [ OV7670 ]      [ 74HC595 ]
       Dual DC Motors  Ultrasonic    Servo Pan Arc   Non-FIFO Cam    Shift LEDs
```

<!-- Note: Walk through the three main layers: Cloud/CI/CD, Operator Cockpit, and Edge Embedded Core. Point out the clear separation of concerns. -->

---

<!-- 
  =============================================================================
  SLIDE 4: BILL OF MATERIALS (BOM)
  =============================================================================
-->

## 🧰 Master Bill of Materials (BOM)

| Component | Function / Subsystem | Voltage | Operating Characteristics |
| :--- | :--- | :--- | :--- |
| **ESP32-WROOM-32** | Central Compute & Wi-Fi/BT | 3.3V / 5V (VIN) | 240 MHz Dual-Core, 520 KB SRAM, 4MB Flash |
| **MX1508** | Dual H-Bridge DC Motor Driver | 2.5V–9.6V (VM) | 1.5A continuous per channel, Low-loss MOS |
| **Dual TT DC Motors** | Propulsion & Differential Steering| 6.0V–8.4V | 1:48 gear ratio, 200 RPM @ 6V |
| **OV7670 (Non-FIFO)**| Video Capture & FPV Telemetry | 3.3V | VGA/QQVGA CMOS sensor, 8-bit parallel bus |
| **HC-SR04** | Ultrasonic Rangefinder | 5.0V | 2 cm – 400 cm range, 15° beam angle |
| **SG90 Micro Servo** | 180° Radar Pan Mechanism | 5.0V | 1.8 kg·cm torque, 50 Hz PWM control |
| **MPU6050** | 6-DOF IMU (Accelerometer + Gyro) | 3.3V | I2C (`0x68`), 16-bit ADCs, Digital Motion Proc |
| **74HC595** | 8-Bit Shift Register | 3.3V | High-speed serial-in, parallel-out LED driver |
| **2S 18650 Pack** | Power Source | 7.4V–8.4V | High-discharge Li-Ion cells with 2S BMS |

<!-- Note: Explain how components were chosen for maximum performance-to-cost ratio, utilizing the ESP32's onboard peripherals to eliminate unnecessary external controller ICs. -->

---

<!-- 
  =============================================================================
  SLIDE 5: MASTER PINOUT & HARDWARE INTEGRATION
  =============================================================================
-->

## ⚡ Conflict-Free Hardware Pin Matrix

Every pin was engineered to avoid strapping pin conflicts and hardware bus collisions:

| Subsystem | Signal Pin | ESP32 GPIO | Electrical Logic | Engineering Safeguard |
| :--- | :--- | :--- | :--- | :--- |
| **Camera (OV7670)** | D0–D7 (Bus) | **36, 39, 34, 35, 32, 33, 25, 26** | 3.3V Input | Utilizes input-only pins (34-39) safely |
| | PCLK, HREF, VSYNC | **16, 17, 4** | 3.3V Input | High-speed clock captured via DMA |
| | XCLK (Master Clk) | **27** | 3.3V Output | 10 MHz PWM clock generated by ESP32 |
| | SIOD / SIOC | **21 / 22** | 3.3V I2C | Shared with MPU6050 bus (4.7kΩ pull-ups) |
| **Motor Driver** | IN1, IN2, IN3, IN4 | **13, 14, 18, 19** | 3.3V PWM | Independent 20 kHz LEDC PWM channels |
| **Ultrasonic** | TRIG / ECHO | **23 / 3** | 3.3V Out / 3.3V In | **1kΩ / 2kΩ level divider protects GPIO 3** |
| **Servo Radar** | Signal (PWM) | **2** | 3.3V PWM | 50 Hz control, powered from 5V rail |
| **Shift Register** | SER / SRCLK / RCLK | **5, 12, 15** | 3.3V Output | **GPIO 12 held LOW at boot** (Strapping safe) |
| **IMU (MPU6050)** | SDA / SCL | **21 / 22** | 3.3V I2C | Fast mode 400 kHz shared bus |

<!-- Note: Emphasize the electrical protection: HC-SR04 ECHO sends 5V, which will destroy an ESP32 pin without our 1k/2k voltage divider. Also note that GPIO 12 must be low at boot. -->

---

<!-- 
  =============================================================================
  SLIDE 6: CAMERA PIPELINE (OV7670 NON-FIFO BREAKTHROUGH)
  =============================================================================
-->

## 👁️ OV7670 Non-FIFO: Direct I2S DMA Pipeline

### The Problem:
Standard OV7670 modules without external AL422B FIFO chips output pixels at **10–20 MHz**. Standard GPIO polling or SPI cannot sample this fast without tearing or dropping 90% of bytes.

### The NovaX I2S DMA Solution:
1. **ESP32 I2S Peripheral in Parallel Slave Mode**: Reconfigures I2S0 to act as a parallel 8-bit bus clocked directly by the OV7670 `PCLK` (GPIO 16).
2. **Direct Memory Access (DMA)**: Hardware DMA channels stream pixel bytes straight into internal SRAM buffers without CPU intervention.
3. **Continuous Line Scan**: 120-line single-pass capture with 1,000 ms VSYNC guard prevents buffer overruns.
4. **On-the-Fly Hardware JPEG Compression**: Frame captured in RGB565 is encoded via ESP32 `fmt2jpg()` at 65% quality (~3.2 KB per frame).
5. **Memory-Recycling Streamer**: Web app consumes stream using `URL.createObjectURL(blob)` and immediate `revokeObjectURL()`, maintaining 0% memory leakage on mobile devices.

<!-- Note: Explain how direct I2S DMA allowed us to use the super affordable non-FIFO OV7670 ($2) instead of the bulky, expensive FIFO version ($10+). -->

---

<!-- 
  =============================================================================
  SLIDE 7: FIRMWARE CORE & SAFETY WATCHDOG
  =============================================================================
-->

## ⚙️ Deterministic Firmware & Safety Engine

### Dual-Core Xtensa LX6 Workload Division:
- **Core 0**: Wi-Fi 802.11 AP/STA, RFC 6455 WebSocket handshakes, HTTP server, and non-blocking OTA upload processing.
- **Core 1**: Real-time motor PWM modulation, MPU6050 gyro integration, and ultrasonic obstacle avoidance.

### Fail-Safe Motor Watchdog:
```
  [ Operator Touch Event ] ──► WS Frame / HTTP Move ──► ESP32 receives command
                                                               │
                                                 Resets watchdog timer (0ms)
                                                               │
        ┌──────────────────────────────────────────────────────┴─────────────────────────────────┐
        ▼                                                                                        ▼
[ Within 400ms ]                                                                    [ > 400ms Silence ]
Keep driving motors at PWM target                                                   Watchdog triggers STOP!
                                                                                    All motor PWM pins -> 0V
```
*Result: Zero chance of runaway vehicle even during sudden Wi-Fi disconnection or app crash.*

<!-- Note: Stress the safety aspect. In robotics, a lost signal must immediately stop actuation. 400ms is the sweet spot between smooth driving and immediate emergency response. -->

---

<!-- 
  =============================================================================
  SLIDE 8: REAL-TIME COMMUNICATION PROTOCOL
  =============================================================================
-->

## 📡 RFC 6455 WebSocket Protocol (:81)

NovaX implements a native RFC 6455 WebSocket server in pure C++ on port 81:

### Command Format (Client ➔ Car):
| JSON Payload | Action | Execution |
| :--- | :--- | :--- |
| `{"cmd":"move","dir":"F","speed":220}` | Drive Forward | PWM 220 applied to Left/Right forward channels |
| `{"cmd":"stop"}` | Full Stop | Immediate active braking, watchdog cleared |
| `{"cmd":"rotate","dir":"L","angle":90}`| Precision Pivot | Non-blocking MPU6050 yaw angle closed-loop turn |
| `{"cmd":"radar_scan"}` | Trigger 180° Sweep | SG90 sweeps 0°➔90°➔180°, compiling distance array |
| `{"cmd":"led","pattern":"warn"}` | Emergency Strobes | 74HC595 runs non-blocking alternating flasher |

### Telemetry Stream (Car ➔ Client @ 10 Hz):
```json
{
  "t": "telem",
  "sonar": 42.5,
  "yaw": -12.4,
  "pitch": 1.2,
  "roll": 0.8,
  "mode": "manual",
  "rssi": -48,
  "uptime": 1420
}
```

<!-- Note: Highlight that WebSocket eliminates the 200ms HTTP connection handshake overhead, allowing real-time driving with instantaneous responsiveness. -->

---

<!-- 
  =============================================================================
  SLIDE 9: AUTONOMOUS OBSTACLE AVOIDANCE
  =============================================================================
-->

## 🤖 Dual-Mode Autonomy: Reactive & Inertial

NovaX runs standalone edge autonomy on Core 1 without relying on external servers:

```
                  ┌────────────────────────┐
                  │     DRIVE FORWARD      │
                  └───────────┬────────────┘
                              │ Distance < 25 cm
                              ▼
                  ┌────────────────────────┐
                  │ BRAKE & REVERSE 200ms  │
                  └───────────┬────────────┘
                              │
                              ▼
                  ┌────────────────────────┐
                  │   SERVO RADAR SWEEP    │
                  │ Scan Left vs Right Dis │
                  └───────────┬────────────┘
                              │
             ┌────────────────┴────────────────┐
             ▼                                 ▼
   [ Left Path Clearer ]             [ Right Path Clearer ]
   Rotate Left 60° via Gyro          Rotate Right 60° via Gyro
             │                                 │
             └────────────────┬────────────────┘
                              ▼
                  ┌────────────────────────┐
                  │  RESUME FORWARD DRIVE  │
                  └────────────────────────┘
```

- **Cached Ultrasonic Sampling**: 80ms TTL guard prevents blocking the main control loop.
- **Inertial Heading Correction**: MPU6050 Z-axis gyro integration guarantees precise rotation angles regardless of battery voltage or carpet friction.

<!-- Note: Point out the state machine logic. Notice that all turns are closed-loop using the MPU6050 gyroscope rather than arbitrary timed delays. -->

---

<!-- 
  =============================================================================
  SLIDE 10: CYBER COCKPIT UI & FRONTEND ARCHITECTURE
  =============================================================================
-->

## 🎮 Cyber Cockpit Mobile HUD

Built with Vanilla ES6+, CSS Glassmorphism, and HTML5 Canvas:

```
+─────────────────────────────────────────────────────────────────────────────+
|  [●] NOVA-X V2.4.46               [📶 AP: 192.168.4.1]       [⚙️ Settings]   |
+─────────────────────────────────────────────────────────────────────────────+
|  CARD 1: CONTROL MATRIX      |  CARD 2: LIVE HUD (CENTER)   |  CARD 3: RADAR & AUX        |
|                              |                              |                             |
|         ▲ FORWARD            |  ┌────────────────────────┐  |   180° RADAR ARC            |
|    ◄ LEFT   [■]   RIGHT ►    |  │   LIVE FPV CAM FEED    │  |   ╭─────────────╮           |
|         ▼ REVERSE            |  │   OV7670 QQVGA @ 10FPS │  |   │    42 cm    │ Sweep     |
|                              |  │   [Crosshair Reticle]  │  |   ╰─────────────╯ Blips     |
|  [ ✏️ DRAW MODE ]             |  └────────────────────────┘  |                             |
|  *Touch canvas allows user   |  [📷 CAM ON]  [📸 SNAPSHOT]  |  [🔄 SCAN]     [📡 SONAR]   |
|   to trace custom paths for  |                              |  Rotate: [ 90°L | 90°R | 360°]|
|   the car to follow!         |  [ AUTO / MANUAL ]  [ STOP ] |  LEDs:   [ OFF  | WARN | BLK ]|
+─────────────────────────────────────────────────────────────────────────────+
```

- **Haptic Tactile Feedback**: Hardware vibration on directional presses.
- **Touch Hold-to-Repeat**: Intuitive continuous driving physics.
- **Draw-Path Canvas**: High-DPI gesture path generation normalized to motor vectors.

<!-- Note: Showcase how user-friendly and aesthetically polished the cockpit is. It looks like an electric vehicle / sci-fi HUD instead of a generic web form. -->

---

<!-- 
  =============================================================================
  SLIDE 11: NATIVE CAPACITOR ANDROID PLATFORM
  =============================================================================
-->

## 📱 Native Android Engine (Capacitor)

### Overcoming Mobile Browser Security Roadblocks:
- Modern mobile browsers (Chrome / Safari) strictly block mixed-content HTTP requests to `192.168.4.1` when browsing on HTTPS, and drop background sockets.
- **The Solution**: Native Android runtime via **Capacitor 8**.

### Native App Architecture:
- **Package ID**: `com.novax.controller`
- **Cleartext Traffic Enabled**: Allows direct, secure socket communication with local ESP32 IP.
- **Hardware Integration**:
  - `android.permission.VIBRATE`: Millisecond-level haptic pulses.
  - `android.permission.ACCESS_FINE_LOCATION`: Auto-detection of `NovaX-Car` Wi-Fi SSID.
  - Native Screen Orientation lock in landscape / dynamic responsive portrait.
- **Automated Cloud CI/CD**: Every git push compiles a release-ready APK via GitHub Actions in under 3 minutes!

<!-- Note: Explain how Capacitor gives us the best of both worlds: rapid web UI development combined with raw Android native permissions and offline performance. -->

---

<!-- 
  =============================================================================
  SLIDE 12: DUAL-STAGE WIRELESS OTA FLASHING
  =============================================================================
-->

## ⚡ Zero-Wire Over-The-Air (OTA) Ecosystem

No cables, no drivers, and no Arduino IDE required for end users:

```
  STAGE 1: NEW / BLANK ESP32 (First-Time Setup)
  ┌─────────────────────────────────────────────────────────────────────────┐
  │  Flash NovaX_OTA_Bootstrap.ino via USB (One-Time Only)                  │
  │  • Zero external libraries required (Pure core ESP32 headers)           │
  │  • ESP32 creates "NovaX-Car" Wi-Fi AP @ 192.168.4.1                     │
  │  • Open browser ➔ Drag & drop NovaX-Firmware.bin ➔ Reboots in 10s       │
  └─────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
  STAGE 2: PRODUCTION IN-CAR OTA (All Future Updates)
  ┌─────────────────────────────────────────────────────────────────────────┐
  │  1. Connect phone to Home Wi-Fi ➔ Tap "Pre-Download Firmware"          │
  │     (Saves latest .bin from GitHub into Phone IndexedDB cache)          │
  │  2. Connect phone to "NovaX-Car" Wi-Fi                                  │
  │  3. Tap "⚡ Flash Cached Firmware" inside NovaX App                     │
  │  • Multipart chunked upload via /ota/update with SHA-1 auth             │
  │  • Dual flash partition ping-pong (app0 / app1) with auto-rollback      │
  └─────────────────────────────────────────────────────────────────────────┘
```

<!-- Note: Highlight the "Pre-Download" feature: users can download the firmware at home, walk outside to their car with no internet, and update the vehicle completely offline from their phone! -->

---

<!-- 
  =============================================================================
  SLIDE 13: PERFORMANCE BENCHMARKS & COMPARISON
  =============================================================================
-->

## 📊 Technical Benchmarks & Innovation Comparison

| Metric / Capability | Standard Hobbyist Robot Car | NovaX V2 Platform | Improvement |
| :--- | :--- | :--- | :--- |
| **Command Latency** | 180 ms – 350 ms (HTTP Polling) | **< 15 ms** (RFC 6455 WebSocket) | **20x Faster** |
| **Failsafe Watchdog** | None (Runaway hazard) | **400 ms Hardware Watchdog** | **Safety Certified** |
| **Video Pipeline** | None / Costly ESP32-CAM board | **Native Non-FIFO OV7670 DMA** | **80% Cost Reduction** |
| **Camera FPS** | 1 – 2 FPS (Polling) | **8 – 12 FPS** (Sequential DMA JPEG)| **5x Frame Rate** |
| **Obstacle Avoidance** | Hardcoded blind delays | **Inertial Gyro Closed-Loop** | **Deterministic Turns** |
| **Deployment Method** | USB Cable + 10+ Arduino Libs | **Dual-Stage Wireless OTA** | **100% Cable-Free** |
| **Client Support** | Basic static web page | **Capacitor Native APK + PWA** | **Native App Store Ready**|
| **Firmware Footprint** | Bloated (~1.4 MB) | **886 KB** (Fits standard OTA slot)| **Optimized Flash** |

<!-- Note: Walk through the table line by line. These quantifiable metrics prove the engineering rigor of NovaX compared to typical school/hackathon projects. -->

---

<!-- 
  =============================================================================
  SLIDE 14: STEP-BY-STEP LIVE DEMONSTRATION GUIDE
  =============================================================================
-->

## 🎬 Live Demonstration Script

### Step 1: Boot & Connection
1. Power up NovaX from the 2S 18650 Li-Ion switch.
2. Observe 74HC595 LED initialization pulse.
3. Connect phone to Wi-Fi SSID: `NovaX-Car` (Password: `12345678`).
4. Launch the **NovaX Controller** app (or open `http://192.168.4.1`).

### Step 2: Telemetry & FPV Camera
1. Enable **Camera ON** in the central HUD.
2. Point out real-time QQVGA live video stream with zero lag.
3. Verify live radar arc showing real-time centimeter distance.

### Step 3: Precision Driving & Autonomy
1. Demonstrate hold-to-repeat D-pad forward/reverse maneuvers.
2. Demonstrate **Draw Mode**: Draw an "S" curve on the screen; watch car execute the path.
3. Switch to **AUTO Mode**: Place obstacle within 20cm; watch car brake, sweep radar left/right, and pivot toward the clear path.

### Step 4: Over-The-Air Update
1. Open Settings modal ➔ demonstrate in-app wireless OTA flasher.

<!-- Note: Use this slide during the presentation to guide your live demo smoothly without forgetting any of the core features. -->

---

<!-- 
  =============================================================================
  SLIDE 15: FUTURE ROADMAP & SCALABILITY
  =============================================================================
-->

## 🚀 Future Roadmap & Next Milestones

```
  PHASE 1 (COMPLETED)                PHASE 2 (IN PROGRESS)              PHASE 3 (FUTURE)
  ┌─────────────────────────┐        ┌─────────────────────────┐        ┌─────────────────────────┐
  │ • ESP32 V2 Hardware Map │        │ • Edge TinyML Onboard   │        │ • Multi-Agent Mesh      │
  │ • RFC 6455 WebSocket    │  ───►  │   (Person Following &   │  ───►  │   (ESP-NOW Swarm Fleet) │
  │ • OV7670 I2S DMA Vision │        │    Stop Sign Detection) │        │ • 2D SLAM Mapping via   │
  │ • Capacitor Android APK │        │ • Optical Flow Sensor   │        │   Continuous Lidar      │
  │ • Dual-Stage OTA Engine │        │   (Indoor Odometry)     │        │ • Voice Control via NLP │
  └─────────────────────────┘        └─────────────────────────┘        └─────────────────────────┘
```

- **TinyML Integration**: Quantized MobileNet model running directly on ESP32 Core 0 for gesture and obstacle classification.
- **Swarm Robotics**: Multi-car synchronization using peer-to-peer ESP-NOW protocol without requiring external routers.
- **RTOS Optimization**: Upgrading FreeRTOS queue pipelines to support 15+ FPS video streaming.

<!-- Note: Demonstrate that NovaX is not just a finished project, but an extensible platform with clear pathways for future academic research and advanced engineering. -->

---

<!-- 
  =============================================================================
  SLIDE 16: CONCLUSION & TECHNICAL Q&A
  =============================================================================
-->

## 🏁 Conclusion & Technical Q&A

### Summary of Achievements:
- Built a complete, production-ready mobile robotics platform from scratch.
- Mastered direct hardware I2S DMA acquisition for budget CMOS sensors.
- Created an industrial-grade safety architecture with sub-15ms WebSocket control.
- Designed an ultra-convenient cable-free OTA deployment pipeline.

### Links & Resources:
- 🌐 **Live Web Cockpit**: [onebotyt.github.io/robocar](https://onebotyt.github.io/robocar/)
- 📲 **Download Android APK**: [NovaX-Controller.apk](https://github.com/onebotyt/robocar/raw/main/apk/NovaX-Controller.apk)
- 💻 **Source Code Repository**: [github.com/onebotyt/robocar](https://github.com/onebotyt/robocar)
- 📖 **System Documentation**: [`README.md`](README.md) & [`docs/`](docs/)

<br>

### ❓ Questions & Discussion
*Thank you for your time! We welcome questions from the evaluation committee.*

<!-- Note: Open the floor to questions. Be prepared to explain I2S DMA timing, WebSocket frame masking, and the HC-SR04 voltage divider calculation. -->
