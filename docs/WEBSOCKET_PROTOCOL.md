# ⚡ NovaX V2 - WebSocket Protocol Specification

## 1. Overview
The NovaX V2 system uses **WebSocket** as the primary low-latency, bi-directional communication channel between the Android Controller App (or Web Cockpit) and the ESP32 robot car.

- **Port**: Default WebSocket service runs on **port 81** (`ws://<robot-ip>:81/`) or upgraded on port 80.
- **Framing**: RFC 6455 text frames containing JSON payloads.
- **Watchdog / Keepalive**: The client sends periodic `ping` messages (or directional hold commands). The ESP32 enforces a **400ms motor command timeout**. If no valid drive command is received within 400ms, or if the WebSocket disconnects, the ESP32 automatically halts all motors.

---

## 2. Client → ESP32 Commands

All client commands are JSON objects containing a `"type"` string property.

### 2.1 Motor Drive Command
Sent continuously while the user holds a D-pad button or moves an analog control (sent at ~80–120ms intervals).
```json
{
  "type": "move",
  "dir": "F",
  "speed": 180
}
```
- `"dir"` (string, required):
  - `"F"`: Forward cruise
  - `"B"`: Reverse
  - `"L"`: Pivot turn left
  - `"R"`: Pivot turn right
  - `"S"`: Stop / Neutral
- `"speed"` (integer, optional): PWM value between `0` and `255` (default: `180`).

### 2.2 Immediate Emergency STOP
Stops both motors instantly and resets watchdog timer.
```json
{
  "type": "stop"
}
```

### 2.3 Drive Mode Switch
Switches between manual remote control and autonomous obstacle-avoidance navigation.
```json
{
  "type": "mode",
  "value": "manual"
}
```
- `"value"` (string): `"manual"` or `"auto"`.

### 2.4 Gyroscope Assisted Rotation
Executes a precise angle turn using MPU6050 gyroscope feedback.
```json
{
  "type": "rotate",
  "dir": "left"
}
```
- `"dir"` (string): `"left"` (90° turn left), `"right"` (90° turn right), or `"360"` (360° spin).

### 2.5 LED Pattern Effect
Sets the 74HC595 shift register LED animation.
```json
{
  "type": "led",
  "pattern": "blink"
}
```
- `"pattern"` (string): `"off"`, `"blink"`, `"warn"`, or `"pulse"`.

### 2.6 Radar Scan Trigger
Requests the SG90 servo to sweep the HC-SR04 ultrasonic sensor from -90° to +90° and stream angle/distance measurements.
```json
{
  "type": "scan"
}
```

### 2.7 Ultrasonic Sensor Enable / Disable
Enables or disables the HC-SR04 sensor pinging.
```json
{
  "type": "sonar",
  "state": "on"
}
```
- `"state"` (string): `"on"` or `"off"`.

### 2.8 Path Waypoint Execution (Draw Mode)
Transmits drawn trajectory points to the ESP32.
```json
{
  "type": "path",
  "points": [
    {"x": 0.50, "y": 1.00},
    {"x": 0.42, "y": 0.70},
    {"x": 0.35, "y": 0.40},
    {"x": 0.30, "y": 0.10}
  ]
}
```

### 2.9 Heartbeat Ping
Keeps the connection active and verifies round-trip latency.
```json
{
  "type": "ping"
}
```

---

## 3. ESP32 → Client Telemetry & Events

### 3.1 Live Telemetry Stream
Broadcast by ESP32 at ~10 Hz (every 100ms) to all connected clients.
```json
{
  "type": "telemetry",
  "distance": 42,
  "heading": 183.5,
  "battery": 3.92,
  "mode": "manual",
  "camera": true,
  "wifiMode": "AP",
  "ip": "192.168.4.1",
  "uptime": 12480
}
```
- `"distance"` (integer): Front ultrasonic distance reading in centimeters (`400` if no obstacle).
- `"heading"` (float): MPU6050 estimated yaw rotation in degrees (`0.0` to `359.9`).
- `"battery"` (float): Estimated battery voltage (e.g. `3.92` V).
- `"mode"` (string): Current robot state (`"manual"` or `"auto"`).
- `"camera"` (boolean): `true` if OV7670 camera sensor initialized successfully.
- `"wifiMode"` (string): `"AP"` or `"STA"`.
- `"ip"` (string): Current IP address of the robot car.
- `"uptime"` (integer): ESP32 uptime in seconds.

### 3.2 Real-time Radar Point Stream
Broadcast during an active servo sweep as the servo steps through angles.
```json
{
  "type": "radar",
  "angle": 30,
  "distance": 86
}
```
- `"angle"` (integer): Angle in degrees from -90° (full left) to +90° (full right).
- `"distance"` (integer): Measured distance at this angle in cm.

### 3.3 Radar Sweep Completed Summary
Broadcast when a full servo sweep finishes.
```json
{
  "type": "radar_summary",
  "left": 140,
  "front": 42,
  "right": 210,
  "done": true
}
```

### 3.4 Heartbeat Pong
Reply to client ping message.
```json
{
  "type": "pong",
  "time": 12480102
}
```

---

## 4. Connection Lifecycle & Safety

```
[ Android App ]                                      [ ESP32 V2 ]
       |                                                   |
       |----- WebSocket Connect (ws://192.168.4.1:81) ---->| (RFC 6455 Handshake)
       |<---- Connected + Initial Status ------------------|
       |                                                   |
       |---- {"type":"move", "dir":"F", "speed":180} ----->| (Reset watchdog = 0ms)
       |                                                   | Motors run FORWARD
       |                                                   |
       |  [No command received for > 400ms]                |
       |                                                   | Watchdog Triggers:
       |                                                   | => Motors STOP
       |                                                   |
       |X- - - Connection Drops / Link Lost - - - - - - - X|
       |                                                   | OnDisconnect Trigger:
       |                                                   | => Motors STOP
       |<---- Auto-Reconnect (every 1.5s) -----------------|
```
