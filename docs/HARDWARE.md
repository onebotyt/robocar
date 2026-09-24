# 🔌 NovaX V2 - Final Hardware & GPIO Specification

## 1. Complete GPIO Pin Map

| Subsystem | Function / Pin Name | ESP32 GPIO | Electrical Requirements / Notes |
| :--- | :--- | :--- | :--- |
| **OV7670 Camera** | D0 | **GPIO 36** | Input-only (ADC1_CH0 / SENSOR_VP) |
| | D1 | **GPIO 39** | Input-only (ADC1_CH3 / SENSOR_VN) |
| | D2 | **GPIO 34** | Input-only |
| | D3 | **GPIO 35** | Input-only |
| | D4 | **GPIO 32** | Digital I/O |
| | D5 | **GPIO 33** | Digital I/O |
| | D6 | **GPIO 25** | Digital I/O (DAC1) |
| | D7 | **GPIO 26** | Digital I/O (DAC2) |
| | XCLK | **GPIO 27** | Master clock signal (10 MHz PWM) |
| | PCLK | **GPIO 16** | Pixel clock input |
| | HREF | **GPIO 17** | Horizontal reference input |
| | VSYNC | **GPIO 4** | Vertical synchronization input |
| | SIOD | **GPIO 21** | Camera I2C/SCCB Data *(Shared I2C bus)* |
| | SIOC | **GPIO 22** | Camera I2C/SCCB Clock *(Shared I2C bus)* |
| | RESET | **3.3V** | Pull HIGH to 3.3V rail |
| | PWDN | **GND** | Pull LOW to GND for normal operation |
| **MX1508 Dual Motor** | IN1 (Motor A Forward) | **GPIO 13** | Left motor forward PWM (0-255) |
| | IN2 (Motor A Reverse) | **GPIO 14** | Left motor reverse PWM (0-255) |
| | IN3 (Motor B Forward) | **GPIO 18** | Right motor forward PWM (0-255) |
| | IN4 (Motor B Reverse) | **GPIO 19** | Right motor reverse PWM (0-255) |
| **HC-SR04 Ultrasonic** | TRIG | **GPIO 23** | 10µs trigger pulse output |
| | ECHO | **GPIO 3** | **MUST use 1k/2k voltage divider!** |
| **SG90 Micro Servo** | Signal (PWM) | **GPIO 2** | 50 Hz PWM servo control |
| | VCC | **5V** | Power from 5V regulated rail (not ESP32 3.3V) |
| **74HC595 Shift Register** | SER (Data) | **GPIO 5** | Serial data input |
| | SRCLK (Clock) | **GPIO 12** | Shift register clock |
| | RCLK (Latch) | **GPIO 15** | Storage register latch |
| | OE (Output Enable) | **GND** | Active LOW |
| | MR (Master Reset) | **3.3V** | Active LOW (tied HIGH to 3.3V) |
| | VCC | **3.3V** | Logic power |
| **MPU6050 6-DOF IMU** | SDA | **GPIO 21** | I2C Data *(Shared with OV7670 SIOD)* |
| | SCL | **GPIO 22** | I2C Clock *(Shared with OV7670 SIOC)* |
| | VCC | **3.3V** | Power from 3.3V rail |
| | AD0 | **GND** | I2C Address `0x68` |

---

## 2. Critical Wiring Schematics

### 2.1 HC-SR04 ECHO Voltage Divider
The HC-SR04 operates at 5V logic. Its ECHO pin outputs 5V pulses that will damage the 3.3V-tolerant ESP32 GPIO3.
You **MUST** wire a two-resistor voltage divider:

```
HC-SR04 (5V)                   ESP32 (3.3V Logic)
   ECHO ───[ 1kΩ Resistor ]───┬───> GPIO 3 (ECHO_PIN)
                              │
                       [ 2kΩ Resistor ]
                              │
                             GND
```
*Voltage at GPIO 3:* $5\text{V} \times \frac{2000}{1000 + 2000} \approx 3.33\text{V}$ (Safe for ESP32).

---

### 2.2 Shared I2C Bus (GPIO21 / GPIO22)
Both the **MPU6050** and the **OV7670** (SCCB interface) share `GPIO 21` (SDA/SIOD) and `GPIO 22` (SCL/SIOC).
This works seamlessly because they operate at different I2C device addresses:
- **MPU6050 I2C Address**: `0x68` (or `0x69` if AD0 is HIGH)
- **OV7670 SCCB Address**: `0x21` (7-bit address) / `0x42` (8-bit write address)

Make sure 4.7kΩ pull-up resistors to 3.3V are present on both SDA and SCL lines (most breakout boards already have built-in pull-ups).

---

### 2.3 Power Architecture
- **Battery**: 2S Li-Ion / LiPo battery (7.4V nominal, 8.4V full charge).
- **Step-Down (Buck Converter)**: 5V 3A buck converter powers:
  - ESP32 5V / VIN pin
  - SG90 servo motor (avoids servo brownout on the ESP32 3.3V rail!)
- **MX1508 Motor Driver**:
  - Powered directly from 7.4V battery pack (Motor VCC) or 5V rail.
- **Common Ground**: All GND pins (ESP32, MX1508, SG90, HC-SR04, 74HC595, battery) must be connected together.
