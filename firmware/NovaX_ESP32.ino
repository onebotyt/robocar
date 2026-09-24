/* NovaX_Final.ino
   Single-file firmware for NOVA X (ESP8266)
   - AP mode web UI
   - Radar UI integrated (waves + scan line)
   - Ultrasonic servo scan (non-blocking)
   - Obstacle-avoid auto behavior (reverse, scan, choose direction)
   - Motor control (MX1508 style pin usage)
   - Pin mapping: set at top
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <pgmspace.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoOTA.h>

// =========================
// === USER CONFIGURE ====
// =========================
// =========================
// === FINAL ESP32 PIN MAP ===
// =========================
// OV7670 (18-pin, no FIFO)
#define CAM_D0       36
#define CAM_D1       39
#define CAM_D2       34
#define CAM_D3       35
#define CAM_D4       32
#define CAM_D5       33
#define CAM_D6       25
#define CAM_D7       26
#define CAM_XCLK     27
#define CAM_PCLK     16
#define CAM_HREF     17
#define CAM_VSYNC     4
#define CAM_SIOD     21
#define CAM_SIOC     22

// MX1508
#define IN1          13
#define IN2          14
#define IN3          18
#define IN4          19

// HC-SR04
#define TRIG_PIN     23
#define ECHO_PIN      3   // 1k top / 2k bottom divider

// SG90
#define SERVO_PIN     2

// 74HC595
#define LED_DATA      5
#define LED_CLOCK    12
#define LED_LATCH    15

// MPU6050
#define MPU_SDA      21
#define MPU_SCL      22

// AP credentials
const char* AP_SSID = "NovaX-V2";
const char* AP_PASS = "12345678";
const char* NOVAX_VERSION = "2.1.0-pwa-ota";
const char* OTA_TOKEN = "NovaX-OTA-ChangeMe";

// Behavior tuning
const long AUTO_CHECK_MS = 200;
const int OBSTACLE_THRESHOLD_CM = 90; // detect obstacle if below this
const int REVERSE_TARGET_CM = 40;     // reverse until >= this
const int SERVO_STEP_MS = 120;        // servo step time in scan
const int SERVO_STEP_DEG = 30;        // sweep stepping (e.g. -90, -60, -30, 0, 30, 60, 90)
const int TURN_PWM = 220;             // ESP32 PWM 0..255
const int FORWARD_PWM = 180;          // forward cruise PWM 0..255
const unsigned long TURN_DURATION_MS = 5000;
const unsigned long FORWARD_AFTER_TURN_MS = 1000;
const unsigned long REVERSE_TIMEOUT_MS = 7000;

// =========================
// === PROGMEM HTML UI ====
// =========================
// This is the "A" radar UI (semicircle waves, scanline). Kept compact but complete.
// =========================
// === GLOBAL OBJECTS ====
// =========================
WebServer server(80);
Servo scanServo;
Adafruit_MPU6050 mpu;
float gyroZBias = 0.0f;
float yawDeg = 0.0f;
unsigned long lastGyroUs = 0;
bool cameraOK = false;
uint8_t ledState = 0;
String ledEffect = "off";
unsigned long ledEffectAt = 0;
volatile bool manualMode = true;

// scan state machine
enum ScanPhase { IDLE, SWEEP };
volatile ScanPhase scanPhase = IDLE;
volatile bool scanningActive = false;
volatile bool scanResultReady = false;
int scanLeft = 400, scanFront = 400, scanRight = 400;
unsigned long lastServoStep = 0;
int sweepAngle = -90;
int sweepDir = 1; // +1 increasing

// auto state
enum AutoState { A_IDLE, A_DETECTED, A_REVERSING, A_SCANNING, A_TURNING, A_FORWARDING };
AutoState autoState = A_IDLE;
unsigned long autoStateAt = 0;

// sonar enable
volatile bool sonarEnabled = true;

// =========================
// === HELPER FUNCTIONS ====
// =========================
void pinSetup(){
  pinMode(IN1, OUTPUT); analogWrite(IN1, 0);
  pinMode(IN2, OUTPUT); analogWrite(IN2, 0);
  pinMode(IN3, OUTPUT); analogWrite(IN3, 0);
  pinMode(IN4, OUTPUT); analogWrite(IN4, 0);
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

void write595(uint8_t value){
  digitalWrite(LED_LATCH, LOW);
  shiftOut(LED_DATA, LED_CLOCK, MSBFIRST, value);
  digitalWrite(LED_LATCH, HIGH);
}

// motorWrite: pwmVal positive -> forward pin pwm, negative -> backward pin pwm
void motorWrite(int pinF, int pinB, int pwmVal){
  pwmVal = constrain(pwmVal, -255, 255);
  if (pwmVal > 0){
    analogWrite(pinF, pwmVal); analogWrite(pinB, 0);
  } else if (pwmVal < 0){
    analogWrite(pinF, 0); analogWrite(pinB, -pwmVal);
  } else {
    analogWrite(pinF, 0); analogWrite(pinB, 0);
  }
}

void motorMix(float x, float y){
  float left = y + x;
  float right = y - x;
  float maxv = max(fabs(left), fabs(right));
  if (maxv > 1.0f) { left /= maxv; right /= maxv; }
  motorWrite(IN1, IN2, (int)(left * 255.0f));
  motorWrite(IN3, IN4, (int)(right * 255.0f));
}

void stopCar(){ motorWrite(IN1, IN2, 0); motorWrite(IN3, IN4, 0); }

// ultrasonic read (blocking pulseIn with timeout)
long readUltrasonicCM(){
  if(!sonarEnabled) return 400;
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 20000); // timeout 20ms
  long cm = (dur * 0.034) / 2;
  if (cm <= 0 || cm > 400) return 400;
  return cm;
}

// =========================
// === SCAN STATE MACHINE ==
// =========================
void startScan(){
  if (scanPhase != IDLE) return;
  scanPhase = SWEEP;
  scanningActive = true;
  scanResultReady = false;
  scanLeft = scanFront = scanRight = 400;
  sweepAngle = -90; sweepDir = 1;
  scanServo.attach(SERVO_PIN);
  lastServoStep = 0;
}

void stepScan(){
  if (scanPhase == IDLE) return;
  unsigned long now = millis();
  if (now - lastServoStep < SERVO_STEP_MS) return;
  lastServoStep = now;

  // write servo: convert sweepAngle (-90..90) to 0..180
  int writeAngle = sweepAngle + 90;
  scanServo.write(writeAngle);
  delay(30); // small settling time for servo

  // read distance at specific key angles: -90, 0, 90
  if (sweepAngle == -90) scanLeft = readUltrasonicCM();
  else if (sweepAngle == 0) scanFront = readUltrasonicCM();
  else if (sweepAngle == 90) scanRight = readUltrasonicCM();

  // step
  sweepAngle += sweepDir * SERVO_STEP_DEG;
  if (sweepAngle > 90 && scanPhase == SWEEP){
    // finished sweep back (we stepped past -> finish)
    scanServo.write(90);
    delay(80);
    scanServo.detach();
    scanResultReady = true;
    scanningActive = false;
    scanPhase = IDLE;
    // ensure servo returns to center
    sweepAngle = -90;
  }
}

// =========================
// === AUTO STATE MACHINE ==
// =========================
void startReverse(){
  autoState = A_REVERSING;
  autoStateAt = millis();
  // start reversing with moderate PWM
  motorWrite(IN1, IN2, -190);
  motorWrite(IN3, IN4, -190);
}

void updateGyro(){
  sensors_event_t a,g,t;
  mpu.getEvent(&a,&g,&t);
  unsigned long now = micros();
  float dt = (now - lastGyroUs) / 1000000.0f;
  lastGyroUs = now;
  if(dt <= 0 || dt > 0.1f) return;
  float z = g.gyro.z - gyroZBias;
  yawDeg += z * 57.2957795f * dt;
}

void calibrateGyro(){
  delay(500);
  float sum=0;
  for(int i=0;i<400;i++){
    sensors_event_t a,g,t; mpu.getEvent(&a,&g,&t);
    sum += g.gyro.z; delay(3);
  }
  gyroZBias = sum/400.0f;
  yawDeg=0; lastGyroUs=micros();
}

void turnByGyro(float targetDeg, bool right){
  yawDeg=0; lastGyroUs=micros();
  unsigned long start=millis();
  while(millis()-start < 4000){
    updateGyro();
    float turned = right ? -yawDeg : yawDeg;
    if(turned >= targetDeg) break;
    float remaining = targetDeg - turned;
    int pwm = (remaining > 35) ? 220 : (remaining > 12 ? 150 : 105);
    if(right){
      motorWrite(IN1,IN2,pwm);
      motorWrite(IN3,IN4,-pwm);
    } else {
      motorWrite(IN1,IN2,-pwm);
      motorWrite(IN3,IN4,pwm);
    }
    delay(4);
  }
  stopCar();
  delay(80);
}

void performDecisionAfterScan(){
  int L=scanLeft, R=scanRight;
  if(R >= L) turnByGyro(90.0f,true);
  else turnByGyro(90.0f,false);
  autoState=A_FORWARDING;
  autoStateAt=millis();
  motorMix(0,0.6f);
}

void startReverse(){
  autoState=A_REVERSING; autoStateAt=millis();
  motorWrite(IN1,IN2,-190); motorWrite(IN3,IN4,-190);
}

void stepAuto(){
  if(autoState==A_IDLE){
    static unsigned long lastCheck=0;
    if(millis()-lastCheck<AUTO_CHECK_MS) return;
    lastCheck=millis();
    long d=readUltrasonicCM();
    if(d<OBSTACLE_THRESHOLD_CM){ stopCar(); autoState=A_DETECTED; }
    else motorMix(0,0.55f);
  } else if(autoState==A_DETECTED){
    startReverse();
  } else if(autoState==A_REVERSING){
    long d=readUltrasonicCM();
    if(d>=REVERSE_TARGET_CM || millis()-autoStateAt>REVERSE_TIMEOUT_MS){
      stopCar(); delay(80); startScan(); autoState=A_SCANNING;
    } else {
      motorWrite(IN1,IN2,-170); motorWrite(IN3,IN4,-170);
    }
  } else if(autoState==A_SCANNING){
    if(scanResultReady){ scanResultReady=false; performDecisionAfterScan(); }
  } else if(autoState==A_FORWARDING){
    if(millis()-autoStateAt>FORWARD_AFTER_TURN_MS){ stopCar(); autoState=A_IDLE; }
  }
}

int motorSpeed(){ return 190; }

// =========================
// === OTA / VERSION =======
// =========================
bool otaAuthorized(){
  return server.hasHeader("X-NovaX-OTA") && server.header("X-NovaX-OTA") == OTA_TOKEN;
}

void handleOtaStatus(){
  addCors();
  String ip = (WiFi.getMode()==WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  String s = "{\"version\":\"" + String(NOVAX_VERSION) + "\",\"ip\":\"" + ip + "\",\"ota\":true}";
  server.send(200, "application/json", s);
}

void handleOtaUpload(){
  HTTPUpload& upload = server.upload();
  if(!otaAuthorized()){
    if(upload.status == UPLOAD_FILE_START) Serial.println("OTA rejected: bad token");
    return;
  }
  if(upload.status == UPLOAD_FILE_START){
    stopCar();
    Serial.printf("OTA start: %s\n", upload.filename.c_str());
    if(!Update.begin(UPDATE_SIZE_UNKNOWN)){
      Update.printError(Serial);
    }
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(Update.isRunning()) Update.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(Update.end(true)){
      Serial.printf("OTA complete: %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if(upload.status == UPLOAD_FILE_ABORTED){
    Update.abort();
    Serial.println("OTA aborted");
  }
}

void handleOtaFinish(){
  if(!otaAuthorized()){ addCors(); server.send(401,"text/plain","unauthorized"); return; }
  if(Update.hasError()){ addCors(); server.send(500,"text/plain","OTA failed"); return; }
  addCors(); server.send(200,"application/json","{\"status\":\"ok\",\"restarting\":true}");
  delay(500);
  ESP.restart();
}

void setupArduinoOTA(){
  ArduinoOTA.setHostname("novax");
  ArduinoOTA.setPassword(OTA_TOKEN);
  ArduinoOTA.onStart([](){ stopCar(); Serial.println("ArduinoOTA start"); });
  ArduinoOTA.onEnd([](){ Serial.println("ArduinoOTA end"); });
  ArduinoOTA.onError([](ota_error_t error){ Serial.printf("ArduinoOTA error %u\n", error); });
  ArduinoOTA.begin();
}

// =========================
// === HTTP HANDLERS =======
// =========================
void addCors(){
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleRoot(){
  addCors();
  server.send(200, "text/plain", "NovaX API OK");
}

Preferences wifiPrefs;
const uint8_t MAX_SAVED_WIFI = 5;
int selectedWifi = -1;

String wifiJson(bool includePass=false){
  String out="{\"selected\":"+String(selectedWifi)+",\"networks\":[";
  bool first=true;
  uint8_t count=wifiPrefs.getUChar("count",0);
  for(uint8_t i=0;i<count && i<MAX_SAVED_WIFI;i++){
    String ssid=wifiPrefs.getString((String("s")+i).c_str(),"");
    if(ssid.length()==0) continue;
    if(!first) out+=","; first=false;
    out+="{\"ssid\":\""+ssid+"\"";
    if(includePass) out+=",\"password\":\""+wifiPrefs.getString((String("p")+i).c_str(),"")+"\"";
    out+="}";
  }
  out+="]}"; return out;
}

void handleWifiScan(){
  int n=WiFi.scanNetworks(false,true);
  String out="{\"networks\":[";
  for(int i=0;i<n;i++){
    if(i) out+=",";
    String ssid=WiFi.SSID(i); ssid.replace("\\","\\\\"); ssid.replace("\"","\\\"");
    out+="{\"ssid\":\""+ssid+"\",\"rssi\":"+String(WiFi.RSSI(i))+",\"secure\":"+String(WiFi.encryptionType(i)!=WIFI_AUTH_OPEN?1:0)+"}";
  }
  out+="]}"; WiFi.scanDelete(); addCors(); server.send(200,"application/json",out);
}

void handleWifiSaved(){ addCors(); server.send(200,"application/json",wifiJson()); }

void handleWifiSave(){
  if(!server.hasArg("ssid") || !server.hasArg("password")){server.send(400,"text/plain","missing ssid/password");return;}
  String ssid=server.arg("ssid"), pass=server.arg("password");
  uint8_t count=wifiPrefs.getUChar("count",0); int found=-1;
  for(uint8_t i=0;i<count;i++) if(wifiPrefs.getString((String("s")+i).c_str(),"")==ssid){found=i;break;}
  if(found<0){ if(count>=MAX_SAVED_WIFI){server.send(409,"text/plain","saved network limit reached");return;} found=count++; }
  wifiPrefs.putString((String("s")+found).c_str(),ssid); wifiPrefs.putString((String("p")+found).c_str(),pass); wifiPrefs.putUChar("count",count);
  if(selectedWifi<0) { selectedWifi=found; wifiPrefs.putInt("sel",selectedWifi); }
  addCors(); server.send(200,"application/json",wifiJson());
}

void handleWifiSelect(){
  if(!server.hasArg("index")){server.send(400,"text/plain","missing index");return;} int idx=server.arg("index").toInt(); uint8_t count=wifiPrefs.getUChar("count",0);
  if(idx<0 || idx>=count){server.send(400,"text/plain","bad index");return;} selectedWifi=idx; wifiPrefs.putInt("sel",idx); addCors(); server.send(200,"application/json",wifiJson());
}

void handleWifiDelete(){
  if(!server.hasArg("index")){server.send(400,"text/plain","missing index");return;} int idx=server.arg("index").toInt(); uint8_t count=wifiPrefs.getUChar("count",0);
  if(idx<0 || idx>=count){server.send(400,"text/plain","bad index");return;}
  for(int i=idx;i<count-1;i++){ wifiPrefs.putString((String("s")+i).c_str(),wifiPrefs.getString((String("s")+(i+1)).c_str(),"")); wifiPrefs.putString((String("p")+i).c_str(),wifiPrefs.getString((String("p")+(i+1)).c_str(),"")); }
  wifiPrefs.remove((String("s")+(count-1)).c_str()); wifiPrefs.remove((String("p")+(count-1)).c_str());
  if(selectedWifi==idx) selectedWifi=-1;
  else if(selectedWifi>idx) selectedWifi--;
  count--; wifiPrefs.putUChar("count",count);
  if(selectedWifi<0 && count>0) selectedWifi=0;
  wifiPrefs.putInt("sel",selectedWifi); addCors(); server.send(200,"application/json",wifiJson());
}

void handleSwitchSTA(){
  uint8_t count=wifiPrefs.getUChar("count",0);
  if(count==0 || selectedWifi<0 || selectedWifi>=count){server.send(400,"text/plain","no selected saved network");return;}
  String ssid=wifiPrefs.getString((String("s")+selectedWifi).c_str(),""); String pass=wifiPrefs.getString((String("p")+selectedWifi).c_str(),"");
  String response="{\"ssid\":\""+ssid+"\",\"host\":\"http://novax.local\"}"; addCors(); server.send(200,"application/json",response); delay(150);
  WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); WiFi.setHostname("novax"); WiFi.begin(ssid.c_str(),pass.c_str());
  unsigned long t=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-t<12000){delay(100);}
  if(WiFi.status()==WL_CONNECTED){MDNS.end(); MDNS.begin("novax"); Serial.print("STA IP: "); Serial.println(WiFi.localIP());}
}

void handleSwitchAP(){
  addCors(); server.send(200,"application/json","{\"mode\":\"AP\"}"); delay(150);
  WiFi.disconnect(true,true); WiFi.mode(WIFI_AP); WiFi.softAP(AP_SSID,AP_PASS);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
}

void handleStatus(){
  long d=readUltrasonicCM();
  String wifiMode = (WiFi.getMode()==WIFI_AP ? "AP" : (WiFi.getMode()==WIFI_STA ? "STA" : "OTHER"));
  String ip = (WiFi.getMode()==WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  String s="{\"distance\":"+String(d)+",\"mode\":"+String(manualMode?1:0)+",\"yaw\":"+String(yawDeg,1)+",\"camera\":"+String(cameraOK?1:0)+",\"wifiMode\":\""+wifiMode+"\",\"ip\":\""+ip+"\",\"version\":\""+String(NOVAX_VERSION)+"\"}";
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.send(200,"application/json",s);
}

void handleMove(){
  if (!server.hasArg("d")) { server.send(400, "text/plain", "missing d"); return; }
  String d = server.arg("d");
  if (!manualMode) { stopCar(); server.send(200,"text/plain","auto"); return; }

  if (d == "F") motorMix(0.0, 1.0);      // forward
  else if (d == "B") motorMix(0.0, -1.0); // backward
  else if (d == "L") {
    motorWrite(IN1, IN2, -motorSpeed());
    motorWrite(IN3, IN4, motorSpeed());
  }
  else if (d == "R") {
    motorWrite(IN1, IN2, motorSpeed());
    motorWrite(IN3, IN4, -motorSpeed());
  }
  else if (d == "S") stopCar();

  server.send(200, "text/plain", "OK");
}
void handleMode(){
  if (!server.hasArg("set")) { server.send(400,"text/plain","missing"); return; }
  String m = server.arg("set");
  manualMode = (m == "manual");
  if (!manualMode) { autoState = A_IDLE; } else { stopCar(); autoState = A_IDLE; }
  server.send(200, "text/plain", "OK");
}

void handleStop(){
  stopCar();
  server.send(200,"text/plain","OK");
}

void handleScan(){
  startScan();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"started\"}");
}

void handleScanResult(){
  String res = "{";
  res += "\"left\":" + String(scanLeft) + ",";
  res += "\"front\":" + String(scanFront) + ",";
  res += "\"right\":" + String(scanRight) + ",";
  res += "\"done\":" + String((scanPhase==IDLE && !scanningActive && scanResultReady) ? 1 : (scanningActive?0:(scanResultReady?1:0)));
  res += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", res);
}

void handleSonarToggle(){
  if (!server.hasArg("mode")) { server.send(400,"text/plain","missing"); return; }
  String m = server.arg("mode");
  sonarEnabled = (m == "on");
  server.send(200,"text/plain","OK");
}

void handleRotate(){
  if(!server.hasArg("dir")){ server.send(400,"text/plain","missing"); return; }
  if(!manualMode){ server.send(200,"text/plain","auto"); return; }
  String d=server.arg("dir");
  if(d=="left") turnByGyro(90,false);
  else if(d=="right") turnByGyro(90,true);
  else if(d=="360") turnByGyro(360,true);
  server.send(200,"text/plain","OK");
}

void handleLed(){
  if(!server.hasArg("effect")){ server.send(400,"text/plain","missing"); return; }
  ledEffect=server.arg("effect");
  ledEffectAt=millis();
  if(ledEffect=="off") write595(0);
  else if(ledEffect=="warn") write595(0b11000011);
  else if(ledEffect=="blink") write595(0b10101010);
  else if(ledEffect=="pulse") write595(0b00011000);
  else write595(0);
  server.send(200,"text/plain","OK");
}

void updateLEDs(){
  if(ledEffect=="off") return;
  unsigned long now=millis();
  if(ledEffect=="blink" && now-ledEffectAt>=300){
    ledEffectAt=now; static bool on=false; on=!on; write595(on?0xFF:0x00);
  } else if(ledEffect=="warn" && now-ledEffectAt>=500){
    ledEffectAt=now; static bool on=false; on=!on; write595(on?0xC3:0x00);
  } else if(ledEffect=="pulse" && now-ledEffectAt>=250){
    ledEffectAt=now; static uint8_t p=0x18; p=(p==0x18)?0x3C:0x18; write595(p);
  }
}

void handlePath() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "no data");
    return;
  }

  String body = server.arg("plain");   // "x1,y1;x2,y2;..."

  // ---- parse average X from the string ----
  float sumX = 0.0;
  int count = 0;

  int start = 0;
  while (start < body.length()) {
    int comma = body.indexOf(',', start);
    if (comma == -1) break;
    int semi = body.indexOf(';', start);
    if (semi == -1) semi = body.length();

    String sx = body.substring(start, comma);   // "0.123"
    float x = sx.toFloat();
    sumX += x;
    count++;

    if (semi >= body.length()) break;
    start = semi + 1;
  }

  if (count == 0) {
    server.send(400, "text/plain", "bad data");
    return;
  }

  float avgX = sumX / count;   // 0.0 .. 1.0, 0.5 is center

  // make sure we are in manual-style control
  manualMode = true;

  stopCar();   // start from stop

  // ---- decide motion from avgX ----
  if (avgX < 0.45) {
    // path mostly left -> curve left (right motors faster)
    motorWrite(IN1, IN2, FORWARD_PWM / 2);  // left slow
    motorWrite(IN3, IN4, FORWARD_PWM);      // right fast
  } else if (avgX > 0.55) {
    // path mostly right -> curve right (left motors faster)
    motorWrite(IN1, IN2, FORWARD_PWM);      // left fast
    motorWrite(IN3, IN4, FORWARD_PWM / 2);  // right slow
  } else {
    // path mostly centered -> straight
    motorMix(0.0, 0.8);                     // go straight
  }

  server.send(200, "text/plain", "OK");
}

// =========================
// === OV7670 CAMERA ======
// =========================
bool initCamera(){
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = CAM_D0; c.pin_d1 = CAM_D1; c.pin_d2 = CAM_D2; c.pin_d3 = CAM_D3;
  c.pin_d4 = CAM_D4; c.pin_d5 = CAM_D5; c.pin_d6 = CAM_D6; c.pin_d7 = CAM_D7;
  c.pin_xclk = CAM_XCLK; c.pin_pclk = CAM_PCLK; c.pin_vsync = CAM_VSYNC; c.pin_href = CAM_HREF;
  c.pin_sccb_sda = CAM_SIOD; c.pin_sccb_scl = CAM_SIOC;
  c.pin_pwdn = -1; c.pin_reset = -1;
  c.xclk_freq_hz = 10000000;
  c.pixel_format = PIXFORMAT_RGB565;
  c.frame_size = FRAMESIZE_QQVGA;
  c.jpeg_quality = 12;
  c.fb_count = 1;
  c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  c.fb_location = CAMERA_FB_IN_DRAM;
  esp_err_t err = esp_camera_init(&c);
  if(err != ESP_OK){ Serial.printf("Camera init failed: 0x%x\\n", err); return false; }
  sensor_t *sensor=esp_camera_sensor_get();
  if(sensor){ sensor->set_vflip(sensor,1); sensor->set_hmirror(sensor,0); }
  return true;
}

void handleCamera(){
  if(!cameraOK){ server.send(503,"text/plain","camera unavailable"); return; }
  camera_fb_t *fb=esp_camera_fb_get();
  if(!fb){ server.send(503,"text/plain","capture failed"); return; }
  uint8_t *jpg=nullptr; size_t jpgLen=0;
  bool ok=frame2jpg(fb, 55, &jpg, &jpgLen);
  esp_camera_fb_return(fb);
  if(!ok || !jpg){ server.send(500,"text/plain","jpeg conversion failed"); return; }
  WiFiClient client=server.client();
  server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate");
  server.setContentLength(jpgLen);
  server.send(200,"image/jpeg");
  client.write(jpg,jpgLen);
  free(jpg);
}

void initMPU(){
  Wire.begin(MPU_SDA,MPU_SCL);
  if(!mpu.begin()){
    Serial.println("MPU6050 not found");
    return;
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  calibrateGyro();
}


// =========================
// === SETUP & LOOP =======
// =========================
void setup(){
  Serial.begin(115200);
  delay(300);
  pinSetup();
  initMPU();

  wifiPrefs.begin("wifi", false);
  selectedWifi = wifiPrefs.getInt("sel", -1);

  // Every power-on intentionally starts in AP mode. STA is entered only by the PWA command.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setHostname("novax");
  MDNS.begin("novax");
  setupArduinoOTA();
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/wifi/scan", handleWifiScan);
  server.on("/wifi/saved", handleWifiSaved);
  server.on("/wifi/save", handleWifiSave);
  server.on("/wifi/select", handleWifiSelect);
  server.on("/wifi/delete", handleWifiDelete);
  server.on("/wifi/switchSta", handleSwitchSTA);
  server.on("/wifi/switchAp", handleSwitchAP);
  server.on("/status", handleStatus);
  server.on("/move", handleMove);
  server.on("/mode", handleMode);
  server.on("/stop", handleStop);
  server.on("/scan", handleScan);
  server.on("/scanResult", handleScanResult);
  server.on("/sonarToggle", handleSonarToggle);
  server.on("/rotate", handleRotate);
  server.on("/led", handleLed);
  server.on("/path", HTTP_POST, handlePath);
  server.on("/cam.jpg", handleCamera);
  server.on("/ota/status", handleOtaStatus);
  server.on("/ota/update", HTTP_POST, handleOtaFinish, handleOtaUpload);
  const char* headerKeys[] = {"X-NovaX-OTA"};
  server.collectHeaders(headerKeys, 1);
  server.begin();

  scanServo.attach(SERVO_PIN,500,2400);
  scanServo.write(90);
  delay(250);
  scanServo.detach();

  cameraOK=initCamera();
  Serial.println(cameraOK ? "OV7670 ready" : "OV7670 failed");
  Serial.println("NovaX V2 ready. Open 192.168.4.1");
}

void loop(){
  server.handleClient();
  ArduinoOTA.handle();
  stepScan();
  updateGyro();
  updateLEDs();
  if(!manualMode) stepAuto();
}
