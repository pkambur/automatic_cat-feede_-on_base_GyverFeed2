/*
  Скетч к проекту "Автокормушка 2" с поддержкой ESP8266
  - Страница проекта (схемы, описания): https://alexgyver.ru/gyverfeed2/
  - Исходники на GitHub: https://github.com/AlexGyver/GyverFeed2/
  - ESP8266 модуль для управления через Яндекс Станцию
  Проблемы с загрузкой? Читай гайд для новичков: https://alexgyver.ru/arduino-first/
  AlexGyver, AlexGyver Technologies, 2020-2024
*/

// v2.2 - добавлена поддержка ESP8266 для управления через Яндекс Станцию

// Клик - внеочередная кормёжка
// Удержание - задаём размер порции
const byte feedTime[][2] = {
  {7, 0},       // часы, минуты. НЕ НАЧИНАТЬ ЧИСЛО С НУЛЯ
  {12, 0},
  {16, 0},
  {20, 0},
};

#define EE_RESET 13         // любое число 0-255. Измени, чтобы сбросить настройки и обновить время
#define FEED_SPEED 3500     // задержка между шагами мотора (мкс)
#define BTN_PIN 2           // кнопка
#define STEPS_FRW 19        // шаги вперёд
#define STEPS_BKW 13        // шаги назад
const byte drvPins[] = {3, 4, 5, 6};  // драйвер (фазаА1, фазаА2, фазаВ1, фазаВ2)

// ========= ESP8266 ПОДДЕРЖКА ==========
#define ESP_FEED_PIN 7      // Пин для сигнала от ESP8266 (измените на свободный пин)
#define ESP_STATUS_PIN 8    // Пин для отправки статуса на ESP8266 (опционально)

// =========================================================
#include <EEPROM.h>
#include "microDS3231.h"
MicroDS3231 rtc;

#include "EncButton.h"
EncButton<EB_TICK, BTN_PIN> btn;
int feedAmount = 100;

// Переменные для ESP8266
bool espFeeding = false;
unsigned long lastESPFeedTime = 0;
const unsigned long ESP_FEED_TIMEOUT = 10000; // 10 секунд таймаут

void setup() {
  rtc.begin();
  if (EEPROM.read(0) != EE_RESET) {   // первый запуск
    EEPROM.write(0, EE_RESET);
    EEPROM.put(1, feedAmount);
    rtc.setTime(BUILD_SEC, BUILD_MIN, BUILD_HOUR, BUILD_DAY, BUILD_MONTH, BUILD_YEAR);
  }
  EEPROM.get(1, feedAmount);
  
  // Настройка пинов
  for (byte i = 0; i < 4; i++) pinMode(drvPins[i], OUTPUT);   // пины выходы
  
  // ESP8266 пины
  pinMode(ESP_FEED_PIN, INPUT_PULLUP);
  pinMode(ESP_STATUS_PIN, OUTPUT);
  digitalWrite(ESP_STATUS_PIN, LOW);
  
  // Инициализация Serial для отладки
  Serial.begin(9600);
  Serial.println("GyverFeed v2.2 с ESP8266 поддержкой");
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
    Serial.print("Новый размер порции: ");
    Serial.println(feedAmount);
  }
  
  // ========= ОБРАБОТКА ESP8266 ==========
  checkESPFeed();
  
  // Отправка статуса на ESP8266 (опционально)
  static uint32_t statusTmr = 0;
  if (millis() - statusTmr > 5000) {  // каждые 5 секунд
    statusTmr = millis();
    sendStatusToESP();
  }
}

void checkESPFeed() {
  // Проверка сигнала от ESP8266
  if (digitalRead(ESP_FEED_PIN) == LOW && !espFeeding) {
    Serial.println("Получен сигнал кормления от ESP8266");
    espFeeding = true;
    lastESPFeedTime = millis();
    feed();
  }
  
  // Сброс флага ESP кормления
  if (espFeeding && (millis() - lastESPFeedTime > ESP_FEED_TIMEOUT)) {
    espFeeding = false;
    Serial.println("ESP кормление завершено");
  }
}

void sendStatusToESP() {
  // Отправка статуса на ESP8266 через пин
  // Можно использовать для индикации состояния
  static bool statusLed = false;
  statusLed = !statusLed;
  digitalWrite(ESP_STATUS_PIN, statusLed);
}

void feed() {
  Serial.println("Запуск кормления...");
  for (int i = 0; i < feedAmount; i++) oneRev();
  disableMotor();
  Serial.println("Кормление завершено");
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