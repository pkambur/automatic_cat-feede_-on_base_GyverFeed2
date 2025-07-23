/*
  Скетч к проекту "Автокормушка 2"
  - Страница проекта (схемы, описания): https://alexgyver.ru/gyverfeed2/
  - Исходники на GitHub: https://github.com/AlexGyver/GyverFeed2/
  Проблемы с загрузкой? Читай гайд для новичков: https://alexgyver.ru/arduino-first/
  AlexGyver, AlexGyver Technologies, 2020
*/

// v2.1 - исправлен баг с невыключением мотора

// Клик - внеочередная кормёжка
// Удержание - задаём размер порции
const byte feedTime[][2] = {
  {7, 0},       // часы, минуты. НЕ НАЧИНАТЬ ЧИСЛО С НУЛЯ
  {12, 0},
  {16, 0},
  {20, 0},
};

#define EE_RESET 12         // любое число 0-255. Измени, чтобы сбросить настройки и обновить время
#define BTN_PIN 2           // кнопка
#define ESP_FEED_PIN 7      // пин для сигнала от ESP8266
#define FEED_SPEED 3500     // задержка между шагами мотора (мкс)
#define STEPS_FRW 19        // шаги вперёд
#define STEPS_BKW 13        // шаги назад
const byte drvPins[] = {3, 4, 5, 6};  // драйвер (фазаА1, фазаА2, фазаВ1, фазаВ2)

// =========================================================
#include <EEPROM.h>
#include "microDS3231.h"
MicroDS3231 rtc;

#include "EncButton.h"
EncButton<EB_TICK, BTN_PIN> btn;
int feedAmount = 100;

void setup() {
  rtc.begin();
  Serial.begin(9600); // Для связи с ESP8266 по TX/RX
  if (EEPROM.read(0) != EE_RESET) {   // первый запуск
    EEPROM.write(0, EE_RESET);
    EEPROM.put(1, feedAmount);
    rtc.setTime(BUILD_SEC, BUILD_MIN, BUILD_HOUR, BUILD_DAY, BUILD_MONTH, BUILD_YEAR);
  }
  EEPROM.get(1, feedAmount);
  for (byte i = 0; i < 4; i++) pinMode(drvPins[i], OUTPUT);   // пины выходы
  pinMode(ESP_FEED_PIN, INPUT_PULLUP); // Для сигнала от ESP8266 (если вдруг понадобится)
}

void loop() {
  static uint32_t tmr = 0;
  if (millis() - tmr > 500) {           // два раза в секунду
    static byte prevMin = 0;
    tmr = millis();
    DateTime now = rtc.getTime();
    if (prevMin != now.minute) {
      prevMin = now.minute;
      for (byte i = 0; i < sizeof(feedTime) / 2; i++)    // для всего расписания
        if (feedTime[i][0] == now.hour && feedTime[i][1] == now.minute) feed();
    }
  }

  btn.tick();
  if (btn.click()) feed();

  if (btn.hold()) {
    int newAmount = 0;
    while (btn.isHold()) {
      btn.tick();
      oneRev();
      newAmount++;
    }
    disableMotor();
    feedAmount = newAmount;
    EEPROM.put(1, feedAmount);
  }

  // Проверка команд по Serial от ESP8266
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    Serial.print("Получена команда: "); Serial.println(cmd); // Отладочный вывод
    if (cmd == "FEED") {
      Serial.println("Запуск кормления (FEED)");
      feed();
    } else if (cmd.startsWith("FEED:")) {
      int amount = cmd.substring(5).toInt();
      Serial.print("Запуск кормления с количеством: "); Serial.println(amount); // Отладочный вывод
      if (amount > 0) {
        int oldAmount = feedAmount;
        feedAmount = amount;
        feed();
        feedAmount = oldAmount;
      }
    }
  }

  // Проверка сигнала от ESP8266 для запуска кормления (не используется)
  static bool espPrev = HIGH;
  bool espNow = digitalRead(ESP_FEED_PIN);
  if (espPrev == HIGH && espNow == LOW) { // фронт LOW
    feed();
    delay(1000); // Защита от дребезга и повторных срабатываний
  }
  espPrev = espNow;
}

void feed() {
  for (int i = 0; i < feedAmount; i++) oneRev();
  disableMotor();
}

// выключаем ток на мотор
void disableMotor() {
  for (byte i = 0; i < 4; i++) digitalWrite(drvPins[i], 0);
}

void oneRev() {
  for (int i = 0; i < STEPS_BKW; i++) runMotor(-1);
  for (int i = 0; i < STEPS_FRW; i++) runMotor(1);
}

const byte steps[] = {0b1010, 0b0110, 0b0101, 0b1001};
void runMotor(int8_t dir) {
  static byte step = 0;
  for (byte i = 0; i < 4; i++) digitalWrite(drvPins[i], bitRead(steps[step & 0b11], i));
  delayMicroseconds(FEED_SPEED);
  step += dir;
}