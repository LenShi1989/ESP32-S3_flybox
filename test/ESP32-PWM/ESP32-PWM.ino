#include <ESP32Servo.h>

Servo esc;

#define ESC_PIN 5

void setup()
{
  Serial.begin(115200);

  esc.setPeriodHertz(50);
  esc.attach(ESC_PIN, 1000, 2000);

  // 啟動 ESC
  esc.writeMicroseconds(1000);
  delay(5000);
}

void loop()
{
  // 最低油門
  esc.writeMicroseconds(1000);
  delay(3000);

  // 低速測試
  esc.writeMicroseconds(1300);
  delay(3000);

  // 停止
  esc.writeMicroseconds(1000);
  delay(3000);
}