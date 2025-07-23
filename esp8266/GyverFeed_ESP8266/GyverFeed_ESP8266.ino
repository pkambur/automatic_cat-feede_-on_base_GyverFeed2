#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <time.h>

// ========= НАСТРОЙКИ ==========
#define WIFI_SSID "YOUR_WIFI_SSID"        // Имя WiFi сети
#define WIFI_PASS "YOUR_WIFI_PASSWORD"    // Пароль WiFi сети
#define DEVICE_NAME "Автокормушка"        // Имя устройства

// Пины для управления кормлением
#define FEED_PIN 0                        // Пин для сигнала кормления (GPIO0, подтянуть к 3.3 В через 10 кОм)
#define LED_PIN 2                         // Встроенный LED для индикации (GPIO2)

// ========= ПЕРЕМЕННЫЕ ==========
ESP8266WebServer server(80);
WiFiClient client;
HTTPClient http;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttReconnectAttempt = 0;

// MQTT топики
#define MQTT_COMMAND_TOPIC "gyverfeed/command"
#define MQTT_STATUS_TOPIC  "gyverfeed/status"

void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttReconnect();
void mqttPublishStatus(const char* status);

bool isConnected = false;
bool isFeeding = false;
unsigned long lastFeedTime = 0;
unsigned long lastFeedTimestamp = 0;
const unsigned long FEED_TIMEOUT = 5000;  // 5 секунд на кормление

char lastFeedTimeStr[25] = "";

// Структура для настроек
struct Settings {
  char ssid[32];
  char password[64];
  char deviceName[32];
  int feedAmount;  // Количество корма в процентах или импульсах
  // MQTT
  char mqttServer[64];
  int mqttPort;
  char mqttUser[32];
  char mqttPass[32];
} settings;

// ========= ФУНКЦИИ ==========

