#include <Wire.h>                          // PHẢI TRƯỚC hd44780
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define SLAVE_ADDRESS 0x08

// ====================== HARDWARE CONFIG ======================
#define ONE_WIRE_BUS 2
#define FAN1_PIN 9
#define FAN2_PIN 10
#define BUTTON_10_15 4
#define BUTTON_15_20 5
#define BUTTON_20_25 6
#define BUTTON_ON_OFF 7

// ====================== PWM CONFIG ============================
const int MIN_PWM = 100;
const int MAX_PWM_FAN1 = 255;
const int MAX_PWM_FAN2 = 255;

// ====================== SYSTEM VARIABLES ======================
hd44780_I2Cexp lcd;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

float tempC = 0.0;
float lowerBound = 10, upperBound = 15;
bool systemOn = false;

union FloatBytes {
  float value;
  byte bytes[4];
} tempUnion;

unsigned long lastTempRead = 0;
const unsigned long TEMP_INTERVAL = 1000;

volatile byte currentModeByte = 0;
volatile byte lastActiveMode = 1;  // Mode cuối cùng khi đang ON (mặc định 1: 10-15)
volatile bool needLCDUpdate = true;

unsigned long lastI2CActivity = 0;
const unsigned long I2C_WATCHDOG_TIMEOUT = 5000;

void setup() {
  TCCR1A = (TCCR1A & 0b11111100) | 0b01;
  TCCR1B = (TCCR1B & 0b11111000) | 0b10;

  int status = lcd.begin(16, 2);
  if (status) hd44780::fatalError(status);
  lcd.backlight();

  pinMode(BUTTON_10_15, INPUT_PULLUP);
  pinMode(BUTTON_15_20, INPUT_PULLUP);
  pinMode(BUTTON_20_25, INPUT_PULLUP);
  pinMode(BUTTON_ON_OFF, INPUT_PULLUP);

  pinMode(FAN1_PIN, OUTPUT);
  pinMode(FAN2_PIN, OUTPUT);
  analogWrite(FAN1_PIN, 0);
  analogWrite(FAN2_PIN, 0);

  sensors.begin();

  Wire.begin(SLAVE_ADDRESS);
  Wire.onReceive(receiveCommand);
  Wire.onRequest(requestData);

  resetI2C();

  // Hiển thị khởi động
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode:10-15 C");
  lcd.setCursor(13, 0);
  lcd.print("OFF");

  lcd.setCursor(0, 1);
  lcd.print("TEMP:      C");  // Cố định chữ "TEMP:" và " C"

  lastI2CActivity = millis();
}

void updateModeLCD() {
  lcd.setCursor(0, 0);
  // In đầy đủ một chuỗi để tránh mất dấu "-"
  String modeText = "Mode:";
  if (lowerBound == 10 && upperBound == 15) modeText += "10-15";
  else if (lowerBound == 15 && upperBound == 20) modeText += "15-20";
  else if (lowerBound == 20 && upperBound == 25) modeText += "20-25";
  else modeText += String((int)lowerBound) + "-" + String((int)upperBound);  // Dự phòng
  
  modeText += " C";  // Thêm đơn vị
  
  // Đảm bảo đủ 12 ký tự để overwrite hết phần cũ
  while (modeText.length() < 12) modeText += " ";
  
  lcd.print(modeText);
}

void updateSystemStatusDisplay() {
  lcd.setCursor(13, 0);
  lcd.print(systemOn ? "ON " : "OFF");
}

// Chỉ update phần số nhiệt độ (dòng 2, từ cột 6 đến 11)
void updateTempDisplay() {
  lcd.setCursor(6, 1);  // Vị trí ngay sau "TEMP: "
  if (tempC == DEVICE_DISCONNECTED_C) {
    lcd.print("Err   ");  // Xóa phần cũ
  } else {
    if (tempC < 10) lcd.print(" ");           // Căn chỉnh cho số có 1 chữ số
    if (tempC >= 10 && tempC < 100) lcd.print(" "); // Căn chỉnh cho số có 2 chữ số
    lcd.print(tempC, 1);                      // In 1 chữ thập phân
    lcd.print("   ");                         // Xóa phần thừa nếu cần
  }
}

// ==================== I2C HANDLERS ====================
void receiveCommand(int bytes) {
  if (bytes > 0) {
    byte cmd = Wire.read();
    if (cmd == 0) { 
      systemOn = false; 
      analogWrite(FAN1_PIN, 0); 
      analogWrite(FAN2_PIN, 0); 
      if (currentModeByte >= 1 && currentModeByte <= 3) lastActiveMode = currentModeByte;
      currentModeByte = 0; 
    }
    else if (cmd == 1) { lowerBound = 10; upperBound = 15; systemOn = true; currentModeByte = 1; lastActiveMode = 1; }
    else if (cmd == 2) { lowerBound = 15; upperBound = 20; systemOn = true; currentModeByte = 2; lastActiveMode = 2; }
    else if (cmd == 3) { lowerBound = 20; upperBound = 25; systemOn = true; currentModeByte = 3; lastActiveMode = 3; }
    needLCDUpdate = true;
    lastI2CActivity = millis();
  }
}

