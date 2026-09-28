/*
 * ===================================================================================
 *  NovaX V2 (No-Camera Edition) - Autonomous & Remote Robot Car Firmware
 *  Hardware Target: ESP32-WROOM-32 (V2 Hardware Map)
 *  Architecture: WebSocket Real-Time Control (:81) + REST Config (:80) + OTA Engine
 *  Version: 2.4.52  (Pure Car Edition: Motors + Sonar + Radar Servo + MPU6050 Gyro + LEDs)
 * ===================================================================================
 */
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Compatibility macros for mbedtls 2.x vs 3.x
#if !defined(MBEDTLS_DEPRECATED_REMOVED) && !defined(mbedtls_sha1_starts_ret)
  #define mbedtls_sha1_starts_ret(ctx)              mbedtls_sha1_starts(ctx)
  #define mbedtls_sha1_update_ret(ctx, input, ilen) mbedtls_sha1_update(ctx, input, ilen)
  #define mbedtls_sha1_finish_ret(ctx, output)      mbedtls_sha1_finish(ctx, output)
#endif

// ===================================================================================
// 1. FINALIZED ESP32 V2 GPIO MAPPING — PURE CAR (CUSTOM HARDWARE LAYOUT)
// ===================================================================================
// --- MX1508 Dual H-Bridge Motor Driver ---
#define MOTOR_IN1       13   // Left Motor Forward
#define MOTOR_IN2       14   // Left Motor Reverse
#define MOTOR_IN3       16   // Right Motor Forward
#define MOTOR_IN4       17   // Right Motor Reverse

// --- HC-SR04 Ultrasonic Distance Sensor ---
#define TRIG_PIN        23   // Trigger Pin (Output)
#define ECHO_PIN        34   // Echo Pin (Input only, uses 1k/2k voltage divider to 3.3V!)

// --- SG90 Micro Servo ---
#define SERVO_PIN       25   // Radar Servo PWM Signal

// --- 74HC595 8-Bit Shift Register (LED Effects) ---
#define LED_DATA         5   // SER   (Data)
#define LED_CLOCK       18   // SH_CP (Clock)
#define LED_LATCH       19   // ST_CP (Latch)
// 74HC595: VCC=3.3V, OE=GND, MR=3.3V

// --- MPU6050 6-DOF IMU (Dedicated I2C Bus) ---
#define MPU_SDA         21   // Dedicated I2C Data
#define MPU_SCL         22   // Dedicated I2C Clock

// ===================================================================================
// 2. CONSTANTS & SYSTEM CONFIGURATION
// ===================================================================================
const char* FIRMWARE_VERSION  = "2.4.55";
const char* HARDWARE_VERSION  = "ESP32-V2-NOCAM";
const char* BUILD_DATE        = "2026-09-28";
const char* AP_DEFAULT_SSID   = "NovaX-Car";
const char* AP_DEFAULT_PASS   = "12345678";
const char* OTA_DEFAULT_TOKEN = "NovaX-OTA-ChangeMe";

// Motor Safety Watchdog
const unsigned long MOTOR_WATCHDOG_MS = 400;

// Autonomous Navigation Tuning
const int   OBSTACLE_LIMIT_CM    = 50;
const int   CRUISE_SPEED_PWM     = 175;
const int   TURN_SPEED_PWM       = 210;

// Sonar cache TTL (ms) to prevent blocking pulseIn loops
const unsigned long SONAR_CACHE_MS = 80;

// WebSocket limits
const size_t WS_MAX_PAYLOAD = 4096;
const unsigned long WS_BYTE_TIMEOUT_MS = 50;
const uint8_t MAX_WIFI_PROFILES = 5;
const uint8_t MAX_WS_CLIENTS = 4;

// ===================================================================================
// 3. GLOBAL INSTANCES & SYSTEM STATE
// ===================================================================================
WebServer        restServer(80);
WiFiServer       wsServer(81);
WiFiClient       wsClients[MAX_WS_CLIENTS];
Servo            radarServo;
Adafruit_MPU6050 mpu;
Preferences      nvsPrefs;

// Motor Safety & Soft-Start State (Protects against battery sag / brownout)
volatile unsigned long lastDriveCmdMs = 0;
volatile bool motorRunning = false;
volatile bool manualMode   = true;
volatile bool carStopped   = false;
int targetPwmLeft          = 0;
int targetPwmRight         = 0;
int currentPwmLeft         = 0;
int currentPwmRight        = 0;
unsigned long lastMotorRampMs = 0;

// MPU6050 Attitude & Gyro State
bool          mpuAvailable   = false;
uint8_t       mpuI2cAddress  = 0x68;
float         gyroZBias      = 0.0f;
float         yawHeading     = 0.0f;
float         pitchAngle     = 0.0f;
float         rollAngle      = 0.0f;
unsigned long lastGyroMicros = 0;

// 74HC595 LED State
String        currentLedEffect = "off";
unsigned long lastLedUpdateMs  = 0;

// Radar & Sonar State
bool          sonarActive       = true;
long          sonarCacheValue   = 400;
unsigned long sonarCacheTime    = 0;
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
AutoDriveState autoState       = AUTO_STOPPED;
unsigned long  autoActionTimer = 0;

// Non-blocking gyro rotate state
enum RotateState { ROT_IDLE, ROT_TURNING };
RotateState   rotateState     = ROT_IDLE;
float         rotateTarget    = 0.0f;
bool          rotateTurnRight = true;
float         rotateInitYaw   = 0.0f;
unsigned long rotateStartMs   = 0;

// Wi-Fi deferred STA connect & scan state
bool          pendingStaConnect = false;
String        pendingStaSsid    = "";
String        pendingStaPass    = "";
unsigned long pendingStaDelay   = 0;
bool          wifiScanPending   = false;
unsigned long wifiScanStartedMs = 0;

// OTA auth flag
bool otaAuthorised   = false;
int  activeWifiIndex = -1;

// Forward Declarations
void stopCar();
void motorWrite(int pinF, int pinB, int pwm);
void motorMix(float turn, float speed);
void setRadarServo(int angle);
void startRadarScan();
void stepRadarScan();
long readUltrasonicCM();
void broadcastWsText(const String& payload);
void handleWsMessage(WiFiClient& client, const String& msg);
void updateLEDs();
void stepAutoNav();
void updateGyroHeading();
void calibrateGyro();
void stepNonBlockingRotate();
void pollWebSocketServer();
String getWifiProfilesJson();
void handleWifiScan();
void handleWifiSaved();
void handleWifiSave();
void handleWifiSelect();
void handleWifiDelete();
void handleSwitchSTA();
void handleSwitchAP();
void processPendingStaConnect();

