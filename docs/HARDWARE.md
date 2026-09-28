# 🔌 NovaX V2 - Final Hardware & GPIO Specification

## 1. Complete GPIO Pin Map

| Subsystem | Function / Pin Name | ESP32 GPIO | Electrical Requirements / Notes |
| :--- | :--- | :--- | :--- |
| **MX1508 Dual Motor** | IN1 (Motor A Forward) | **GPIO 13** | Left motor forward PWM (0-255) |
| | IN2 (Motor A Reverse) | **GPIO 14** | Left motor reverse PWM (0-255) |
| | IN3 (Motor B Forward) | **GPIO 16** | Right motor forward PWM (0-255) |
| | IN4 (Motor B Reverse) | **GPIO 17** | Right motor reverse PWM (0-255) |
| **HC-SR04 Ultrasonic** | TRIG | **GPIO 23** | 10µs trigger pulse output |
| | ECHO | **GPIO 34** | **Input-only pin. MUST use 1k/2k voltage divider!** |
| **SG90 Micro Servo** | Signal (PWM) | **GPIO 25** | 50 Hz PWM servo control (DAC1 pin) |
| | VCC | **5V** | Power from 5V regulated rail (not ESP32 3.3V) |
| **74HC595 Shift Register** | SER (Data) | **GPIO 5** | Serial data input |
| | SH_CP (Clock) | **GPIO 18** | Shift register clock |
| | ST_CP (Latch) | **GPIO 19** | Storage register latch |
| | OE (Output Enable) | **GND** | Active LOW |
| | MR (Master Reset) | **3.3V** | Active LOW (tied HIGH to 3.3V) |
| | VCC | **3.3V** | Logic power |
| **MPU6050 6-DOF IMU** | SDA | **GPIO 21** | Dedicated I2C Data bus |
| | SCL | **GPIO 22** | Dedicated I2C Clock bus |
| | VCC | **3.3V** | Power from 3.3V rail |
| | AD0 | **GND** | I2C Address `0x68` |

---

## 2. Critical Wiring Schematics

### 2.1 HC-SR04 ECHO Voltage Divider
The HC-SR04 operates at 5V logic. Its ECHO pin outputs 5V pulses that will damage the 3.3V-tolerant ESP32 GPIO pins.
Wire a two-resistor voltage divider to the ESP32 input-only pin **GPIO 34**:

```
HC-SR04 (5V)                   ESP32 (3.3V Logic)
   ECHO ───[ 1kΩ Resistor ]───┬───> GPIO 34 (ECHO_PIN)
                              │
                       [ 2kΩ Resistor ]
                              │
                             GND
```
*Voltage at GPIO 34:* $5\text{V} \times \frac{2000}{1000 + 2000} \approx 3.33\text{V}$ (Safe for ESP32).

---

### 2.2 Dedicated I2C Bus (GPIO21 / GPIO22)
The **MPU6050** uses `GPIO 21` (SDA) and `GPIO 22` (SCL) running at Fast-Mode 400 kHz.
- **I2C Address**: `0x68` (with AD0 tied to GND).
- Ensure 4.7kΩ pull-up resistors to 3.3V are present on both SDA and SCL lines (standard MPU6050 breakout boards include built-in pull-ups).

---

### 2.3 Power Architecture
- **Battery**: 2S Li-Ion / LiPo battery (7.4V nominal, 8.4V full charge).
- **Step-Down (Buck Converter)**: 5V 3A buck converter powers:
  - ESP32 5V / VIN pin
  - SG90 servo motor (avoids servo brownout on the ESP32 3.3V rail!)
  - HC-SR04 VCC pin
- **MX1508 Motor Driver**:
  - Powered directly from 7.4V battery pack (Motor VCC) for maximum torque.
- **Common Ground**: All GND pins (ESP32, MX1508, SG90, HC-SR04, 74HC595, battery) must be connected together.
