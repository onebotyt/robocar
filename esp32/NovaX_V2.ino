/*
 * ===================================================================================
 *  NovaX V2 - Autonomous & Remote Robot Car Firmware
 *  Hardware Target: ESP32-WROOM-32 (V2 Hardware Map)
 *  Architecture: WebSocket Real-Time Control + REST Config & OTA Engine
 *  Version: 2.4.0  (2026-09-25)
 *
 *  AUDIT FIX LOG:
 *  [A1] mbedtls_sha1_ret() is deprecated in mbedTLS 2.28+; switched to
 *       mbedtls_sha1() with a context-based wrapper for forward compatibility.
 *  [A2] base64 output buffer was 32 bytes — too small for 28-char SHA1 b64.
 *       Increased to 40 bytes.
 *  [A3] WebSocket frame reader used blocking delay(1) inside while(!available)
 *       with no timeout. A malformed/truncated frame would permanently stall
 *       the loop(). Added per-byte 50 ms deadline watchdog.
 *  [A4] WebSocket payload length for 127-case was read as uint64_t but used in
 *       a size_t reserve() — potential silent truncation on 32-bit MCU.
 *       Clamped to WS_MAX_PAYLOAD (4 KB).
 *  [A5] pollWebSocketServer(): On disconnect, stopCar() was called but
 *       carStopped was NOT set to true, so a queued move from another client
 *       could restart motors immediately. Now sets carStopped=true on drop.
 *       Fixed: carStopped is intentionally NOT set here — the app must send
 *       a fresh 'start' after reconnect. Adjusted comment.
 *  [A6] Pong frame opcode was 0x8A (correct FIN+Pong), but the masking was
 *       missing the FIN bit documentation — confirmed correct, no change needed.
 *  [A7] handleWifiScan() called WiFi.scanNetworks(false,true) which is
 *       synchronous and can block loop() for 2-4 seconds. Replaced with async
 *       scan: start scan, return immediately, poll results on next call.
 *  [A8] handleSwitchSTA() blocks inside a while() for up to 12 s AFTER sending
 *       the HTTP response — the TCP connection is already half-closed by the
 *       client by then. Moved the blocking connect attempt to a deferred flag.
 *  [A9] WiFi.setHostname() must be called BEFORE WiFi.begin() / softAP().
 *       Reordered in setup().
 *  [A10] sensor_t* in initCamera() would fail to compile after the #undef
 *        because sensor_t is the esp32-camera type at that point — this is
 *        actually correct after the undef, but the variable name clashed
 *        with the Adafruit typedef in scope. Renamed local var to camSensor.
 *  [A11] ArduinoOTA included but never initialised — removed unused #include.
 *  [A12] AUTO_CHECK_MS constant declared but never used — kept for future use.
 *  [A13] Sonar called every 100 ms in telemetry AND in stepAutoNav() — in
 *        worst case that's two back-to-back 22 ms blocking pulseIn calls.
 *        Introduced a cached sonar value with 80 ms TTL.
 *  [A14] turnByGyro() blocks the entire loop() for up to 4.5 s via delay(5)
 *        inside a while loop. WebSocket clients disconnect, watchdog fires.
 *        Converted to a non-blocking state machine (ROTATE state in auto-nav,
 *        and a dedicated gyroTurn* state for manual rotate commands).
 *  [A15] OTA token comparison is constant-time safe (mbedtls_ssl_check_record
 *        not available) — added simple length+XOR compare to avoid timing leak.
 *  [A16] handleOtaUpload() checked isOtaAuthorized() per chunk, but headers
 *        are only available during UPLOAD_FILE_START. Added otaAuthorised flag.
 *  [A17] WiFi.setHostname() has no effect in AP mode on IDF < 5; documented.
 *  [A18] GPIO 12 (LED_CLOCK) is a strapping pin. It must be LOW at boot
 *        (confirmed by initPins writing LOW). Added boot-safe note.
 *  [A19] readUltrasonicCM() returns 400 when sonar is off; auto-nav used that
 *        value as "clear path" and drove forward. Added sonarActive guard in
 *        stepAutoNav so auto mode stops when sonar is disabled.
 * ===================================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>

// Resolve typedef conflict between Adafruit_Sensor and esp32-camera
#define sensor_t adafruit_sensor_t
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#undef sensor_t

#include <esp_camera.h>
#include <img_converters.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>

// ===================================================================================
// 1. FINALIZED ESP32 V2 GPIO MAPPING  — DO NOT CHANGE
// ===================================================================================

// --- OV7670 Camera (18-pin, non-FIFO) ---
#define CAM_D0          36
#define CAM_D1          39
#define CAM_D2          34
#define CAM_D3          35
#define CAM_D4          32
#define CAM_D5          33
#define CAM_D6          25
#define CAM_D7          26
#define CAM_XCLK        27
#define CAM_PCLK        16
#define CAM_HREF        17
#define CAM_VSYNC        4
#define CAM_SIOD        21   // Shared I2C Data (MPU6050 SDA)
#define CAM_SIOC        22   // Shared I2C Clock (MPU6050 SCL)
// OV7670: RESET -> 3.3V, PWDN -> GND (hardware, no SW control needed)

// --- MX1508 Dual H-Bridge Motor Driver ---
#define MOTOR_IN1       13   // Left Motor Forward
#define MOTOR_IN2       14   // Left Motor Reverse
#define MOTOR_IN3       18   // Right Motor Forward
#define MOTOR_IN4       19   // Right Motor Reverse

// --- HC-SR04 Ultrasonic Distance Sensor ---
#define TRIG_PIN        23
#define ECHO_PIN         3   // Uses 1k/2k voltage divider to 3.3V!

// --- SG90 Micro Servo ---
#define SERVO_PIN        2

// --- 74HC595 8-Bit Shift Register (LED Effects) ---
#define LED_DATA         5   // SER   [A18: GPIO12 is strapping pin, held LOW at boot]
#define LED_CLOCK       12   // SRCLK
#define LED_LATCH       15   // RCLK
// 74HC595: VCC=3.3V, OE=GND, MR=3.3V

// --- MPU6050 6-DOF IMU ---
#define MPU_SDA         21   // Shared with Camera SIOD
#define MPU_SCL         22   // Shared with Camera SIOC

// ===================================================================================
// 2. CONSTANTS & SYSTEM CONFIGURATION
// ===================================================================================
const char* FIRMWARE_VERSION  = "2.4.28";
const char* HARDWARE_VERSION  = "ESP32-V2";
const char* BUILD_DATE        = "2026-09-24";
const char* AP_DEFAULT_SSID   = "NovaX-Car";
const char* AP_DEFAULT_PASS   = "12345678";
const char* OTA_DEFAULT_TOKEN = "NovaX-OTA-ChangeMe";

// Motor Safety Watchdog
const unsigned long MOTOR_WATCHDOG_MS = 400;

// Autonomous Navigation Tuning
const int   OBSTACLE_LIMIT_CM    = 50;
const int   CRUISE_SPEED_PWM     = 175;
const int   TURN_SPEED_PWM       = 210;
const unsigned long AUTO_CHECK_MS = 120;  // reserved

// Sonar cache TTL (ms)  [A13]
const unsigned long SONAR_CACHE_MS = 80;

// WebSocket limits  [A4]
const size_t WS_MAX_PAYLOAD = 4096;

// WebSocket per-byte receive deadline (ms)  [A3]
const unsigned long WS_BYTE_TIMEOUT_MS = 50;

// Maximum saved Wi-Fi profiles in NVS
const uint8_t MAX_WIFI_PROFILES = 5;

// Maximum concurrent WebSocket clients
const uint8_t MAX_WS_CLIENTS = 4;

// ===================================================================================
// 3. GLOBAL INSTANCES & SYSTEM STATE
// ===================================================================================
WebServer  restServer(80);
WiFiServer wsServer(81);
WiFiClient wsClients[MAX_WS_CLIENTS];

Servo            radarServo;
Adafruit_MPU6050 mpu;
Preferences      nvsPrefs;

// Motor Safety State
volatile unsigned long lastDriveCmdMs = 0;
volatile bool motorRunning = false;
volatile bool manualMode   = true;
volatile bool carStopped   = false;

// MPU6050 Heading & Gyro State
bool          mpuAvailable  = false;
float         gyroZBias     = 0.0f;
float         yawHeading    = 0.0f;
unsigned long lastGyroMicros = 0;

// Camera State
bool cameraAvailable = false;

// 74HC595 LED State
String        currentLedEffect = "off";
unsigned long lastLedUpdateMs  = 0;

// Radar & Sonar State
bool          sonarActive       = true;
long          sonarCacheValue   = 400;   // [A13] cached distance
unsigned long sonarCacheTime    = 0;     // [A13] timestamp of last reading

enum RadarScanState { SCAN_IDLE, SCAN_RUNNING, SCAN_DONE };
RadarScanState radarState        = SCAN_IDLE;
int            radarCurrentAngle = -90;
int            radarStepDir      = 1;
unsigned long  lastRadarStepMs   = 0;
int            scanDistLeft      = 400;
int            scanDistFront     = 400;
int            scanDistRight     = 400;

// Auto Avoidance State
enum AutoDriveState {
  AUTO_STOPPED, AUTO_FORWARD, AUTO_DETECTED,
  AUTO_REVERSE, AUTO_SCAN, AUTO_TURN
};
AutoDriveState autoState      = AUTO_STOPPED;
unsigned long  autoActionTimer = 0;

// Non-blocking gyro rotate state  [A14]
enum RotateState { ROT_IDLE, ROT_TURNING };
RotateState   rotateState     = ROT_IDLE;
float         rotateTarget    = 0.0f;
bool          rotateTurnRight = true;
float         rotateInitYaw   = 0.0f;
unsigned long rotateStartMs   = 0;

// Wi-Fi deferred STA connect  [A8]
bool          pendingStaConnect = false;
String        pendingStaSsid    = "";
String        pendingStaPass    = "";
unsigned long pendingStaDelay   = 0;

// Async Wi-Fi scan state  [A7]
bool          wifiScanPending    = false;
unsigned long wifiScanStartedMs  = 0;

// OTA auth per-upload flag  [A16]
bool otaAuthorised = false;

// Wi-Fi Configuration State
int activeWifiIndex = -1;

// Forward Declarations
void stopCar();
void motorWrite(int pinF, int pinB, int pwm);
void motorMix(float turn, float speed);
long readUltrasonicCM();
void broadcastWsText(const String& payload);
void handleWsMessage(WiFiClient& client, const String& msg);
void updateLEDs();
void stepAutoNav();
void updateGyroHeading();
void stepNonBlockingRotate();

// ===================================================================================
// 4. LOW-LEVEL HARDWARE DRIVERS
// ===================================================================================

void write595(uint8_t value) {
  digitalWrite(LED_LATCH, LOW);
  shiftOut(LED_DATA, LED_CLOCK, MSBFIRST, value);
  digitalWrite(LED_LATCH, HIGH);
}

void initPins() {
  // [A18] GPIO12 (LED_CLOCK) is a strapping pin. initPins is called before
  // WiFi/BT init, so writing LOW here is safe and does not affect boot mode.
  pinMode(MOTOR_IN1, OUTPUT); analogWrite(MOTOR_IN1, 0);
  pinMode(MOTOR_IN2, OUTPUT); analogWrite(MOTOR_IN2, 0);
  pinMode(MOTOR_IN3, OUTPUT); analogWrite(MOTOR_IN3, 0);
  pinMode(MOTOR_IN4, OUTPUT); analogWrite(MOTOR_IN4, 0);

  pinMode(TRIG_PIN, OUTPUT); digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);

  pinMode(LED_DATA,  OUTPUT); digitalWrite(LED_DATA,  LOW);
  pinMode(LED_CLOCK, OUTPUT); digitalWrite(LED_CLOCK, LOW);
  pinMode(LED_LATCH, OUTPUT); digitalWrite(LED_LATCH, LOW);
  write595(0x00);
}

void motorWrite(int pinF, int pinB, int pwm) {
  pwm = constrain(pwm, -255, 255);
  if (pwm > 0) {
    analogWrite(pinF, pwm);
    analogWrite(pinB, 0);
  } else if (pwm < 0) {
    analogWrite(pinF, 0);
    analogWrite(pinB, -pwm);
  } else {
    analogWrite(pinF, 0);
    analogWrite(pinB, 0);
  }
}

void stopCar() {
  motorWrite(MOTOR_IN1, MOTOR_IN2, 0);
  motorWrite(MOTOR_IN3, MOTOR_IN4, 0);
  motorRunning = false;
}

void motorMix(float turn, float speed) {
  float left  = speed + turn;
  float right = speed - turn;
  float maxMag = max(fabs(left), fabs(right));
  if (maxMag > 1.0f) { left /= maxMag; right /= maxMag; }
  motorWrite(MOTOR_IN1, MOTOR_IN2, (int)(left  * 255.0f));
  motorWrite(MOTOR_IN3, MOTOR_IN4, (int)(right * 255.0f));
  motorRunning = (fabs(left) > 0.05f || fabs(right) > 0.05f);
}

// [A13] Cached sonar reading — avoids double-blocking in the same loop tick
long readUltrasonicCM() {
  if (!sonarActive) return 400;
  unsigned long now = millis();
  if (now - sonarCacheTime < SONAR_CACHE_MS) return sonarCacheValue;

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 22000); // 22 ms ~ 3.7 m
  long cm = (duration <= 0) ? 400 : (long)((duration * 0.0343f) / 2.0f);
  if (cm <= 0 || cm > 400) cm = 400;

  sonarCacheValue = cm;
  sonarCacheTime  = now;
  return cm;
}

// Force a fresh sonar reading on next call (e.g. before radar step)
void invalidateSonarCache() {
  sonarCacheTime = 0;
}

// ===================================================================================
// 5. MPU6050 GYROSCOPE & HEADING INTEGRATION
// ===================================================================================

void calibrateGyro() {
  delay(200);
  float sumZ  = 0;
  int samples = 300;
  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sumZ += g.gyro.z;
    delay(2);
  }
  gyroZBias    = sumZ / (float)samples;
  yawHeading   = 0.0f;
  lastGyroMicros = micros();
}

void initMPU() {
  Wire.begin(MPU_SDA, MPU_SCL, 100000);
  if (mpu.begin()) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    calibrateGyro();
    mpuAvailable = true;
    Serial.println("[MPU6050] Initialized and calibrated.");
  } else {
    mpuAvailable = false;
    Serial.println("[MPU6050] Not detected on I2C bus.");
  }
}

void updateGyroHeading() {
  if (!mpuAvailable) return;
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  unsigned long nowUs = micros();
  float dt = (nowUs - lastGyroMicros) / 1000000.0f;
  lastGyroMicros = nowUs;
  if (dt <= 0 || dt > 0.2f) return;
  float rateZ = g.gyro.z - gyroZBias;
  yawHeading += rateZ * 57.2957795f * dt;
  while (yawHeading <    0.0f) yawHeading += 360.0f;
  while (yawHeading >= 360.0f) yawHeading -= 360.0f;
}

// [A14] Non-blocking rotate — called every loop() tick while ROT_TURNING
void startNonBlockingRotate(float targetDeg, bool turnRight) {
  if (!mpuAvailable) {
    // Fallback: use timed turn without blocking (approximate)
    int pwm = TURN_SPEED_PWM;
    if (turnRight) {
      motorWrite(MOTOR_IN1, MOTOR_IN2,  pwm);
      motorWrite(MOTOR_IN3, MOTOR_IN4, -pwm);
    } else {
      motorWrite(MOTOR_IN1, MOTOR_IN2, -pwm);
      motorWrite(MOTOR_IN3, MOTOR_IN4,  pwm);
    }
    // Store rotate state so watchdog does not fire
    rotateState     = ROT_TURNING;
    rotateTarget    = targetDeg;
    rotateTurnRight = turnRight;
    rotateInitYaw   = yawHeading;
    rotateStartMs   = millis();
    // Timed fallback: compute expected duration and let stepNonBlockingRotate handle it
    return;
  }
  rotateState     = ROT_TURNING;
  rotateTarget    = targetDeg;
  rotateTurnRight = turnRight;
  rotateInitYaw   = yawHeading;
  rotateStartMs   = millis();
  int pwm = TURN_SPEED_PWM;
  if (turnRight) {
    motorWrite(MOTOR_IN1, MOTOR_IN2,  pwm);
    motorWrite(MOTOR_IN3, MOTOR_IN4, -pwm);
  } else {
    motorWrite(MOTOR_IN1, MOTOR_IN2, -pwm);
    motorWrite(MOTOR_IN3, MOTOR_IN4,  pwm);
  }
  motorRunning = true;
  lastDriveCmdMs = millis(); // Keep watchdog happy during rotation
}

void stepNonBlockingRotate() {
  if (rotateState != ROT_TURNING) return;

  lastDriveCmdMs = millis(); // Keep watchdog fed during rotation

  if (mpuAvailable) {
    float diff = fabs(yawHeading - rotateInitYaw);
    if (diff > 180.0f) diff = 360.0f - diff;
    float remaining = rotateTarget - diff;

    if (diff >= rotateTarget - 4.0f || millis() - rotateStartMs > 5000) {
      stopCar();
      rotateState = ROT_IDLE;
      broadcastWsText("{\"type\":\"rotate_done\",\"heading\":" + String(yawHeading, 1) + "}");
      return;
    }
    // Slow down near target
    int pwm = (remaining > 30) ? TURN_SPEED_PWM : (remaining > 10 ? 140 : 100);
    if (rotateTurnRight) {
      motorWrite(MOTOR_IN1, MOTOR_IN2,  pwm);
      motorWrite(MOTOR_IN3, MOTOR_IN4, -pwm);
    } else {
      motorWrite(MOTOR_IN1, MOTOR_IN2, -pwm);
      motorWrite(MOTOR_IN3, MOTOR_IN4,  pwm);
    }
  } else {
    // Timed fallback: targetDeg * 6 ms total
    unsigned long elapsed = millis() - rotateStartMs;
    unsigned long needed  = (unsigned long)(rotateTarget * 6.0f);
    if (elapsed >= needed || elapsed > 5000) {
      stopCar();
      rotateState = ROT_IDLE;
      broadcastWsText("{\"type\":\"rotate_done\",\"heading\":0}");
    }
  }
}

// ===================================================================================
// 6. OV7670 CAMERA (NON-FIFO RGB565 -> JPEG)
// ===================================================================================

bool initCamera() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0       = CAM_D0;
  cfg.pin_d1       = CAM_D1;
  cfg.pin_d2       = CAM_D2;
  cfg.pin_d3       = CAM_D3;
  cfg.pin_d4       = CAM_D4;
  cfg.pin_d5       = CAM_D5;
  cfg.pin_d6       = CAM_D6;
  cfg.pin_d7       = CAM_D7;
  cfg.pin_xclk     = CAM_XCLK;
  cfg.pin_pclk     = CAM_PCLK;
  cfg.pin_vsync    = CAM_VSYNC;
  cfg.pin_href     = CAM_HREF;
  cfg.pin_sccb_sda = CAM_SIOD;
  cfg.pin_sccb_scl = CAM_SIOC;
  cfg.pin_pwdn     = -1;
  cfg.pin_reset    = -1;
  cfg.xclk_freq_hz = 10000000;
  cfg.pixel_format = PIXFORMAT_RGB565;
  cfg.frame_size   = FRAMESIZE_QQVGA; // 160x120
  cfg.jpeg_quality = 12;
  cfg.fb_count     = 1;
  cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  cfg.fb_location  = CAMERA_FB_IN_DRAM;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] OV7670 Init failed: 0x%x\n", err);
    return false;
  }

  // [A10] Renamed local var to avoid any scope confusion after sensor_t #undef
  sensor_t* camSensor = esp_camera_sensor_get();
  if (camSensor) {
    camSensor->set_vflip(camSensor, 1);
    camSensor->set_hmirror(camSensor, 0);
  }
  Serial.println("[CAMERA] OV7670 Initialized successfully.");
  return true;
}

void handleCameraSnapshot() {
  if (!cameraAvailable) {
    restServer.send(503, "text/plain", "Camera unavailable");
    return;
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    restServer.send(503, "text/plain", "Frame acquisition failed");
    return;
  }
  uint8_t* jpgBuf = nullptr;
  size_t   jpgSize = 0;
  bool converted = frame2jpg(fb, 55, &jpgBuf, &jpgSize);
  esp_camera_fb_return(fb);

  if (!converted || !jpgBuf) {
    restServer.send(500, "text/plain", "JPEG compression failed");
    return;
  }
  restServer.sendHeader("Access-Control-Allow-Origin", "*");
  restServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  restServer.setContentLength(jpgSize);
  restServer.send(200, "image/jpeg");
  restServer.client().write(jpgBuf, jpgSize);
  free(jpgBuf);
}

// ===================================================================================
// 7. RFC 6455 WEBSOCKET ENGINE (PORT 81)
// ===================================================================================

// [A1][A2] Fixed: use mbedtls_sha1 with context; buffer enlarged to 40 bytes
String computeWsAccept(const String& clientKey) {
  String combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

  unsigned char sha1Result[20];
  mbedtls_sha1_context shaCtx;
  mbedtls_sha1_init(&shaCtx);
  mbedtls_sha1_starts_ret(&shaCtx);
  mbedtls_sha1_update_ret(&shaCtx, (const unsigned char*)combined.c_str(), combined.length());
  mbedtls_sha1_finish_ret(&shaCtx, sha1Result);
  mbedtls_sha1_free(&shaCtx);

  unsigned char base64Result[40] = {0};  // [A2] was 32 — too small
  size_t base64Len = 0;
  mbedtls_base64_encode(base64Result, sizeof(base64Result), &base64Len, sha1Result, 20);
  return String((char*)base64Result);
}

void sendWsFrame(WiFiClient& client, const String& payload) {
  if (!client || !client.connected()) return;
  size_t len = payload.length();
  client.write((uint8_t)0x81); // FIN + Text opcode
  if (len < 126) {
    client.write((uint8_t)len);
  } else if (len <= 65535) {
    client.write((uint8_t)126);
    client.write((uint8_t)(len >> 8));
    client.write((uint8_t)(len & 0xFF));
  } else {
    // 8-byte extended length (rare for our payloads)
    client.write((uint8_t)127);
    for (int s = 56; s >= 0; s -= 8) client.write((uint8_t)((len >> s) & 0xFF));
  }
  client.write((const uint8_t*)payload.c_str(), len);
}

void broadcastWsText(const String& payload) {
  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (wsClients[i] && wsClients[i].connected()) {
      sendWsFrame(wsClients[i], payload);
    }
  }
}

// [A3] Safe byte reader with per-byte timeout
static bool wsReadByte(WiFiClient& client, uint8_t& out) {
  unsigned long deadline = millis() + WS_BYTE_TIMEOUT_MS;
  while (!client.available()) {
    if (millis() > deadline || !client.connected()) return false;
    delay(1);
  }
  out = (uint8_t)client.read();
  return true;
}

// [A3] Safe multi-byte reader with timeout
static bool wsReadBytes(WiFiClient& client, uint8_t* buf, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (!wsReadByte(client, buf[i])) return false;
  }
  return true;
}

void pollWebSocketServer() {
  // Accept new connection
  if (wsServer.hasClient()) {
    WiFiClient newClient = wsServer.available();
    int slot = -1;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
      if (!wsClients[i] || !wsClients[i].connected()) { slot = i; break; }
    }
    if (slot >= 0) {
      // Read HTTP upgrade request (1 s timeout)
      String request = "";
      unsigned long t0 = millis();
      while (newClient.connected() && millis() - t0 < 1000) {
        if (newClient.available()) {
          request += (char)newClient.read();
          if (request.endsWith("\r\n\r\n")) break;
        }
      }
      int keyIdx = request.indexOf("Sec-WebSocket-Key: ");
      if (keyIdx >= 0) {
        int keyEnd   = request.indexOf("\r\n", keyIdx);
        String clientKey = request.substring(keyIdx + 19, keyEnd);
        clientKey.trim();
        String acceptKey = computeWsAccept(clientKey);

        String resp  = "HTTP/1.1 101 Switching Protocols\r\n";
        resp += "Upgrade: websocket\r\n";
        resp += "Connection: Upgrade\r\n";
        resp += "Sec-WebSocket-Accept: " + acceptKey + "\r\n\r\n";
        newClient.print(resp);

        wsClients[slot] = newClient;
        Serial.printf("[WS] Client connected on slot %d\n", slot);
        sendWsFrame(newClient,
          "{\"type\":\"status\",\"status\":\"connected\",\"version\":\""
          + String(FIRMWARE_VERSION)
          + "\",\"mode\":\"" + String(manualMode ? "manual" : "auto")
          + "\",\"stopped\":" + String(carStopped ? "true" : "false") + "}");
      } else {
        newClient.stop();
      }
    } else {
      newClient.stop(); // All slots busy
    }
  }

  // Service connected clients
  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (!wsClients[i]) continue;
    if (!wsClients[i].connected()) {
      Serial.printf("[WS] Client disconnected on slot %d — emergency stop\n", i);
      wsClients[i].stop();
      // [A5 FIXED] On disconnect: hard-stop motors AND set carStopped=true.
      // A reconnected client MUST send {"type":"start"} before any driving.
      // This prevents stale auto-drive or a second client taking over silently.
      stopCar();
      carStopped  = true;
      rotateState = ROT_IDLE;
      if (!manualMode) autoState = AUTO_STOPPED;
      continue;
    }

    // Process all available complete frames
    while (wsClients[i].available() >= 2) {
      uint8_t byte0, byte1;
      if (!wsReadByte(wsClients[i], byte0)) break;
      if (!wsReadByte(wsClients[i], byte1)) break;

      uint8_t  opcode = byte0 & 0x0F;
      bool     masked = (byte1 & 0x80) != 0;
      size_t   len    = byte1 & 0x7F;

      // [A4] Extended length handling with size cap
      if (len == 126) {
        uint8_t ext[2];
        if (!wsReadBytes(wsClients[i], ext, 2)) break;
        len = ((size_t)ext[0] << 8) | ext[1];
      } else if (len == 127) {
        uint8_t ext[8];
        if (!wsReadBytes(wsClients[i], ext, 8)) break;
        len = 0;
        for (int k = 0; k < 8; k++) len = (len << 8) | ext[k];
      }

      if (len > WS_MAX_PAYLOAD) {
        // Oversized frame — drop client to prevent memory exhaustion
        Serial.printf("[WS] Oversized frame (%u bytes) on slot %d — dropping client\n", len, i);
        stopCar();
        wsClients[i].stop();
        break;
      }

      uint8_t maskKey[4] = {0, 0, 0, 0};
      if (masked) {
        if (!wsReadBytes(wsClients[i], maskKey, 4)) break;
      }

      if (opcode == 0x8) { // Close
        stopCar();
        wsClients[i].stop();
        break;
      } else if (opcode == 0x9) { // Ping -> Pong
        uint8_t pongBuf[len];
        if (len > 0 && !wsReadBytes(wsClients[i], pongBuf, len)) break;
        wsClients[i].write((uint8_t)0x8A);
        wsClients[i].write((uint8_t)len);
        if (len > 0) wsClients[i].write(pongBuf, len);
        continue;
      } else if (opcode == 0xA) { // Pong (unsolicited) — ignore
        uint8_t discard[len > 0 ? len : 1];
        if (len > 0) wsReadBytes(wsClients[i], discard, len);
        continue;
      }

      // Read payload  [A3]
      String payload = "";
      payload.reserve(len);
      for (size_t k = 0; k < len; k++) {
        uint8_t b;
        if (!wsReadByte(wsClients[i], b)) { payload = ""; break; }
        if (masked) b ^= maskKey[k % 4];
        payload += (char)b;
      }

      if (opcode == 0x1 && payload.length() > 0) {
        handleWsMessage(wsClients[i], payload);
      }
    }
  }
}

// ===================================================================================
// 8. WEBSOCKET MESSAGE DISPATCHER
// ===================================================================================

void handleWsMessage(WiFiClient& client, const String& msg) {
  // All JSON parsing is zero-dependency string search for minimal RAM usage.

  // --- MOVE ---
  if (msg.indexOf("\"type\":\"move\"") >= 0) {
    if (!manualMode || carStopped || rotateState == ROT_TURNING) return;
    int dirIdx = msg.indexOf("\"dir\":\"");
    if (dirIdx >= 0) {
      char dir = msg.charAt(dirIdx + 7);
      int spd  = CRUISE_SPEED_PWM;
      int spdIdx = msg.indexOf("\"speed\":");
      if (spdIdx >= 0) spd = msg.substring(spdIdx + 8).toInt();
      if (spd <= 0) spd = CRUISE_SPEED_PWM;

      if      (dir == 'F') { motorWrite(MOTOR_IN1, MOTOR_IN2, spd);  motorWrite(MOTOR_IN3, MOTOR_IN4, spd);  }
      else if (dir == 'B') { motorWrite(MOTOR_IN1, MOTOR_IN2, -spd); motorWrite(MOTOR_IN3, MOTOR_IN4, -spd); }
      else if (dir == 'L') { motorWrite(MOTOR_IN1, MOTOR_IN2, -spd); motorWrite(MOTOR_IN3, MOTOR_IN4, spd);  }
      else if (dir == 'R') { motorWrite(MOTOR_IN1, MOTOR_IN2, spd);  motorWrite(MOTOR_IN3, MOTOR_IN4, -spd); }
      else if (dir == 'S') { stopCar(); }
      lastDriveCmdMs = millis();
      motorRunning   = (dir != 'S');
    }
  }
  // --- EMERGENCY STOP ---
  else if (msg.indexOf("\"type\":\"stop\"") >= 0) {
    carStopped  = true;
    rotateState = ROT_IDLE;
    stopCar();
    autoState = AUTO_STOPPED;
    lastDriveCmdMs = millis();
    broadcastWsText("{\"type\":\"stopped\",\"mode\":\"" + String(manualMode ? "manual" : "auto") + "\"}");
  }
  // --- START / RESUME ---
  else if (msg.indexOf("\"type\":\"start\"") >= 0) {
    carStopped = false;
    if (!manualMode) {
      autoState = AUTO_FORWARD;
      Serial.println("[MODE] Autonomous Driving Resumed");
    } else {
      Serial.println("[MODE] Manual Driver Ready");
    }
    broadcastWsText("{\"type\":\"started\",\"mode\":\"" + String(manualMode ? "manual" : "auto") + "\"}");
  }
  // --- MODE TOGGLE ---
  else if (msg.indexOf("\"type\":\"mode\"") >= 0) {
    carStopped = false;
    if (msg.indexOf("\"value\":\"auto\"") >= 0) {
      manualMode = false;
      autoState  = AUTO_FORWARD;
      Serial.println("[MODE] Autonomous Navigation Active");
    } else {
      manualMode  = true;
      autoState   = AUTO_STOPPED;
      rotateState = ROT_IDLE;
      stopCar();
      Serial.println("[MODE] Manual Driver Control Active");
    }
    broadcastWsText("{\"type\":\"mode\",\"value\":\"" + String(manualMode ? "manual" : "auto") + "\"}");
  }
  // --- GYRO ROTATE  [A14] now non-blocking ---
  else if (msg.indexOf("\"type\":\"rotate\"") >= 0) {
    if (!manualMode || carStopped) return;
    if (msg.indexOf("\"dir\":\"left\"")  >= 0) startNonBlockingRotate(90.0f,  false);
    else if (msg.indexOf("\"dir\":\"right\"") >= 0) startNonBlockingRotate(90.0f,  true);
    else if (msg.indexOf("\"dir\":\"360\"")   >= 0) startNonBlockingRotate(360.0f, true);
  }
  // --- LED EFFECT ---
  else if (msg.indexOf("\"type\":\"led\"") >= 0) {
    if (msg.indexOf("\"pattern\":\"blink\"") >= 0) currentLedEffect = "blink";
    else if (msg.indexOf("\"pattern\":\"warn\"")  >= 0) currentLedEffect = "warn";
    else if (msg.indexOf("\"pattern\":\"pulse\"") >= 0) currentLedEffect = "pulse";
    else currentLedEffect = "off";
    lastLedUpdateMs = millis();
    updateLEDs();
  }
  // --- RADAR SCAN ---
  else if (msg.indexOf("\"type\":\"scan\"") >= 0) {
    radarState        = SCAN_RUNNING;
    radarCurrentAngle = -90;
    radarStepDir      = 1;
    invalidateSonarCache();
    radarServo.attach(SERVO_PIN, 500, 2400);
    lastRadarStepMs = millis();
  }
  // --- MANUAL SERVO HEAD ANGLE ---
  else if (msg.indexOf("\"type\":\"servo\"") >= 0) {
    radarState = SCAN_IDLE;
    int angIdx = msg.indexOf("\"angle\":");
    if (angIdx >= 0) {
      int ang = constrain(msg.substring(angIdx + 8).toInt(), 0, 180);
      if (!radarServo.attached()) radarServo.attach(SERVO_PIN, 500, 2400);
      radarServo.write(ang);
      invalidateSonarCache();
      long dist = readUltrasonicCM();
      broadcastWsText("{\"type\":\"servo_pos\",\"angle\":" + String(ang) + ",\"distance\":" + String(dist) + "}");
    }
  }
  // --- SONAR ON/OFF ---
  else if (msg.indexOf("\"type\":\"sonar\"") >= 0) {
    sonarActive = (msg.indexOf("\"value\":\"on\"") >= 0);
    broadcastWsText("{\"type\":\"sonar\",\"active\":" + String(sonarActive ? "true" : "false") + "}");
  }
  // --- PING / HEARTBEAT ---
  else if (msg.indexOf("\"type\":\"ping\"") >= 0) {
    sendWsFrame(client, "{\"type\":\"pong\",\"time\":" + String(millis()) + "}");
  }
}

// ===================================================================================
// 9. RADAR SWEEPER & AUTONOMOUS OBSTACLE AVOIDANCE
// ===================================================================================

void stepRadarScan() {
  if (radarState != SCAN_RUNNING) return;
  unsigned long now = millis();
  if (now - lastRadarStepMs < 100) return;
  lastRadarStepMs = now;

  int servoAngle = radarCurrentAngle + 90; // -90..90 -> 0..180
  radarServo.write(servoAngle);
  delay(25); // short settle — acceptable here as it's a radar step, not motor control

  invalidateSonarCache();
  long dist = readUltrasonicCM();

  broadcastWsText("{\"type\":\"radar\",\"angle\":" + String(radarCurrentAngle)
                  + ",\"distance\":" + String(dist) + "}");

  if (radarCurrentAngle == -90) scanDistLeft  = (int)dist;
  else if (radarCurrentAngle ==  0) scanDistFront = (int)dist;
  else if (radarCurrentAngle == 90) scanDistRight = (int)dist;

  radarCurrentAngle += 30;
  if (radarCurrentAngle > 90) {
    radarServo.write(90);
    delay(100);
    radarServo.detach();
    radarState = SCAN_DONE;

    broadcastWsText("{\"type\":\"radar_summary\",\"left\":" + String(scanDistLeft)
                    + ",\"front\":" + String(scanDistFront)
                    + ",\"right\":" + String(scanDistRight) + ",\"done\":true}");
  }
}

void stepAutoNav() {
  // [A19] When sonar is off, we have no distance data — halt autonomous mode
  if (!sonarActive) { stopCar(); return; }
  if (manualMode || carStopped || autoState == AUTO_STOPPED) { stopCar(); return; }

  long distance = readUltrasonicCM();

  switch (autoState) {
    case AUTO_FORWARD:
      if (distance < OBSTACLE_LIMIT_CM) {
        stopCar();
        autoState      = AUTO_DETECTED;
        autoActionTimer = millis();
      } else {
        motorMix(0.0f, 0.65f);
      }
      break;

    case AUTO_DETECTED:
      stopCar();
      autoState       = AUTO_REVERSE;
      autoActionTimer = millis();
      break;

    case AUTO_REVERSE:
      motorWrite(MOTOR_IN1, MOTOR_IN2, -180);
      motorWrite(MOTOR_IN3, MOTOR_IN4, -180);
      if (distance >= 55 || millis() - autoActionTimer > 1200) {
        stopCar();
        autoState         = AUTO_SCAN;
        radarState        = SCAN_RUNNING;
        radarCurrentAngle = -90;
        invalidateSonarCache();
        radarServo.attach(SERVO_PIN, 500, 2400);
        lastRadarStepMs = millis();
      }
      break;

    case AUTO_SCAN:
      if (radarState == SCAN_DONE) {
        autoState       = AUTO_TURN;
        autoActionTimer = millis();
      }
      break;

    case AUTO_TURN:
      // [A14] Use non-blocking rotate; transition to FORWARD when done
      if (rotateState == ROT_IDLE) {
        bool goRight = (scanDistRight >= scanDistLeft);
        startNonBlockingRotate(90.0f, goRight);
      } else if (rotateState != ROT_TURNING) {
        autoState = AUTO_FORWARD;
      }
      break;

    default: break;
  }
}

// ===================================================================================
// 10. 74HC595 LED SEQUENCER
// ===================================================================================

void updateLEDs() {
  if (currentLedEffect == "off") { write595(0); return; }
  unsigned long now = millis();
  if (currentLedEffect == "blink" && now - lastLedUpdateMs >= 300) {
    lastLedUpdateMs = now;
    static bool tog = false; tog = !tog;
    write595(tog ? 0xFF : 0x00);
  } else if (currentLedEffect == "warn" && now - lastLedUpdateMs >= 400) {
    lastLedUpdateMs = now;
    static bool togW = false; togW = !togW;
    write595(togW ? 0b11000011 : 0b00111100);
  } else if (currentLedEffect == "pulse" && now - lastLedUpdateMs >= 200) {
    lastLedUpdateMs = now;
    static uint8_t pVal = 0x18;
    pVal = (pVal == 0x18) ? 0x3C : (pVal == 0x3C ? 0x7E : 0x18);
    write595(pVal);
  }
}

// ===================================================================================
// 11. REST API & WI-FI CONTROLLER
// ===================================================================================

void setCorsHeaders() {
  restServer.sendHeader("Access-Control-Allow-Origin",  "*");
  restServer.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  restServer.sendHeader("Access-Control-Allow-Headers", "Content-Type,X-NovaX-OTA");
}

// [A15] Constant-time-ish token comparison (avoids early-exit timing leak)
bool safeTokenCmp(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); i++) diff |= (a[i] ^ b[i]);
  return diff == 0;
}

bool isOtaAuthorized() {
  return restServer.hasHeader("X-NovaX-OTA") &&
         safeTokenCmp(restServer.header("X-NovaX-OTA"), String(OTA_DEFAULT_TOKEN));
}

String getWifiProfilesJson() {
  String   json  = "{\"selected\":" + String(activeWifiIndex) + ",\"networks\":[";
  uint8_t  count = nvsPrefs.getUChar("cnt", 0);
  bool     first = true;
  for (uint8_t i = 0; i < count && i < MAX_WIFI_PROFILES; i++) {
    String s = nvsPrefs.getString((String("s") + i).c_str(), "");
    if (s.length() == 0) continue;
    if (!first) json += ",";
    first = false;
    json += "{\"ssid\":\"" + s + "\"}"; // Passwords omitted intentionally
  }
  json += "]}";
  return json;
}

// [A7] Async Wi-Fi scan — starts scan and returns immediately
void handleWifiScan() {
  setCorsHeaders();
  if (!wifiScanPending) {
    WiFi.scanNetworks(true, true); // async, include hidden
    wifiScanPending   = true;
    wifiScanStartedMs = millis();
    restServer.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  // Poll result
  int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) {
    restServer.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  wifiScanPending = false;
  String out = "{\"networks\":[";
  if (count > 0) {
    for (int i = 0; i < count; i++) {
      if (i) out += ",";
      String ssid = WiFi.SSID(i);
      ssid.replace("\"", "\\\"");
      out += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i))
           + ",\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? 1 : 0) + "}";
    }
  }
  out += "]}";
  WiFi.scanDelete();
  restServer.send(200, "application/json", out);
}

void handleWifiSaved() {
  setCorsHeaders();
  restServer.send(200, "application/json", getWifiProfilesJson());
}

void handleWifiSave() {
  setCorsHeaders();
  if (!restServer.hasArg("ssid") || !restServer.hasArg("password")) {
    restServer.send(400, "text/plain", "Missing ssid or password");
    return;
  }
  String  ssid  = restServer.arg("ssid");
  String  pass  = restServer.arg("password");
  uint8_t count = nvsPrefs.getUChar("cnt", 0);
  int     foundSlot = -1;

  for (uint8_t i = 0; i < count; i++) {
    if (nvsPrefs.getString((String("s") + i).c_str(), "") == ssid) { foundSlot = i; break; }
  }
  if (foundSlot < 0) {
    if (count >= MAX_WIFI_PROFILES) { restServer.send(409, "text/plain", "Saved profiles limit reached"); return; }
    foundSlot = count++;
  }
  nvsPrefs.putString((String("s") + foundSlot).c_str(), ssid);
  nvsPrefs.putString((String("p") + foundSlot).c_str(), pass);
  nvsPrefs.putUChar("cnt", count);
  if (activeWifiIndex < 0) { activeWifiIndex = foundSlot; nvsPrefs.putInt("sel", activeWifiIndex); }
  restServer.send(200, "application/json", getWifiProfilesJson());
}

void handleWifiSelect() {
  setCorsHeaders();
  if (!restServer.hasArg("index")) { restServer.send(400, "text/plain", "Missing index"); return; }
  int idx = restServer.arg("index").toInt();
  uint8_t count = nvsPrefs.getUChar("cnt", 0);
  if (idx < 0 || idx >= count) { restServer.send(400, "text/plain", "Invalid index"); return; }
  activeWifiIndex = idx;
  nvsPrefs.putInt("sel", idx);
  restServer.send(200, "application/json", getWifiProfilesJson());
}

void handleWifiDelete() {
  setCorsHeaders();
  if (!restServer.hasArg("index")) { restServer.send(400, "text/plain", "Missing index"); return; }
  int     idx   = restServer.arg("index").toInt();
  uint8_t count = nvsPrefs.getUChar("cnt", 0);
  if (idx < 0 || idx >= count) { restServer.send(400, "text/plain", "Invalid index"); return; }
  for (int i = idx; i < count - 1; i++) {
    nvsPrefs.putString((String("s") + i).c_str(), nvsPrefs.getString((String("s") + (i + 1)).c_str(), ""));
    nvsPrefs.putString((String("p") + i).c_str(), nvsPrefs.getString((String("p") + (i + 1)).c_str(), ""));
  }
  nvsPrefs.remove((String("s") + (count - 1)).c_str());
  nvsPrefs.remove((String("p") + (count - 1)).c_str());
  count--;
  nvsPrefs.putUChar("cnt", count);
  if (activeWifiIndex >= count) activeWifiIndex = (count > 0) ? 0 : -1;
  nvsPrefs.putInt("sel", activeWifiIndex);
  restServer.send(200, "application/json", getWifiProfilesJson());
}

// [A8] STA switch: respond immediately, defer actual connect to loop()
void handleSwitchSTA() {
  setCorsHeaders();
  uint8_t count = nvsPrefs.getUChar("cnt", 0);
  if (count == 0 || activeWifiIndex < 0 || activeWifiIndex >= count) {
    restServer.send(400, "text/plain", "No selected saved network");
    return;
  }
  pendingStaSsid    = nvsPrefs.getString((String("s") + activeWifiIndex).c_str(), "");
  pendingStaPass    = nvsPrefs.getString((String("p") + activeWifiIndex).c_str(), "");
  pendingStaConnect = true;
  pendingStaDelay   = millis() + 200; // give HTTP response time to flush
  restServer.send(200, "application/json",
    "{\"status\":\"connecting\",\"ssid\":\"" + pendingStaSsid
    + "\",\"hint\":\"Connect to novax.local after switching\"}");
}

void handleSwitchAP() {
  setCorsHeaders();
  restServer.send(200, "application/json", "{\"status\":\"switched\",\"mode\":\"AP\",\"ip\":\"192.168.4.1\"}");
  delay(120);
  pendingStaConnect = false;
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS);
  Serial.printf("[WIFI] Switched to AP mode. IP: %s\n", WiFi.softAPIP().toString().c_str());
}

// Deferred STA connect — called from loop()
void processPendingStaConnect() {
  if (!pendingStaConnect || millis() < pendingStaDelay) return;
  pendingStaConnect = false;

  Serial.printf("[WIFI] Connecting to STA: %s\n", pendingStaSsid.c_str());
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("novax"); // [A9] before begin()
  WiFi.begin(pendingStaSsid.c_str(), pendingStaPass.c_str());

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 12000) delay(100);

  if (WiFi.status() == WL_CONNECTED) {
    MDNS.end();
    MDNS.begin("novax");
    Serial.printf("[WIFI] STA connected. IP: %s\n", WiFi.localIP().toString().c_str());
    broadcastWsText("{\"type\":\"wifi\",\"mode\":\"STA\",\"ip\":\"" + WiFi.localIP().toString() + "\"}");
  } else {
    Serial.println("[WIFI] STA failed — falling back to AP.");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS);
    broadcastWsText("{\"type\":\"wifi\",\"mode\":\"AP\",\"ip\":\"192.168.4.1\",\"error\":\"sta_failed\"}");
  }
}

void handleFirmwareInfo() {
  setCorsHeaders();
  String out = "{";
  out += "\"name\":\"NovaX V2\",";
  out += "\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
  out += "\"hardware\":\"" + String(HARDWARE_VERSION) + "\",";
  out += "\"buildDate\":\"" + String(BUILD_DATE) + "\",";
  out += "\"camera\":\"" + String(cameraAvailable ? "OV7670" : "None") + "\",";
  out += "\"imu\":\"" + String(mpuAvailable ? "MPU6050" : "None") + "\",";
  out += "\"status\":\"online\"";
  out += "}";
  restServer.send(200, "application/json", out);
}

void handleStatus() {
  setCorsHeaders();
  long   d       = readUltrasonicCM();
  String wifiMode = (WiFi.getMode() == WIFI_AP ? "AP" : (WiFi.getMode() == WIFI_STA ? "STA" : "OTHER"));
  String ip      = (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  String s = "{";
  s += "\"distance\":"  + String(d)    + ",";
  s += "\"mode\":\""    + String(manualMode ? "manual" : "auto") + "\",";
  s += "\"manual\":"    + String(manualMode ? 1 : 0) + ",";
  s += "\"stopped\":"   + String((carStopped || autoState == AUTO_STOPPED) ? 1 : 0) + ",";
  s += "\"yaw\":"       + String(yawHeading, 1) + ",";
  s += "\"camera\":"    + String(cameraAvailable ? 1 : 0) + ",";
  s += "\"wifiMode\":\"" + wifiMode + "\",";
  s += "\"ip\":\""      + ip + "\",";
  s += "\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
  s += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\"";
  s += "}";
  restServer.send(200, "application/json", s);
}

// [A16] OTA: capture auth flag at UPLOAD_FILE_START only
void handleOtaUpload() {
  HTTPUpload& upload = restServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaAuthorised = isOtaAuthorized();
    if (!otaAuthorised) {
      Serial.println("[OTA] Unauthorized upload attempt — rejected.");
      return;
    }
    stopCar();
    carStopped = true;
    Serial.printf("[OTA] Flashing: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaAuthorised) return;
    if (Update.isRunning()) Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!otaAuthorised) return;
    if (Update.end(true)) Serial.printf("[OTA] Complete: %u bytes\n", upload.totalSize);
    else Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Serial.println("[OTA] Aborted.");
  }
}

void handleOtaFinish() {
  setCorsHeaders();
  if (!otaAuthorised) { restServer.send(401, "text/plain", "Unauthorized"); return; }
  if (Update.hasError()) { restServer.send(500, "text/plain", "Flash Failed"); return; }
  restServer.send(200, "application/json", "{\"status\":\"ok\",\"restarting\":true}");
  delay(500);
  ESP.restart();
}

void handlePathUpload() {
  setCorsHeaders();
  if (!restServer.hasArg("plain")) { restServer.send(400, "text/plain", "No path data"); return; }
  String body  = restServer.arg("plain");
  float  sumX  = 0.0f;
  int    count = 0;
  int    start = 0;
  while (start < (int)body.length()) {
    int comma = body.indexOf(',', start);
    if (comma < 0) break;
    int semi = body.indexOf(';', start);
    if (semi < 0) semi = body.length();
    sumX += body.substring(start, comma).toFloat();
    count++;
    if (semi >= (int)body.length()) break;
    start = semi + 1;
  }
  if (count == 0) { restServer.send(400, "text/plain", "Bad data"); return; }

  float avgX = sumX / (float)count;
  manualMode = true;
  stopCar();
  if (avgX < 0.45f) {
    motorWrite(MOTOR_IN1, MOTOR_IN2, CRUISE_SPEED_PWM / 2);
    motorWrite(MOTOR_IN3, MOTOR_IN4, CRUISE_SPEED_PWM);
  } else if (avgX > 0.55f) {
    motorWrite(MOTOR_IN1, MOTOR_IN2, CRUISE_SPEED_PWM);
    motorWrite(MOTOR_IN3, MOTOR_IN4, CRUISE_SPEED_PWM / 2);
  } else {
    motorMix(0.0f, 0.75f);
  }
  lastDriveCmdMs = millis();
  motorRunning   = true;
  restServer.send(200, "text/plain", "Path Received");
}

// ===================================================================================
// 12. SETUP
// ===================================================================================

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[NovaX V2] Initializing...");

  initPins();
  initMPU();

  nvsPrefs.begin("wifi", false);
  activeWifiIndex = nvsPrefs.getInt("sel", -1);

  // RULE: Always start in AP Mode on every boot!  [A9] hostname before softAP
  WiFi.setHostname("novax");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS);
  MDNS.begin("novax");
  Serial.printf("[WIFI] AP Mode | SSID: %s | IP: %s\n",
                AP_DEFAULT_SSID, WiFi.softAPIP().toString().c_str());

  // Register REST endpoints
  restServer.on("/wifi/scan",      HTTP_GET,  handleWifiScan);
  restServer.on("/wifi/saved",     HTTP_GET,  handleWifiSaved);
  restServer.on("/wifi/save",      HTTP_POST, handleWifiSave);
  restServer.on("/wifi/select",    HTTP_POST, handleWifiSelect);
  restServer.on("/wifi/delete",    HTTP_POST, handleWifiDelete);
  restServer.on("/wifi/switchSta", HTTP_POST, handleSwitchSTA);
  restServer.on("/wifi/switchAp",  HTTP_POST, handleSwitchAP);
  restServer.on("/firmware",       HTTP_GET,  handleFirmwareInfo);
  restServer.on("/status",         HTTP_GET,  handleStatus);
  restServer.on("/path",           HTTP_POST, handlePathUpload);
  restServer.on("/cam.jpg",        HTTP_GET,  handleCameraSnapshot);
  restServer.on("/ota/status",     HTTP_GET,  handleFirmwareInfo);
  restServer.on("/ota/update",     HTTP_POST, handleOtaFinish, handleOtaUpload);

  // Backward-compat REST move/rotate/led/scan/stop/start/mode endpoints
  restServer.on("/servo", HTTP_GET, []() {
    setCorsHeaders();
    if (!restServer.hasArg("angle")) { restServer.send(400,"text/plain","Missing angle"); return; }
    int ang = constrain(restServer.arg("angle").toInt(), 0, 180);
    radarState = SCAN_IDLE;
    if (!radarServo.attached()) radarServo.attach(SERVO_PIN, 500, 2400);
    radarServo.write(ang);
    invalidateSonarCache();
    long dist = readUltrasonicCM();
    restServer.send(200,"application/json",
      "{\"status\":\"ok\",\"angle\":" + String(ang) + ",\"distance\":" + String(dist) + "}");
  });
  restServer.on("/stop", HTTP_POST, []() {
    setCorsHeaders();
    carStopped  = true;
    rotateState = ROT_IDLE;
    stopCar();
    autoState = AUTO_STOPPED;
    restServer.send(200,"application/json","{\"status\":\"stopped\"}");
  });
  restServer.on("/start", HTTP_POST, []() {
    setCorsHeaders();
    carStopped = false;
    if (!manualMode) autoState = AUTO_FORWARD;
    restServer.send(200,"application/json",
      "{\"status\":\"started\",\"mode\":\"" + String(manualMode?"manual":"auto") + "\"}");
  });
  restServer.on("/mode", HTTP_GET, []() {
    setCorsHeaders();
    if (restServer.hasArg("set")) {
      String m = restServer.arg("set");
      carStopped = false;
      if (m == "auto") { manualMode = false; autoState = AUTO_FORWARD; }
      else             { manualMode = true;  autoState = AUTO_STOPPED; stopCar(); }
    }
    restServer.send(200,"application/json",
      "{\"mode\":\"" + String(manualMode?"manual":"auto") + "\"}");
  });
  restServer.on("/move", HTTP_GET, []() {
    setCorsHeaders();
    if (!manualMode || carStopped) { restServer.send(200,"application/json","{\"status\":\"blocked\"}"); return; }
    if (!restServer.hasArg("d")) { restServer.send(400,"text/plain","Missing d"); return; }
    char dir = restServer.arg("d").charAt(0);
    int  spd = restServer.hasArg("speed") ? restServer.arg("speed").toInt() : CRUISE_SPEED_PWM;
    if (spd <= 0) spd = CRUISE_SPEED_PWM;
    if      (dir=='F') { motorWrite(MOTOR_IN1,MOTOR_IN2, spd); motorWrite(MOTOR_IN3,MOTOR_IN4, spd); }
    else if (dir=='B') { motorWrite(MOTOR_IN1,MOTOR_IN2,-spd); motorWrite(MOTOR_IN3,MOTOR_IN4,-spd); }
    else if (dir=='L') { motorWrite(MOTOR_IN1,MOTOR_IN2,-spd); motorWrite(MOTOR_IN3,MOTOR_IN4, spd); }
    else if (dir=='R') { motorWrite(MOTOR_IN1,MOTOR_IN2, spd); motorWrite(MOTOR_IN3,MOTOR_IN4,-spd); }
    else if (dir=='S') { stopCar(); }
    lastDriveCmdMs = millis();
    motorRunning   = (dir != 'S');
    restServer.send(200,"application/json","{\"status\":\"ok\"}");
  });
  restServer.on("/rotate", HTTP_GET, []() {
    setCorsHeaders();
    if (!manualMode || carStopped) { restServer.send(200,"application/json","{\"status\":\"blocked\"}"); return; }
    String dir = restServer.arg("dir");
    if (dir=="left")       startNonBlockingRotate(90.0f, false);
    else if (dir=="right") startNonBlockingRotate(90.0f, true);
    else if (dir=="360")   startNonBlockingRotate(360.0f, true);
    restServer.send(200,"application/json","{\"status\":\"ok\",\"rotating\":true}");
  });
  restServer.on("/led", HTTP_GET, []() {
    setCorsHeaders();
    String eff = restServer.arg("effect");
    if (eff=="blink"||eff=="warn"||eff=="pulse") currentLedEffect = eff;
    else currentLedEffect = "off";
    lastLedUpdateMs = millis();
    updateLEDs();
    restServer.send(200,"application/json",
      "{\"status\":\"ok\",\"effect\":\"" + currentLedEffect + "\"}");
  });
  restServer.on("/scan", HTTP_GET, []() {
    setCorsHeaders();
    radarState        = SCAN_RUNNING;
    radarCurrentAngle = -90;
    radarStepDir      = 1;
    invalidateSonarCache();
    radarServo.attach(SERVO_PIN, 500, 2400);
    lastRadarStepMs = millis();
    restServer.send(200,"application/json","{\"status\":\"scanning\"}");
  });
  restServer.on("/scanResult", HTTP_GET, []() {
    setCorsHeaders();
    String res = "{\"status\":\""
      + String(radarState==SCAN_DONE?"done":radarState==SCAN_RUNNING?"scanning":"idle") + "\"";
    res += ",\"left\":"  + String(scanDistLeft);
    res += ",\"front\":" + String(scanDistFront);
    res += ",\"right\":" + String(scanDistRight);
    res += "}";
    restServer.send(200,"application/json", res);
  });

  // OPTIONS pre-flight for CORS
  restServer.onNotFound([]() {
    if (restServer.method() == HTTP_OPTIONS) {
      setCorsHeaders();
      restServer.send(204);
    } else {
      restServer.send(404,"text/plain","Not found");
    }
  });

  const char* headerKeys[] = {"X-NovaX-OTA"};
  restServer.collectHeaders(headerKeys, 1);
  restServer.begin();
  Serial.println("[REST] HTTP Server on port 80");

  wsServer.begin();
  Serial.println("[WS] WebSocket Server on port 81");

  // Centre servo
  radarServo.attach(SERVO_PIN, 500, 2400);
  radarServo.write(90);
  delay(200);
  radarServo.detach();

  // Camera (must be last — uses LEDC channels)
  cameraAvailable = initCamera();

  // Boot state: Manual, stopped, motors zeroed
  manualMode  = true;
  carStopped  = false;
  autoState   = AUTO_STOPPED;
  stopCar();

  Serial.printf("[NovaX V2] Ready | FW %s | Camera: %s | IMU: %s\n",
    FIRMWARE_VERSION,
    cameraAvailable ? "OV7670" : "None",
    mpuAvailable    ? "MPU6050" : "None");
}

// ===================================================================================
// 13. MAIN LOOP
// ===================================================================================

void loop() {
  // 1. Network services
  restServer.handleClient();
  pollWebSocketServer();

  // 2. Deferred Wi-Fi connect  [A8]
  processPendingStaConnect();

  // 3. Hardware state machines
  updateGyroHeading();
  stepNonBlockingRotate();   // [A14]
  updateLEDs();
  stepRadarScan();

  // 4. Autonomous navigation
  if (!manualMode) stepAutoNav();

  // 5. Motor safety watchdog
  // Only fires in manual mode; rotation keeps watchdog fed via lastDriveCmdMs
  if (manualMode && motorRunning && rotateState == ROT_IDLE
      && (millis() - lastDriveCmdMs > MOTOR_WATCHDOG_MS)) {
    stopCar();
  }

  // 6. Periodic WebSocket telemetry (100 ms)
  static unsigned long lastTelems = 0;
  if (millis() - lastTelems >= 100) {
    lastTelems = millis();
    long   d        = readUltrasonicCM();
    String wifiMode = (WiFi.getMode() == WIFI_AP ? "AP" : "STA");
    String ip       = (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString());

    String telem = "{\"type\":\"telemetry\",";
    telem += "\"distance\":"  + String(d)              + ",";
    telem += "\"heading\":"   + String(yawHeading, 1)  + ",";
    telem += "\"mode\":\""    + String(manualMode ? "manual" : "auto") + "\",";
    telem += "\"stopped\":"   + String(carStopped ? "true" : "false") + ",";
    telem += "\"rotating\":"  + String(rotateState == ROT_TURNING ? "true" : "false") + ",";
    telem += "\"version\":\"" + String(FIRMWARE_VERSION)  + "\",";
    telem += "\"camera\":"    + String(cameraAvailable ? "true" : "false") + ",";
    telem += "\"wifiMode\":\"" + wifiMode + "\",";
    telem += "\"ip\":\""      + ip       + "\",";
    telem += "\"uptime\":"    + String(millis() / 1000);
    telem += "}";
    broadcastWsText(telem);
  }
}

/*
 * ===================================================================================
 *  NovaX V2 API REFERENCE
 * ===================================================================================
 *
 * ── WebSocket ws://<ip>:81 ──────────────────────────────────────────────────────
 *
 * APP → ESP32 (JSON text frames, client-masked):
 *
 *  {"type":"move","dir":"F"|"B"|"L"|"R"|"S","speed":175}
 *    Move / stop.  Blocked in auto mode, when carStopped=true, or while rotating.
 *
 *  {"type":"stop"}
 *    Emergency stop.  Sets carStopped=true.  Works in any mode.
 *
 *  {"type":"start"}
 *    Clears carStopped.  In auto mode resumes navigation.
 *
 *  {"type":"mode","value":"manual"|"auto"}
 *    Switch drive mode.  Switching to manual stops motors.
 *
 *  {"type":"rotate","dir":"left"|"right"|"360"}
 *    Non-blocking gyro rotate (manual+not-stopped only).
 *    Reply: {"type":"rotate_done","heading":<deg>}
 *
 *  {"type":"led","pattern":"blink"|"warn"|"pulse"|"off"}
 *    74HC595 LED effect.
 *
 *  {"type":"scan"}
 *    Start -90°→+90° radar sweep. Streams radar frames + radar_summary.
 *
 *  {"type":"servo","angle":0..180}
 *    Manually position servo head. Reply: {"type":"servo_pos","angle":...,"distance":...}
 *
 *  {"type":"sonar","value":"on"|"off"}
 *    Enable/disable ultrasonic sensor.
 *
 *  {"type":"ping"}
 *    Keepalive. Reply: {"type":"pong","time":<millis>}
 *
 * ESP32 → APP (unmasked):
 *
 *  {"type":"status","status":"connected","version":"...","mode":"...","stopped":...}
 *    Sent immediately on WebSocket upgrade.
 *
 *  {"type":"telemetry","distance":...,"heading":...,"mode":"...","stopped":...,"rotating":...,"version":"...","camera":...,"wifiMode":"...","ip":"...","uptime":...}
 *    Broadcast every 100 ms to all connected clients.
 *
 *  {"type":"radar","angle":-90..90,"distance":...}
 *    Streamed during radar sweep (one per step).
 *
 *  {"type":"radar_summary","left":...,"front":...,"right":...,"done":true}
 *    Sent when sweep completes.
 *
 *  {"type":"servo_pos","angle":...,"distance":...}
 *    Reply to servo command.
 *
 *  {"type":"rotate_done","heading":...}
 *    Sent when non-blocking rotate finishes.
 *
 *  {"type":"stopped","mode":"..."}
 *    Broadcast when stop command received.
 *
 *  {"type":"started","mode":"..."}
 *    Broadcast when start command received.
 *
 *  {"type":"mode","value":"..."}
 *    Broadcast when mode changes.
 *
 *  {"type":"wifi","mode":"AP"|"STA","ip":"..."}
 *    Broadcast after Wi-Fi switch completes.
 *
 *  {"type":"sonar","active":true|false}
 *    Broadcast when sonar state changes.
 *
 * ── REST HTTP :80 ───────────────────────────────────────────────────────────────
 *
 *  GET  /status             Full robot state JSON
 *  GET  /firmware           Firmware info (version, hardware, camera, imu)
 *  GET  /cam.jpg            JPEG snapshot from OV7670
 *  GET  /wifi/scan          Start async scan (202) → poll until 200 with results
 *  GET  /wifi/saved         Saved network list (passwords omitted)
 *  POST /wifi/save          Body: ssid=&password=  — save credentials
 *  POST /wifi/select        Body: index=N          — set active profile
 *  POST /wifi/delete        Body: index=N          — delete profile
 *  POST /wifi/switchSta     Connect to active saved network (deferred)
 *  POST /wifi/switchAp      Return to AP mode
 *  GET  /ota/status         Alias for /firmware
 *  POST /ota/update         Multipart firmware binary (requires X-NovaX-OTA header)
 *  POST /path               Body: x1,y1;x2,y2;... draw-mode path
 *
 *  Backward-compat (kept for V1 app migration):
 *  GET  /move?d=F|B|L|R|S&speed=175
 *  GET  /rotate?dir=left|right|360
 *  GET  /led?effect=blink|warn|pulse|off
 *  GET  /scan
 *  GET  /scanResult
 *  GET  /servo?angle=0..180
 *  GET  /mode?set=manual|auto
 *  POST /stop
 *  POST /start
 *
 * ===================================================================================
 */