// ===================================================================================
// 4. LOW-LEVEL HARDWARE DRIVERS & SLEW-RATE MOTOR CONTROLLER
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

// Slew-rate ramping: smooths rapid acceleration to prevent power rail brownout & Wi-Fi disconnect
void stepMotorRamp() {
  unsigned long now = millis();
  if (now - lastMotorRampMs < 10) return;
  lastMotorRampMs = now;

  const int RAMP_STEP = 35; // Ramps 0 to 255 in ~70ms — stops inrush voltage collapse!

  if (currentPwmLeft < targetPwmLeft) {
    currentPwmLeft = min(currentPwmLeft + RAMP_STEP, targetPwmLeft);
  } else if (currentPwmLeft > targetPwmLeft) {
    currentPwmLeft = max(currentPwmLeft - RAMP_STEP, targetPwmLeft);
  }

  if (currentPwmRight < targetPwmRight) {
    currentPwmRight = min(currentPwmRight + RAMP_STEP, targetPwmRight);
  } else if (currentPwmRight > targetPwmRight) {
    currentPwmRight = max(currentPwmRight - RAMP_STEP, targetPwmRight);
  }

  motorWrite(MOTOR_IN1, MOTOR_IN2, currentPwmLeft);
  motorWrite(MOTOR_IN3, MOTOR_IN4, currentPwmRight);
}

void stopCar() {
  targetPwmLeft   = 0;
  targetPwmRight  = 0;
  currentPwmLeft  = 0;
  currentPwmRight = 0;
  motorWrite(MOTOR_IN1, MOTOR_IN2, 0);
  motorWrite(MOTOR_IN3, MOTOR_IN4, 0);
  motorRunning = false;
}

void motorMix(float turn, float speed) {
  if (carStopped || (!manualMode && autoState == AUTO_STOPPED)) {
    stopCar();
    return;
  }
  turn  = constrain(turn,  -1.0f, 1.0f);
  speed = constrain(speed, -1.0f, 1.0f);

  float left  = speed + turn;
  float right = speed - turn;

  float maxMag = max(fabs(left), fabs(right));
  if (maxMag > 1.0f) {
    left  /= maxMag;
    right /= maxMag;
  }

  targetPwmLeft  = (int)(left  * 255.0f);
  targetPwmRight = (int)(right * 255.0f);

  motorRunning   = (targetPwmLeft != 0 || targetPwmRight != 0);
  lastDriveCmdMs = millis();
}

// Ultrasonic with TTL Cache
long readUltrasonicCM() {
  if (!sonarActive) return 400;
  unsigned long now = millis();
  if (now - sonarCacheTime < SONAR_CACHE_MS && sonarCacheValue > 0) {
    return sonarCacheValue;
  }
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 25000);
  if (duration == 0) {
    sonarCacheValue = 400;
  } else {
    sonarCacheValue = (long)(duration * 0.0343f / 2.0f);
    if (sonarCacheValue <= 0 || sonarCacheValue > 400) sonarCacheValue = 400;
  }
  sonarCacheTime = now;
  return sonarCacheValue;
}

void invalidateSonarCache() {
  sonarCacheTime = 0;
}

// Servo Motor Head Control
void setRadarServo(int angle) {
  angle = constrain(angle, 0, 180);
  if (!radarServo.attached()) {
    radarServo.attach(SERVO_PIN, 500, 2400);
  }
  radarServo.write(angle);
}

// Radar Sweeper State Machine
void startRadarScan() {
  radarState        = SCAN_RUNNING;
  radarCurrentAngle = -90;
  radarStepDir      = 1;
  invalidateSonarCache();
  setRadarServo(0); // -90 deg maps to 0 deg
  lastRadarStepMs   = millis();
}

void stepRadarScan() {
  if (radarState != SCAN_RUNNING) return;
  unsigned long now = millis();
  if (now - lastRadarStepMs < 90) return;
  lastRadarStepMs = now;

  int servoAngle = radarCurrentAngle + 90; // -90..90 -> 0..180
  setRadarServo(servoAngle);

  invalidateSonarCache();
  long dist = readUltrasonicCM();

  broadcastWsText("{\"type\":\"radar\",\"angle\":" + String(radarCurrentAngle)
                  + ",\"distance\":" + String(dist) + "}");

  if (radarCurrentAngle == -90)      scanDistLeft  = (int)dist;
  else if (radarCurrentAngle == 0)   scanDistFront = (int)dist;
  else if (radarCurrentAngle == 90)  scanDistRight = (int)dist;

  radarCurrentAngle += 30;
  if (radarCurrentAngle > 90) {
    setRadarServo(90);
    delay(100);
    radarServo.detach();
    radarState = SCAN_DONE;

    broadcastWsText("{\"type\":\"radar_summary\",\"left\":" + String(scanDistLeft)
                    + ",\"front\":" + String(scanDistFront)
                    + ",\"right\":" + String(scanDistRight) + ",\"done\":true}");
  }
}

// 74HC595 LED Animation Patterns
void updateLEDs() {
  unsigned long now = millis();
  if (currentLedEffect == "off") {
    write595(0x00);
  } else if (currentLedEffect == "blink") {
    if (now - lastLedUpdateMs > 300) {
      lastLedUpdateMs = now;
      static bool toggle = false;
      toggle = !toggle;
      write595(toggle ? 0xFF : 0x00);
    }
  } else if (currentLedEffect == "warn") {
    if (now - lastLedUpdateMs > 150) {
      lastLedUpdateMs = now;
      static bool warnToggle = false;
      warnToggle = !warnToggle;
      write595(warnToggle ? 0xAA : 0x55);
    }
  } else if (currentLedEffect == "pulse") {
    if (now - lastLedUpdateMs > 80) {
      lastLedUpdateMs = now;
      static uint8_t pulsePos = 0;
      pulsePos = (pulsePos + 1) % 8;
      write595(1 << pulsePos);
    }
  }
}

