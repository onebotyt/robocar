/*
 * ===================================================================================
 *  NovaX V3 (ESP32-WROOM-32 Edition) - Autonomous & Remote Robot Car Firmware
 *  Target Hardware: ESP32 Dev Module / ESP32-WROOM-32 (NO PSRAM)
 *  Subsystems:
 *    1. OV7670 Camera (FIFO-less I2S Direct Capture -> ESP32 JPEG 160x120 Q90 -> TCP :5000)
 *    2. MPU6050 6-DOF IMU (Attitude, Gyro Yaw Integration, Shared I2C : GPIO21/22)
 *    3. MX1508 Dual H-Bridge Motor Driver (GPIO 16, 17, 18, 19 with Soft-Start Slew)
 *    4. HC-SR04 Ultrasonic Distance Sensor (TRIG=23, ECHO=27 via 1k/2k Divider)
 *    5. SG90 Micro Servo (Radar Scanning on GPIO4 via ESP32Servo)
 *    6. 74HC595 8-Bit Shift Register (LED Effects on GPIO 0, 2, 12)
 *    7. Networking: AP (NovaX-Car) + STA + HTTP (:80) + WebSocket (:81) + Web OTA
 * ===================================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#define sensor_t adafruit_sensor_t
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#undef sensor_t
#include <ESP32Servo.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>
#include <pgmspace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "img_converters.h"
#include "esp_log.h"
#include "esp32-hal-ledc.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "soc/soc.h"
#include "soc/gpio_sig_map.h"
#include "soc/i2s_reg.h"
#include "soc/i2s_struct.h"
#include "soc/io_mux_reg.h"
#include "rom/lldesc.h"
#include "esp_intr_alloc.h"
#include "driver/periph_ctrl.h"
#include "esp_heap_caps.h"

// ===================================================================================
// 1. PIN CONFIGURATION & HARDWARE MATRIX (ESP32-WROOM-32)
// ===================================================================================

// --- OV7670 Camera (FIFO-less I2S Direct Capture) ---
#define NVX_CAM_D0      36   // GPIO36 (VP, input only)
#define NVX_CAM_D1      39   // GPIO39 (VN, input only)
#define NVX_CAM_D2      34   // GPIO34 (input only)
#define NVX_CAM_D3      35   // GPIO35 (input only)
#define NVX_CAM_D4      32   // GPIO32
#define NVX_CAM_D5      33   // GPIO33
#define NVX_CAM_D6      25   // GPIO25
#define NVX_CAM_D7      26   // GPIO26
#define NVX_CAM_XCLK    15   // GPIO15 (10 MHz PWM via LEDC Channel 0)
#define NVX_CAM_PCLK    14   // GPIO14 (Pixel Clock to I2S0)
#define NVX_CAM_VSYNC   13   // GPIO13 (Frame VSYNC to I2S0 & ISR)
// Note: HREF is physically DISCONNECTED / NOT USED (routed internally to 0x38 logic-HIGH)
#define NVX_CAM_SIOD    21   // GPIO21 (Shared I2C SDA with MPU6050, 4.7k pull-up to 3.3V)
#define NVX_CAM_SIOC    22   // GPIO22 (Shared I2C SCL with MPU6050, 4.7k pull-up to 3.3V)

// Camera Frame Dimensions & Output
#define NVX_FRAME_W     160
#define NVX_FRAME_H     120
#define NVX_FRAME_BPP   2    // RGB565 (2 bytes per pixel)
#define NVX_FRAME_BYTES ((size_t)NVX_FRAME_W * NVX_FRAME_H * NVX_FRAME_BPP) // 38,400 bytes

// --- MX1508 Dual H-Bridge Motor Driver ---
#define MOTOR_IN1       16   // GPIO16 (Left Motor Forward, LEDC Channel 2)
#define MOTOR_IN2       17   // GPIO17 (Left Motor Reverse, LEDC Channel 3) - HREF reused safely!
#define MOTOR_IN3       18   // GPIO18 (Right Motor Forward, LEDC Channel 4)
#define MOTOR_IN4       19   // GPIO19 (Right Motor Reverse, LEDC Channel 5)

// --- HC-SR04 Ultrasonic Distance Sensor ---
#define TRIG_PIN        23   // GPIO23 (Sonar Trigger Pulse Output)
#define ECHO_PIN        27   // GPIO27 (Sonar Echo Input - uses 1k/2k divider from 5V echo!)

// --- SG90 Radar Micro Servo ---
#define SERVO_PIN        4   // GPIO4  (ESP32Servo PWM Output)

// --- 74HC595 8-Bit Shift Register (LED Lighting & Effects) ---
#define LED_DATA         0   // GPIO0  (SER / Serial Data Input)
#define LED_CLOCK        2   // GPIO2  (SRCLK / Shift Register Clock)
#define LED_LATCH       12   // GPIO12 (RCLK / Storage Register Latch)
// 74HC595: VCC=3.3V, GND=GND, OE=GND, MR=3.3V

// --- MPU6050 6-DOF IMU ---
#define MPU_SDA         21   // Shared I2C Data
#define MPU_SCL         22   // Shared I2C Clock
#define MPU_ADDR        0x68 // I2C Address (AD0 connected to GND)
#define GYRO_REVERSE_Z  false // Set to true if yaw rotation direction is inverted

#define SERIAL_ENABLED   1

#if SERIAL_ENABLED
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

// ===================================================================================
// 2. CONSTANTS & SYSTEM CONFIGURATION
// ===================================================================================
const char* FIRMWARE_VERSION  = "3.0.0-ESP32";
const char* HARDWARE_VERSION  = "ESP32-WROOM-V3-CAM";
const char* BUILD_DATE        = "2026-09-29";
const char* AP_DEFAULT_SSID   = "NovaX-Car";
const char* AP_DEFAULT_PASS   = "12345678";
const char* OTA_DEFAULT_TOKEN = "NovaX-OTA-ChangeMe";

// Camera TCP Stream Config
const uint16_t CAM_TCP_PORT   = 5000;
const uint8_t  JPEG_QUALITY   = 90;
#define NVX_JPEG_TXBUF_BYTES  4096

// Motor Safety Watchdog (ms)
const unsigned long MOTOR_WATCHDOG_MS = 400;

// Autonomous Navigation Tuning
const int OBSTACLE_LIMIT_CM = 50;
const int CRUISE_SPEED_PWM  = 175;
const int TURN_SPEED_PWM    = 210;

// Sonar cache TTL (ms)
const unsigned long SONAR_CACHE_MS = 80;

// WebSocket limits
const size_t WS_MAX_PAYLOAD = 4096;
const uint8_t MAX_WS_CLIENTS = 4;

// LEDC Motor Channels
const int MOTOR_PWM_FREQ = 1000;
const int MOTOR_PWM_RES  = 8;
const int LEDC_CH_IN1    = 2;
const int LEDC_CH_IN2    = 3;
const int LEDC_CH_IN3    = 4;
const int LEDC_CH_IN4    = 5;

// ===================================================================================
// 3. GLOBAL INSTANCES & SYSTEM STATE
// ===================================================================================
WebServer        restServer(80);
WiFiServer       wsServer(81);
WiFiClient       wsClients[MAX_WS_CLIENTS];
WiFiServer       cameraTcpServer(CAM_TCP_PORT);
WiFiClient       cameraTcpClient;
Servo            radarServo;
Adafruit_MPU6050 mpu;
Preferences      nvsPrefs;

// Camera State
bool cameraAvailable       = false;
static uint8_t* s_jpegInput = nullptr;
static uint8_t  s_jpegTxBuf[NVX_JPEG_TXBUF_BYTES];
static size_t   s_jpegTxUsed = 0;
static size_t   s_jpegBytesSent = 0;
static bool     s_jpegCallbackFailed = false;
static uint32_t cameraFrameId = 0;
static uint32_t cameraFramesSent = 0;
static uint32_t cameraFpsStart = 0;

// Motor Safety & Soft-Start Slew State
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
float         gyroZBias      = 0.0f;
float         yawHeading     = 0.0f;
float         pitchAngle     = 0.0f;
float         rollAngle      = 0.0f;
unsigned long lastGyroMicros = 0;
// Raw IMU sensor values for 500ms diagnostics
float         rawAX          = 0.0f;
float         rawAY          = 0.0f;
float         rawAZ          = 0.0f;
float         rawGX          = 0.0f;
float         rawGY          = 0.0f;
float         rawGZ          = 0.0f;

// 74HC595 LED State
String        currentLedEffect = "off";
unsigned long lastLedUpdateMs  = 0;
uint8_t       currentLedMask   = 0x00;

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

// Non-blocking Gyro Rotate State
enum RotateState { ROT_IDLE, ROT_TURNING };
RotateState   rotateState     = ROT_IDLE;
float         rotateTarget    = 0.0f;
bool          rotateTurnRight = true;
float         rotateInitYaw   = 0.0f;
float         rotatePrevYaw   = 0.0f;
float         rotateAccumYaw  = 0.0f;
unsigned long rotateStartMs   = 0;

// Wi-Fi / NVS State
const int MAX_SAVED_NETS = 3;
struct SavedNetwork {
  char ssid[33];
  char pass[65];
  uint8_t valid;
};
SavedNetwork savedNets[MAX_SAVED_NETS];
int selectedNetIdx = 0;
bool otaAuthorised = false;

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
void write595(uint8_t val);
void stepAutoNav();
bool scanI2CBus();
void initMPU();
void updateGyroHeading();
void calibrateGyro();
void stepNonBlockingRotate();
void pollWebSocketServer();
bool sendJPEGFrame();
void stepCameraService();

// ===================================================================================
// 4. LOW-LEVEL OV7670 I2S/DMA CAMERA DRIVER
// ===================================================================================

#define OV7670_ADDR 0x21

// OV7670 Registers
#define REG_GAIN     0x00
#define REG_BLUE     0x01
#define REG_RED      0x02
#define REG_VREF     0x03
#define REG_COM1     0x04
#define REG_BAVE     0x05
#define REG_GbAVE    0x06
#define REG_AECHH    0x07
#define REG_RAVE     0x08
#define REG_COM2     0x09
#define  COM2_OUT_DRIVE_4x  0x03
#define REG_PID      0x0A
#define REG_VER      0x0B
#define REG_COM3     0x0C
#define  COM3_DCWEN         0x04
#define REG_COM4     0x0D
#define  COM4_AEC_FULL      0x00
#define REG_COM5     0x0E
#define REG_COM6     0x0F
#define REG_AECH     0x10
#define REG_CLKRC    0x11
#define  CLK_RSVD           0x80
#define REG_COM7     0x12
#define  COM7_RESET         0x80
#define  COM7_FMT_QVGA      0x10
#define  COM7_RGB           0x04
#define REG_COM8     0x13
#define  COM8_FASTAEC       0x80
#define  COM8_AECSTEP       0x40
#define  COM8_BFILT         0x20
#define  COM8_RSVD          0x08
#define  COM8_AGC           0x04
#define  COM8_AWB           0x02
#define  COM8_AEC           0x01
#define REG_COM9     0x14
#define  COM9_AGC_GAIN_8x   0x20
#define  COM9_AGC_GAIN_16x  0x30
#define REG_COM10    0x15
#define  COM10_PCLK_HB      0x20
#define  COM10_VS_NEG       0x02
#define REG_HSTART   0x17
#define REG_HSTOP    0x18
#define REG_VSTART   0x19
#define REG_VSTOP    0x1A
#define REG_PSHFT    0x1B
#define REG_MIDH     0x1C
#define REG_MIDL     0x1D
#define REG_MVFP     0x1E
#define REG_ADCCTR0  0x20
#define REG_AEW      0x24
#define REG_AEB      0x25
#define REG_VPT      0x26
#define REG_BBIAS    0x27
#define REG_GbBIAS   0x28
#define REG_EXHCH    0x2A
#define REG_EXHCL    0x2B
#define REG_RBIAS    0x2C
#define REG_ADVFL    0x2D
#define REG_ADVFH    0x2E
#define REG_YAVE     0x2F
#define REG_HSYST    0x30
#define REG_HSYEN    0x31
#define REG_HREF     0x32
#define REG_CHLF     0x33
#define REG_ARBLM    0x34
#define REG_ADC      0x37
#define REG_ACOM     0x38
#define REG_OFON     0x39
#define REG_TSLB     0x3A
#define REG_COM11    0x3B
#define  COM11_FR_BY_4      0x40
#define  COM11_EXP          0x02
#define REG_COM12    0x3C
#define REG_COM13    0x3D
#define  COM13_GAMMA        0x80
#define  COM13_UVSAT        0x40
#define REG_COM14    0x3E
#define  COM14_DCWEN        0x10
#define  COM14_MANUAL       0x08
#define  COM14_PCLKDIV_2    0x01
#define REG_EDGE     0x3F
#define REG_COM15    0x40
#define  COM15_R00FF        0xC0
#define  COM15_RGB565       0x10
#define REG_COM16    0x41
#define  COM16_YUV_ENHANC   0x08
#define  COM16_DE_NOISE     0x10
#define  COM16_AWBGAIN      0x02
#define REG_COM17    0x42
#define REG_REG76    0x76
#define REG_DNSTH    0x4C
#define REG_MTX1     0x4F
#define REG_MTX2     0x50
#define REG_MTX3     0x51
#define REG_MTX4     0x52
#define REG_MTX5     0x53
#define REG_MTX6     0x54
#define REG_MTXS     0x58
#define REG_CONTRAS  0x56
#define REG_SATCTR   0xC9
#define REG_BRIGHT   0x55
#define REG_NT_CTRL  0x89
#define REG_SCALING_XSC      0x70
#define REG_SCALING_YSC      0x71
#define REG_SCALING_DCWCTR   0x72
#define  SCALING_DCWCTR_VDS_by_2 0x01
#define  SCALING_DCWCTR_HDS_by_2 0x10
#define REG_SCALING_PCLK_DIV 0x73
#define  SCALING_PCLK_DIV_RSVD   0xF0
#define  SCALING_PCLK_DIV_2      0x01
#define REG_SCALING_PCLK_DELAY 0xA2
#define REG_DBLV     0x6B
#define  DBLV_CLK_x4         0x40
#define REG_RGB444   0x8C
#define  R444_DISABLE        0x00
#define REG_SLOP     0x7A
#define REG_GAM1     0x7B
#define REG_GAM2     0x7C
#define REG_GAM3     0x7D
#define REG_GAM4     0x7E
#define REG_GAM5     0x7F
#define REG_GAM6     0x80
#define REG_GAM7     0x81
#define REG_GAM8     0x82
#define REG_GAM9     0x83
#define REG_GAM10    0x84
#define REG_GAM11    0x85
#define REG_GAM12    0x86
#define REG_GAM13    0x87
#define REG_GAM14    0x88
#define REG_GAM15    0x89
#define REG_BD50MAX  0xA5

struct regval_list {
  uint8_t reg_num;
  uint8_t value;
};

static const struct regval_list qqvga_OV7670[] PROGMEM = {
  {REG_COM3, COM3_DCWEN},
  {REG_COM14, COM14_DCWEN | COM14_PCLKDIV_2},
  {REG_SCALING_XSC, 0x3a},
  {REG_SCALING_YSC, 0x35},
  {REG_SCALING_DCWCTR, 0x22},
  {REG_SCALING_PCLK_DIV, 0xf2},
  {REG_SCALING_PCLK_DELAY, 0x02},
  {0xff, 0xff}
};

static const struct regval_list rgb565_OV7670[] PROGMEM = {
  {REG_RGB444, 0},
  {REG_COM1, 0x0},
  {REG_COM15, COM15_R00FF | COM15_RGB565},
  {REG_TSLB, 0x04},
  {REG_COM9, COM9_AGC_GAIN_16x | 0x08},
  {REG_MTX1, 0xb3},
  {REG_MTX2, 0xb3},
  {REG_MTX3, 0},
  {REG_MTX4, 0x3d},
  {REG_MTX5, 0xa7},
  {REG_MTX6, 0xe4},
  {REG_COM13, COM13_GAMMA | COM13_UVSAT},
  {0xff, 0xff}
};

static const struct regval_list OV7670_default2_regs[] PROGMEM = {
  {REG_TSLB, 0x04},
  {REG_COM15, COM15_R00FF | COM15_RGB565},
  {REG_COM7, COM7_FMT_QVGA | COM7_RGB},
  {REG_HREF, 0x80},
  {REG_HSTART, 0x16},
  {REG_HSTOP, 0x04},
  {REG_VSTART, 0x02},
  {REG_VSTOP, 0x7b},
  {REG_VREF, 0x06},
  {REG_COM3, COM3_DCWEN},
  {REG_COM14, COM14_DCWEN | COM14_MANUAL | COM14_PCLKDIV_2},
  {REG_SCALING_XSC, 0x3a},
  {REG_SCALING_YSC, 0x35},
  {REG_SCALING_DCWCTR, SCALING_DCWCTR_VDS_by_2 | SCALING_DCWCTR_HDS_by_2},
  {REG_SCALING_PCLK_DIV, SCALING_PCLK_DIV_RSVD | SCALING_PCLK_DIV_2},
  {REG_SCALING_PCLK_DELAY, 0x02},
  {REG_CLKRC, CLK_RSVD | 0x01},
  {REG_SLOP, 0x20},
  {REG_GAM1, 0x1c},
  {REG_GAM2, 0x28},
  {REG_GAM3, 0x3c},
  {REG_GAM4, 0x55},
  {REG_GAM5, 0x68},
  {REG_GAM6, 0x76},
  {REG_GAM7, 0x80},
  {REG_GAM8, 0x88},
  {REG_GAM9, 0x8f},
  {REG_GAM10, 0x96},
  {REG_GAM11, 0xa3},
  {REG_GAM12, 0xaf},
  {REG_GAM13, 0xc4},
  {REG_GAM14, 0xd7},
  {REG_GAM15, 0xe8},
  {REG_COM8, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT},
  {REG_GAIN, 0x00},
  {REG_AECH, 0x00},
  {REG_COM4, COM4_AEC_FULL},
  {REG_COM9, COM9_AGC_GAIN_8x | 0x08},
  {REG_BD50MAX, 0x05},
  {0xff, 0xff}
};

// I2S/DMA Engine State
static const size_t s_buf_line_width = (size_t)NVX_FRAME_W * NVX_FRAME_BPP;
static const size_t s_buf_height     = NVX_FRAME_H;
static lldesc_t s_dma_desc[2];
static uint32_t* s_dma_buf[2] = {nullptr, nullptr};
static uint8_t*  s_fb[2]      = {nullptr, nullptr};
static volatile int s_fb_idx = 0;
static intr_handle_t s_i2s_intr_handle = nullptr;
static SemaphoreHandle_t s_data_ready = nullptr;
static SemaphoreHandle_t s_line_ready = nullptr;
static SemaphoreHandle_t s_vsync_catch = nullptr;
static volatile int s_cur_buffer = 0;
static volatile uint16_t s_line_count = 0;
static volatile bool s_i2s_running = false;
static volatile bool s_vsync_check = false;
static bool s_cam_initialized = false;

static void IRAM_ATTR VSYNC_isr(void* arg) {
  GPIO.status1_w1tc.val = (1 << (NVX_CAM_VSYNC - 32));
  if (s_vsync_check) {
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_vsync_catch, &hp);
    if (hp) portYIELD_FROM_ISR();
  }
}

static inline void i2s_conf_reset() {
  const uint32_t flags =
      I2S_RX_RESET_M |
      I2S_RX_FIFO_RESET_M |
      I2S_TX_RESET_M |
      I2S_TX_FIFO_RESET_M;

  I2S0.conf.val |= flags;
  I2S0.conf.val &= ~flags;

  while (I2S0.state.rx_fifo_reset_back) {
    ;
  }
}

static void i2s_readStart(int buf_idx) {
  i2s_conf_reset();
  I2S0.rx_eof_num = s_buf_line_width / 2;
  I2S0.in_link.addr = (uint32_t)&s_dma_desc[buf_idx];
  I2S0.in_link.start = 1;
  I2S0.int_clr.val = I2S0.int_raw.val;
  I2S0.int_ena.in_done = 1;
  esp_intr_enable(s_i2s_intr_handle);
  I2S0.conf.rx_start = 1;
}

static void i2s_stop() {
  esp_intr_disable(s_i2s_intr_handle);
  i2s_conf_reset();
  I2S0.conf.rx_start = 0;
  I2S0.in_link.stop = 1;
  I2S0.int_clr.val = I2S0.int_raw.val;
  I2S0.int_ena.val = 0;
  s_i2s_running = false;
}

static void line_filter_task(void *pvParameters) {
  for (;;) {
    xSemaphoreTake(s_data_ready, portMAX_DELAY);
    const int buf_idx = !s_cur_buffer;
    s_fb_idx = (s_fb_idx + 1) & 1;
    uint8_t* dst = s_fb[s_fb_idx];
    const uint32_t* src = s_dma_buf[buf_idx];
    for (int i = 0; i < s_buf_line_width / 2; ++i) {
      uint32_t v = src[i];
      dst[i * 2 + 0] = (uint8_t)(v & 0x000000FF);
      dst[i * 2 + 1] = (uint8_t)((v & 0x00FF0000) >> 16);
    }
    xSemaphoreGive(s_line_ready);
  }
}

static void IRAM_ATTR i2s_isr(void* arg) {
  I2S0.int_clr.val = I2S0.int_raw.val;
  s_cur_buffer = !s_cur_buffer;
  ++s_line_count;
  if (s_line_count >= s_buf_height) {
    i2s_stop();
  } else {
    i2s_readStart(s_cur_buffer);
  }
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(s_data_ready, &hp);
  if (hp) portYIELD_FROM_ISR();
}

static bool waitForVsyncStart(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (digitalRead(NVX_CAM_VSYNC) == LOW) {
    if (millis() - start > timeoutMs) return false;
    delayMicroseconds(50);
  }
  start = millis();
  while (digitalRead(NVX_CAM_VSYNC) == HIGH) {
    if (millis() - start > timeoutMs) return false;
    delayMicroseconds(50);
  }
  start = millis();
  while (digitalRead(NVX_CAM_VSYNC) == LOW) {
    if (millis() - start > timeoutMs) return false;
    delayMicroseconds(50);
  }
  return true;
}

static void i2s_frameReadStart() {
  while (xSemaphoreTake(s_vsync_catch, 0) == pdTRUE) {}
  while (xSemaphoreTake(s_line_ready, 0) == pdTRUE) {}
  while (xSemaphoreTake(s_data_ready, 0) == pdTRUE) {}

  if (!waitForVsyncStart(2000)) {
    s_vsync_check = false;
    DEBUG_PRINTF("[CAM] FRAME START TIMEOUT: VSYNC=%d\n", digitalRead(NVX_CAM_VSYNC));
    return;
  }
  s_vsync_check = false;
  s_cur_buffer = 0;
  s_line_count = 0;
  s_i2s_running = true;
  i2s_readStart(0);
}

static uint16_t* camera_getLine(uint16_t lineno) {
  if (!s_cam_initialized) return nullptr;
  const uint32_t start = millis();
  do {
    if (!s_i2s_running) {
      s_vsync_check = true;
      i2s_frameReadStart();
      if (!s_i2s_running) {
        s_vsync_check = false;
        return nullptr;
      }
    }
    if (xSemaphoreTake(s_line_ready, pdMS_TO_TICKS(1500)) != pdTRUE) {
      if (s_i2s_running) i2s_stop();
      s_vsync_check = false;
      return nullptr;
    }
    if (millis() - start > 1500) {
      if (s_i2s_running) i2s_stop();
      s_vsync_check = false;
      return nullptr;
    }
  } while (lineno != s_line_count);

  return (uint16_t*)s_fb[s_fb_idx];
}

// SCCB / I2C Helpers
static void nvxWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(OV7670_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

static uint8_t nvxReadReg(uint8_t reg) {
  uint8_t value = 0xFF;
  Wire.beginTransmission(OV7670_ADDR);
  Wire.write(reg);
  Wire.endTransmission(true);
  delayMicroseconds(80);
  Wire.requestFrom((uint8_t)OV7670_ADDR, (size_t)1, true);
  if (Wire.available()) {
    value = Wire.read();
  }
  return value;
}

static void nvxWriteRegs(const struct regval_list* list) {
  while (true) {
    uint8_t r = pgm_read_byte(&list->reg_num);
    uint8_t v = pgm_read_byte(&list->value);
    if (r == 0xFF && v == 0xFF) break;
    nvxWriteReg(r, v);
    ++list;
  }
  delay(10);
}

static void nvxRewriteCLKRC() {
  uint8_t v = nvxReadReg(REG_CLKRC);
  nvxWriteReg(REG_CLKRC, v);
}

static void nvxSetPCLK(uint8_t pre, uint8_t pll) {
  uint8_t v = nvxReadReg(REG_CLKRC);
  nvxWriteReg(REG_CLKRC, (v & 0x80) | pre);
  v = nvxReadReg(REG_DBLV);
  nvxWriteReg(REG_DBLV, (v & 0x3F) | pll);
  nvxRewriteCLKRC();
}

static void nvxSetHStart(uint16_t hstart) {
  uint16_t hstop = (hstart + 640) % 784;
  nvxWriteReg(REG_HSTART, (uint8_t)(hstart / 8));
  nvxWriteReg(REG_HSTOP,  (uint8_t)(hstop / 8));
  nvxWriteReg(REG_HREF,   0x80 | (uint8_t)((hstop % 8) << 3) | (uint8_t)(hstart % 8));
  nvxRewriteCLKRC();
}

static void nvxSetVStart(uint16_t vstart) {
  uint16_t vstop = vstart + 480;
  nvxWriteReg(REG_VSTART, (uint8_t)(vstart / 4));
  nvxWriteReg(REG_VSTOP,  (uint8_t)(vstop / 4));
  nvxWriteReg(REG_VREF,   (uint8_t)((vstop % 4) << 2) | (uint8_t)(vstart % 4));
  nvxRewriteCLKRC();
}

static void nvxSetResolutionQQVGA() {
  uint8_t temp = nvxReadReg(REG_COM7);
  temp &= 0x47;
  nvxWriteReg(REG_COM7, temp | COM7_FMT_QVGA);

  // Enable 1/2 horizontal + vertical downsampling
  nvxWriteReg(REG_COM3, COM3_DCWEN);
  nvxWriteReg(REG_COM14, COM14_DCWEN | COM14_PCLKDIV_2);
  nvxWriteReg(REG_SCALING_XSC, 0x3A);
  nvxWriteReg(REG_SCALING_YSC, 0x35);

  // QVGA: divide horizontal and vertical source by 2
  nvxWriteReg(REG_SCALING_DCWCTR, SCALING_DCWCTR_VDS_by_2 | SCALING_DCWCTR_HDS_by_2);

  // DSP PCLK divider = 2
  nvxWriteReg(REG_SCALING_PCLK_DIV, SCALING_PCLK_DIV_RSVD | SCALING_PCLK_DIV_2);
  nvxWriteReg(REG_SCALING_PCLK_DELAY, 0x02);

  // Standard 320x240 OV7670 QVGA window with downsampling (from proven v27/v28):
  nvxWriteReg(REG_HSTART, 0x16);
  nvxWriteReg(REG_HSTOP,  0x04);
  nvxWriteReg(REG_HREF,   0x24);
  nvxWriteReg(REG_VSTART, 0x02);
  nvxWriteReg(REG_VSTOP,  0x7A);
  nvxWriteReg(REG_VREF,   0x0A);

  // Keep XCLK at 10 MHz and sensor PLL at x4
  nvxSetPCLK(1, DBLV_CLK_x4);
}

static void nvxSetRGB565() {
  uint8_t temp = nvxReadReg(REG_COM7) & 0x7A;
  nvxWriteReg(REG_COM7, temp | COM7_RGB);
  nvxWriteRegs(rgb565_OV7670);
  nvxRewriteCLKRC();
}

static void nvxTuneImageQuality() {
  nvxWriteReg(REG_COM16, COM16_YUV_ENHANC | COM16_DE_NOISE | COM16_AWBGAIN);
  nvxWriteReg(REG_EDGE, 0x12);          // modest edge enhancement
  nvxWriteReg(REG_DNSTH, 0x02);         // light denoise threshold
  nvxWriteReg(REG_CONTRAS, 0x48);       // slightly stronger contrast
  nvxWriteReg(REG_SATCTR, 0x68);        // slightly stronger color saturation
  nvxWriteReg(REG_BRIGHT, 0x00);        // neutral brightness

  uint8_t com8 = nvxReadReg(REG_COM8);
  com8 |= COM8_AGC | COM8_AWB | COM8_AEC;
  nvxWriteReg(REG_COM8, com8);

  DEBUG_PRINTLN("[CAM] IMAGE TUNING: contrast+ saturation+ edge+ denoise+ AWB/AGC/AEC");
}

static esp_err_t dma_desc_init() {
  const size_t dma_bytes = s_buf_line_width * 2;
  for (int i = 0; i < 2; ++i) {
    s_dma_buf[i] = (uint32_t*)malloc(dma_bytes);
    if (!s_dma_buf[i]) return ESP_ERR_NO_MEM;
    memset(s_dma_buf[i], 0, dma_bytes);
    memset(&s_dma_desc[i], 0, sizeof(lldesc_t));
    s_dma_desc[i].length = dma_bytes;
    s_dma_desc[i].size = dma_bytes;
    s_dma_desc[i].owner = 1;
    s_dma_desc[i].sosf = 1;
    s_dma_desc[i].buf = (uint8_t*)s_dma_buf[i];
    s_dma_desc[i].offset = i;
    s_dma_desc[i].empty = 0;
    s_dma_desc[i].eof = 1;
    s_dma_desc[i].qe.stqe_next = nullptr;
  }
  return ESP_OK;
}

static void i2s_init() {
  gpio_config_t conf = {
    .pin_bit_mask = 0,
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };

  const int in_pins[] = {
    NVX_CAM_D0, NVX_CAM_D1, NVX_CAM_D2, NVX_CAM_D3,
    NVX_CAM_D4, NVX_CAM_D5, NVX_CAM_D6, NVX_CAM_D7,
    NVX_CAM_PCLK, NVX_CAM_VSYNC
  };

  for (int p : in_pins) {
    conf.pin_bit_mask |= (1ULL << p);
  }
  gpio_config(&conf);

  gpio_matrix_in(NVX_CAM_D0, I2S0I_DATA_IN0_IDX, false);
  gpio_matrix_in(NVX_CAM_D1, I2S0I_DATA_IN1_IDX, false);
  gpio_matrix_in(NVX_CAM_D2, I2S0I_DATA_IN2_IDX, false);
  gpio_matrix_in(NVX_CAM_D3, I2S0I_DATA_IN3_IDX, false);
  gpio_matrix_in(NVX_CAM_D4, I2S0I_DATA_IN4_IDX, false);
  gpio_matrix_in(NVX_CAM_D5, I2S0I_DATA_IN5_IDX, false);
  gpio_matrix_in(NVX_CAM_D6, I2S0I_DATA_IN6_IDX, false);
  gpio_matrix_in(NVX_CAM_D7, I2S0I_DATA_IN7_IDX, false);
  gpio_matrix_in(NVX_CAM_VSYNC, I2S0I_V_SYNC_IDX, false);

  // HREF disconnected: tie to constant HIGH (0x38)
  gpio_matrix_in(0x38, I2S0I_H_SYNC_IDX, false);
  gpio_matrix_in(0x38, I2S0I_H_ENABLE_IDX, false);
  gpio_matrix_in(NVX_CAM_PCLK, I2S0I_WS_IN_IDX, false);

  periph_module_enable(PERIPH_I2S0_MODULE);

  const uint32_t lc = I2S_IN_RST_S | I2S_AHBM_RST_S | I2S_AHBM_FIFO_RST_S;
  I2S0.lc_conf.val |= lc;
  I2S0.lc_conf.val &= ~lc;

  i2s_conf_reset();

  I2S0.conf.rx_slave_mod = 1;
  I2S0.conf2.lcd_en = 1;
  I2S0.conf2.camera_en = 1;
  I2S0.clkm_conf.clkm_div_a = 1;
  I2S0.clkm_conf.clkm_div_b = 0;
  I2S0.clkm_conf.clkm_div_num = 2;
  I2S0.fifo_conf.dscr_en = 1;
  I2S0.fifo_conf.rx_fifo_mod_force_en = 1;
  I2S0.fifo_conf.rx_fifo_mod = 1;
  I2S0.conf_chan.rx_chan_mod = 1;
  I2S0.sample_rate_conf.rx_bits_mod = 16;
  I2S0.conf.rx_right_first = 0;
  I2S0.conf.rx_msb_right = 0;
  I2S0.conf.rx_msb_shift = 0;
  I2S0.conf.rx_mono = 0;
  I2S0.conf.rx_short_sync = 0;

  gpio_set_intr_type((gpio_num_t)NVX_CAM_VSYNC, GPIO_INTR_NEGEDGE);
  gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  gpio_isr_handler_add((gpio_num_t)NVX_CAM_VSYNC, VSYNC_isr, nullptr);

  esp_intr_alloc(
    ETS_I2S0_INTR_SOURCE,
    ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM,
    &i2s_isr, nullptr, &s_i2s_intr_handle
  );
}

static bool nvxCameraBegin() {
  pinMode(NVX_CAM_XCLK, OUTPUT);
  ledcSetup(LEDC_CHANNEL_0, 10000000, 2);
  ledcAttachPin(NVX_CAM_XCLK, LEDC_CHANNEL_0);
  ledcWrite(LEDC_CHANNEL_0, 2); // 50% duty clock at 10 MHz
  delay(20);

  i2s_init();

  s_data_ready  = xSemaphoreCreateBinary();
  s_line_ready  = xSemaphoreCreateBinary();
  s_vsync_catch = xSemaphoreCreateBinary();

  if (!s_data_ready || !s_line_ready || !s_vsync_catch) return false;

  for (int i = 0; i < 2; ++i) {
    s_fb[i] = (uint8_t*)malloc(s_buf_line_width);
    if (!s_fb[i]) return false;
    memset(s_fb[i], 0, s_buf_line_width);
  }

  if (dma_desc_init() != ESP_OK) return false;

  if (xTaskCreatePinnedToCore(
        line_filter_task, "nvx_line_filter", 3072, nullptr, 9, nullptr, 1
      ) != pdPASS) {
    return false;
  }

  nvxWriteReg(REG_COM7, COM7_RESET);
  delay(100);

  nvxWriteRegs(OV7670_default2_regs);
  nvxSetResolutionQQVGA();
  nvxSetRGB565();
  nvxTuneImageQuality();

  uint8_t com11 = nvxReadReg(REG_COM11);
  com11 &= (uint8_t)~0x60;
  com11 |= COM11_EXP;
  nvxWriteReg(REG_COM11, com11);
  nvxSetPCLK(1, DBLV_CLK_x4);
  nvxWriteReg(REG_COM10, nvxReadReg(REG_COM10) | COM10_VS_NEG);
  nvxRewriteCLKRC();
  delay(100);

  const uint8_t pid = nvxReadReg(REG_PID);
  const uint8_t ver = nvxReadReg(REG_VER);
  Serial.printf("[OV7670] PID=0x%02X VER=0x%02X\n", pid, ver);

  if (pid == 0xFF && ver == 0xFF) return false;

  s_cam_initialized = true;
  camera_getLine(NVX_FRAME_H); // Prime one frame
  return true;
}

// Direct JPEG TCP Streaming Callback & Frame Sender
static bool sendAllJPEG(const uint8_t* data, size_t len) {
  while (len) {
    if (!cameraTcpClient.connected()) return false;
    const size_t sent = cameraTcpClient.write(data, len);
    if (sent == 0) {
      delay(0);
      continue;
    }
    data += sent;
    len  -= sent;
  }
  return true;
}

static bool flushJpegTxBuffer() {
  if (s_jpegTxUsed == 0) return true;
  if (!sendAllJPEG(s_jpegTxBuf, s_jpegTxUsed)) return false;
  s_jpegBytesSent += s_jpegTxUsed;
  s_jpegTxUsed = 0;
  return true;
}

static size_t jpegStreamCallbackFixed(void* arg, size_t index, const void* data, size_t len) {
  (void)arg;
  (void)index;
  if (!data || len == 0) return 0;
  const uint8_t* src = (const uint8_t*)data;
  const size_t originalLen = len;

  while (len) {
    const size_t room = NVX_JPEG_TXBUF_BYTES - s_jpegTxUsed;
    const size_t take = (len < room) ? len : room;
    memcpy(s_jpegTxBuf + s_jpegTxUsed, src, take);
    s_jpegTxUsed += take;
    src += take;
    len -= take;

    if (s_jpegTxUsed == NVX_JPEG_TXBUF_BYTES) {
      if (!flushJpegTxBuffer()) {
        s_jpegCallbackFailed = true;
        return 0;
      }
    }
  }
  return originalLen;
}

bool sendJPEGFrame() {
  if (!s_jpegInput || !cameraTcpClient.connected()) return false;

  const size_t LINE_BYTES = (size_t)NVX_FRAME_W * NVX_FRAME_BPP;

  for (uint16_t y = 0; y < NVX_FRAME_H; ++y) {
    uint16_t* line = camera_getLine(y + 1);
    if (!line) {
      DEBUG_PRINTF("[CAM] CAPTURE FAIL line=%u\n", (unsigned)y);
      return false;
    }
    memcpy(s_jpegInput + (size_t)y * LINE_BYTES, line, LINE_BYTES);
  }

  // Swap byte endianness for fmt2jpg_cb
  for (size_t i = 0; i < NVX_FRAME_BYTES; i += 2) {
    const uint8_t t = s_jpegInput[i];
    s_jpegInput[i]   = s_jpegInput[i + 1];
    s_jpegInput[i + 1] = t;
  }

  const uint32_t id = cameraFrameId++;

  // 18-byte NVJ2 Packet Header
  uint8_t header[18] = {
    'N','V','J','2',
    (uint8_t)(NVX_FRAME_W),
    (uint8_t)(NVX_FRAME_W >> 8),
    (uint8_t)(NVX_FRAME_H),
    (uint8_t)(NVX_FRAME_H >> 8),
    2, 0,
    (uint8_t)id,
    (uint8_t)(id >> 8),
    (uint8_t)(id >> 16),
    (uint8_t)(id >> 24),
    0xFF, 0xFF, 0xFF, 0xFF
  };

  if (!sendAllJPEG(header, sizeof(header))) return false;

  s_jpegTxUsed = 0;
  s_jpegBytesSent = 0;
  s_jpegCallbackFailed = false;

  const bool encoded = fmt2jpg_cb(
    s_jpegInput,
    NVX_FRAME_BYTES,
    NVX_FRAME_W,
    NVX_FRAME_H,
    PIXFORMAT_RGB565,
    JPEG_QUALITY,
    jpegStreamCallbackFixed,
    nullptr
  );

  if (!encoded || s_jpegCallbackFailed) return false;
  if (!flushJpegTxBuffer()) return false;

  ++cameraFramesSent;
  const uint32_t now = millis();
  if (now - cameraFpsStart >= 1000) {
    const float fps = cameraFramesSent * 1000.0f / (float)(now - cameraFpsStart);
    DEBUG_PRINTF("[CAM] JPEG 160x120 | Q=%u | %lu B | %.1f FPS | id=%lu\n",
      JPEG_QUALITY, (unsigned long)s_jpegBytesSent, fps, (unsigned long)id);
    cameraFramesSent = 0;
    cameraFpsStart = now;
  }

  return true;
}

void stepCameraService() {
  if (!cameraAvailable) return;

  if (cameraTcpClient && cameraTcpClient.connected()) {
    if (!sendJPEGFrame()) {
      DEBUG_PRINTLN("[CAM] Client disconnected.");
      cameraTcpClient.stop();
    }
    return;
  }

  if (cameraTcpClient) cameraTcpClient.stop();

  WiFiClient incoming = cameraTcpServer.available();
  if (incoming) {
    incoming.setNoDelay(true);
    incoming.setTimeout(1000);
    cameraTcpClient = incoming;
    cameraFrameId = 0;
    cameraFramesSent = 0;
    cameraFpsStart = millis();
    DEBUG_PRINTLN("[CAM] Client connected on TCP port 5000!");
  }
}

// ===================================================================================
// 5. 74HC595 LED SHIFT REGISTER DRIVER
// ===================================================================================
void write595(uint8_t value) {
  digitalWrite(LED_LATCH, LOW);
  shiftOut(LED_DATA, LED_CLOCK, MSBFIRST, value);
  digitalWrite(LED_LATCH, HIGH);
  currentLedMask = value;
}

void updateLEDs() {
  unsigned long now = millis();

  if (currentLedEffect == "off") {
    if (currentLedMask != 0x00) write595(0x00);
    return;
  }

  if (currentLedEffect == "blink") {
    if (now - lastLedUpdateMs >= 250) {
      lastLedUpdateMs = now;
      write595(currentLedMask == 0x00 ? 0xFF : 0x00);
    }
  } else if (currentLedEffect == "warn") {
    if (now - lastLedUpdateMs >= 120) {
      lastLedUpdateMs = now;
      write595(currentLedMask == 0x00 ? 0xAA : (currentLedMask == 0xAA ? 0x55 : 0x00));
    }
  } else if (currentLedEffect == "pulse") {
    if (now - lastLedUpdateMs >= 70) {
      lastLedUpdateMs = now;
      static uint8_t pulseStep = 0;
      static const uint8_t pulseTable[8] = { 0x00, 0x18, 0x3C, 0x7E, 0xFF, 0x7E, 0x3C, 0x18 };
      write595(pulseTable[pulseStep]);
      pulseStep = (pulseStep + 1) % 8;
    }
  }
}

// ===================================================================================
// 6. LOW-LEVEL MOTOR DRIVER & SLEW CONTROLLER
// ===================================================================================
void initPins() {
  // Motor LEDC PWM channels
  ledcSetup(LEDC_CH_IN1, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(MOTOR_IN1, LEDC_CH_IN1);
  ledcWrite(LEDC_CH_IN1, 0);

  ledcSetup(LEDC_CH_IN2, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(MOTOR_IN2, LEDC_CH_IN2);
  ledcWrite(LEDC_CH_IN2, 0);

  ledcSetup(LEDC_CH_IN3, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(MOTOR_IN3, LEDC_CH_IN3);
  ledcWrite(LEDC_CH_IN3, 0);

  ledcSetup(LEDC_CH_IN4, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(MOTOR_IN4, LEDC_CH_IN4);
  ledcWrite(LEDC_CH_IN4, 0);

  // Sonar pins
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);

  // 74HC595 pins
  pinMode(LED_DATA, OUTPUT);  digitalWrite(LED_DATA, LOW);
  pinMode(LED_CLOCK, OUTPUT); digitalWrite(LED_CLOCK, LOW);
  pinMode(LED_LATCH, OUTPUT); digitalWrite(LED_LATCH, LOW);
  write595(0x00);
}

void motorWrite(int chF, int chB, int pwm) {
  pwm = constrain(pwm, -255, 255);
  if (pwm > 0) {
    ledcWrite(chF, pwm);
    ledcWrite(chB, 0);
  } else if (pwm < 0) {
    ledcWrite(chF, 0);
    ledcWrite(chB, -pwm);
  } else {
    ledcWrite(chF, 0);
    ledcWrite(chB, 0);
  }
}

void stepMotorRamp() {
  unsigned long now = millis();
  if (now - lastMotorRampMs < 10) return;
  lastMotorRampMs = now;

  const int RAMP_STEP = 35;

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

  motorWrite(LEDC_CH_IN1, LEDC_CH_IN2, currentPwmLeft);
  motorWrite(LEDC_CH_IN3, LEDC_CH_IN4, currentPwmRight);
}

void stopCar() {
  rotateState     = ROT_IDLE;
  targetPwmLeft   = 0;
  targetPwmRight  = 0;
  currentPwmLeft  = 0;
  currentPwmRight = 0;
  motorWrite(LEDC_CH_IN1, LEDC_CH_IN2, 0);
  motorWrite(LEDC_CH_IN3, LEDC_CH_IN4, 0);
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

// ===================================================================================
// 7. ULTRASONIC & SG90 RADAR SERVO
// ===================================================================================
long readUltrasonicCM() {
  if (!sonarActive) return 400;
  unsigned long now = millis();
  if (now - sonarCacheTime < SONAR_CACHE_MS && sonarCacheValue > 0) {
    return sonarCacheValue;
  }

  if (digitalRead(ECHO_PIN) == HIGH) {
    unsigned long waitStart = micros();
    while (digitalRead(ECHO_PIN) == HIGH && (micros() - waitStart < 2000)) yield();
  }

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 25000);
  static int consecutiveFails = 0;

  if (duration == 0 || duration >= 25000) {
    consecutiveFails++;
    if (consecutiveFails >= 2) sonarCacheValue = 400;
  } else if (duration < 120) {
    consecutiveFails++;
    if (consecutiveFails >= 4) sonarCacheValue = 2;
  } else {
    consecutiveFails = 0;
    long cm = (long)(duration / 58.2f);
    if (cm >= 2 && cm <= 400) {
      if (sonarCacheValue > 0 && sonarCacheValue < 400) {
        sonarCacheValue = (long)(sonarCacheValue * 0.25f + cm * 0.75f);
      } else {
        sonarCacheValue = cm;
      }
    } else {
      sonarCacheValue = 400;
    }
  }

  sonarCacheTime = now;
  return sonarCacheValue;
}

void invalidateSonarCache() {
  sonarCacheTime = 0;
}

void setRadarServo(int angle) {
  angle = constrain(angle, 0, 180);
  if (!radarServo.attached()) {
    radarServo.attach(SERVO_PIN, 544, 2400);
  }
  radarServo.write(angle);
}

void startRadarScan() {
  radarState        = SCAN_RUNNING;
  radarCurrentAngle = -90;
  radarStepDir      = 1;
  invalidateSonarCache();
  setRadarServo(0);
  lastRadarStepMs   = millis();
}

void stepRadarScan() {
  if (radarState != SCAN_RUNNING) return;
  unsigned long now = millis();
  if (now - lastRadarStepMs < 130) return;
  lastRadarStepMs = now;

  int servoAngle = radarCurrentAngle + 90;
  setRadarServo(servoAngle);
  invalidateSonarCache();
  long dist = readUltrasonicCM();

  broadcastWsText("{\"type\":\"radar\",\"angle\":" + String(radarCurrentAngle)
                  + ",\"distance\":" + String(dist) + "}");

  if (radarCurrentAngle == -90)     scanDistLeft  = (int)dist;
  else if (radarCurrentAngle == 0)  scanDistFront = (int)dist;
  else if (radarCurrentAngle == 90) scanDistRight = (int)dist;

  radarCurrentAngle += 30;
  if (radarCurrentAngle > 90) {
    setRadarServo(90);
    radarState = SCAN_DONE;
    broadcastWsText("{\"type\":\"radar_summary\",\"left\":" + String(scanDistLeft)
                    + ",\"front\":" + String(scanDistFront)
                    + ",\"right\":" + String(scanDistRight) + ",\"done\":true}");
  }
}

// ===================================================================================
// 8. MPU6050 ATTITUDE & GYRO
// ===================================================================================

bool scanI2CBus() {
  Serial.println("[I2C] Scanning GPIO21 (SDA) / GPIO22 (SCL)...");
  int nDevices = 0;
  bool ovFound  = false;
  bool mpuFound = false;

  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission(true);
    if (error == 0) {
      Serial.printf("[I2C] Device found at 0x%02X", address);
      if (address == 0x21) {
        Serial.print(" (OV7670 Camera SCCB)");
        ovFound = true;
      } else if (address == 0x68) {
        Serial.print(" (MPU6050 IMU - AD0=GND)");
        mpuFound = true;
      } else if (address == 0x69) {
        Serial.print(" (MPU6050 IMU - AD0=VCC/Floating)");
        mpuFound = true;
      }
      Serial.println();
      nDevices++;
    }
  }

  if (nDevices == 0) {
    Serial.println("[I2C] WARNING: No I2C devices acknowledged!");
    Serial.println("[I2C] Ensure 3.3V power & 4.7k pull-ups on GPIO 21 (SDA) and GPIO 22 (SCL).");
  } else {
    Serial.printf("[I2C] Scan complete: %d device(s) found.\n", nDevices);
    if (ovFound && !mpuFound) {
      Serial.println("[I2C] Notice: Camera (0x21) detected, but MPU6050 did NOT respond.");
      Serial.println("[I2C] Check MPU6050 VCC, GND, SDA, and SCL wiring.");
    }
  }

  return mpuFound;
}

void calibrateGyro() {
  if (!mpuAvailable) return;
  stopCar();
  Serial.println("[MPU6050] Calibrating zero-rate bias...");
  delay(500);

  float sumZ = 0.0f;
  for (int i = 0; i < 250; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sumZ += g.gyro.z;
    delay(2);
    yield();
  }

  gyroZBias      = sumZ / 250.0f;
  yawHeading     = 0.0f;
  pitchAngle     = 0.0f;
  rollAngle      = 0.0f;
  lastGyroMicros = micros();

  Serial.printf("[MPU6050] Gyro Z Bias = %.6f rad/s\n", gyroZBias);
}

void initMPU() {
  Serial.println("\n[MPU6050] Initializing...");
  uint8_t foundAddr = 0;

  if (mpu.begin(0x68, &Wire)) {
    foundAddr = 0x68;
  } else {
    delay(20);
    if (mpu.begin(0x69, &Wire)) {
      foundAddr = 0x69;
    }
  }

  if (foundAddr == 0) {
    mpuAvailable = false;
    Serial.println("[MPU6050] NOT DETECTED at 0x68 or 0x69");
    Serial.println("[MPU6050] GYRO: OFF (Operating in dead-reckoning fallback mode)");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    mpuAvailable = true;
    Serial.printf("[MPU6050] DETECTED at 0x%02X\n", foundAddr);
    Serial.println("[MPU6050] Range: +/-8G | Gyro: +/-500 DPS");
    calibrateGyro();
    Serial.println("[MPU6050] READY");
  }
}

void updateGyroHeading() {
  if (!mpuAvailable) return;
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  rawAX = a.acceleration.x;
  rawAY = a.acceleration.y;
  rawAZ = a.acceleration.z;
  rawGX = g.gyro.x;
  rawGY = g.gyro.y;
  rawGZ = g.gyro.z;

  unsigned long nowUs = micros();
  float dt = (nowUs - lastGyroMicros) / 1000000.0f;
  lastGyroMicros = nowUs;

  if (dt <= 0.0f || dt > 0.2f) return;

  // Integrated Yaw
  float rateZ = rawGZ - gyroZBias;
#if GYRO_REVERSE_Z
  rateZ = -rateZ;
#endif

  yawHeading += rateZ * 57.2957795f * dt;
  while (yawHeading <    0.0f) yawHeading += 360.0f;
  while (yawHeading >= 360.0f) yawHeading -= 360.0f;

  // Accelerometer Pitch & Roll
  float rawPitch = atan2(rawAY, sqrt(rawAX * rawAX + rawAZ * rawAZ)) * 57.2957795f;
  float rawRoll  = atan2(-rawAX, rawAZ) * 57.2957795f;

  pitchAngle = pitchAngle * 0.85f + rawPitch * 0.15f;
  rollAngle  = rollAngle  * 0.85f + rawRoll  * 0.15f;
}

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
  rotatePrevYaw   = yawHeading;
  rotateAccumYaw  = 0.0f;
  rotateStartMs   = millis();
  motorRunning    = true;
}

void stepNonBlockingRotate() {
  if (rotateState != ROT_TURNING) return;
  lastDriveCmdMs = millis();

  bool finished = false;
  if (mpuAvailable) {
    float delta = yawHeading - rotatePrevYaw;
    if (delta > 180.0f) delta -= 360.0f;
    else if (delta < -180.0f) delta += 360.0f;
    rotateAccumYaw += fabs(delta);
    rotatePrevYaw = yawHeading;

    if (rotateAccumYaw >= rotateTarget || (millis() - rotateStartMs > 8000)) {
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
// 9. AUTONOMOUS COLLISION AVOIDANCE
// ===================================================================================
void stepAutoNav() {
  if (manualMode || carStopped) return;
  unsigned long now = millis();

  switch (autoState) {
    case AUTO_FORWARD: {
      long d = readUltrasonicCM();
      if (d > 0 && d < OBSTACLE_LIMIT_CM) {
        stopCar();
        autoState = AUTO_DETECTED;
        autoActionTimer = now;
      } else {
        motorMix(0.0f, (float)CRUISE_SPEED_PWM / 255.0f);
      }
      break;
    }
    case AUTO_DETECTED: {
      stopCar();
      if (now - autoActionTimer >= 200) {
        startRadarScan();
        autoState = AUTO_SCAN;
      }
      break;
    }
    case AUTO_SCAN: {
      stepRadarScan();
      if (radarState == SCAN_DONE) {
        if (scanDistLeft > scanDistRight && scanDistLeft > OBSTACLE_LIMIT_CM) {
          startNonBlockingRotate(60.0f, false);
        } else if (scanDistRight >= scanDistLeft && scanDistRight > OBSTACLE_LIMIT_CM) {
          startNonBlockingRotate(60.0f, true);
        } else {
          startNonBlockingRotate(120.0f, true);
        }
        autoState = AUTO_TURN;
      }
      break;
    }
    case AUTO_TURN: {
      if (rotateState == ROT_IDLE) {
        autoState = AUTO_FORWARD;
      }
      break;
    }
    default:
      break;
  }
}

// ===================================================================================
// 10. WEBSOCKET PROTOCOL ENGINE
// ===================================================================================
static String computeWsAccept(const String& key) {
  String combined = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t digest[20];
  mbedtls_sha1((const unsigned char*)combined.c_str(), combined.length(), digest);
  char out[32];
  size_t n = 0;
  mbedtls_base64_encode((unsigned char*)out, sizeof(out), &n, digest, sizeof(digest));
  out[n] = '\0';
  return String(out);
}

void broadcastWsText(const String& payload) {
  size_t len = payload.length();
  uint8_t frame[10];
  size_t hLen = 0;
  frame[0] = 0x81;
  if (len < 126) {
    frame[1] = (uint8_t)len;
    hLen = 2;
  } else {
    frame[1] = 126;
    frame[2] = (uint8_t)(len >> 8);
    frame[3] = (uint8_t)(len & 0xFF);
    hLen = 4;
  }

  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (wsClients[i] && wsClients[i].connected()) {
      wsClients[i].write(frame, hLen);
      wsClients[i].write((const uint8_t*)payload.c_str(), len);
    }
  }
}

void handleWsMessage(WiFiClient& client, const String& msg) {
  // 1. JSON Frame Handling (Primary for NovaX App / app.js)
  if (msg.startsWith("{") && msg.endsWith("}")) {
    int tIdx = msg.indexOf("\"type\":");
    if (tIdx != -1) {
      int vStart = msg.indexOf('"', tIdx + 7);
      if (vStart != -1) {
        int vEnd = msg.indexOf('"', vStart + 1);
        if (vEnd != -1) {
          String type = msg.substring(vStart + 1, vEnd);

          // Ping / Heartbeat
          if (type == "ping") {
            broadcastWsText("{\"type\":\"pong\"}");
            return;
          }

          // Drive Movement: {"type":"move","dir":"F","speed":180}
          if (type == "move") {
            int dIdx = msg.indexOf("\"dir\":");
            char dir = 'S';
            if (dIdx != -1) {
              int dStart = msg.indexOf('"', dIdx + 6);
              if (dStart != -1) {
                dir = msg.charAt(dStart + 1);
              }
            }
            int spd = CRUISE_SPEED_PWM;
            int sIdx = msg.indexOf("\"speed\":");
            if (sIdx != -1) {
              int sValStart = sIdx + 8;
              while (msg.charAt(sValStart) == ' ' || msg.charAt(sValStart) == ':') sValStart++;
              spd = msg.substring(sValStart).toInt();
              if (spd <= 0 || spd > 255) spd = CRUISE_SPEED_PWM;
            }

            if (manualMode && !carStopped && rotateState == ROT_IDLE) {
              float s = (float)spd / 255.0f;
              if      (dir == 'F') motorMix( 0.0f,  s);
              else if (dir == 'B') motorMix( 0.0f, -s);
              else if (dir == 'L') motorMix(-s,     0.0f);
              else if (dir == 'R') motorMix( s,     0.0f);
              else if (dir == 'S') stopCar();
            }
            return;
          }

          // Emergency Stop
          if (type == "stop") {
            carStopped = true;
            stopCar();
            broadcastWsText("{\"type\":\"stopped\",\"stopped\":true}");
            return;
          }

          // Drive Ready / Resume
          if (type == "start") {
            carStopped = false;
            if (!manualMode) autoState = AUTO_FORWARD;
            broadcastWsText("{\"type\":\"started\",\"stopped\":false}");
            return;
          }

          // Autonomous / Manual Mode Toggle
          if (type == "mode") {
            int valIdx = msg.indexOf("\"value\":");
            if (valIdx != -1) {
              int vS = msg.indexOf('"', valIdx + 8);
              int vE = msg.indexOf('"', vS + 1);
              String val = msg.substring(vS + 1, vE);
              if (val == "auto") {
                manualMode = false;
                carStopped = false;
                autoState  = AUTO_FORWARD;
              } else {
                manualMode = true;
                autoState  = AUTO_STOPPED;
                stopCar();
              }
              broadcastWsText("{\"type\":\"mode\",\"value\":\"" + String(manualMode ? "manual" : "auto") + "\"}");
            }
            return;
          }

          // Precision Gyro Rotation: {"type":"rotate","dir":"left"|"right"|"360"}
          if (type == "rotate") {
            int dIdx = msg.indexOf("\"dir\":");
            if (dIdx != -1) {
              int dS = msg.indexOf('"', dIdx + 6);
              int dE = msg.indexOf('"', dS + 1);
              String rDir = msg.substring(dS + 1, dE);
              if (!carStopped && manualMode) {
                if (rDir == "left" || rDir == "90L") {
                  startNonBlockingRotate(90.0f, false);
                } else if (rDir == "right" || rDir == "90R") {
                  startNonBlockingRotate(90.0f, true);
                } else if (rDir == "360") {
                  startNonBlockingRotate(360.0f, true);
                }
              }
            }
            return;
          }

          // Radar Scan Trigger: {"type":"scan"}
          if (type == "scan") {
            if (manualMode) startRadarScan();
            return;
          }

          // Servo Pan Head Angle: {"type":"servo","angle":90}
          if (type == "servo") {
            int aIdx = msg.indexOf("\"angle\":");
            if (aIdx != -1) {
              int aStart = aIdx + 8;
              while (msg.charAt(aStart) == ' ' || msg.charAt(aStart) == ':') aStart++;
              int angle = msg.substring(aStart).toInt();
              setRadarServo(angle);
              broadcastWsText("{\"type\":\"servo_pos\",\"angle\":" + String(radarServo.read())
                              + ",\"distance\":" + String(readUltrasonicCM()) + "}");
            }
            return;
          }

          // Gyroscope Calibration: {"type":"calibrate_gyro"}
          if (type == "calibrate_gyro") {
            calibrateGyro();
            broadcastWsText("{\"type\":\"gyro_calibrated\",\"success\":true,\"bias\":" + String(gyroZBias, 4) + "}");
            return;
          }
        }
      }
    }
  }

  // 2. Plain Text / Legacy Protocol Handlers
  if (msg == "PING" || msg == "ping") {
    broadcastWsText("{\"type\":\"pong\"}");
    return;
  }

  // Joystick Stick Control: "STICK:turn,speed"
  if (msg.startsWith("STICK:")) {
    int comma = msg.indexOf(',', 6);
    if (comma > 6) {
      float turn  = msg.substring(6, comma).toFloat();
      float speed = msg.substring(comma + 1).toFloat();
      if (manualMode && !carStopped && rotateState == ROT_IDLE) {
        motorMix(turn, speed);
      }
    }
    return;
  }

  // Discrete Drive Commands
  if (msg == "F" || msg == "forward")  { motorMix( 0.0f,  1.0f); return; }
  if (msg == "B" || msg == "backward") { motorMix( 0.0f, -1.0f); return; }
  if (msg == "L" || msg == "left")     { motorMix(-1.0f,  0.0f); return; }
  if (msg == "R" || msg == "right")    { motorMix( 1.0f,  0.0f); return; }
  if (msg == "S" || msg == "stop")     { stopCar(); return; }

  // Emergency Stop & Resume
  if (msg == "EMERGENCY_STOP" || msg == "STOP") {
    carStopped = true;
    stopCar();
    broadcastWsText("{\"type\":\"stopped\",\"stopped\":true}");
    return;
  }
  if (msg == "RESUME" || msg == "START") {
    carStopped = false;
    if (!manualMode) autoState = AUTO_FORWARD;
    broadcastWsText("{\"type\":\"started\",\"stopped\":false}");
    return;
  }

  // Gyro Rotate Commands
  if (msg == "90L") { startNonBlockingRotate(90.0f, false); return; }
  if (msg == "90R") { startNonBlockingRotate(90.0f, true);  return; }
  if (msg == "360") { startNonBlockingRotate(360.0f, true); return; }

  // Gyro Calibration
  if (msg == "CAL_GYRO" || msg == "CALIBRATE") {
    calibrateGyro();
    broadcastWsText("{\"type\":\"gyro_calibrated\",\"success\":true,\"bias\":" + String(gyroZBias, 4) + "}");
    return;
  }

  // Radar Sweeper Trigger
  if (msg == "SCAN") { startRadarScan(); return; }

  // Mode Selection
  if (msg == "MODE:MANUAL") {
    manualMode = true;
    autoState  = AUTO_STOPPED;
    stopCar();
    broadcastWsText("{\"type\":\"mode\",\"value\":\"manual\"}");
    return;
  }
  if (msg == "MODE:AUTO") {
    manualMode = false;
    carStopped = false;
    autoState  = AUTO_FORWARD;
    broadcastWsText("{\"type\":\"mode\",\"value\":\"auto\"}");
    return;
  }

  // LED Commands
  if (msg.startsWith("LED:")) {
    currentLedEffect = msg.substring(4);
    updateLEDs();
    return;
  }
}

void pollWebSocketServer() {
  if (wsServer.hasClient()) {
    WiFiClient newClient = wsServer.available();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
      if (!wsClients[i] || !wsClients[i].connected()) {
        wsClients[i] = newClient;
        break;
      }
    }
  }

  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (wsClients[i] && wsClients[i].connected() && wsClients[i].available()) {
      WiFiClient& c = wsClients[i];

      // Check HTTP Upgrade Handshake
      if (c.peek() == 'G') {
        String req = "";
        while (c.available()) {
          char ch = (char)c.read();
          req += ch;
          if (req.endsWith("\r\n\r\n")) break;
        }
        int kIdx = req.indexOf("Sec-WebSocket-Key: ");
        if (kIdx != -1) {
          int kEnd = req.indexOf("\r\n", kIdx);
          String key = req.substring(kIdx + 19, kEnd);
          key.trim();
          String accept = computeWsAccept(key);
          c.print("HTTP/1.1 101 Switching Protocols\r\n"
                  "Upgrade: websocket\r\n"
                  "Connection: Upgrade\r\n"
                  "Sec-WebSocket-Accept: " + accept + "\r\n\r\n");
        }
        continue;
      }

      // Read WebSocket Frame
      uint8_t b0 = c.read();
      uint8_t b1 = c.read();
      bool masked = (b1 & 0x80) != 0;
      size_t len  = b1 & 0x7F;
      if (len == 126) {
        len = ((size_t)c.read() << 8) | c.read();
      }

      uint8_t mask[4] = {0,0,0,0};
      if (masked) {
        c.readBytes(mask, 4);
      }

      if (len <= WS_MAX_PAYLOAD) {
        String payload = "";
        for (size_t k = 0; k < len; k++) {
          uint8_t b = c.read();
          if (masked) b ^= mask[k % 4];
          payload += (char)b;
        }
        uint8_t opcode = b0 & 0x0F;
        if (opcode == 0x01) {
          handleWsMessage(c, payload);
        } else if (opcode == 0x08) {
          c.stop();
        }
      }
    }
  }
}

// ===================================================================================
// 11. REST API & NVS PREFERENCES
// ===================================================================================
void loadSavedNetworks() {
  nvsPrefs.begin("novax_wifi", true);
  selectedNetIdx = nvsPrefs.getInt("sel", 0);
  for (int i = 0; i < MAX_SAVED_NETS; i++) {
    String sKey = "s" + String(i);
    String pKey = "p" + String(i);
    String vKey = "v" + String(i);
    String ssid = nvsPrefs.getString(sKey.c_str(), "");
    String pass = nvsPrefs.getString(pKey.c_str(), "");
    savedNets[i].valid = nvsPrefs.getUChar(vKey.c_str(), 0);
    strncpy(savedNets[i].ssid, ssid.c_str(), 32);
    savedNets[i].ssid[32] = '\0';
    strncpy(savedNets[i].pass, pass.c_str(), 64);
    savedNets[i].pass[64] = '\0';
  }
  nvsPrefs.end();
}

void saveSavedNetworks() {
  nvsPrefs.begin("novax_wifi", false);
  nvsPrefs.putInt("sel", selectedNetIdx);
  for (int i = 0; i < MAX_SAVED_NETS; i++) {
    String sKey = "s" + String(i);
    String pKey = "p" + String(i);
    String vKey = "v" + String(i);
    nvsPrefs.putString(sKey.c_str(), savedNets[i].ssid);
    nvsPrefs.putString(pKey.c_str(), savedNets[i].pass);
    nvsPrefs.putUChar(vKey.c_str(), savedNets[i].valid);
  }
  nvsPrefs.end();
}

static void setCorsHeaders() {
  restServer.sendHeader("Access-Control-Allow-Origin", "*");
  restServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  restServer.sendHeader("Access-Control-Allow-Headers", "*");
}

// Embedded Web Flasher Page
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>NovaX V3 (ESP32-WROOM)</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: sans-serif; background: #0b0f19; color: #f0f4fc; text-align: center; padding: 20px; }
    .card { background: #131b2e; border: 1px solid #1f2c4c; border-radius: 12px; max-width: 480px; margin: 0 auto; padding: 20px; }
    h2 { color: #38bdf8; margin: 0 0 6px 0; }
    .sub { color: #94a3b8; font-size: 13px; margin: 0 0 12px 0; }
    .cam-box { width: 100%; max-width: 320px; aspect-ratio: 160/120; background: #02070d; border-radius: 8px; margin: 10px auto; overflow: hidden; border: 1px solid #38bdf8; display: flex; align-items: center; justify-content: center; }
    .cam-img { width: 100%; height: 100%; object-fit: contain; }
    .btn { background: #2563eb; color: #fff; border: 0; padding: 9px 18px; border-radius: 8px; font-weight: bold; cursor: pointer; margin: 4px; }
    .btn-green { background: #059669; }
  </style>
</head>
<body>
  <div class="card">
    <h2>NovaX V3 Cockpit</h2>
    <p class="sub">OV7670 160x120 JPEG | MPU6050 Gyro | MX1508 Motors</p>
    <div class="cam-box">
      <img id="liveCam" src="/cam.jpg" alt="Live Camera" class="cam-img">
    </div>
    <div>
      <button class="btn btn-green" onclick="refreshCam()">Snapshot</button>
      <a href="/status"><button class="btn">Status JSON</button></a>
    </div>
  </div>
  <script>
    let active = true;
    function refreshCam() {
      document.getElementById('liveCam').src = '/cam.jpg?t=' + Date.now();
    }
    setInterval(refreshCam, 200);
  </script>
</body>
</html>
)rawliteral";

void handleOtaUpload() {
  HTTPUpload& upload = restServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    stopCar();
    carStopped = true;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      DEBUG_PRINTLN("[OTA] Update.begin failed!");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning()) {
      Update.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      DEBUG_PRINTLN("[OTA] Success! Restarting...");
    }
  }
}

void handleOtaFinish() {
  setCorsHeaders();
  if (Update.hasError()) {
    restServer.send(500, "text/plain", "Flash Failed");
  } else {
    restServer.send(200, "application/json", "{\"status\":\"ok\",\"restarting\":true}");
    delay(500);
    ESP.restart();
  }
}

// ===================================================================================
// 12. SETUP & SERVICES INITIALIZATION
// ===================================================================================
void setup() {
#if SERIAL_ENABLED
  Serial.begin(115200);
  delay(300);
  Serial.println("\n========================================");
  Serial.println("NovaX V3 ESP32");
  Serial.println("========================================");
#endif

  // 2. Motor pins OFF
  initPins();
  stopCar();

  // 3. LED pins OFF
  write595(0x00);

  // 4. I2C Bus Recovery: toggle SCL 16 times to free any stuck slave on GPIO 21/22
  pinMode(NVX_CAM_SIOC, OUTPUT);
  pinMode(NVX_CAM_SIOD, INPUT_PULLUP);
  for (int i = 0; i < 16; i++) {
    digitalWrite(NVX_CAM_SIOC, HIGH);
    delayMicroseconds(5);
    digitalWrite(NVX_CAM_SIOC, LOW);
    delayMicroseconds(5);
  }
  digitalWrite(NVX_CAM_SIOC, HIGH);
  delayMicroseconds(5);

  // 5. Shared I2C Bus on GPIO 21 (SDA) and GPIO 22 (SCL) at 100 kHz
  Wire.begin(NVX_CAM_SIOD, NVX_CAM_SIOC, 100000);
  Wire.setClock(100000);
  Wire.setTimeOut(25);

  // 6. Run I2C scanner to discover both camera (0x21) and IMU (0x68/0x69)
  scanI2CBus();

  // 7. Initialize MPU6050 FIRST (clean I2C state before camera register writes)
  initMPU();

  // 8. Initialize OV7670 Camera (160x120 QQVGA, Direct JPEG Stream)
  s_jpegInput = (uint8_t*)heap_caps_malloc(NVX_FRAME_BYTES, MALLOC_CAP_8BIT);
  Serial.println("\n[OV7670] Initializing...");
  if (s_jpegInput && nvxCameraBegin()) {
    cameraAvailable = true;
    Serial.println("[OV7670] READY");
    Serial.println("[CAM] 160x120 JPEG Q90");
  } else {
    cameraAvailable = false;
    Serial.println("[OV7670] FAILED to initialize");
  }

  // 10. Initialize SG90 Radar Servo
  radarServo.setPeriodHertz(50);
  radarServo.attach(SERVO_PIN, 544, 2400);
  radarServo.write(90);

  // 11. Wi-Fi Setup: AP + STA (Resilient AP always remains alive)
  loadSavedNetworks();
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS);

  if (savedNets[selectedNetIdx].valid && strlen(savedNets[selectedNetIdx].ssid) > 0) {
    DEBUG_PRINTF("[WIFI] Connecting to saved STA: %s\n", savedNets[selectedNetIdx].ssid);
    WiFi.begin(savedNets[selectedNetIdx].ssid, savedNets[selectedNetIdx].pass);
  } else {
    // Default backup network
    WiFi.begin("Airtel_Ruby", "Rubyblack@567");
  }

  MDNS.begin("novax");

  // 12. REST Server Routes
  restServer.on("/", HTTP_GET, []() {
    setCorsHeaders();
    restServer.send_P(200, "text/html", INDEX_HTML);
  });

  restServer.on("/cam.jpg", HTTP_GET, []() {
    setCorsHeaders();
    if (!cameraAvailable || !s_jpegInput) {
      restServer.send(503, "text/plain", "Camera not available");
      return;
    }
    const size_t LINE_BYTES = (size_t)NVX_FRAME_W * NVX_FRAME_BPP;
    for (uint16_t y = 0; y < NVX_FRAME_H; ++y) {
      uint16_t* line = camera_getLine(y + 1);
      if (!line) {
        restServer.send(500, "text/plain", "Frame capture error");
        return;
      }
      memcpy(s_jpegInput + (size_t)y * LINE_BYTES, line, LINE_BYTES);
    }
    for (size_t i = 0; i < NVX_FRAME_BYTES; i += 2) {
      const uint8_t t = s_jpegInput[i];
      s_jpegInput[i]   = s_jpegInput[i + 1];
      s_jpegInput[i + 1] = t;
    }
    uint8_t* outJpg = nullptr;
    size_t outJpgLen = 0;
    if (fmt2jpg(s_jpegInput, NVX_FRAME_BYTES, NVX_FRAME_W, NVX_FRAME_H, PIXFORMAT_RGB565, JPEG_QUALITY, &outJpg, &outJpgLen) && outJpg) {
      restServer.sendHeader("Access-Control-Allow-Origin", "*");
      restServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      restServer.send_P(200, "image/jpeg", (const char*)outJpg, outJpgLen);
      free(outJpg);
    } else {
      restServer.send(500, "text/plain", "JPEG encode error");
    }
  });

  restServer.on("/status", HTTP_GET, []() {
    setCorsHeaders();
    long d = readUltrasonicCM();
    String json = "{";
    json += "\"firmware\":\""  + String(FIRMWARE_VERSION)  + "\",";
    json += "\"version\":\""   + String(FIRMWARE_VERSION)  + "\",";
    json += "\"camera\":"      + String(cameraAvailable ? "true" : "false") + ",";
    json += "\"gyro\":"        + String(mpuAvailable ? "true" : "false") + ",";
    json += "\"heading\":"     + String(yawHeading, 1)     + ",";
    json += "\"yaw\":"         + String(yawHeading, 1)     + ",";
    json += "\"pitch\":"       + String(pitchAngle, 1)     + ",";
    json += "\"roll\":"        + String(rollAngle, 1)      + ",";
    json += "\"distance\":"    + String(d)                 + ",";
    json += "\"mode\":\""      + String(manualMode ? "manual" : "auto") + "\",";
    json += "\"stopped\":"     + String(carStopped ? "true" : "false")  + ",";
    json += "\"battery\":4.15,";
    json += "\"wifiMode\":\""  + String(WiFi.getMode() == WIFI_MODE_APSTA ? "AP+STA" : "AP") + "\",";
    json += "\"ip\":\""        + WiFi.softAPIP().toString() + "\",";
    json += "\"freeHeap\":"    + String(ESP.getFreeHeap());
    json += "}";
    restServer.send(200, "application/json", json);
  });

  restServer.on("/move", HTTP_GET, []() {
    setCorsHeaders();
    if (!manualMode || carStopped) { restServer.send(200, "application/json", "{\"status\":\"blocked\"}"); return; }
    if (!restServer.hasArg("d")) { restServer.send(400, "text/plain", "Missing d"); return; }
    char dir = restServer.arg("d").charAt(0);
    int spd = restServer.hasArg("speed") ? restServer.arg("speed").toInt() : CRUISE_SPEED_PWM;
    float s = (float)spd / 255.0f;
    if      (dir == 'F') motorMix( 0.0f,  s);
    else if (dir == 'B') motorMix( 0.0f, -s);
    else if (dir == 'L') motorMix(-s,     0.0f);
    else if (dir == 'R') motorMix( s,     0.0f);
    else if (dir == 'S') stopCar();
    restServer.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  restServer.on("/stop", HTTP_POST, []() {
    setCorsHeaders();
    carStopped = true;
    stopCar();
    broadcastWsText("{\"type\":\"stopped\",\"stopped\":true}");
    restServer.send(200, "application/json", "{\"status\":\"stopped\"}");
  });

  restServer.on("/start", HTTP_POST, []() {
    setCorsHeaders();
    carStopped = false;
    if (!manualMode) autoState = AUTO_FORWARD;
    broadcastWsText("{\"type\":\"started\",\"stopped\":false}");
    restServer.send(200, "application/json", "{\"status\":\"started\"}");
  });

  restServer.on("/mode", HTTP_GET, []() {
    setCorsHeaders();
    if (restServer.hasArg("set")) {
      String m = restServer.arg("set");
      if (m == "auto") {
        manualMode = false;
        carStopped = false;
        autoState  = AUTO_FORWARD;
      } else {
        manualMode = true;
        autoState  = AUTO_STOPPED;
        stopCar();
      }
      broadcastWsText("{\"type\":\"mode\",\"value\":\"" + String(manualMode ? "auto" : "manual") + "\"}");
    }
    restServer.send(200, "application/json", "{\"mode\":\"" + String(manualMode ? "manual" : "auto") + "\"}");
  });

  restServer.on("/rotate", HTTP_GET, []() {
    setCorsHeaders();
    if (restServer.hasArg("dir")) {
      String rDir = restServer.arg("dir");
      if (rDir == "left" || rDir == "90L") {
        startNonBlockingRotate(90.0f, false);
      } else if (rDir == "right" || rDir == "90R") {
        startNonBlockingRotate(90.0f, true);
      } else if (rDir == "360") {
        startNonBlockingRotate(360.0f, true);
      }
    }
    restServer.send(200, "application/json", "{\"status\":\"rotating\"}");
  });

  restServer.on("/servo", HTTP_GET, []() {
    setCorsHeaders();
    if (restServer.hasArg("angle")) {
      int ang = constrain(restServer.arg("angle").toInt(), 0, 180);
      setRadarServo(ang);
    }
    restServer.send(200, "application/json", "{\"angle\":" + String(radarServo.read())
                   + ",\"distance\":" + String(readUltrasonicCM()) + "}");
  });

  restServer.on("/scan", HTTP_GET, []() {
    setCorsHeaders();
    startRadarScan();
    restServer.send(200, "application/json", "{\"status\":\"scanning\"}");
  });

  restServer.on("/scanResult", HTTP_GET, []() {
    setCorsHeaders();
    String json = "{\"left\":" + String(scanDistLeft)
                + ",\"front\":" + String(scanDistFront)
                + ",\"right\":" + String(scanDistRight)
                + ",\"done\":" + String(radarState == SCAN_DONE ? "true" : "false") + "}";
    restServer.send(200, "application/json", json);
  });

  restServer.on("/calibrate_gyro", HTTP_POST, []() {
    setCorsHeaders();
    calibrateGyro();
    broadcastWsText("{\"type\":\"gyro_calibrated\",\"success\":true,\"bias\":" + String(gyroZBias, 4) + "}");
    restServer.send(200, "application/json", "{\"status\":\"ok\",\"bias\":" + String(gyroZBias, 4) + "}");
  });

  restServer.on("/wifi/scan", HTTP_GET, []() {
    setCorsHeaders();
    int n = WiFi.scanNetworks(false, false);
    String json = "{\"networks\":[";
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]}";
    restServer.send(200, "application/json", json);
  });

  restServer.on("/wifi/saved", HTTP_GET, []() {
    setCorsHeaders();
    String json = "{\"selected\":" + String(selectedNetIdx) + ",\"networks\":[";
    for (int i = 0; i < MAX_SAVED_NETS; i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + String(savedNets[i].valid ? savedNets[i].ssid : "") + "\"}";
    }
    json += "]}";
    restServer.send(200, "application/json", json);
  });

  restServer.on("/wifi/save", HTTP_GET, []() {
    setCorsHeaders();
    if (restServer.hasArg("ssid")) {
      String s = restServer.arg("ssid");
      String p = restServer.hasArg("password") ? restServer.arg("password") : "";
      int slot = selectedNetIdx;
      for (int i = 0; i < MAX_SAVED_NETS; i++) {
        if (!savedNets[i].valid || String(savedNets[i].ssid) == s) { slot = i; break; }
      }
      strncpy(savedNets[slot].ssid, s.c_str(), 32);
      strncpy(savedNets[slot].pass, p.c_str(), 64);
      savedNets[slot].valid = 1;
      selectedNetIdx = slot;
      saveSavedNetworks();
      WiFi.begin(savedNets[slot].ssid, savedNets[slot].pass);
    }
    restServer.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  restServer.on("/wifi/select", HTTP_GET, []() {
    setCorsHeaders();
    if (restServer.hasArg("index")) {
      int idx = constrain(restServer.arg("index").toInt(), 0, MAX_SAVED_NETS - 1);
      selectedNetIdx = idx;
      saveSavedNetworks();
      if (savedNets[idx].valid) {
        WiFi.begin(savedNets[idx].ssid, savedNets[idx].pass);
      }
    }
    restServer.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  restServer.on("/wifi/delete", HTTP_GET, []() {
    setCorsHeaders();
    if (restServer.hasArg("index")) {
      int idx = constrain(restServer.arg("index").toInt(), 0, MAX_SAVED_NETS - 1);
      savedNets[idx].valid = 0;
      savedNets[idx].ssid[0] = '\0';
      savedNets[idx].pass[0] = '\0';
      saveSavedNetworks();
    }
    restServer.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  restServer.on("/wifi/switchSta", HTTP_GET, []() {
    setCorsHeaders();
    String host = "http://" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
    String ssid = savedNets[selectedNetIdx].valid ? String(savedNets[selectedNetIdx].ssid) : "NovaX-Car";
    restServer.send(200, "application/json", "{\"status\":\"ok\",\"host\":\"" + host + "\",\"ssid\":\"" + ssid + "\"}");
  });

  restServer.on("/wifi/switchAp", HTTP_GET, []() {
    setCorsHeaders();
    WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS);
    restServer.send(200, "application/json", "{\"status\":\"ok\",\"host\":\"http://192.168.4.1\"}");
  });

  restServer.on("/update", HTTP_POST, handleOtaFinish, handleOtaUpload);
  restServer.on("/ota/update", HTTP_POST, handleOtaFinish, handleOtaUpload);

  restServer.onNotFound([]() {
    if (restServer.method() == HTTP_OPTIONS) {
      setCorsHeaders();
      restServer.send(204);
    } else {
      restServer.send(404, "text/plain", "Not found");
    }
  });

  // 12. Start HTTP
  restServer.begin();

  // 13. Start WebSocket
  wsServer.begin();
  wsServer.setNoDelay(true);

  // 14. Start camera TCP
  if (cameraAvailable) {
    cameraTcpServer.begin();
  }

  stopCar();

  Serial.println("\n========================================");
  Serial.println("NovaX READY");
  Serial.println("========================================\n");
}

// ===================================================================================
// 13. MAIN LOOP
// ===================================================================================
void loop() {
  // 1. Network & Control Handlers
  restServer.handleClient();
  pollWebSocketServer();

  // 2. Gyroscope & Attitude Updates
  updateGyroHeading();

  // Temporary IMU Serial Diagnostics (Every 500ms)
#if SERIAL_ENABLED
  static unsigned long lastImuDiagMs = 0;
  if (millis() - lastImuDiagMs >= 500) {
    lastImuDiagMs = millis();
    if (mpuAvailable) {
      Serial.println("[IMU]");
      Serial.printf("AX=%.3f\nAY=%.3f\nAZ=%.3f\n", rawAX, rawAY, rawAZ);
      Serial.printf("GX=%.4f\nGY=%.4f\nGZ=%.4f\n", rawGX, rawGY, rawGZ);
      Serial.printf("YAW=%.2f\nPITCH=%.2f\nROLL=%.2f\n", yawHeading, pitchAngle, rollAngle);
    } else {
      Serial.println("[IMU] MPU6050 NOT DETECTED (GYRO: OFF)");
    }
  }
#endif

  // 3. Motor Safety Watchdog & Ramp Slew
  if (manualMode && motorRunning && rotateState == ROT_IDLE
      && (millis() - lastDriveCmdMs > MOTOR_WATCHDOG_MS)) {
    stopCar();
  }
  stepNonBlockingRotate();
  stepMotorRamp();

  // 4. Autonomous Collision Avoidance & Radar
  if (!manualMode) {
    stepAutoNav();
  }
  stepRadarScan();

  // 5. LED State Machine
  updateLEDs();

  // 6. Camera TCP Streaming Service (Independent & Non-Blocking)
  stepCameraService();

  // 7. Periodic WebSocket Telemetry Stream (100ms)
  static unsigned long lastTelems = 0;
  if (millis() - lastTelems >= 100) {
    lastTelems = millis();
    long d = readUltrasonicCM();
    String telem = "{\"type\":\"telemetry\",";
    telem += "\"version\":\""  + String(FIRMWARE_VERSION)  + "\",";
    telem += "\"distance\":"   + String(d)                 + ",";
    telem += "\"heading\":"    + String(yawHeading, 1)     + ",";
    telem += "\"yaw\":"        + String(yawHeading, 1)     + ",";
    telem += "\"pitch\":"      + String(pitchAngle, 1)     + ",";
    telem += "\"roll\":"       + String(rollAngle, 1)      + ",";
    telem += "\"mode\":\""     + String(manualMode ? "manual" : "auto") + "\",";
    telem += "\"stopped\":"    + String(carStopped ? "true" : "false")  + ",";
    telem += "\"battery\":4.15,";
    telem += "\"wifiMode\":\"" + String(WiFi.getMode() == WIFI_MODE_APSTA ? "AP+STA" : "AP") + "\",";
    telem += "\"ip\":\""       + WiFi.softAPIP().toString() + "\",";
    telem += "\"camera\":"     + String(cameraAvailable ? "true" : "false") + ",";
    telem += "\"gyro\":"       + String(mpuAvailable ? "true" : "false")    + ",";
    telem += "\"uptime\":"     + String(millis() / 1000);
    telem += "}";
    broadcastWsText(telem);
  }

  // 8. Periodic Serial Debug Telemetry (1000ms)
#if SERIAL_ENABLED
  static unsigned long lastSerialDbgMs = 0;
  if (millis() - lastSerialDbgMs >= 1000) {
    lastSerialDbgMs = millis();
    long d = readUltrasonicCM();
    Serial.printf("[STATUS] CAM: %-3s | IMU: %-4s | Yaw: %5.1f | Pitch: %4.1f | Roll: %4.1f | Sonar: %3ld cm | Servo: %3d deg\n",
      cameraAvailable ? "ON" : "OFF",
      mpuAvailable ? "OK" : "NONE",
      yawHeading, pitchAngle, rollAngle, d, radarServo.read());
  }
#endif

  delay(1);
  yield();
}
