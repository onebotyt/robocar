# 🌐 NovaX V2 - REST API Reference

The ESP32 Web Server provides RESTful HTTP endpoints for device configuration, Wi-Fi management, firmware OTA upgrades, camera snapshots, and backward-compatible fallbacks.

All endpoints support Cross-Origin Resource Sharing (`CORS`) with `Access-Control-Allow-Origin: *`.

---

## 1. System & Device Information

### `GET /firmware`
Returns hardware, firmware version, build metadata, and system health.
- **Request**: `GET /firmware`
- **Response**: `200 OK` (JSON)
```json
{
  "name": "NovaX V2",
  "version": "2.2.0",
  "hardware": "ESP32-WROOM-32",
  "buildDate": "Sep 24 2026",
  "camera": "OV7670",
  "imu": "MPU6050",
  "status": "online"
}
```

### `GET /status`
Returns complete snapshot of device telemetry.
- **Request**: `GET /status`
- **Response**: `200 OK` (JSON)
```json
{
  "distance": 42,
  "mode": 1,
  "yaw": 183.5,
  "camera": 1,
  "wifiMode": "AP",
  "ip": "192.168.4.1",
  "version": "2.2.0"
}
```

---

## 2. Wi-Fi Configuration Endpoints

### `GET /wifi/scan`
Scans for available 2.4 GHz Wi-Fi access points.
- **Request**: `GET /wifi/scan`
- **Response**: `200 OK` (JSON)
```json
{
  "networks": [
    {"ssid": "Home-Network", "rssi": -55, "secure": 1},
    {"ssid": "Guest-WiFi", "rssi": -72, "secure": 0}
  ]
}
```

### `GET /wifi/saved`
Lists saved Wi-Fi profiles stored in ESP32 NVS (Non-Volatile Storage).
*Security rule: Passwords are NEVER returned in this response.*
- **Request**: `GET /wifi/saved`
- **Response**: `200 OK` (JSON)
```json
{
  "selected": 0,
  "networks": [
    {"ssid": "Home-Network"},
    {"ssid": "Office-Lab"}
  ]
}
```

### `POST /wifi/save`
Saves a new Wi-Fi profile to ESP32 NVS. Up to 5 profiles are retained.
*Important: Saving credentials does NOT automatically connect.*
- **Request**: `POST /wifi/save`
- **Form Parameters / Query Parameters**:
  - `ssid`: Network SSID (string, max 32 chars)
  - `password`: Network passphrase (string, max 64 chars)
- **Response**: `200 OK` (JSON list of saved networks) or `400 Bad Request`.

### `POST /wifi/select`
Selects the active saved Wi-Fi profile index.
- **Request**: `POST /wifi/select?index=0`
- **Response**: `200 OK` (JSON)

### `POST /wifi/delete`
Deletes a saved Wi-Fi profile by index from ESP32 NVS.
- **Request**: `POST /wifi/delete?index=0`
- **Response**: `200 OK` (JSON)

### `POST /wifi/switchSta`
Instructs the ESP32 to disconnect AP mode and connect as a station to the selected saved Wi-Fi network.
- **Request**: `POST /wifi/switchSta`
- **Response**: `200 OK` (JSON)
```json
{
  "status": "connecting",
  "ssid": "Home-Network",
  "host": "http://novax.local"
}
```

### `POST /wifi/switchAp`
Instructs the ESP32 to disconnect from the home router and return to standalone Access Point mode (`NovaX-Car`).
- **Request**: `POST /wifi/switchAp`
- **Response**: `200 OK` (JSON)
```json
{
  "status": "switched",
  "mode": "AP",
  "ip": "192.168.4.1"
}
```

---

## 3. Firmware OTA (Over-The-Air) Endpoints

### `GET /ota/status`
Checks if the OTA service is active and returns current firmware metadata.
- **Request**: `GET /ota/status`
- **Response**: `200 OK` (JSON)
```json
{
  "version": "2.2.0",
  "ip": "192.168.4.1",
  "ota": true
}
```

### `POST /ota/update`
Uploads a compiled firmware binary (`firmware.bin`) to flash memory.
- **Security**: Requires header `X-NovaX-OTA: NovaX-OTA-ChangeMe` (or configured secret token).
- **Safety Interlock**: All motors are immediately cut off and disabled before flashing starts.
- **Response**: `200 OK` on flash success with automatic reboot in 500ms; `401 Unauthorized` or `500 Server Error` on failure.

---

## 4. Live Camera & Visual Endpoints

### `GET /cam.jpg`
Captures and returns a single JPEG frame from the OV7670 camera.
- **Request**: `GET /cam.jpg`
- **Response**: `200 OK` (`Content-Type: image/jpeg`) with `Cache-Control: no-store`.

---

## 5. Path Navigation Endpoint

### `POST /path`
Receives normalized path waypoints from Draw Mode.
- **Request**: `POST /path`
- **Body**: Semicolon-delimited coordinate string (e.g. `0.50,1.00;0.45,0.70;0.35,0.40`).
- **Response**: `200 OK` (`text/plain`).