// ===================================================================================
// 5. MPU6050 GYROSCOPE, ATTITUDE & CALIBRATION (WITH AUTO I2C BUS SCAN)
// ===================================================================================
void calibrateGyro() {
  stopCar();
  delay(100);
  float sumZ  = 0;
  int samples = 200;
  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sumZ += g.gyro.z;
    delay(2);
  }
  gyroZBias      = sumZ / (float)samples;
  yawHeading     = 0.0f;
  pitchAngle     = 0.0f;
  rollAngle      = 0.0f;
  lastGyroMicros = micros();
  Serial.printf("[MPU6050] Calibrated. Zero bias: %.4f rad/s\n", gyroZBias);
}

void initMPU() {
  // Configure internal pullups on I2C lines
  pinMode(MPU_SDA, INPUT_PULLUP);
  pinMode(MPU_SCL, INPUT_PULLUP);
  delay(80); // Allow 5V booster rail to settle

  // Unwedge stuck I2C bus if slave held SDA low
  pinMode(MPU_SCL, OUTPUT);
  for (int i = 0; i < 9; i++) {
    digitalWrite(MPU_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(MPU_SCL, LOW);
    delayMicroseconds(5);
  }
  pinMode(MPU_SCL, INPUT_PULLUP);
  delay(15);

  Wire.begin(MPU_SDA, MPU_SCL, 100000);
  Wire.setTimeOut(50); // Prevent bus lockup from stalling main loop

  // Both standard MPU6050 addresses:
  // 0x68 (when AD0 is connected to GND)
  // 0x69 (when AD0 is connected to 5V/3.3V or floating with internal pull-up)
  const uint8_t TARGET_ADDRS[] = { 0x68, 0x69 };

  for (int attempt = 1; attempt <= 3; attempt++) {
    for (int i = 0; i < 2; i++) {
      uint8_t addr = TARGET_ADDRS[i];
      Serial.printf("[MPU6050] Probing address 0x%02X (attempt %d)...\n", addr, attempt);

      Wire.beginTransmission(addr);
      byte err = Wire.endTransmission();
      if (err == 0) {
        Serial.printf("[MPU6050] Responding at address 0x%02X! Calling mpu.begin()...\n", addr);
        if (mpu.begin(addr, &Wire)) {
          mpuI2cAddress = addr;
          mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
          mpu.setGyroRange(MPU6050_RANGE_500_DEG);
          mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
          calibrateGyro();
          mpuAvailable = true;
          Serial.printf("[MPU6050] Succeeded on address 0x%02X! Calibrated & Ready.\n", addr);
          return;
        } else {
          Serial.printf("[MPU6050] ACK at 0x%02X but mpu.begin() failed. Trying alternate address...\n", addr);
        }
      }
    }
    delay(60);
  }

  // Fallback: Scan full 7-bit I2C bus in case module is at another address
  Serial.println("[MPU6050] Neither 0x68 nor 0x69 responded. Scanning entire I2C bus (0x01..0x7F)...");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C] Found responding device at 0x%02X\n", a);
      found++;
      if (mpu.begin(a, &Wire)) {
        mpuI2cAddress = a;
        mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
        mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
        calibrateGyro();
        mpuAvailable = true;
        Serial.printf("[MPU6050] Initialized at fallback address 0x%02X!\n", a);
        return;
      }
    }
  }

  mpuAvailable = false;
  Serial.println("[MPU6050] Not detected on I2C bus.");
  if (found == 0) {
    Serial.println("[MPU6050] Check wiring: VCC->5V Booster, GND->GND, SDA->21, SCL->22.");
    Serial.println("[MPU6050] If AD0 is floating or pulled HIGH, address is 0x69. If AD0 is GND, address is 0x68.");
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

  // Integrated Yaw
  float rateZ = g.gyro.z - gyroZBias;
  yawHeading += rateZ * 57.2957795f * dt;
  while (yawHeading <    0.0f) yawHeading += 360.0f;
  while (yawHeading >= 360.0f) yawHeading -= 360.0f;

  // Accelerometer Pitch & Roll
  float ax = a.acceleration.x;
  float ay = a.acceleration.y;
  float az = a.acceleration.z;
  float rawPitch = atan2(ay, sqrt(ax * ax + az * az)) * 57.2957795f;
  float rawRoll  = atan2(-ax, az) * 57.2957795f;

  // Low-pass smooth filter
  pitchAngle = pitchAngle * 0.85f + rawPitch * 0.15f;
  rollAngle  = rollAngle  * 0.85f + rawRoll  * 0.15f;
}

// Non-blocking gyro rotate state
void startNonBlockingRotate(float targetDeg, bool turnRight) {
  int pwm = TURN_SPEED_PWM;
  if (turnRight) {
    targetPwmLeft  =  pwm;
    targetPwmRight = -pwm;
  } else {
    targetPwmLeft  = -pwm;
    targetPwmRight =  pwm;
  }
  rotateState     = ROT_TURNING;
  rotateTarget    = targetDeg;
  rotateTurnRight = turnRight;
  rotateInitYaw   = yawHeading;
  rotateStartMs   = millis();
  motorRunning    = true;
}

void stepNonBlockingRotate() {
  if (rotateState != ROT_TURNING) return;
  lastDriveCmdMs = millis(); // Keep watchdog happy

  bool finished = false;
  if (mpuAvailable) {
    float diff = fabs(yawHeading - rotateInitYaw);
    if (diff > 180.0f) diff = 360.0f - diff;
    if (diff >= rotateTarget || (millis() - rotateStartMs > 5000)) {
      finished = true;
    }
  } else {
    unsigned long estTime = (unsigned long)((rotateTarget / 90.0f) * 450.0f);
    if (millis() - rotateStartMs >= estTime) finished = true;
  }

  if (finished) {
    stopCar();
    rotateState = ROT_IDLE;
    broadcastWsText("{\"type\":\"rotate_done\",\"heading\":" + String(yawHeading, 1) + "}");
  }
}

