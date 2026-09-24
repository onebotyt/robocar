/*
 * ===================================================================================
 *  NovaX V2 - Autonomous & Remote Robot Car Firmware
 *  Hardware Target: ESP32-WROOM-32 (V2 Hardware Map)
 *  Architecture: WebSocket Real-Time Control + REST Config & OTA Engine
 * ===================================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>

// ===================================================================================
// 1. FINALIZED ESP32 V2 GPIO MAPPING
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
#define CAM_SIOD        21   // Shared I2C Data
#define CAM_SIOC        22   // Shared I2C Clock
// RESET -> 3.3V, PWDN -> GND

// --- MX1508 Dual H-Bridge Motor Driver ---
#define MOTOR_IN1       13   // Left Motor Forward
#define MOTOR_IN2       14   // Left Motor Reverse
#define MOTOR_IN3       18   // Right Motor Forward
#define MOTOR_IN4       19   // Right Motor Reverse

// --- HC-SR04 Ultrasonic Distance Sensor ---
#define TRIG_PIN        23
#define ECHO_PIN         3   // MUST use 1k/2k voltage divider to 3.3V!

// --- SG90 Micro Servo ---
#define SERVO_PIN        2

// --- 74HC595 8-Bit Shift Register (LED Effects) ---
#define LED_DATA         5   // SER
#define LED_CLOCK       12   // SRCLK
#define LED_LATCH       15   // RCLK
// VCC = 3.3V, OE = GND, MR = 3.3V

// --- MPU6050 6-DOF IMU ---
#define MPU_SDA         21   // Shared with Camera SIOD
#define MPU_SCL         22   // Shared with Camera SIOC

// ===================================================================================
// 2. CONSTANTS & SYSTEM CONFIGURATION
// ===================================================================================
const char* FIRMWARE_VERSION  = "2.3.0";
const char* HARDWARE_VERSION  = "ESP32-V2";
const char* BUILD_DATE        = "Sep 25 2026";
const char* AP_DEFAULT_SSID   = "NovaX-Car";
const char* AP_DEFAULT_PASS   = "12345678";
const char* OTA_DEFAULT_TOKEN = "NovaX-OTA-ChangeMe";

// Motor Safety Watchdog (in milliseconds)
const unsigned long MOTOR_WATCHDOG_MS = 400;

// Autonomous Navigation Tuning
const int OBSTACLE_LIMIT_CM        = 50;   // Trigger avoidance when distance < 50cm
const int CRUISE_SPEED_PWM         = 175;  // 0..255
const int TURN_SPEED_PWM           = 210;
const unsigned long AUTO_CHECK_MS  = 120;

// Maximum saved Wi-Fi profiles in NVS
const uint8_t MAX_WIFI_PROFILES    = 5;

// Maximum concurrent WebSocket clients
const uint8_t MAX_WS_CLIENTS       = 4;

// ===================================================================================
// 3. GLOBAL INSTANCES & SYSTEM STATE
// ===================================================================================
WebServer restServer(80);
WiFiServer wsServer(81);
WiFiClient wsClients[MAX_WS_CLIENTS];

Servo radarServo;
Adafruit_MPU6050 mpu;
Preferences nvsPrefs;

// Motor Safety State
volatile unsigned long lastDriveCmdMs = 0;
volatile bool motorRunning = false;
volatile bool manualMode   = true;

// MPU6050 Heading & Gyro State
bool mpuAvailable = false;
float gyroZBias   = 0.0f;
float yawHeading  = 0.0f;
unsigned long lastGyroMicros = 0;

// Camera State
bool cameraAvailable = false;

// 74HC595 LED State
String currentLedEffect = "off";
unsigned long lastLedUpdateMs = 0;
uint8_t ledShiftVal = 0;

// Radar & Sonar State
bool sonarActive = true;
enum RadarScanState { SCAN_IDLE, SCAN_RUNNING, SCAN_DONE };
RadarScanState radarState = SCAN_IDLE;
int radarCurrentAngle = -90;
int radarStepDir = 1;
unsigned long lastRadarStepMs = 0;
int scanDistLeft = 400, scanDistFront = 400, scanDistRight = 400;

// Auto Avoidance State
enum AutoDriveState { AUTO_FORWARD, AUTO_DETECTED, AUTO_REVERSE, AUTO_SCAN, AUTO_TURN };
AutoDriveState autoState = AUTO_FORWARD;
unsigned long autoActionTimer = 0;

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

// ===================================================================================
// 4. LOW-LEVEL HARDWARE DRIVERS
// ===================================================================================

void write595(uint8_t value) {
  digitalWrite(LED_LATCH, LOW);
  shiftOut(LED_DATA, LED_CLOCK, MSBFIRST, value);
  digitalWrite(LED_LATCH, HIGH);
}

void initPins() {
  pinMode(MOTOR_IN1, OUTPUT); analogWrite(MOTOR_IN1, 0);
  pinMode(MOTOR_IN2, OUTPUT); analogWrite(MOTOR_IN2, 0);
  pinMode(MOTOR_IN3, OUTPUT); analogWrite(MOTOR_IN3, 0);
  pinMode(MOTOR_IN4, OUTPUT); analogWrite(MOTOR_IN4, 0);

  pinMode(TRIG_PIN, OUTPUT); digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);

  pinMode(LED_DATA, OUTPUT);
  pinMode(LED_CLOCK, OUTPUT);
  pinMode(LED_LATCH, OUTPUT);
  digitalWrite(LED_DATA, LOW);
  digitalWrite(LED_CLOCK, LOW);
  digitalWrite(LED_LATCH, LOW);
  write595(0);
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
  if (maxMag > 1.0f) {
    left  /= maxMag;
    right /= maxMag;
  }
  motorWrite(MOTOR_IN1, MOTOR_IN2, (int)(left * 255.0f));
  motorWrite(MOTOR_IN3, MOTOR_IN4, (int)(right * 255.0f));
  motorRunning = (fabs(left) > 0.05f || fabs(right) > 0.05f);
}

long readUltrasonicCM() {
  if (!sonarActive) return 400;
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 22000); // 22ms timeout ~ 3.7 meters
  if (duration <= 0) return 400;
  long cm = (duration * 0.0343) / 2;
  return (cm <= 0 || cm > 400) ? 400 : cm;
}

// ===================================================================================
// 5. MPU6050 GYROSCOPE & HEADING INTEGRATION
// ===================================================================================

void calibrateGyro() {
  delay(200);
  float sumZ = 0;
  int samples = 300;
  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sumZ += g.gyro.z;
    delay(2);
  }
  gyroZBias = sumZ / (float)samples;
  yawHeading = 0.0f;
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

  while (yawHeading < 0.0f)   yawHeading += 360.0f;
  while (yawHeading >= 360.0f) yawHeading -= 360.0f;
}

void turnByGyro(float targetDeg, bool turnRight) {
  if (!mpuAvailable) {
    // Fallback: fixed time turn if gyro offline
    int pwm = TURN_SPEED_PWM;
    if (turnRight) {
      motorWrite(MOTOR_IN1, MOTOR_IN2, pwm);
      motorWrite(MOTOR_IN3, MOTOR_IN4, -pwm);
    } else {
      motorWrite(MOTOR_IN1, MOTOR_IN2, -pwm);
      motorWrite(MOTOR_IN3, MOTOR_IN4, pwm);
    }
    delay(turnRight ? (int)(targetDeg * 6) : (int)(targetDeg * 6));
    stopCar();
    return;
  }

  float initialYaw = yawHeading;
  float accumulated = 0.0f;
  unsigned long turnStartTime = millis();

  while (millis() - turnStartTime < 4500) {
    updateGyroHeading();
    float diff = fabs(yawHeading - initialYaw);
    if (diff > 180.0f) diff = 360.0f - diff;
    accumulated = diff;

    if (accumulated >= targetDeg - 4.0f) break;

    float remaining = targetDeg - accumulated;
    int pwm = (remaining > 30) ? TURN_SPEED_PWM : (remaining > 10 ? 140 : 100);

    if (turnRight) {
      motorWrite(MOTOR_IN1, MOTOR_IN2, pwm);
      motorWrite(MOTOR_IN3, MOTOR_IN4, -pwm);
    } else {
      motorWrite(MOTOR_IN1, MOTOR_IN2, -pwm);
      motorWrite(MOTOR_IN3, MOTOR_IN4, pwm);
    }
    delay(5);
  }
  stopCar();
  delay(60);
}

// ===================================================================================
// 6. OV7670 CAMERA CAPTURE (NON-FIFO RGB565 / JPEG)
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

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_vflip(sensor, 1);
    sensor->set_hmirror(sensor, 0);
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
  size_t jpgSize = 0;
  bool converted = frame2jpg(fb, 55, &jpgBuf, &jpgSize);
  esp_camera_fb_return(fb);

  if (!converted || !jpgBuf) {
    restServer.send(500, "text/plain", "JPEG compression failed");
    return;
  }

  WiFiClient client = restServer.client();
  restServer.sendHeader("Access-Control-Allow-Origin", "*");
  restServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  restServer.setContentLength(jpgSize);
  restServer.send(200, "image/jpeg");
  client.write(jpgBuf, jpgSize);
  free(jpgBuf);
}

// ===================================================================================
// 7. EMBEDDED ZERO-DEPENDENCY RFC 6455 WEBSOCKET ENGINE (PORT 81)
// ===================================================================================

String computeWsAccept(const String& clientKey) {
  String combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  unsigned char sha1Result[20];
  mbedtls_sha1_ret((const unsigned char*)combined.c_str(), combined.length(), sha1Result);

  unsigned char base64Result[32];
  size_t base64Len = 0;
  mbedtls_base64_encode(base64Result, sizeof(base64Result), &base64Len, sha1Result, 20);
  return String((char*)base64Result);
}

void sendWsFrame(WiFiClient& client, const String& payload) {
  if (!client || !client.connected()) return;
  size_t len = payload.length();
  client.write(0x81); // FIN + Text opcode
  if (len < 126) {
    client.write((uint8_t)len);
  } else if (len <= 65535) {
    client.write(126);
    client.write((uint8_t)(len >> 8));
    client.write((uint8_t)(len & 0xFF));
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

void pollWebSocketServer() {
  // Check for new incoming client connection
  if (wsServer.hasClient()) {
    WiFiClient newClient = wsServer.available();
    int slot = -1;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
      if (!wsClients[i] || !wsClients[i].connected()) {
        slot = i;
        break;
      }
    }

    if (slot >= 0) {
      // Perform RFC 6455 handshake
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
        int keyEnd = request.indexOf("\r\n", keyIdx);
        String clientKey = request.substring(keyIdx + 19, keyEnd);
        clientKey.trim();
        String acceptKey = computeWsAccept(clientKey);

        String response = "HTTP/1.1 101 Switching Protocols\r\n";
        response += "Upgrade: websocket\r\n";
        response += "Connection: Upgrade\r\n";
        response += "Sec-WebSocket-Accept: " + acceptKey + "\r\n\r\n";
        newClient.print(response);

        wsClients[slot] = newClient;
        Serial.printf("[WebSocket] Client connected on slot %d\n", slot);

        // Send initial connection welcome & telemetry
        sendWsFrame(newClient, "{\"type\":\"status\",\"status\":\"connected\",\"version\":\"" + String(FIRMWARE_VERSION) + "\"}");
      } else {
        newClient.stop();
      }
    } else {
      newClient.stop(); // Busy
    }
  }

  // Handle incoming data from connected clients
  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (!wsClients[i]) continue;

    if (!wsClients[i].connected()) {
      Serial.printf("[WebSocket] Client disconnected on slot %d\n", i);
      wsClients[i].stop();
      // CRITICAL MOTOR SAFETY: Halt motors when WebSocket client drops!
      stopCar();
      continue;
    }

    while (wsClients[i].available() >= 2) {
      uint8_t byte0 = wsClients[i].read();
      uint8_t byte1 = wsClients[i].read();

      uint8_t opcode = byte0 & 0x0F;
      bool masked    = (byte1 & 0x80) != 0;
      uint64_t len   = byte1 & 0x7F;

      if (len == 126) {
        while (wsClients[i].available() < 2) delay(1);
        len = (wsClients[i].read() << 8) | wsClients[i].read();
      } else if (len == 127) {
        while (wsClients[i].available() < 8) delay(1);
        len = 0;
        for (int k = 0; k < 8; k++) len = (len << 8) | wsClients[i].read();
      }

      uint8_t maskKey[4] = {0, 0, 0, 0};
      if (masked) {
        while (wsClients[i].available() < 4) delay(1);
        wsClients[i].readBytes(maskKey, 4);
      }

      if (opcode == 0x8) { // Connection Close
        stopCar();
        wsClients[i].stop();
        break;
      } else if (opcode == 0x9) { // Ping
        wsClients[i].write(0x8A); // Pong
        wsClients[i].write((uint8_t)0);
        continue;
      }

      // Read payload
      String payload = "";
      payload.reserve(len);
      for (size_t k = 0; k < len; k++) {
        while (!wsClients[i].available()) delay(1);
        uint8_t b = wsClients[i].read();
        if (masked) b ^= maskKey[k % 4];
        payload += (char)b;
      }

      if (opcode == 0x1) { // Text JSON Frame
        handleWsMessage(wsClients[i], payload);
      }
    }
  }
}

// ===================================================================================
// 8. WEBSOCKET MESSAGE DISPATCHER & MOTOR WATCHDOG
// ===================================================================================

void handleWsMessage(WiFiClient& client, const String& msg) {
  // Simple, robust zero-dependency JSON extraction
  // 1. Move Command
  if (msg.indexOf("\"type\":\"move\"") >= 0) {
    if (!manualMode) return;
    int dirIdx = msg.indexOf("\"dir\":\"");
    if (dirIdx >= 0) {
      char dir = msg.charAt(dirIdx + 7);
      int spd = CRUISE_SPEED_PWM;
      int spdIdx = msg.indexOf("\"speed\":");
      if (spdIdx >= 0) spd = msg.substring(spdIdx + 8).toInt();
      if (spd <= 0) spd = CRUISE_SPEED_PWM;

      if (dir == 'F') {
        motorWrite(MOTOR_IN1, MOTOR_IN2, spd);
        motorWrite(MOTOR_IN3, MOTOR_IN4, spd);
      } else if (dir == 'B') {
        motorWrite(MOTOR_IN1, MOTOR_IN2, -spd);
        motorWrite(MOTOR_IN3, MOTOR_IN4, -spd);
      } else if (dir == 'L') {
        motorWrite(MOTOR_IN1, MOTOR_IN2, -spd);
        motorWrite(MOTOR_IN3, MOTOR_IN4, spd);
      } else if (dir == 'R') {
        motorWrite(MOTOR_IN1, MOTOR_IN2, spd);
        motorWrite(MOTOR_IN3, MOTOR_IN4, -spd);
      } else if (dir == 'S') {
        stopCar();
      }

      // RESET WATCHDOG TIMER
      lastDriveCmdMs = millis();
      motorRunning = (dir != 'S');
    }
  }
  // 2. Emergency Stop
  else if (msg.indexOf("\"type\":\"stop\"") >= 0) {
    stopCar();
    lastDriveCmdMs = millis();
    sendWsFrame(client, "{\"type\":\"stopped\"}");
  }
  // 3. Mode Toggle
  else if (msg.indexOf("\"type\":\"mode\"") >= 0) {
    if (msg.indexOf("\"value\":\"auto\"") >= 0) {
      manualMode = false;
      autoState = AUTO_FORWARD;
      Serial.println("[MODE] Autonomous Navigation Active");
    } else {
      manualMode = true;
      stopCar();
      Serial.println("[MODE] Manual Driver Control Active");
    }
    broadcastWsText("{\"type\":\"mode\",\"value\":\"" + String(manualMode ? "manual" : "auto") + "\"}");
  }
  // 4. Gyroscope Assisted Rotation
  else if (msg.indexOf("\"type\":\"rotate\"") >= 0) {
    if (!manualMode) return;
    if (msg.indexOf("\"dir\":\"left\"") >= 0) {
      turnByGyro(90.0f, false);
    } else if (msg.indexOf("\"dir\":\"right\"") >= 0) {
      turnByGyro(90.0f, true);
    } else if (msg.indexOf("\"dir\":\"360\"") >= 0) {
      turnByGyro(360.0f, true);
    }
  }
  // 5. LED Effect
  else if (msg.indexOf("\"type\":\"led\"") >= 0) {
    if (msg.indexOf("\"pattern\":\"blink\"") >= 0) currentLedEffect = "blink";
    else if (msg.indexOf("\"pattern\":\"warn\"") >= 0) currentLedEffect = "warn";
    else if (msg.indexOf("\"pattern\":\"pulse\"") >= 0) currentLedEffect = "pulse";
    else currentLedEffect = "off";
    lastLedUpdateMs = millis();
    updateLEDs();
  }
  // 6. Radar Scan Trigger
  else if (msg.indexOf("\"type\":\"scan\"") >= 0) {
    radarState = SCAN_RUNNING;
    radarCurrentAngle = -90;
    radarStepDir = 1;
    radarServo.attach(SERVO_PIN, 500, 2400);
    lastRadarStepMs = millis();
  }
  // 7. Manual Servo Head Angle Command (<  SCAN  >)
  else if (msg.indexOf("\"type\":\"servo\"") >= 0) {
    radarState = SCAN_IDLE;
    int angIdx = msg.indexOf("\"angle\":");
    if (angIdx >= 0) {
      int ang = msg.substring(angIdx + 8).toInt();
      if (ang < 0) ang = 0;
      if (ang > 180) ang = 180;
      if (!radarServo.attached()) {
        radarServo.attach(SERVO_PIN, 500, 2400);
      }
      radarServo.write(ang);
      long dist = readUltrasonicCM();
      broadcastWsText("{\"type\":\"servo_pos\",\"angle\":" + String(ang) + ",\"distance\":" + String(dist) + "}");
    }
  }
  // 8. Ping / Heartbeat
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

  int servoAngle = radarCurrentAngle + 90; // Convert -90..90 to 0..180
  radarServo.write(servoAngle);
  delay(25);

  long dist = readUltrasonicCM();

  // Stream current radar point over WebSocket
  String radarMsg = "{\"type\":\"radar\",\"angle\":" + String(radarCurrentAngle) + ",\"distance\":" + String(dist) + "}";
  broadcastWsText(radarMsg);

  if (radarCurrentAngle == -90) scanDistLeft = dist;
  else if (radarCurrentAngle == 0) scanDistFront = dist;
  else if (radarCurrentAngle == 90) scanDistRight = dist;

  radarCurrentAngle += 30;
  if (radarCurrentAngle > 90) {
    // Return servo to center and finish
    radarServo.write(90);
    delay(100);
    radarServo.detach();
    radarState = SCAN_DONE;

    String summaryMsg = "{\"type\":\"radar_summary\",\"left\":" + String(scanDistLeft) +
                        ",\"front\":" + String(scanDistFront) +
                        ",\"right\":" + String(scanDistRight) + ",\"done\":true}";
    broadcastWsText(summaryMsg);
  }
}

void stepAutoNav() {
  if (manualMode) return;

  long distance = readUltrasonicCM();

  switch (autoState) {
    case AUTO_FORWARD:
      if (distance < OBSTACLE_LIMIT_CM) {
        stopCar();
        autoState = AUTO_DETECTED;
        autoActionTimer = millis();
      } else {
        motorMix(0.0f, 0.65f); // Forward
      }
      break;

    case AUTO_DETECTED:
      // Brief pause then begin reverse
      stopCar();
      autoState = AUTO_REVERSE;
      autoActionTimer = millis();
      break;

    case AUTO_REVERSE:
      motorWrite(MOTOR_IN1, MOTOR_IN2, -180);
      motorWrite(MOTOR_IN3, MOTOR_IN4, -180);
      if (distance >= 55 || millis() - autoActionTimer > 1200) {
        stopCar();
        autoState = AUTO_SCAN;
        radarState = SCAN_RUNNING;
        radarCurrentAngle = -90;
        radarServo.attach(SERVO_PIN, 500, 2400);
        lastRadarStepMs = millis();
      }
      break;

    case AUTO_SCAN:
      if (radarState == SCAN_DONE) {
        autoState = AUTO_TURN;
        autoActionTimer = millis();
      }
      break;

    case AUTO_TURN:
      if (scanDistRight >= scanDistLeft) {
        turnByGyro(90.0f, true); // Turn right toward open space
      } else {
        turnByGyro(90.0f, false); // Turn left toward open space
      }
      autoState = AUTO_FORWARD;
      break;
  }
}

// ===================================================================================
// 10. 74HC595 LED SEQUENCER
// ===================================================================================

void updateLEDs() {
  if (currentLedEffect == "off") {
    write595(0);
    return;
  }
  unsigned long now = millis();
  if (currentLedEffect == "blink" && now - lastLedUpdateMs >= 300) {
    lastLedUpdateMs = now;
    static bool toggle = false;
    toggle = !toggle;
    write595(toggle ? 0xFF : 0x00);
  } else if (currentLedEffect == "warn" && now - lastLedUpdateMs >= 400) {
    lastLedUpdateMs = now;
    static bool toggleW = false;
    toggleW = !toggleW;
    write595(toggleW ? 0b11000011 : 0b00111100);
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
  restServer.sendHeader("Access-Control-Allow-Origin", "*");
  restServer.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  restServer.sendHeader("Access-Control-Allow-Headers", "Content-Type,X-NovaX-OTA");
}

String getWifiProfilesJson() {
  String json = "{\"selected\":" + String(activeWifiIndex) + ",\"networks\":[";
  uint8_t count = nvsPrefs.getUChar("cnt", 0);
  bool first = true;
  for (uint8_t i = 0; i < count && i < MAX_WIFI_PROFILES; i++) {
    String s = nvsPrefs.getString((String("s") + i).c_str(), "");
    if (s.length() == 0) continue;
    if (!first) json += ",";
    first = false;
    // Passwords are deliberately omitted for security!
    json += "{\"ssid\":\"" + s + "\"}";
  }
  json += "]}";
  return json;
}

void handleWifiScan() {
  setCorsHeaders();
  int count = WiFi.scanNetworks(false, true);
  String out = "{\"networks\":[";
  for (int i = 0; i < count; i++) {
    if (i) out += ",";
    String ssid = WiFi.SSID(i);
    ssid.replace("\"", "\\\"");
    out += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? 1 : 0) + "}";
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
  String ssid = restServer.arg("ssid");
  String pass = restServer.arg("password");
  uint8_t count = nvsPrefs.getUChar("cnt", 0);
  int foundSlot = -1;

  for (uint8_t i = 0; i < count; i++) {
    if (nvsPrefs.getString((String("s") + i).c_str(), "") == ssid) {
      foundSlot = i;
      break;
    }
  }

  if (foundSlot < 0) {
    if (count >= MAX_WIFI_PROFILES) {
      restServer.send(409, "text/plain", "Saved profiles limit reached");
      return;
    }
    foundSlot = count++;
  }

  nvsPrefs.putString((String("s") + foundSlot).c_str(), ssid);
  nvsPrefs.putString((String("p") + foundSlot).c_str(), pass);
  nvsPrefs.putUChar("cnt", count);

  if (activeWifiIndex < 0) {
    activeWifiIndex = foundSlot;
    nvsPrefs.putInt("sel", activeWifiIndex);
  }

  // NOTE: Saving credentials DOES NOT automatically connect!
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
  int idx = restServer.arg("index").toInt();
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
  if (activeWifiIndex >= count) activeWifiIndex = count > 0 ? 0 : -1;
  nvsPrefs.putInt("sel", activeWifiIndex);
  restServer.send(200, "application/json", getWifiProfilesJson());
}

void handleSwitchSTA() {
  setCorsHeaders();
  uint8_t count = nvsPrefs.getUChar("cnt", 0);
  if (count == 0 || activeWifiIndex < 0 || activeWifiIndex >= count) {
    restServer.send(400, "text/plain", "No selected saved network");
    return;
  }
  String ssid = nvsPrefs.getString((String("s") + activeWifiIndex).c_str(), "");
  String pass = nvsPrefs.getString((String("p") + activeWifiIndex).c_str(), "");

  restServer.send(200, "application/json", "{\"status\":\"connecting\",\"ssid\":\"" + ssid + "\",\"host\":\"http://novax.local\"}");
  delay(100);

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("novax");
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long startT = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startT < 12000) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    MDNS.end();
    MDNS.begin("novax");
    Serial.printf("[WIFI] Connected to STA. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    // Fallback back to AP mode if connection failed
    Serial.println("[WIFI] STA connection failed. Falling back to AP mode.");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS);
  }
}

void handleSwitchAP() {
  setCorsHeaders();
  restServer.send(200, "application/json", "{\"status\":\"switched\",\"mode\":\"AP\",\"ip\":\"192.168.4.1\"}");
  delay(100);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS);
  Serial.printf("[WIFI] Running in AP mode. IP: %s\n", WiFi.softAPIP().toString().c_str());
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
  long d = readUltrasonicCM();
  String wifiMode = (WiFi.getMode() == WIFI_AP ? "AP" : (WiFi.getMode() == WIFI_STA ? "STA" : "OTHER"));
  String ip = (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  String s = "{";
  s += "\"distance\":" + String(d) + ",";
  s += "\"mode\":" + String(manualMode ? 1 : 0) + ",";
  s += "\"yaw\":" + String(yawHeading, 1) + ",";
  s += "\"camera\":" + String(cameraAvailable ? 1 : 0) + ",";
  s += "\"wifiMode\":\"" + wifiMode + "\",";
  s += "\"ip\":\"" + ip + "\",";
  s += "\"version\":\"" + String(FIRMWARE_VERSION) + "\"";
  s += "}";
  restServer.send(200, "application/json", s);
}

// OTA Update Handlers
bool isOtaAuthorized() {
  return restServer.hasHeader("X-NovaX-OTA") && restServer.header("X-NovaX-OTA") == OTA_DEFAULT_TOKEN;
}

void handleOtaUpload() {
  HTTPUpload& upload = restServer.upload();
  if (!isOtaAuthorized()) {
    if (upload.status == UPLOAD_FILE_START) Serial.println("[OTA] Unauthorized upload attempt.");
    return;
  }
  if (upload.status == UPLOAD_FILE_START) {
    stopCar(); // CUT OFF MOTORS IMMEDIATELY
    Serial.printf("[OTA] Flashing: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning()) Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[OTA] Complete: %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Serial.println("[OTA] Aborted.");
  }
}

void handleOtaFinish() {
  setCorsHeaders();
  if (!isOtaAuthorized()) { restServer.send(401, "text/plain", "Unauthorized"); return; }
  if (Update.hasError()) { restServer.send(500, "text/plain", "Flash Failed"); return; }
  restServer.send(200, "application/json", "{\"status\":\"ok\",\"restarting\":true}");
  delay(500);
  ESP.restart();
}

void handlePathUpload() {
  setCorsHeaders();
  if (!restServer.hasArg("plain")) {
    restServer.send(400, "text/plain", "No path data");
    return;
  }
  String body = restServer.arg("plain");
  float sumX = 0.0f;
  int count = 0;
  int start = 0;
  while (start < body.length()) {
    int comma = body.indexOf(',', start);
    if (comma == -1) break;
    int semi = body.indexOf(';', start);
    if (semi == -1) semi = body.length();
    float x = body.substring(start, comma).toFloat();
    sumX += x;
    count++;
    if (semi >= body.length()) break;
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
  motorRunning = true;
  restServer.send(200, "text/plain", "Path Received");
}

// ===================================================================================
// 12. SETUP & MAIN EXECUTION LOOP
// ===================================================================================

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[NovaX V2] Initializing...");

  initPins();
  initMPU();

  nvsPrefs.begin("wifi", false);
  activeWifiIndex = nvsPrefs.getInt("sel", -1);

  // RULE: Always start in AP Mode on every boot!
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS);
  WiFi.setHostname("novax");
  MDNS.begin("novax");

  Serial.printf("[WIFI] AP SSID: %s | IP: %s\n", AP_DEFAULT_SSID, WiFi.softAPIP().toString().c_str());

  // Attach REST endpoints
  restServer.on("/wifi/scan", handleWifiScan);
  restServer.on("/wifi/saved", handleWifiSaved);
  restServer.on("/wifi/save", HTTP_POST, handleWifiSave);
  restServer.on("/wifi/select", HTTP_POST, handleWifiSelect);
  restServer.on("/wifi/delete", HTTP_POST, handleWifiDelete);
  restServer.on("/wifi/switchSta", HTTP_POST, handleSwitchSTA);
  restServer.on("/wifi/switchAp", HTTP_POST, handleSwitchAP);
  restServer.on("/firmware", handleFirmwareInfo);
  restServer.on("/status", handleStatus);
  restServer.on("/path", HTTP_POST, handlePathUpload);
  restServer.on("/cam.jpg", handleCameraSnapshot);
  restServer.on("/servo", HTTP_GET, []() {
    setCorsHeaders();
    if (restServer.hasArg("angle")) {
      int ang = restServer.arg("angle").toInt();
      if (ang < 0) ang = 0;
      if (ang > 180) ang = 180;
      radarState = SCAN_IDLE;
      if (!radarServo.attached()) {
        radarServo.attach(SERVO_PIN, 500, 2400);
      }
      radarServo.write(ang);
      long dist = readUltrasonicCM();
      restServer.send(200, "application/json", "{\"status\":\"ok\",\"angle\":" + String(ang) + ",\"distance\":" + String(dist) + "}");
    } else {
      restServer.send(400, "text/plain", "Missing angle parameter");
    }
  });
  restServer.on("/ota/status", handleFirmwareInfo);
  restServer.on("/ota/update", HTTP_POST, handleOtaFinish, handleOtaUpload);

  const char* headerKeys[] = {"X-NovaX-OTA"};
  restServer.collectHeaders(headerKeys, 1);
  restServer.begin();
  Serial.println("[REST] HTTP Server running on port 80");

  // Start WebSocket Server on port 81
  wsServer.begin();
  Serial.println("[WEBSOCKET] Server running on port 81");

  // Center radar servo
  radarServo.attach(SERVO_PIN, 500, 2400);
  radarServo.write(90);
  delay(200);
  radarServo.detach();

  // Initialize OV7670 camera
  cameraAvailable = initCamera();

  Serial.println("[NovaX V2] System Ready!");
}

void loop() {
  // 1. Service HTTP and WebSocket connections
  restServer.handleClient();
  pollWebSocketServer();

  // 2. Hardware state machines
  updateGyroHeading();
  updateLEDs();
  stepRadarScan();

  // 3. Autonomous navigation (when active)
  if (!manualMode) {
    stepAutoNav();
  }

  // 4. CRITICAL MOTOR SAFETY WATCHDOG
  // If in manual mode, motors are running, and no command was received for > 400ms:
  if (manualMode && motorRunning && (millis() - lastDriveCmdMs > MOTOR_WATCHDOG_MS)) {
    stopCar();
  }

  // 5. Periodic WebSocket Telemetry Broadcast (every 100ms)
  static unsigned long lastTelemetryBroadcast = 0;
  if (millis() - lastTelemetryBroadcast >= 100) {
    lastTelemetryBroadcast = millis();
    long d = readUltrasonicCM();
    String wifiMode = (WiFi.getMode() == WIFI_AP ? "AP" : "STA");
    String ip = (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString());

    String telem = "{\"type\":\"telemetry\",";
    telem += "\"distance\":" + String(d) + ",";
    telem += "\"heading\":" + String(yawHeading, 1) + ",";
    telem += "\"battery\":3.95,";
    telem += "\"mode\":\"" + String(manualMode ? "manual" : "auto") + "\",";
    telem += "\"camera\":" + String(cameraAvailable ? "true" : "false") + ",";
    telem += "\"wifiMode\":\"" + wifiMode + "\",";
    telem += "\"ip\":\"" + ip + "\",";
    telem += "\"uptime\":" + String(millis() / 1000);
    telem += "}";
    broadcastWsText(telem);
  }
}