void requestData() {
  Wire.write(currentModeByte);
  tempUnion.value = tempC;
  Wire.write(tempUnion.bytes, 4);
  lastI2CActivity = millis();
}

// ==================== BUTTON HANDLER ====================
void handleButtons() {
  static unsigned long t1, t2, t3, t4;

  // Nút ON/OFF
  if (digitalRead(BUTTON_ON_OFF) == LOW && millis() - t4 > 200) {
    systemOn = !systemOn;

    if (systemOn) {
      // Khi BẬT lại → khôi phục mode cuối cùng trước khi tắt
      currentModeByte = lastActiveMode;

      // Khôi phục lower/upperBound theo mode
      if (currentModeByte == 1) { lowerBound = 10; upperBound = 15; }
      else if (currentModeByte == 2) { lowerBound = 15; upperBound = 20; }
      else if (currentModeByte == 3) { lowerBound = 20; upperBound = 25; }
    } else {
      // Khi TẮT → lưu mode hiện tại làm mode cuối cùng
      if (currentModeByte >= 1 && currentModeByte <= 3) {
        lastActiveMode = currentModeByte;
      }
      currentModeByte = 0;
      analogWrite(FAN1_PIN, 0);
      analogWrite(FAN2_PIN, 0);
    }

    needLCDUpdate = true;
    t4 = millis();
  }

  // Các nút mode (chỉ hoạt động khi đang ON, hoặc bật ON luôn)
  if (digitalRead(BUTTON_10_15) == LOW && millis() - t1 > 300) {
    lowerBound = 10; upperBound = 15;
    systemOn = true;
    currentModeByte = 1;
    lastActiveMode = 1;  // Cập nhật mode cuối cùng
    needLCDUpdate = true;
    t1 = millis();
  }

  if (digitalRead(BUTTON_15_20) == LOW && millis() - t2 > 300) {
    lowerBound = 15; upperBound = 20;
    systemOn = true;
    currentModeByte = 2;
    lastActiveMode = 2;
    needLCDUpdate = true;
    t2 = millis();
  }

  if (digitalRead(BUTTON_20_25) == LOW && millis() - t3 > 300) {
    lowerBound = 20; upperBound = 25;
    systemOn = true;
    currentModeByte = 3;
    lastActiveMode = 3;
    needLCDUpdate = true;
    t3 = millis();
  }
}

void controlFans() {
  if (!systemOn || tempC == DEVICE_DISCONNECTED_C) {
    analogWrite(FAN1_PIN, 0);
    analogWrite(FAN2_PIN, 0);
    return;
  }
  int pwm = (tempC <= lowerBound) ? 0 : (tempC >= upperBound) ? MAX_PWM_FAN1 : map(round(tempC), (int)lowerBound, (int)upperBound, MIN_PWM, MAX_PWM_FAN1);
  analogWrite(FAN1_PIN, pwm);
  analogWrite(FAN2_PIN, pwm);
}

// ==================== UPDATE LCD ====================
void updateLCDIfNeeded() {
  if (needLCDUpdate) {
    lcd.setCursor(0, 0);
    lcd.print("                ");  // Clear dòng 1
    updateModeLCD();
    updateSystemStatusDisplay();
    needLCDUpdate = false;
  }
}

// ==================== RECOVER I2C BUS ====================
void resetI2C() {
  pinMode(A4, OUTPUT);
  pinMode(A5, OUTPUT);
  digitalWrite(A4, HIGH);
  for (int i = 0; i < 10; i++) {
    digitalWrite(A5, HIGH); delayMicroseconds(10);
    digitalWrite(A5, LOW);  delayMicroseconds(10);
  }
  pinMode(A4, INPUT);
  pinMode(A5, INPUT);
  Wire.begin(SLAVE_ADDRESS);
  Wire.onReceive(receiveCommand);
  Wire.onRequest(requestData);
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("TEMP:      C");
  needLCDUpdate = true;
}

void loop() {
  if (millis() - lastTempRead >= TEMP_INTERVAL) {
    sensors.requestTemperatures();
    tempC = sensors.getTempCByIndex(0);
    updateTempDisplay();
    lastTempRead = millis();
  }

  handleButtons();
  controlFans();
  updateLCDIfNeeded();

  if (millis() - lastI2CActivity > I2C_WATCHDOG_TIMEOUT) {
    resetI2C();
    lastI2CActivity = millis();
  }
}