// ===================================================================================
// 6. AUTONOMOUS COLLISION AVOIDANCE STATE MACHINE
// ===================================================================================
void stepAutoNav() {
  if (manualMode || carStopped) return;
  if (!sonarActive) { stopCar(); return; }

  unsigned long now = millis();
  long frontDist = readUltrasonicCM();

  switch (autoState) {
    case AUTO_FORWARD:
      if (frontDist < OBSTACLE_LIMIT_CM && frontDist > 0) {
        stopCar();
        autoState = AUTO_DETECTED;
        autoActionTimer = now;
      } else {
        motorMix(0.0f, (float)CRUISE_SPEED_PWM / 255.0f);
      }
      break;

    case AUTO_DETECTED:
      motorMix(0.0f, -0.6f); // back up briefly
      autoState = AUTO_REVERSE;
      autoActionTimer = now;
      break;

    case AUTO_REVERSE:
      if (now - autoActionTimer > 250) {
        stopCar();
        autoState = AUTO_SCAN;
        autoActionTimer = now;
        setRadarServo(150);
      }
      break;

    case AUTO_SCAN:
      // Sweep Left (150 deg) then Right (30 deg)
      setRadarServo(150);
      delay(220);
      invalidateSonarCache();
      scanDistLeft = readUltrasonicCM();

      setRadarServo(30);
      delay(240);
      invalidateSonarCache();
      scanDistRight = readUltrasonicCM();

      setRadarServo(90);
      delay(180);
      radarServo.detach();

      if (scanDistLeft > scanDistRight && scanDistLeft > 25) {
        startNonBlockingRotate(60.0f, false);
      } else if (scanDistRight > 25) {
        startNonBlockingRotate(60.0f, true);
      } else {
        startNonBlockingRotate(160.0f, true);
      }
      autoState = AUTO_TURN;
      break;

    case AUTO_TURN:
      if (rotateState == ROT_IDLE) {
        autoState = AUTO_FORWARD;
      }
      break;

    case AUTO_STOPPED:
    default:
      stopCar();
      break;
  }
}

// ===================================================================================
// 7. RFC 6455 WEBSOCKET ENGINE (PORT 81)
// ===================================================================================
String computeWsAccept(const String& clientKey) {
  String combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  unsigned char sha1Result[20];
  mbedtls_sha1_context shaCtx;
  mbedtls_sha1_init(&shaCtx);
  mbedtls_sha1_starts(&shaCtx);
  mbedtls_sha1_update(&shaCtx, (const unsigned char*)combined.c_str(), combined.length());
  mbedtls_sha1_finish(&shaCtx, sha1Result);
  mbedtls_sha1_free(&shaCtx);

  unsigned char base64Buf[40];
  size_t outLen = 0;
  mbedtls_base64_encode(base64Buf, sizeof(base64Buf), &outLen, sha1Result, 20);
  base64Buf[outLen] = '\0';
  return String((char*)base64Buf);
}

void sendWsFrame(WiFiClient& client, const String& text) {
  if (!client.connected()) return;
  size_t len = text.length();
  client.write(0x81); // FIN + text opcode
  if (len <= 125) {
    client.write((uint8_t)len);
  } else if (len <= 65535) {
    client.write(126);
    client.write((uint8_t)(len >> 8));
    client.write((uint8_t)(len & 0xFF));
  }
  client.print(text);
}

void broadcastWsText(const String& payload) {
  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (wsClients[i] && wsClients[i].connected()) {
      sendWsFrame(wsClients[i], payload);
    }
  }
}

void handleWsMessage(WiFiClient& client, const String& msg) {
  // --- DRIVE MOVE ---
  if (msg.indexOf("\"type\":\"move\"") >= 0) {
    if (!manualMode || carStopped) return;
    int dirIdx = msg.indexOf("\"dir\":\"");
    if (dirIdx >= 0) {
      char dir = msg.charAt(dirIdx + 7);
      int spdIdx = msg.indexOf("\"speed\":");
      int spd = (spdIdx >= 0) ? msg.substring(spdIdx + 8).toInt() : CRUISE_SPEED_PWM;
      spd = constrain(spd, 0, 255);
      float spdNorm = (float)spd / 255.0f;

      if (dir == 'F')      motorMix( 0.0f,  spdNorm);
      else if (dir == 'B') motorMix( 0.0f, -spdNorm);
      else if (dir == 'L') motorMix(-spdNorm, 0.0f);
      else if (dir == 'R') motorMix( spdNorm, 0.0f);
      else if (dir == 'S') stopCar();
    }
  }
  // --- JOYSTICK VECTOR ---
  else if (msg.indexOf("\"type\":\"stick\"") >= 0) {
    if (!manualMode || carStopped) return;
    int xIdx = msg.indexOf("\"x\":");
    int yIdx = msg.indexOf("\"y\":");
    if (xIdx >= 0 && yIdx >= 0) {
      float x = msg.substring(xIdx + 4).toFloat();
      float y = msg.substring(yIdx + 4).toFloat();
      motorMix(x, y);
    }
  }
  // --- EMERGENCY STOP ---
  else if (msg.indexOf("\"type\":\"stop\"") >= 0) {
    carStopped  = true;
    autoState   = AUTO_STOPPED;
    rotateState = ROT_IDLE;
    stopCar();
    broadcastWsText("{\"type\":\"stopped\"}");
  }
  // --- RESUME / START ---
  else if (msg.indexOf("\"type\":\"start\"") >= 0) {
    carStopped = false;
    if (!manualMode) autoState = AUTO_FORWARD;
    broadcastWsText("{\"type\":\"started\"}");
  }
  // --- MODE SWITCH (MANUAL / AUTO) ---
  else if (msg.indexOf("\"type\":\"mode\"") >= 0) {
    if (msg.indexOf("\"value\":\"auto\"") >= 0) {
      carStopped = false;
      manualMode = false;
      autoState  = AUTO_FORWARD;
    } else {
      manualMode  = true;
      autoState   = AUTO_STOPPED;
      rotateState = ROT_IDLE;
      stopCar();
    }
    broadcastWsText("{\"type\":\"mode\",\"value\":\"" + String(manualMode ? "manual" : "auto") + "\"}");
  }
  // --- GYRO ROTATE ---
  else if (msg.indexOf("\"type\":\"rotate\"") >= 0) {
    if (!manualMode || carStopped) return;
    if (msg.indexOf("\"dir\":\"left\"")  >= 0) startNonBlockingRotate(90.0f,  false);
    else if (msg.indexOf("\"dir\":\"right\"") >= 0) startNonBlockingRotate(90.0f,  true);
    else if (msg.indexOf("\"dir\":\"360\"")   >= 0) startNonBlockingRotate(360.0f, true);
  }
  // --- GYRO CALIBRATION ---
  else if (msg.indexOf("\"type\":\"calibrate_gyro\"") >= 0 || msg.indexOf("\"type\":\"zero_gyro\"") >= 0) {
    if (mpuAvailable) {
      calibrateGyro();
      broadcastWsText("{\"type\":\"gyro_calibrated\",\"success\":true,\"heading\":0.0,\"pitch\":0.0,\"roll\":0.0}");
    } else {
      broadcastWsText("{\"type\":\"gyro_calibrated\",\"success\":false,\"error\":\"MPU6050 not detected\"}");
    }
  }
  // --- MANUAL SERVO HEAD CONTROLS (< SCAN >) ---
  else if (msg.indexOf("\"type\":\"servo\"") >= 0) {
    int angIdx = msg.indexOf("\"angle\":");
    if (angIdx >= 0) {
      int angle = msg.substring(angIdx + 8).toInt();
      angle = constrain(angle, 0, 180);
      radarState = SCAN_IDLE;
      setRadarServo(angle);
      invalidateSonarCache();
      long dist = readUltrasonicCM();
      broadcastWsText("{\"type\":\"servo\",\"angle\":" + String(angle) + ",\"distance\":" + String(dist) + "}");
    }
  }
  // --- RADAR SCAN SWEEP ---
  else if (msg.indexOf("\"type\":\"scan\"") >= 0) {
    startRadarScan();
  }
  // --- LED PATTERN ---
  else if (msg.indexOf("\"type\":\"led\"") >= 0) {
    if (msg.indexOf("\"pattern\":\"blink\"") >= 0) currentLedEffect = "blink";
    else if (msg.indexOf("\"pattern\":\"warn\"")  >= 0) currentLedEffect = "warn";
    else if (msg.indexOf("\"pattern\":\"pulse\"") >= 0) currentLedEffect = "pulse";
    else currentLedEffect = "off";
    lastLedUpdateMs = millis();
    updateLEDs();
  }
  // --- PING ---
  else if (msg.indexOf("\"type\":\"ping\"") >= 0) {
    sendWsFrame(client, "{\"type\":\"pong\",\"time\":" + String(millis()) + "}");
  }
}

