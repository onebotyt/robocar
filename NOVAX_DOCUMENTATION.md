# 🚗 NovaX Robot Car - Complete System Documentation

Welcome to the **NovaX Cyber Cockpit & Native Android Controller System**! This document provides a complete technical and user guide for everything built and configured in this project.

---

## 📋 Table of Contents
1. [Cockpit Architecture & Mobile UI](#1-cockpit-architecture--mobile-ui)
2. [Cockpit Layout Breakdown](#2-cockpit-layout-breakdown)
3. [Capacitor Native Android Application](#3-capacitor-native-android-application)
4. [GitHub Cloud Build (Automatic APK via GitHub Actions)](#4-github-cloud-build-automatic-apk-via-github-actions)
5. [In-App GitHub Hot-Updater System](#5-in-app-github-hot-updater-system)
6. [ESP32 Firmware & Network Setup](#6-esp32-firmware--network-setup)
7. [How to Install the APK on Your Android Phone](#7-how-to-install-the-apk-on-your-android-phone)

---

## 1. Cockpit Architecture & Mobile UI

The frontend is a mobile-first web app that runs equally as a **Progressive Web App (PWA)** and inside a **Native Android WebView (Capacitor)**.

- **Stack**: Pure Vanilla HTML5, CSS3 (Cyber Glassmorphism theme), and Modular Modern JavaScript (ES6+).
- **Mobile Responsive Design**: Structured with 3 cards that dynamically adapt to smartphone touchscreens (portrait and landscape orientations).
- **Low Latency HTTP API**: Communicates directly with the ESP32 Web Server on `http://192.168.4.1` (or local station IP).
- **Haptic Vibration**: Hardware touch feedback for D-pad, radar scanning, and path drawing.

---

## 2. Cockpit Layout Breakdown

```
+-----------------------------------------------------------------------------------+
|  [●] NOVA-X V2.2                              [⚙️ Settings]   [⛶ Fullscreen]     |
+-----------------------------------------------------------------------------------+
|  [ CARD 1: CONTROLS ]      |  [ CARD 2: NOVA X (CENTER) ] |  [ CARD 3: RADAR & AUX ]     |
|                            |                              |                              |
|  ▲ Forward Arrow           |  LIVE CAMERA (OV7670)        |  ╭────────╮ Radar Arc        |
|  ◄ Left   [■]   Right ►    |  ┌────────────────────────┐  |  │  42 cm │ Sweep Needle     |
|  ▼ Reverse Arrow           |  │ Live Video Feed Window │  |  ╰────────╯ Blips (L, F, R)  |
|                            |  │ (FPS Counter & Badge)  │  |                              |
|  [Draw Mode] [Clear] [Send]|  └────────────────────────┘  |  [Scan]        [Sonar ON]    |
|                            |  [Camera ON]  [Snapshot]     |                              |
|  *Tapping "Draw Mode"      |                              |  Rotation:                   |
|   swaps D-pad with touch   |  [MANUAL / AUTO]   [ STOP ]  |  [Rotate L] [Rotate R] [360°]|
|   path-drawing canvas!     |                              |  LEDs:                       |
|                            |                              |  [Off] [Blink] [Warn] [Pulse]|
+-----------------------------------------------------------------------------------+
```

### Card 1: Driving Controls & Autonav Path Drawing (Left)
- **Precision D-Pad**:
  - Teal directional buttons with hold-to-repeat command firing (`/move?dir=F/B/L/R`).
  - Red Emergency Stop center button (`/stop`).
- **Autonav Path-Drawing Canvas**:
  - Tap **Draw Mode** to instantly replace the D-pad with the interactive drawing canvas.
  - High-DPI (Retina) coordinate scaling with dynamic origin watermark.
  - Generates normalized path coordinates (0.0 to 1.0) and transmits them to `POST /path`.
  - Tap **Drive Mode** to switch back to the D-Pad.

### Card 2: Center Live Camera & Master Controls (Center)
- **Live Video Window**:
  - Displays real-time MJPEG / JPEG frames from the ESP32 camera (`/cam.jpg`).
  - **Camera ON / OFF** toggle and **Snapshot** capture buttons.
  - Live FPS counter and standby overlay with targeting reticle.
- **Master Operations**:
  - **MANUAL / AUTO** toggle: Switches between manual driver control and autonomous ultrasonic collision avoidance (`/mode?val=manual|auto`).
  - **STOP**: Emergency full-stop command sent to both motors.
- Clean design: Unnecessary battery status removed for maximum viewing room on mobile screens.

### Card 3: 180° Radar Scope & Auxiliary Controls (Right)
- **Ultrasonic Sweep Radar**:
  - Semicircular sweep scope with concentric distance rings.
  - Animated sweep needle and center distance display in centimeters (`-- cm`).
  - Obstacle detection markers for **Left (L)**, **Front (F)**, and **Right (R)**.
  - **Scan** button: Triggers servo-sweeping obstacle survey (`/scan` & `/scanResult`).
  - **Sonar ON / OFF** toggle: Enables or disables the ultrasonic distance sensor.
- **Rotation Controls (Positioned Below Radar)**:
  - `Rotate L`: 90° pivot left.
  - `Rotate R`: 90° pivot right.
  - `360°`: Full gyro spin in place.
- **LED Lighting Controls (Positioned Below Radar)**:
  - Controls the 74HC595 shift register LED patterns: `Off`, `Blink`, `Warn`, and `Pulse`.

---

## 3. Capacitor Native Android Application

Modern mobile browsers (Chrome / Safari) enforce strict HTTPS and block requests to plain HTTP addresses (like `http://192.168.4.1`). The native Capacitor Android wrapper resolves this completely:

- **Package ID**: `com.novax.controller`
- **Cleartext Traffic**: Enabled via `android:usesCleartextTraffic="true"` in `AndroidManifest.xml` and `cleartext: true` in `capacitor.config.json`.
- **System Permissions Included**:
  - `android.permission.INTERNET`: Direct communication with ESP32 Web Server.
  - `android.permission.ACCESS_NETWORK_STATE` & `ACCESS_WIFI_STATE`: Wi-Fi connectivity.
  - `android.permission.VIBRATE`: Tactile haptic feedback on touch controls.
- **App Icons**: Android mipmap icons generated for `mdpi`, `hdpi`, `xhdpi`, `xxhdpi`, and `xxxhdpi`.

---

## 4. GitHub Cloud Build (Automatic APK via GitHub Actions)

You do **not** need Android Studio or Java installed on your computer. Whenever code is pushed to GitHub, GitHub's cloud runners build the Android `.apk` automatically.

- **Workflow File**: `.github/workflows/build-apk.yml`
- **Runner Configuration**:
  - **OS**: `ubuntu-latest`
  - **Node.js**: `22.x` (required by Capacitor 8 CLI)
  - **JDK**: Java 21 Zulu JDK
  - **Android Compile SDK**: `35`
  - **Gradle**: `8.14.3`
- **Output Artifact**: `NovaX-Controller-APK` containing `NovaX-Controller.apk`.

---

## 5. In-App GitHub Hot-Updater System

NovaX includes a built-in **Live Hot-Update Engine**. You can update the user interface and functionality on your phone without downloading a new APK!

1. When you push new CSS or JavaScript to the `main` branch of `https://github.com/onebotyt/robocar`:
2. The app fetches `https://raw.githubusercontent.com/onebotyt/robocar/main/pwa/version.json`.
3. If a higher version number is detected, a glowing **"Update Available"** notification banner appears on your phone.
4. Tapping **"UPDATE NOW"** downloads the latest `style.css` and `app.js` directly into your phone's persistent storage (`localStorage`) and reloads the app in 2 seconds.
5. In **Settings -> GitHub Hot Updates**, you can also manually check for updates, view changelogs, or tap **"Revert to Original APK"** at any time.

---

## 6. ESP32 Firmware & Network Setup

- **Default Access Point Mode**:
  - **SSID**: `NovaX-Car` (or `NovaX-V2`)
  - **Password**: `12345678`
  - **Default Gateway IP**: `http://192.168.4.1`
- **Station Mode (Home Wi-Fi)**:
  - Connect NovaX to your home router.
  - Access via mDNS: `http://novax.local` or assigned local IP (e.g. `http://192.168.1.150`).
- **REST Endpoints Supported**:
  - `GET /status`: Device telemetry (speed, mode, sonar distance, Wi-Fi IP).
  - `GET /move?dir={F|B|L|R|S}`: Motor drive commands.
  - `POST /path`: Path coordinate waypoints (`x,y;x,y`).
  - `GET /scan` & `GET /scanResult`: Ultrasonic servo sweep.
  - `GET /cam.jpg`: OV7670 live camera snapshot / stream frame.
  - `GET /led?pattern={off|blink|warn|pulse}`: LED shift register modes.
  - `GET /rot?deg={90L|90R|360}`: Quick rotation maneuvers.
  - `POST /update`: Firmware OTA `.bin` upload.

---

## 7. How to Install the APK on Your Android Phone

1. On your phone or computer, open the GitHub Actions page:
   👉 **https://github.com/onebotyt/robocar/actions**
2. Tap on the latest completed workflow run (`Build NovaX Android APK`).
3. Scroll down to the **Artifacts** section at the bottom.
4. Tap **NovaX-Controller-APK** to download `NovaX-Controller-APK.zip`.
5. Unzip the file on your Android phone to extract `NovaX-Controller.apk`.
6. Tap the `.apk` file to install it.
   *(If prompted, allow **"Install unknown apps"** for your file browser / Chrome).*
7. Connect your phone's Wi-Fi to the robot car (`NovaX-Car`), open the **NovaX Controller** app, and drive!
