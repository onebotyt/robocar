# 📶 NovaX V2 - Wi-Fi Architecture & Guide

## 1. Principles & Boot Architecture

- **Always Starts in AP Mode**: Every time the ESP32 powers on or resets, it starts in standalone **Access Point (AP) Mode**:
  - **SSID**: `NovaX-Car`
  - **Password**: `12345678`
  - **Default IP**: `192.168.4.1`
  - **DHCP Subnet**: `192.168.4.0/24`
- **No Physical Button**: Mode switching is entirely handled via software commands through the Android app / Web Cockpit.
- **No Bluetooth**: Wi-Fi provides all communication.

---

## 2. Managing Saved Wi-Fi Profiles

The ESP32 stores up to **5 Wi-Fi profiles** in its non-volatile flash storage using the `Preferences` library (`namespace: "wifi"`).

### Workflow:
1. **Connect Phone to ESP32**:
   Connect your phone's Wi-Fi to `NovaX-Car`. Open the app.
2. **Scan Networks**:
   Open **Settings -> Wi-Fi Manager**. Tap **Scan Networks**.
   The ESP32 issues `GET /wifi/scan` and lists nearby SSIDs with signal strength (RSSI).
3. **Save Network**:
   Select your home network from the list, type the password, and tap **Save Network** (`POST /wifi/save`).
   > **CRITICAL RULE**: Saving credentials **DOES NOT** automatically disconnect or connect to the network. This prevents leaving the user stranded without a connection if a wrong password was entered.
4. **Security Protection**:
   The `GET /wifi/saved` endpoint **never** echoes back the Wi-Fi password. Only the network name (SSID) is displayed in the UI.

---

## 3. Switching to Station (STA) Mode

When you want the robot car to connect to your home router:
1. Select the desired saved network profile.
2. Explicitly tap **"Switch to STA"** (`POST /wifi/switchSta`).
3. The ESP32 attempts to connect to your home router.
4. While connecting, it announces its hostname via mDNS as:
   👉 **`http://novax.local`**
5. Connect your phone to your home Wi-Fi.
6. Open the app and connect to `http://novax.local` (or the IP assigned by your router).

---

## 4. Returning to Access Point (AP) Mode

If you take your robot car outdoors away from your home router:
1. Tap **"Switch to AP"** in Settings (`POST /wifi/switchAp`).
2. The ESP32 immediately re-enables `NovaX-Car` at `192.168.4.1`.
3. If STA mode fails to connect after 15 seconds, the ESP32 automatically recovers back to AP mode to prevent lockouts.