void pollWebSocketServer() {
  if (wsServer.hasClient()) {
    WiFiClient newClient = wsServer.available();
    int slot = -1;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
      if (!wsClients[i] || !wsClients[i].connected()) { slot = i; break; }
    }
    if (slot >= 0) {
      wsClients[slot] = newClient;
    } else {
      newClient.stop();
    }
  }

  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (!wsClients[i] || !wsClients[i].connected()) continue;
    WiFiClient& client = wsClients[i];

    if (client.available()) {
      String req = "";
      unsigned long t0 = millis();
      while (client.available() && (millis() - t0 < 50)) {
        req += (char)client.read();
      }

      if (req.startsWith("GET ") && req.indexOf("Upgrade: websocket") >= 0) {
        int keyIdx = req.indexOf("Sec-WebSocket-Key: ");
        if (keyIdx >= 0) {
          int keyEnd = req.indexOf("\r\n", keyIdx);
          String key = req.substring(keyIdx + 19, keyEnd);
          key.trim();
          String accept = computeWsAccept(key);
          client.print("HTTP/1.1 101 Switching Protocols\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Accept: " + accept + "\r\n\r\n");
        }
      } else if (req.length() >= 2) {
        uint8_t b0 = (uint8_t)req[0];
        uint8_t b1 = (uint8_t)req[1];
        uint8_t opcode = b0 & 0x0F;
        bool masked = (b1 & 0x80) != 0;
        size_t payloadLen = b1 & 0x7F;

        size_t pos = 2;
        if (payloadLen == 126 && req.length() >= 4) {
          payloadLen = ((uint8_t)req[2] << 8) | (uint8_t)req[3];
          pos = 4;
        }

        if (masked && req.length() >= pos + 4 + payloadLen) {
          uint8_t mask[4];
          for (int m = 0; m < 4; m++) mask[m] = (uint8_t)req[pos++];
          String decoded = "";
          decoded.reserve(payloadLen);
          for (size_t p = 0; p < payloadLen; p++) {
            decoded += (char)((uint8_t)req[pos++] ^ mask[p % 4]);
          }
          if (opcode == 0x01) { // Text
            handleWsMessage(client, decoded);
          } else if (opcode == 0x09) { // Ping
            client.write(0x8A);
            client.write((uint8_t)0);
          }
        }
      }
    }
  }
}

// ===================================================================================
// 8. WI-FI STA NETWORK MANAGER & PROFILES
// ===================================================================================
String getWifiProfilesJson() {
  String   json  = "{\"selected\":" + String(activeWifiIndex) + ",\"networks\":[";
  uint8_t  count = nvsPrefs.getUChar("cnt", 0);
  bool     first = true;
  for (uint8_t i = 0; i < count && i < MAX_WIFI_PROFILES; i++) {
    String s = nvsPrefs.getString((String("s") + i).c_str(), "");
    if (s.length() == 0) continue;
    if (!first) json += ",";
    first = false;
    json += "{\"ssid\":\"" + s + "\"}";
  }
  json += "]}";
  return json;
}

void setCorsHeaders() {
  restServer.sendHeader("Access-Control-Allow-Origin", "*");
  restServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  restServer.sendHeader("Access-Control-Allow-Headers", "*");
}

void handleWifiScan() {
  setCorsHeaders();
  if (!wifiScanPending) {
    WiFi.scanNetworks(true, true); // Async scan, include hidden networks
    wifiScanPending   = true;
    wifiScanStartedMs = millis();
    restServer.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) {
    if (millis() - wifiScanStartedMs > 8000) {
      wifiScanPending = false;
      WiFi.scanDelete();
      restServer.send(504, "application/json", "{\"error\":\"scan_timeout\"}");
      return;
    }
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
  pendingStaDelay   = millis() + 200; // Allow HTTP response to flush
  restServer.send(200, "application/json",
    "{\"status\":\"connecting\",\"ssid\":\"" + pendingStaSsid
    + "\",\"host\":\"http://novax.local\"}");
}

void handleSwitchAP() {
  setCorsHeaders();
  restServer.send(200, "application/json", "{\"status\":\"switched\",\"mode\":\"AP\",\"ip\":\"192.168.4.1\"}");
  delay(120);
  pendingStaConnect = false;
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS);
  Serial.printf("[WIFI] Switched to AP mode. IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void processPendingStaConnect() {
  if (!pendingStaConnect || millis() < pendingStaDelay) return;
  pendingStaConnect = false;

  Serial.printf("[WIFI] Connecting to Station SSID: %s\n", pendingStaSsid.c_str());
  WiFi.begin(pendingStaSsid.c_str(), pendingStaPass.c_str());

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) delay(100);

  if (WiFi.status() == WL_CONNECTED) {
    MDNS.end();
    MDNS.begin("novax");
    Serial.printf("[WIFI] STA connected! IP: %s\n", WiFi.localIP().toString().c_str());
    broadcastWsText("{\"type\":\"wifi\",\"mode\":\"STA\",\"ip\":\"" + WiFi.localIP().toString() + "\"}");
  } else {
    Serial.println("[WIFI] STA connect timed out — maintaining AP.");
    broadcastWsText("{\"type\":\"wifi\",\"mode\":\"AP\",\"ip\":\"192.168.4.1\",\"error\":\"sta_failed\"}");
  }
}

