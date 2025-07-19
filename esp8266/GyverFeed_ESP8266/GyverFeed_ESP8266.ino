/*
  ESP8266 модуль для управления автокормушкой через Яндекс Станцию
  - Подключение к WiFi
  - Yandex Smart Home API
  - Управление кормлением через Алису
  - Веб-интерфейс для настройки
  
  AlexGyver, AlexGyver Technologies, 2024
  Исправлен и дополнен: 17 июля 2025
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// ========= НАСТРОЙКИ ==========
#define WIFI_SSID "YOUR_WIFI_SSID"        // Имя WiFi сети
#define WIFI_PASS "YOUR_WIFI_PASSWORD"    // Пароль WiFi сети
#define DEVICE_NAME "Автокормушка"        // Имя устройства для Алисы
#define YANDEX_TOKEN "YOUR_YANDEX_TOKEN"  // Токен Яндекс Smart Home (замените на реальный)

// Пины для управления кормлением
#define FEED_PIN 0                        // Пин для сигнала кормления (GPIO0, подтянуть к 3.3 В через 10 кОм)
#define LED_PIN 2                         // Встроенный LED для индикации (GPIO2)

// ========= ПЕРЕМЕННЫЕ ==========
ESP8266WebServer server(80);
WiFiClient client;
HTTPClient http;

void loadSettings();
void saveSettings();
void registerDevice();
void setupWebServer();
void connectToWiFi();
void setupAP();
void startFeeding();
void stopFeeding();
void handleDiscovery();
void handleQuery();
void handleAction();

bool isConnected = false;
bool isFeeding = false;
unsigned long lastFeedTime = 0;
const unsigned long FEED_TIMEOUT = 5000;  // 5 секунд на кормление

// Структура для настроек
struct Settings {
  char ssid[32];
  char password[64];
  char deviceName[32];
  char yandexToken[128];
  int feedAmount;  // Количество корма в процентах или импульсах
} settings;

// ========= ФУНКЦИИ ==========

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== GyverFeed ESP8266 ===");
  
  // Инициализация пинов
  pinMode(FEED_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(FEED_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  
  // Инициализация EEPROM
  EEPROM.begin(512);
  loadSettings();
  
  // Подключение к WiFi
  connectToWiFi();
  
  // Настройка веб-сервера
  setupWebServer();
  
  // Регистрация в Яндекс Smart Home
  registerDevice();
  
  Serial.println("ESP8266 готов к работе!");
}

void loop() {
  server.handleClient();
  
  // Проверка состояния кормления
  if (isFeeding && (millis() - lastFeedTime > FEED_TIMEOUT)) {
    stopFeeding();
  }
  
  // Проверка состояния Wi-Fi
  if (isConnected && WiFi.status() != WL_CONNECTED) {
    Serial.println("Потеряно соединение с WiFi, попытка переподключения...");
    isConnected = false;
    digitalWrite(LED_PIN, LOW);
    connectToWiFi();
  }
  
  // Мигание LED при подключении
  if (isConnected) {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 2000) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      lastBlink = millis();
    }
  }
  
  delay(10);
}

// ========= WIFI ==========

void connectToWiFi() {
  Serial.print("Подключение к WiFi: ");
  Serial.println(settings.ssid);
  
  WiFi.begin(settings.ssid, settings.password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    isConnected = true;
    Serial.println("\nWiFi подключен!");
    Serial.print("IP адрес: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_PIN, HIGH);
  } else {
    Serial.println("\nОшибка подключения к WiFi! Переход в режим AP.");
    setupAP();
    // Периодическая попытка подключения (1 минута)
    for (int i = 0; i < 12; i++) {
      delay(5000);
      if (WiFi.status() == WL_CONNECTED) {
        isConnected = true;
        digitalWrite(LED_PIN, HIGH);
        setupWebServer();
        registerDevice();
        break;
      }
    }
  }
}

void setupAP() {
  Serial.println("Запуск точки доступа для настройки...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("AvtoKormushka", "12345678");
  
  Serial.print("AP IP адрес: ");
  Serial.println(WiFi.softAPIP());
}

// ========= ВЕБ-СЕРВЕР ==========

void setupWebServer() {
  // Главная страница
  server.on("/", HTTP_GET, []() {
    String html = R"(
      <!DOCTYPE html>
      <html>
      <head>
        <title>GyverFeed ESP8266</title>
        <meta charset="UTF-8">
      </head>
      <body>
        <h1>GyverFeed ESP8266</h1>
        <p>Статус: %STATUS%</p>
        <form action="/save" method="POST">
          <label>WiFi SSID: <input type="text" name="ssid" value="%SSID%"></label><br>
          <label>WiFi Password: <input type="password" name="password" value="%PASS%"></label><br>
          <label>Device Name: <input type="text" name="deviceName" value="%DEVICENAME%"></label><br>
          <label>Yandex Token: <input type="text" name="yandexToken" value="%YANDEXTOKEN%"></label><br>
          <label>Feed Amount: <input type="number" name="feedAmount" value="%FEEDAMOUNT%" min="1" max="1000"></label><br>
          <input type="submit" value="Сохранить">
        </form>
        <form action="/feed" method="POST">
          <input type="submit" value="Покормить сейчас">
        </form>
        <form action="/restart" method="POST">
          <input type="submit" value="Перезагрузить">
        </form>
      </body>
      </html>
    )";
    html.replace("%STATUS%", isConnected ? "Подключен к WiFi" : "Не подключен");
    html.replace("%SSID%", String(settings.ssid));
    html.replace("%PASS%", String(settings.password));
    html.replace("%DEVICENAME%", String(settings.deviceName));
    html.replace("%YANDEXTOKEN%", String(settings.yandexToken));
    html.replace("%FEEDAMOUNT%", String(settings.feedAmount));
    server.send(200, "text/html", html);
  });
  
  // Сохранение настроек
  server.on("/save", HTTP_POST, []() {
    if (server.hasArg("ssid")) strcpy(settings.ssid, server.arg("ssid").c_str());
    if (server.hasArg("password")) strcpy(settings.password, server.arg("password").c_str());
    if (server.hasArg("deviceName")) strcpy(settings.deviceName, server.arg("deviceName").c_str());
    if (server.hasArg("yandexToken")) strcpy(settings.yandexToken, server.arg("yandexToken").c_str());
    if (server.hasArg("feedAmount")) settings.feedAmount = server.arg("feedAmount").toInt();
    
    saveSettings();
    
    String response = "Настройки сохранены! Перезагрузите устройство для применения.";
    server.send(200, "text/plain", response);
  });
  
  // Команда кормления
  server.on("/feed", HTTP_POST, []() {
    startFeeding();
    server.send(200, "text/plain", "Кормление запущено!");
  });
  
  // Перезагрузка
  server.on("/restart", HTTP_POST, []() {
    server.send(200, "text/plain", "Перезагрузка...");
    delay(1000);
    ESP.restart();
  });
  
  // API для Яндекс Smart Home
  server.on("/yandex/discovery", HTTP_POST, handleDiscovery);
  server.on("/yandex/query", HTTP_POST, handleQuery);
  server.on("/yandex/action", HTTP_POST, handleAction);
  
  server.begin();
  Serial.println("Веб-сервер запущен");
}

// ========= YANDEX SMART HOME API ==========

void registerDevice() {
  if (!isConnected) return;
  
  Serial.println("Регистрация устройства в Яндекс Smart Home...");
  http.begin(client, "https://api.iot.yandex.net/v1.0/devices/register");
  http.addHeader("Authorization", "Bearer " + String(settings.yandexToken));
  http.addHeader("Content-Type", "application/json");
  String payload = "{\"id\":\"gyverfeed_001\",\"name\":\"" + String(settings.deviceName) + "\"}";
  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    Serial.println("Устройство зарегистрировано!");
  } else {
    Serial.print("Ошибка регистрации: ");
    Serial.println(httpCode);
  }
  http.end();
}

void handleDiscovery() {
  String response = R"({
    "request_id": ")" + String(random(1000000)) + R"(",
    "payload": {
      "user_id": "user123",
      "devices": [{
        "id": "gyverfeed_001",
        "name": ")" + String(settings.deviceName) + R"(",
        "description": "Автоматическая кормушка для питомцев",
        "room": "Кухня",
        "type": "devices.types.smart_device",
        "capabilities": [{
          "type": "devices.capabilities.on_off",
          "retrievable": true,
          "parameters": {
            "split": false
          }
        }],
        "properties": [{
          "type": "devices.properties.float",
          "retrievable": true,
          "parameters": {
            "instance": "temperature",
            "unit": "unit.temperature.celsius"
          }
        }]
      }]
    }
  })";
  server.send(200, "application/json", response);
}

void handleQuery() {
  String response = R"({
    "request_id": ")" + String(random(1000000)) + R"(",
    "payload": {
      "devices": [{
        "id": "gyverfeed_001",
        "capabilities": [{
          "type": "devices.capabilities.on_off",
          "state": {
            "instance": "on",
            "value": )" + String(isFeeding ? "true" : "false") + R"(
          }
        }],
        "properties": [{
          "type": "devices.properties.float",
          "state": {
            "instance": "temperature",
            "value": 25.0
          }
        }]
      }]
    }
  })";
  server.send(200, "application/json", response);
}

void handleAction() {
  String body = server.arg("plain");
  DynamicJsonDocument doc(2048); // Увеличен размер буфера
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    Serial.print("Ошибка парсинга JSON: ");
    Serial.println(error.c_str());
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  String deviceId = doc["payload"]["devices"][0]["id"].as<String>();
  String capabilityType = doc["payload"]["devices"][0]["capabilities"][0]["type"].as<String>();
  
  if (deviceId == "gyverfeed_001" && capabilityType == "devices.capabilities.on_off") {
    bool value = doc["payload"]["devices"][0]["capabilities"][0]["state"]["value"].as<bool>();
    
    if (value) {
      startFeeding();
    } else {
      stopFeeding();
    }
  }
  
  String response = R"({
    "request_id": ")" + String(random(1000000)) + R"(",
    "payload": {
      "devices": [{
        "id": "gyverfeed_001",
        "capabilities": [{
          "type": "devices.capabilities.on_off",
          "state": {
            "instance": "on",
            "action_result": {
              "status": "DONE"
            }
          }
        }]
      }]
    }
  })";
  server.send(200, "application/json", response);
}

// ========= КОРМЛЕНИЕ ==========

void startFeeding() {
  if (isFeeding) return;
  
  Serial.println("Запуск кормления...");
  isFeeding = true;
  lastFeedTime = millis();
  
  // Отправка сигнала на Arduino с учётом количества корма
  int pulses = settings.feedAmount / 100; // Пример: 100 = 1 импульс, 200 = 2 импульса
  for (int i = 0; i < pulses; i++) {
    digitalWrite(FEED_PIN, HIGH);
    delay(100);
    digitalWrite(FEED_PIN, LOW);
    delay(100);
  }
  
  // Мигание LED во время кормления
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

void stopFeeding() {
  if (!isFeeding) return;
  
  Serial.println("Остановка кормления");
  isFeeding = false;
  digitalWrite(FEED_PIN, LOW);
}

// ========= EEPROM ==========

void loadSettings() {
  EEPROM.get(0, settings);
  
  // Проверка валидности данных
  if (strlen(settings.ssid) == 0 || settings.feedAmount < 1 || settings.feedAmount > 1000) {
    // Загрузка настроек по умолчанию
    strcpy(settings.ssid, WIFI_SSID);
    strcpy(settings.password, WIFI_PASS);
    strcpy(settings.deviceName, DEVICE_NAME);
    strcpy(settings.yandexToken, YANDEX_TOKEN);
    settings.feedAmount = 100; // Значение по умолчанию
    saveSettings();
  }
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
  Serial.println("Настройки сохранены в EEPROM");
}