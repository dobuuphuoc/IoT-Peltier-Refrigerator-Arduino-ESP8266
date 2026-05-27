#define BLYNK_TEMPLATE_ID "TMPL6jbtCIuCG"
#define BLYNK_TEMPLATE_NAME "ControlTemp"
#define BLYNK_AUTH_TOKEN "I2Hwykw4rPs-qO8D70iwq-95bXlOW5aE"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Wire.h>

char ssid[] = "Hưng";
char pass[] = "Hung2004";

#define UNO_ADDRESS 0x08

// mode: 0=OFF , 1=10-15 , 2=15-20 , 3=20-25
int currentMode = 0;
bool internalChange = false;
float tempC = 0.0;

union FloatBytes {
  float value;
  byte bytes[4];
} tempUnion;

BlynkTimer timer;  // Thêm để poll không block

void sendCommand(byte cmd) {
  Wire.beginTransmission(UNO_ADDRESS);
  Wire.write(cmd);  // 0:OFF, 1:10-15, 2:15-20, 3:20-25
  Wire.endTransmission();
}

void applyMode(int m) {
  if (currentMode == m) return;

  currentMode = m;
  internalChange = true;

  // Update UI
  Blynk.virtualWrite(V1, (m == 1));
  Blynk.virtualWrite(V2, (m == 2));
  Blynk.virtualWrite(V3, (m == 3));
  Blynk.virtualWrite(V4, (m == 0));
  internalChange = false;

  // Send to UNO
  sendCommand(m);
}

// BLYNK HANDLERS
BLYNK_WRITE(V1) { if (!internalChange && param.asInt()) applyMode(1); }
BLYNK_WRITE(V2) { if (!internalChange && param.asInt()) applyMode(2); }
BLYNK_WRITE(V3) { if (!internalChange && param.asInt()) applyMode(3); }
BLYNK_WRITE(V4) { if (!internalChange && param.asInt()) applyMode(0); }

void readFromUNO() {
  // Request 5 bytes: 1 byte mode + 4 byte float temp
  Wire.requestFrom(UNO_ADDRESS, 5);
  if (Wire.available() >= 5) {
    byte receivedMode = Wire.read();
    tempUnion.bytes[0] = Wire.read();
    tempUnion.bytes[1] = Wire.read();
    tempUnion.bytes[2] = Wire.read();
    tempUnion.bytes[3] = Wire.read();
    tempC = tempUnion.value;

    Blynk.virtualWrite(V0, tempC);

    // Nếu mode thay đổi từ button trên Uno → update Blynk UI
    if (!internalChange && receivedMode != currentMode) {
      currentMode = receivedMode;
      internalChange = true;
      Blynk.virtualWrite(V1, (currentMode == 1));
      Blynk.virtualWrite(V2, (currentMode == 2));
      Blynk.virtualWrite(V3, (currentMode == 3));
      Blynk.virtualWrite(V4, (currentMode == 0));
      internalChange = false;
    }
  }
}

void setup() {
  Wire.begin();  // Master, default pins SDA=D2, SCL=D1 trên NodeMCU/Wemos
  Wire.setClock(50000);  // Giảm tốc độ I2C để ổn định hơn
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  timer.setInterval(300L, readFromUNO);  // Poll mỗi 300ms để sync nhanh
}

void loop() {
  Blynk.run();
  timer.run();
}