// ===================================================================================
// 9. REST API & OTA HANDLERS
// ===================================================================================
bool isOtaAuthorized() {
  if (!restServer.hasHeader("X-NovaX-OTA")) return true; // Open in local AP mode
  return (restServer.header("X-NovaX-OTA") == OTA_DEFAULT_TOKEN);
}

void handleStatus() {
  setCorsHeaders();
  long   d       = readUltrasonicCM();
  String wifiMode = (WiFi.getMode() == WIFI_AP ? "AP" : (WiFi.status() == WL_CONNECTED ? "STA" : "AP_STA"));
  String ip      = (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
  String s = "{";
  s += "\"distance\":"   + String(d)    + ",";
  s += "\"mode\":\""     + String(manualMode ? "manual" : "auto") + "\",";
  s += "\"manual\":"     + String(manualMode ? 1 : 0) + ",";
  s += "\"stopped\":"    + String((carStopped || autoState == AUTO_STOPPED) ? 1 : 0) + ",";
  s += "\"yaw\":"        + String(yawHeading, 1) + ",";
  s += "\"heading\":"    + String(yawHeading, 1) + ",";
  s += "\"pitch\":"      + String(pitchAngle, 1) + ",";
  s += "\"roll\":"       + String(rollAngle, 1)  + ",";
  s += "\"imu\":\""      + String(mpuAvailable ? "MPU6050" : "None") + "\",";
  s += "\"camera\":0,";
  s += "\"wifiMode\":\"" + wifiMode + "\",";
  s += "\"ip\":\""       + ip + "\",";
  s += "\"version\":\""  + String(FIRMWARE_VERSION) + "\"";
  s += "}";
  restServer.send(200, "application/json", s);
}

void handleFirmwareInfo() {
  setCorsHeaders();
  String out = "{";
  out += "\"name\":\"NovaX V2\",";
  out += "\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
  out += "\"hardware\":\"" + String(HARDWARE_VERSION) + "\",";
  out += "\"buildDate\":\"" + String(BUILD_DATE) + "\",";
  out += "\"camera\":\"None\",";
  out += "\"imu\":\"" + String(mpuAvailable ? "MPU6050" : "None") + "\",";
  out += "\"status\":\"online\"";
  out += "}";
  restServer.send(200, "application/json", out);
}

// Embedded Web Flasher
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>NovaX V2 Control & OTA</title>
  <style>
    :root {
      --bg: #0b0f19;
      --card: #131b2e;
      --border: #1f2d4a;
      --cyan: #06b6d4;
      --green: #10b981;
      --text: #f1f5f9;
      --subtext: #94a3b8;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background: var(--bg); color: var(--text); display: flex; align-items: center; justify-content: center; min-height: 100vh; padding: 1rem; }
    .card { background: var(--card); border: 1px solid var(--border); border-radius: 1rem; max-width: 480px; width: 100%; padding: 2rem; box-shadow: 0 10px 30px rgba(0,0,0,0.5); }
    .badge { display: inline-block; padding: 0.25rem 0.75rem; border-radius: 9999px; font-size: 0.75rem; font-weight: 700; text-transform: uppercase; background: rgba(6, 182, 212, 0.15); color: var(--cyan); border: 1px solid var(--cyan); margin-bottom: 0.75rem; }
    h1 { font-size: 1.5rem; font-weight: 800; margin-bottom: 0.5rem; }
    p { font-size: 0.875rem; color: var(--subtext); line-height: 1.5; margin-bottom: 1.5rem; }
    .dropzone { border: 2px dashed var(--border); border-radius: 0.75rem; padding: 1.5rem; text-align: center; margin-bottom: 1.5rem; cursor: pointer; }
    .dropzone:hover { border-color: var(--cyan); }
    input[type="file"] { display: none; }
    .file-label { font-size: 0.875rem; color: var(--cyan); font-weight: 600; }
    .filename { font-size: 0.8rem; color: var(--text); margin-top: 0.5rem; }
    button { width: 100%; padding: 0.875rem; background: linear-gradient(135deg, #06b6d4, #0284c7); color: #fff; border: none; border-radius: 0.75rem; font-size: 0.95rem; font-weight: 700; cursor: pointer; }
    button:disabled { opacity: 0.5; cursor: not-allowed; }
    .progress-box { margin-top: 1.5rem; display: none; }
    .progress-bar-bg { background: rgba(255, 255, 255, 0.1); border-radius: 9999px; height: 10px; overflow: hidden; margin-bottom: 0.5rem; }
    .progress-fill { background: linear-gradient(90deg, #06b6d4, #10b981); width: 0%; height: 100%; }
    .status-text { font-size: 0.8rem; color: var(--subtext); text-align: center; }
    .alert { padding: 1rem; border-radius: 0.75rem; font-size: 0.875rem; margin-top: 1rem; display: none; }
    .alert.success { background: rgba(16, 185, 129, 0.15); border: 1px solid var(--green); color: #6ee7b7; display: block; }
    .nav-links { display: flex; gap: 0.5rem; margin-top: 1.5rem; justify-content: center; }
    .nav-btn { color: var(--cyan); font-size: 0.8rem; text-decoration: none; border: 1px solid var(--border); padding: 0.4rem 0.8rem; border-radius: 0.5rem; }
  </style>
</head>
<body>
  <div class="card">
    <span class="badge">NovaX V2 • Pure Car Edition</span>
    <h1>Wireless OTA Flasher</h1>
    <p>Upload a new <b>NovaX-Firmware.bin</b> over Wi-Fi without needing a USB cable.</p>

    <div class="dropzone" onclick="document.getElementById('fwInput').click()">
      <div style="font-size: 2rem; margin-bottom: 0.5rem;">⚡</div>
      <div class="file-label">Choose Firmware Binary (.bin)</div>
      <div class="filename" id="fileChosen">No file chosen</div>
      <input type="file" id="fwInput" accept=".bin">
    </div>

    <button id="flashBtn" disabled onclick="uploadFirmware()">Flash Firmware Now</button>

    <div class="progress-box" id="progressBox">
      <div class="progress-bar-bg">
        <div class="progress-fill" id="progressFill"></div>
      </div>
      <div class="status-text" id="statusText">0% Uploaded</div>
    </div>

    <div class="alert" id="alertBox"></div>

    <div class="nav-links">
      <a href="/status" class="nav-btn" target="_blank">📊 Status JSON</a>
      <a href="/calibrate_gyro" class="nav-btn" target="_blank">⚖️ Calibrate Gyro</a>
      <a href="/firmware" class="nav-btn" target="_blank">ℹ️ Firmware</a>
    </div>
  </div>

  <script>
    const fwInput = document.getElementById('fwInput');
    const fileChosen = document.getElementById('fileChosen');
    const flashBtn = document.getElementById('flashBtn');
    const progressBox = document.getElementById('progressBox');
    const progressFill = document.getElementById('progressFill');
    const statusText = document.getElementById('statusText');
    const alertBox = document.getElementById('alertBox');

    fwInput.onchange = () => {
      if (fwInput.files.length > 0) {
        fileChosen.textContent = fwInput.files[0].name;
        flashBtn.disabled = false;
      }
    };

    function uploadFirmware() {
      if (!fwInput.files.length) return;
      flashBtn.disabled = true;
      progressBox.style.display = 'block';

      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/update', true);
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          const percent = Math.round((e.loaded / e.total) * 100);
          progressFill.style.width = percent + '%';
          statusText.textContent = `Uploading: ${percent}%`;
        }
      };
      xhr.onload = () => {
        if (xhr.status === 200) {
          progressFill.style.width = '100%';
          statusText.textContent = 'Complete! Rebooting...';
          alertBox.className = 'alert success';
          alertBox.innerHTML = '<b>Flash Success!</b> Reconnecting in 10s.';
        }
      };
      const fd = new FormData();
      fd.append('firmware', fwInput.files[0]);
      xhr.send(fd);
    }
  </script>
</body>
</html>
)rawliteral";

void handleOtaUpload() {
  HTTPUpload& upload = restServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaAuthorised = isOtaAuthorized();
    if (!otaAuthorised) return;
    stopCar();
    carStopped = true;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaAuthorised && Update.isRunning()) Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (otaAuthorised && Update.end(true)) Serial.println("[OTA] Success!");
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

void handleOtaFinish() {
  setCorsHeaders();
  if (Update.hasError()) { restServer.send(500, "text/plain", "Flash Failed"); return; }
  restServer.send(200, "application/json", "{\"status\":\"ok\",\"restarting\":true}");
  delay(500);
  ESP.restart();
}

// ===================================================================================
// 10. SETUP & ROUTES
// ===================================================================================
void setup() {
  // Disable aggressive brownout detector to prevent motor inrush resets
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(200);
  Serial.println("\n[NovaX V2 - Pure Car Edition] Initializing...");

  initPins();
  initMPU();

  nvsPrefs.begin("wifi", false);
  activeWifiIndex = nvsPrefs.getInt("sel", -1);

  // Use AP_STA mode so the car AP remains accessible while scanning or connecting to home Wi-Fi
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname("novax");
  WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS);
  MDNS.begin("novax");

  Serial.printf("[WIFI] AP Mode Ready | SSID: %s | IP: %s\n",
                AP_DEFAULT_SSID, WiFi.softAPIP().toString().c_str());

  // Web Flasher & Status Routes
  restServer.on("/", HTTP_GET, []() {
    setCorsHeaders();
    restServer.send_P(200, "text/html", INDEX_HTML);
  });
  restServer.on("/update", HTTP_GET, []() {
    setCorsHeaders();
    restServer.send_P(200, "text/html", INDEX_HTML);
  });
  restServer.on("/firmware",   HTTP_GET,  handleFirmwareInfo);
  restServer.on("/status",     HTTP_GET,  handleStatus);
  restServer.on("/ota/status", HTTP_GET,  handleFirmwareInfo);
  restServer.on("/ota/update", HTTP_POST, handleOtaFinish, handleOtaUpload);
  restServer.on("/update",     HTTP_POST, handleOtaFinish, handleOtaUpload);

  // Wi-Fi Network Management Endpoints (Used by Cockpit Network Manager)
  restServer.on("/wifi/scan",      HTTP_GET,  handleWifiScan);
  restServer.on("/wifi/saved",     HTTP_GET,  handleWifiSaved);
  restServer.on("/wifi/save",      HTTP_POST, handleWifiSave);
  restServer.on("/wifi/save",      HTTP_GET,  handleWifiSave);
  restServer.on("/wifi/select",    HTTP_POST, handleWifiSelect);
  restServer.on("/wifi/select",    HTTP_GET,  handleWifiSelect);
  restServer.on("/wifi/delete",    HTTP_POST, handleWifiDelete);
  restServer.on("/wifi/delete",    HTTP_GET,  handleWifiDelete);
  restServer.on("/wifi/switchSta", HTTP_POST, handleSwitchSTA);
  restServer.on("/wifi/switchSta", HTTP_GET,  handleSwitchSTA);
  restServer.on("/wifi/switchAp",  HTTP_POST, handleSwitchAP);
  restServer.on("/wifi/switchAp",  HTTP_GET,  handleSwitchAP);

  // Gyro Calibration (REST)
  restServer.on("/calibrate_gyro", HTTP_POST, []() {
    setCorsHeaders();
    if (mpuAvailable) {
      calibrateGyro();
      restServer.send(200, "application/json", "{\"status\":\"ok\",\"calibrated\":true,\"heading\":0.0,\"pitch\":0.0,\"roll\":0.0}");
    } else {
      restServer.send(503, "application/json", "{\"status\":\"error\",\"message\":\"MPU6050 not detected\"}");
    }
  });
  restServer.on("/calibrate_gyro", HTTP_GET, []() {
    setCorsHeaders();
    if (mpuAvailable) {
      calibrateGyro();
      restServer.send(200, "application/json", "{\"status\":\"ok\",\"calibrated\":true,\"heading\":0.0,\"pitch\":0.0,\"roll\":0.0}");
    } else {
      restServer.send(503, "application/json", "{\"status\":\"error\",\"message\":\"MPU6050 not detected\"}");
    }
  });

  // Servo Head Control (REST)
  restServer.on("/servo", HTTP_GET, []() {
    setCorsHeaders();
    if (!restServer.hasArg("angle")) { restServer.send(400, "text/plain", "Missing angle"); return; }
    int angle = constrain(restServer.arg("angle").toInt(), 0, 180);
    radarState = SCAN_IDLE;
    setRadarServo(angle);
    invalidateSonarCache();
    long dist = readUltrasonicCM();
    restServer.send(200, "application/json",
      "{\"status\":\"ok\",\"angle\":" + String(angle) + ",\"distance\":" + String(dist) + "}");
  });

  // Radar Sweeper Trigger & Results (REST)
  restServer.on("/scan", HTTP_GET, []() {
    setCorsHeaders();
    startRadarScan();
    restServer.send(200, "application/json", "{\"status\":\"scanning\"}");
  });
  restServer.on("/scan", HTTP_POST, []() {
    setCorsHeaders();
    startRadarScan();
    restServer.send(200, "application/json", "{\"status\":\"scanning\"}");
  });
  restServer.on("/scanResult", HTTP_GET, []() {
    setCorsHeaders();
    String res = "{\"status\":\""
      + String(radarState == SCAN_DONE ? "done" : radarState == SCAN_RUNNING ? "scanning" : "idle") + "\"";
    res += ",\"left\":"  + String(scanDistLeft);
    res += ",\"front\":" + String(scanDistFront);
    res += ",\"right\":" + String(scanDistRight);
    res += "}";
    restServer.send(200, "application/json", res);
  });

  // Basic Driving Control (REST)
  restServer.on("/stop", HTTP_POST, []() {
    setCorsHeaders();
    carStopped = true;
    stopCar();
    restServer.send(200, "application/json", "{\"status\":\"stopped\"}");
  });
  restServer.on("/start", HTTP_POST, []() {
    setCorsHeaders();
    carStopped = false;
    if (!manualMode) autoState = AUTO_FORWARD;
    restServer.send(200, "application/json", "{\"status\":\"started\"}");
  });
  restServer.on("/mode", HTTP_GET, []() {
    setCorsHeaders();
    if (restServer.hasArg("val")) {
      manualMode = (restServer.arg("val") != "auto");
      autoState  = manualMode ? AUTO_STOPPED : AUTO_FORWARD;
    } else if (restServer.hasArg("set")) {
      manualMode = (restServer.arg("set") != "auto");
      autoState  = manualMode ? AUTO_STOPPED : AUTO_FORWARD;
    }
    restServer.send(200, "application/json", "{\"mode\":\"" + String(manualMode ? "manual" : "auto") + "\"}");
  });
  restServer.on("/move", HTTP_GET, []() {
    setCorsHeaders();
    if (!manualMode || carStopped) { restServer.send(200, "application/json", "{\"status\":\"blocked\"}"); return; }
    if (!restServer.hasArg("d")) { restServer.send(400, "text/plain", "Missing d"); return; }
    char dir = restServer.arg("d").charAt(0);
    int  spd = restServer.hasArg("speed") ? restServer.arg("speed").toInt() : CRUISE_SPEED_PWM;
    float spdNorm = (float)spd / 255.0f;
    if      (dir == 'F') motorMix( 0.0f,  spdNorm);
    else if (dir == 'B') motorMix( 0.0f, -spdNorm);
    else if (dir == 'L') motorMix(-spdNorm, 0.0f);
    else if (dir == 'R') motorMix( spdNorm, 0.0f);
    else if (dir == 'S') stopCar();
    restServer.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // CORS Preflight Options handler
  restServer.onNotFound([]() {
    if (restServer.method() == HTTP_OPTIONS) {
      setCorsHeaders();
      restServer.send(204);
    } else {
      restServer.send(404, "text/plain", "Not found");
    }
  });

  const char* headerKeys[] = {"X-NovaX-OTA"};
  restServer.collectHeaders(headerKeys, 1);
  restServer.begin();

  wsServer.begin();
  wsServer.setNoDelay(true);

  // Centre radar servo at boot
  setRadarServo(90);
  delay(200);
  radarServo.detach();

  manualMode  = true;
  carStopped  = false;
  autoState   = AUTO_STOPPED;
  stopCar();

  Serial.printf("[NovaX V2] Ready | FW %s | IMU: %s (0x%02X)\n",
    FIRMWARE_VERSION,
    mpuAvailable ? "MPU6050" : "None",
    mpuI2cAddress);
}

// ===================================================================================
// 11. MAIN LOOP
// ===================================================================================
void loop() {
  restServer.handleClient();
  pollWebSocketServer();
  processPendingStaConnect();
  updateGyroHeading();
  stepNonBlockingRotate();
  stepMotorRamp();
  stepRadarScan();
  updateLEDs();

  if (!manualMode) {
    stepAutoNav();
  }

  // Motor Safety Watchdog (400ms)
  if (manualMode && motorRunning && rotateState == ROT_IDLE
      && (millis() - lastDriveCmdMs > MOTOR_WATCHDOG_MS)) {
    stopCar();
  }

  // Periodic Telemetry Stream (100ms)
  static unsigned long lastTelems = 0;
  if (millis() - lastTelems >= 100) {
    lastTelems = millis();
    long   d        = readUltrasonicCM();
    String wifiMode = (WiFi.status() == WL_CONNECTED ? "STA" : "AP");
    String ip       = (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
    String telem = "{\"type\":\"telemetry\",";
    telem += "\"distance\":"   + String(d)              + ",";
    telem += "\"heading\":"    + String(yawHeading, 1)  + ",";
    telem += "\"pitch\":"      + String(pitchAngle, 1)  + ",";
    telem += "\"roll\":"       + String(rollAngle, 1)   + ",";
    telem += "\"mode\":\""     + String(manualMode ? "manual" : "auto") + "\",";
    telem += "\"stopped\":"    + String(carStopped ? "true" : "false") + ",";
    telem += "\"rotating\":"   + String(rotateState == ROT_TURNING ? "true" : "false") + ",";
    telem += "\"version\":\""  + String(FIRMWARE_VERSION)  + "\",";
    telem += "\"camera\":false,";
    telem += "\"wifiMode\":\"" + wifiMode + "\",";
    telem += "\"ip\":\""       + ip       + "\",";
    telem += "\"uptime\":"     + String(millis() / 1000);
    telem += "}";
    broadcastWsText(telem);
  }

  delay(2);
}
