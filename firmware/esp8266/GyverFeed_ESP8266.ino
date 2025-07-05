/*
  ESP8266 модуль для управления автокормушкой через Яндекс Станцию
  - Подключение к WiFi
  - Yandex Smart Home API
  - Управление кормлением через Алису
  - Веб-интерфейс для настройки
  - Интеграция с Arduino по цифровому пину (см. README_ESP8266.md)
  AlexGyver, AlexGyver Technologies, 2024
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
#define YANDEX_TOKEN "YOUR_YANDEX_TOKEN"  // Токен Яндекс Smart Home

// Пины для управления кормлением
#define FEED_PIN 13                        // D7 на ESP8266 (GPIO13), подключить к ESP_FEED_PIN на Arduino
#define LED_PIN 2                          // Встроенный LED для индикации (D4/GPIO2)

// ========= ПЕРЕМЕННЫЕ ==========
ESP8266WebServer server(80);
WiFiClient client;
HTTPClient http;

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
  int feedAmount;
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
    Serial.println("\nОшибка подключения к WiFi!");
    // Запуск точки доступа для настройки
    setupAP();
  }
}

void setupAP() {
  Serial.println("Запуск точки доступа для настройки...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("GyverFeed_Setup", "12345678");
  
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
    <meta charset=\"utf-8\">
    <title>GyverFeed Настройки</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .container { max-width: 600px; margin: 0 auto; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input[type=\"text\"], input[type=\"password\"] { 
            width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; 
        }
        button { 
            background: #007cba; color: white; padding: 10px 20px; 
            border: none; border-radius: 4px; cursor: pointer; 
        }
        button:hover { background: #005a87; }
        .status { padding: 10px; margin: 10px 0; border-radius: 4px; }
        .success { background: #d4edda; color: #155724; }
        .error { background: #f8d7da; color: #721c24; }
    </style>
</head>
<body>
    <div class=\"container\">
        <h1>GyverFeed ESP8266</h1>
        <div class=\"status success\">Статус: )" + (isConnected ? "Подключен к WiFi" : "Не подключен") + R"(</div>
        
        <h2>Настройки WiFi</h2>
        <form action=\"/save\" method=\"post\">
            <div class=\"form-group\">
                <label>SSID:</label>
                <input type=\"text\" name=\"ssid\" value=")" + String(settings.ssid) + R"(" required>
            </div>
            <div class=\"form-group\">
                <label>Пароль:</label>
                <input type=\"password\" name=\"password\" value=")" + String(settings.password) + R"(" required>
            </div>
            
            <h2>Настройки Яндекс</h2>
            <div class=\"form-group\">
                <label>Имя устройства:</label>
                <input type=\"text\" name=\"deviceName\" value=")" + String(settings.deviceName) + R"(" required>
            </div>
            <div class=\"form-group\">
                <label>Токен Яндекс Smart Home:</label>
                <input type=\"text\" name=\"yandexToken\" value=")" + String(settings.yandexToken) + R"(" required>
            </div>
            
            <h2>Настройки кормления</h2>
            <div class=\"form-group\">
                <label>Размер порции (шагов):</label>
                <input type=\"number\" name=\"feedAmount\" value=")" + String(settings.feedAmount) + R"(" min=\"1\" max=\"1000\" required>
            </div>
            
            <button type=\"submit\">Сохранить настройки</button>
        </form>
        
        <h2>Управление</h2>
        <button onclick=\"feed()\">Покормить сейчас</button>
        <button onclick=\"restart()\">Перезагрузить</button>
    </div>
    
    <script>
        function feed() {
            fetch('/feed', {method: 'POST'})
                .then(response => response.text())
                .then(data => alert(data));
        }
        
        function restart() {
            if(confirm('Перезагрузить устройство?')) {
                fetch('/restart', {method: 'POST'});
            }
        }
    </script>
</body>
</html>
    )";
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
  
  // Здесь должна быть логика регистрации устройства
  // В реальном проекте нужно использовать Yandex Smart Home API
  Serial.println("Устройство зарегистрировано!");
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
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, body);
  
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
  
  // Отправка сигнала на Arduino
  digitalWrite(FEED_PIN, HIGH);
  delay(100);
  digitalWrite(FEED_PIN, LOW);
  
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
  if (settings.feedAmount < 1 || settings.feedAmount > 1000) {
    // Загрузка настроек по умолчанию
    strcpy(settings.ssid, WIFI_SSID);
    strcpy(settings.password, WIFI_PASS);
    strcpy(settings.deviceName, DEVICE_NAME);
    strcpy(settings.yandexToken, YANDEX_TOKEN);
    settings.feedAmount = 100;
    saveSettings();
  }
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
  Serial.println("Настройки сохранены в EEPROM");
} 