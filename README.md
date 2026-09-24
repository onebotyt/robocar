# 🚗 NovaX Cyber Cockpit & Mobile App V2

> **High-performance mobile phone controller for the NovaX ESP32 robot car.**

[![Live Web Cockpit](https://img.shields.io/badge/Live%20Demo-GitHub%20Pages-00bfa5?style=for-the-badge&logo=googlechrome)](https://onebotyt.github.io/robocar/)
[![Download Android APK](https://img.shields.io/badge/Download-Android%20APK%20(Direct)-brightgreen?style=for-the-badge&logo=android)](https://github.com/onebotyt/robocar/raw/main/apk/NovaX-Controller.apk)
[![Build NovaX Android APK](https://github.com/onebotyt/robocar/actions/workflows/build-apk.yml/badge.svg)](https://github.com/onebotyt/robocar/actions/workflows/build-apk.yml)

### 🌐 [👉 Click here to test Live Web Cockpit UI](https://onebotyt.github.io/robocar/)

---

### 📥 Download Android Native App (.apk)

| Direct Download Link | GitHub Repository Location | Build Status |
| :--- | :--- | :--- |
| [**📲 Download NovaX-Controller.apk (Direct)**](https://github.com/onebotyt/robocar/raw/main/apk/NovaX-Controller.apk) | [`apk/NovaX-Controller.apk`](apk/NovaX-Controller.apk) | [![Build NovaX Android APK](https://github.com/onebotyt/robocar/actions/workflows/build-apk.yml/badge.svg)](https://github.com/onebotyt/robocar/actions/workflows/build-apk.yml) |

#### ⚡ Quick Phone Installation (30 Seconds):
1. **[👉 Click here to directly download the .apk to your Android phone](https://github.com/onebotyt/robocar/raw/main/apk/NovaX-Controller.apk)**
2. Tap the downloaded `NovaX-Controller.apk` file.
3. If prompted by Android, tap **Settings** and toggle ON **"Allow from this source"**.
4. Tap **Install** and launch **NovaX Controller**!
5. Connect your phone's Wi-Fi to the robot car AP: `NovaX-Car` (Password: `12345678`).

---

## 📱 1. Mobile Phone Screen Cockpit

The controller in `pwa/` has been completely redesigned with a **mobile-first cyber cockpit interface** tailored specifically for smartphone touchscreens:

- **Ergonomic Dual Driving Controls**:
  - **Precision Cyber D-Pad**: Generous 76x64px touch buttons (FWD, REV, LEFT, RIGHT, and glowing STOP) with continuous hold-repeat and haptic vibration feedback.
  - **Virtual Analog Joystick**: Smooth 360° circular touch surface with vector angle, direction, and thrust indicators.
- **Tactical Quick Maneuver Bar**: Instant 90° Turn Left, 90° Turn Right, 360° Gyro Spin, and Full-Width Emergency Brake.
- **Live Camera HUD**: 16:9 OV7670 camera stream with crosshairs, live FPS counter, snapshot capture, and an animated radar standby graphic when camera is OFF.
- **Real-Time Telemetry Bar**: Proximity distance bar with multi-color threat warning (Clear > 60cm, Caution 25-60cm, Hazard < 25cm) and MPU6050 yaw heading with rotating compass rose.
- **180° Radar Sweeper**: Semicircular range radar with animated sweep beam, sensor range rings, and dynamic Left / Front / Right obstacle distance markers.
- **Autonav Path Draw**: Touch canvas that dynamically fits phone screens, with real-time trajectory curve prediction (Left / Straight / Right).
- **74HC595 LED Simulation**: 8-LED shift register status bar animating OFF, BLINK, WARN, and PULSE lighting patterns.
- **Wi-Fi & OTA Manager**: Switch between AP (`192.168.4.1`) and STA (`novax.local`), scan nearby networks, manage up to 5 saved Wi-Fi profiles in ESP32 NVS, and upload `.bin` firmware OTA.
- **Quick IP Switcher & Fullscreen Mode**: Tap the connection status pill to quick-switch robot IP, or tap the fullscreen button to hide mobile browser URL bars.

---

## 📲 2. Converting & Running as a Mobile App

You have two powerful options to run NovaX as a dedicated mobile app on your phone:

### Option A: Install Directly as a PWA (No Build Required)
1. Serve or host the `pwa/` folder (or run `npm.cmd start` on your computer).
2. Connect your phone's Wi-Fi to the ESP32 (`NovaX-V2`, password `12345678`) or local network.
3. Open the controller URL in Chrome (Android) or Safari (iOS).
4. Tap **"Install App"** inside the app or tap browser menu:
   - **Android Chrome**: Tap `⋮` (menu) -> **"Add to Home screen"** or **"Install app"**.
   - **iOS Safari**: Tap Share icon -> **"Add to Home Screen"**.
5. The app launches in full-screen standalone mode with custom NovaX cyber icons and offline caching.

---

### Option B: Native Android App (Capacitor Native APK)
Because modern mobile browsers restrict plain HTTP calls (`http://192.168.4.1`) from public HTTPS sites, the project includes a complete **Capacitor Android wrapper** with `android:usesCleartextTraffic="true"` and `allowMixedContent: true` pre-configured.

#### Prerequisites
- [Android Studio](https://developer.android.com/studio) or Android SDK with Java/JDK installed.

#### Quick Commands (run in project folder):
```bash
# 1. Sync any web changes to the Android native project
npm.cmd run sync

# 2. Open project in Android Studio
npm.cmd run open:android

# 3. Or build the debug APK directly via command line
npm.cmd run build:apk
```

#### Installing the APK on your Phone:
- The generated APK will be at:
  `android/app/build/outputs/apk/debug/app-debug.apk`
- Transfer `app-debug.apk` to your Android phone via USB or WhatsApp/Drive and tap to install!

---

## 🔄 3. GitHub In-App Update System (Hot-OTA & APK Updates)

You can now publish code changes to your GitHub repository and update the app directly on your phone **with 1 tap** without needing to plug the phone into a PC or reinstall the APK!

### How the In-App Update System Works:

1. **In-App Hot-OTA Updater (No APK Re-install)**:
   - When you make changes to HTML, CSS, or JS in `pwa/` (e.g., tweaking steering sensitivity, adding features, or redesigning the cockpit):
   - In `pwa/version.json`, increment the version (e.g. `2.2.1`).
   - Push your code to GitHub:
     ```bash
     git add .
     git commit -m "Update car controls"
     git push origin main
     ```
   - On your phone (connected to internet/mobile data or home Wi-Fi):
     - Open NovaX.
     - Go to the **SYSTEM** tab ➔ **GITHUB APP UPDATES**.
     - Enter your repository (e.g. `username/NovaX_PWA_OTA_V2`) once (it saves automatically).
     - Tap **`CHECK GITHUB`** ➔ the app detects the new version and shows a notification banner.
     - Tap **`UPDATE APP NOW`** (or tap the banner).
     - The app downloads the new `style.css` and `app.js` directly from GitHub, saves them to persistent local storage, and reloads in **2 seconds** with your new code!
     - The updated code persists even when you disconnect from the internet and connect to the ESP32 `NovaX-V2` AP!

2. **Automated Cloud APK Builds (GitHub Actions)**:
   - A ready-to-use GitHub Actions workflow is included at [`.github/workflows/build-apk.yml`](file:///d:/car/NovaX_PWA_OTA_V2/NovaX_PWA_OTA_V2/.github/workflows/build-apk.yml).
   - Whenever you push a git tag (e.g., `git tag v2.2.1 && git push origin v2.2.1`):
     - GitHub automatically compiles the Android APK in the cloud.
     - The APK is attached to a GitHub Release.
     - You can download the new APK on your phone with one tap using the **`LATEST APK`** button in the app!

3. **Reset to Factory Bundle**:
   - If you ever want to revert back to the code originally bundled inside the APK, tap **`RESET BUNDLE`** in the System tab.

---

## 🛠️ Wi-Fi & Firmware Behavior
- **Default Startup Mode**: Every ESP32 boot starts in AP mode: SSID `NovaX-V2`, password `12345678`, IP `192.168.4.1`.
- **Switch to STA**: In the System tab, select a saved network and tap `Switch to STA`. Robot connects to your home Wi-Fi and becomes available at `http://novax.local`.
- **Switch to AP**: Returns ESP32 to direct standalone AP mode (`http://192.168.4.1`).

## 📌 Hardware Pin Mapping
- **OV7670**: D0=36, D1=39, D2=34, D3=35, D4=32, D5=33, D6=25, D7=26, XCLK=27, PCLK=16, HREF=17, VSYNC=4, SIOD=21, SIOC=22.
- **MX1508 Motors**: IN1=13, IN2=14, IN3=18, IN4=19.
- **HC-SR04 Sonar**: TRIG=23, ECHO=3 (through voltage divider).
- **SG90 Servo**: Pin 2.
- **74HC595 LED Shift Register**: DATA=5, CLOCK=12, LATCH=15.
- **MPU6050 Gyro/Accelerometer**: SDA=21, SCL=22.