void setup() {
  Serial.begin(9600); // Синхронизация с Arduino
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
  
  // MQTT
  mqttClient.setServer(settings.mqttServer, settings.mqttPort);
  mqttClient.setCallback(mqttCallback);
  
  // NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Ожидание синхронизации времени...");
  time_t now = time(nullptr);
  int wait = 0;
  while (now < 8 * 3600 * 2 && wait < 30) { // ждем максимум 30 секунд
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    wait++;
  }
  Serial.println();
  if (now > 8 * 3600 * 2) {
    Serial.print("Время синхронизировано: ");
    Serial.println(ctime(&now));
  } else {
    Serial.println("Ошибка синхронизации времени!");
  }
  
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
  
  // MQTT reconnect
  if (strlen(settings.mqttServer) > 0) {
    if (!mqttClient.connected()) {
      unsigned long now = millis();
      if (now - lastMqttReconnectAttempt > 5000) {
        lastMqttReconnectAttempt = now;
        mqttReconnect();
      }
    } else {
      mqttClient.loop();
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
        <p>Статус WiFi: %STATUS%</p>
        <p>Статус MQTT: %MQTTSTATUS%</p>
        <form action="/save" method="POST">
          <label>WiFi SSID: <input type="text" name="ssid" value="%SSID%"></label><br>
          <label>WiFi Password: <input type="password" name="password" value="%PASS%"></label><br>
          <label>Device Name: <input type="text" name="deviceName" value="%DEVICENAME%"></label><br>
          <label>Feed Amount: <input type="number" name="feedAmount" value="%FEEDAMOUNT%" min="1" max="1000"></label><br>
          <hr>
          <h2>MQTT настройки</h2>
          <label>MQTT Server: <input type="text" name="mqttServer" value="%MQTTSERVER%"></label><br>
          <label>MQTT Port: <input type="number" name="mqttPort" value="%MQTTPORT%" min="1" max="65535"></label><br>
          <label>MQTT User: <input type="text" name="mqttUser" value="%MQTTUSER%"></label><br>
          <label>MQTT Password: <input type="password" name="mqttPass" value="%MQTTPASS%"></label><br>
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
    html.replace("%FEEDAMOUNT%", String(settings.feedAmount));
    html.replace("%MQTTSERVER%", String(settings.mqttServer));
    html.replace("%MQTTPORT%", String(settings.mqttPort));
    html.replace("%MQTTUSER%", String(settings.mqttUser));
    html.replace("%MQTTPASS%", String(settings.mqttPass));
    html.replace("%MQTTSTATUS%", mqttClient.connected() ? "MQTT подключен" : "MQTT не подключен");
    server.send(200, "text/html", html);
  });
  
  // Сохранение настроек
  server.on("/save", HTTP_POST, []() {
    if (server.hasArg("ssid")) strcpy(settings.ssid, server.arg("ssid").c_str());
    if (server.hasArg("password")) strcpy(settings.password, server.arg("password").c_str());
    if (server.hasArg("deviceName")) strcpy(settings.deviceName, server.arg("deviceName").c_str());
    if (server.hasArg("feedAmount")) settings.feedAmount = server.arg("feedAmount").toInt();
    // MQTT
    if (server.hasArg("mqttServer")) strcpy(settings.mqttServer, server.arg("mqttServer").c_str());
    if (server.hasArg("mqttPort")) settings.mqttPort = server.arg("mqttPort").toInt();
    if (server.hasArg("mqttUser")) strcpy(settings.mqttUser, server.arg("mqttUser").c_str());
    if (server.hasArg("mqttPass")) strcpy(settings.mqttPass, server.arg("mqttPass").c_str());
    
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
  
  server.begin();
  Serial.println("Веб-сервер запущен");
}

// ========= КОРМЛЕНИЕ ==========

void startFeeding() {
  if (isFeeding) return;
  Serial.println("Запуск кормления...");
  isFeeding = true;
  lastFeedTime = millis();
  time_t nowT = time(nullptr);
  strftime(lastFeedTimeStr, sizeof(lastFeedTimeStr), "%Y-%m-%dT%H:%M:%SZ", gmtime(&nowT));
  mqttPublishStatus("feeding");
  
  // Отправляем команду на Arduino через Serial
  Serial.print("FEED:"); // Используем FEED:[количество] для передачи feedAmount
  Serial.println(settings.feedAmount);
  
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
  mqttPublishStatus("idle");
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
    settings.feedAmount = 100; // Значение по умолчанию
    // MQTT defaults
    strcpy(settings.mqttServer, "");
    settings.mqttPort = 1883;
    strcpy(settings.mqttUser, "");
    strcpy(settings.mqttPass, "");
    saveSettings();
  }
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
  Serial.println("Настройки сохранены в EEPROM");
}

void mqttReconnect() {
  if (mqttClient.connected()) return;
  Serial.print("Подключение к MQTT...");
  String clientId = "GyverFeed-" + String(random(0xffff), HEX);
  bool connected = false;
  if (strlen(settings.mqttUser) > 0) {
    connected = mqttClient.connect(clientId.c_str(), settings.mqttUser, settings.mqttPass);
  } else {
    connected = mqttClient.connect(clientId.c_str());
  }
  if (connected) {
    Serial.println("OK");
    mqttClient.subscribe(MQTT_COMMAND_TOPIC);
    mqttPublishStatus("online");
  } else {
    Serial.print("Ошибка MQTT: ");
    Serial.println(mqttClient.state());
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String msg = String((char*)payload);
  Serial.print("MQTT ["); Serial.print(topic); Serial.print("]: "); Serial.println(msg);
  if (String(topic) == MQTT_COMMAND_TOPIC) {
    if (msg == "feed" || msg == "1" || msg == "on") {
      startFeeding();
    } else if (msg == "0" || msg == "off") {
      stopFeeding();
    } else if (msg.startsWith("feed:")) {
      int amount = msg.substring(5).toInt();
      if (amount > 0 && amount <= 1000) {
        settings.feedAmount = amount;
        saveSettings();
        startFeeding();
      }
    } else if (msg == "reboot") {
      ESP.restart();
    } else if (msg == "status") {
      mqttPublishStatus("manual");
    } else if (msg.startsWith("setname:")) {
      String newName = msg.substring(8);
      if (newName.length() > 0 && newName.length() < 32) {
        newName.toCharArray(settings.deviceName, 32);
        saveSettings();
        mqttPublishStatus("name_changed");
      }
    }
  }
}

void mqttPublishStatus(const char* reason) {
  DynamicJsonDocument doc(256);
  doc["state"] = isFeeding ? "feeding" : "idle";
  doc["deviceName"] = settings.deviceName;
  doc["feedAmount"] = settings.feedAmount;
  doc["lastFeed"] = lastFeedTimeStr;
  doc["reason"] = reason;
  String json;
  serializeJson(doc, json);
  mqttClient.publish(MQTT_STATUS_TOPIC, json.c_str(), true);
}