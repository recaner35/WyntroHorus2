#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <Update.h>
#include <vector> // Cihaz listesi için
#include <ESPmDNS.h>
#include <DNSServer.h>

// mDNS nesnesini tanımla
MDNSResponder mDNS;

// Diğer Horus cihazlarının listesi
const int MAX_OTHER_HORUS = 5;
char otherHorusList[MAX_OTHER_HORUS][32];
int otherHorusCount = 0;

// OTA Settings
const char* github_url = "https://api.github.com/repos/recaner35/WyntroHorus2/releases/latest";
const char* FIRMWARE_VERSION = "v1.0.75";

// WiFi Settings
const char* default_ssid = "HorusAP";
const char* default_password = "12345678";
char ssid[32] = "";
char password[64] = "";
char custom_name[21] = "";
char mDNS_hostname[32] = "";

// Motor Settings
int turnsPerDay = 900;
float turnDuration = 10.0;
int direction = 1;
bool running = false;
int completedTurns = 0;
unsigned long lastHourTime = 0;
int hourlyTurns = turnsPerDay / 24;
static int currentStepIndex = 0;
static unsigned long lastStepTime = 0;
static bool forward = true;
float calculatedStepDelay = 0;

// Motor Pins
const int IN1 = 17;
const int IN2 = 5;
const int IN3 = 18;
const int IN4 = 19;

// BUTON Tanımlamaları
const int TOUCH_PIN = 23;

int touchButtonState;             // Butonun mevcut durumunu tutar
int lastTouchButtonState = LOW;   // Butonun önceki durumunu tutar
unsigned long lastDebounceTime = 0; // En son sinyal değişim zamanı
unsigned long debounceDelay = 50;   // Sinyal kararlılığı için bekleme süresi (ms)

// Motor Constants
const int stepsPerTurn = 4096;
const int rampSteps = 200;
const float minStepDelay = 2.0;
const float maxStepDelay = 10.0;

// Stepper motor step sequence (half-step)
const int steps[8][4] = {
    {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
    {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1}
};

// Global Objects
WebServer server(80);
WebSocketsServer webSocket(81);
TaskHandle_t motorTaskHandle = NULL;
DNSServer dnsServer;
uint8_t baseMac[6];
char mac_suffix[5];
const uint8_t EEPROM_INITIALIZED_FLAG = 0xAA;
StaticJsonDocument<4096> scanDoc;

// Function prototypes
void readSettings();
void loadOtherHorusList();
void writeMotorSettings();
void writeWiFiSettings();
void setupWiFi();
void setupMDNS();
void setupWebServer();
void handleSet();
void handleScan();
void handleSaveWiFi();
void handleStatus();
void handleManualUpdate();
void stopMotor();
void startMotor();
void runMotorTask(void *parameter);
void stepMotor(int step);
float calculateStepDelay(int stepIndex, float baseDelay);
void checkHourlyReset();
void resetMotor();
void updateWebSocket();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
String htmlPage();
String manualUpdatePage();
bool isNewVersionAvailable(String latest, String current);
void checkOTAUpdateTask(void *parameter);
String sanitizeString(String input);

void initEEPROM() {
  uint8_t flag;
  EEPROM.readBytes(0, &flag, 1);
  if (flag != EEPROM_INITIALIZED_FLAG) {
    Serial.println("initEEPROM: EEPROM başlatılmamış, sıfırlanıyor...");
    // Mevcut ayarları oku (eğer varsa)
    char tempSsid[32] = "";
    char tempPassword[64] = "";
    char tempCustomName[21] = "";
    EEPROM.readBytes(1, tempSsid, sizeof(tempSsid));
    EEPROM.readBytes(33, tempPassword, sizeof(tempPassword));
    EEPROM.readBytes(97, tempCustomName, sizeof(tempCustomName));

    // EEPROM'u sıfırla
    for (int i = 0; i < 512; i++) {
      EEPROM.writeByte(i, 0);
    }
      EEPROM.writeByte(0, EEPROM_INITIALIZED_FLAG);

    // Önceki ayarları geri yaz (eğer geçerliyse)
    if (strlen(tempSsid) > 0) {
      EEPROM.writeBytes(1, tempSsid, sizeof(tempSsid));
      EEPROM.writeBytes(33, tempPassword, sizeof(tempPassword));
      EEPROM.writeBytes(97, tempCustomName, sizeof(tempCustomName));
    }
      EEPROM.commit();
      Serial.println("initEEPROM: EEPROM sıfırlandı, mevcut ayarlar korundu.");
  } else {
      Serial.println("initEEPROM: EEPROM zaten başlatılmış.");
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  vTaskDelay(pdMS_TO_TICKS(100));
  initEEPROM();
  
  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed, even after format!");
  } else {
    Serial.println("LittleFS mounted successfully!");
  }
  // LittleFS.begin(); // Bu satır gereksizdi, kaldırıldı.

  readSettings();
  loadOtherHorusList(); // Cihaz listesini yükle
  
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);
  stopMotor();
  setupWiFi();
  setupMDNS();
  setupWebServer();
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  xTaskCreate(runMotorTask, "MotorTask", 4096, NULL, 1, NULL);
}

// YENİ EKLENEN FONKSİYON
void checkTouchButton() {
  // Dokunmatik pinden anlık sinyali oku
  int reading = digitalRead(TOUCH_PIN);

  // Eğer okunan sinyal bir önceki okumadan farklıysa, debounce zamanlayıcısını sıfırla
  if (reading != lastTouchButtonState) {
    lastDebounceTime = millis();
  }

  // Sinyal kararlı hale geldiyse (debounce süresi geçtiyse)
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // Eğer butonun durumu değiştiyse
    if (reading != touchButtonState) {
      touchButtonState = reading;

      // Sadece butonun yeni durumu HIGH (basılmış) ise işlem yap
      if (touchButtonState == HIGH) {
        Serial.println("Dokunmatik butona basıldı.");
        // Motor çalışıyorsa durdur, çalışmıyorsa başlat
        if (running) {
          stopMotor();
        } else {
          startMotor();
        }
      }
    }
  }
  
  // Bir sonraki kontrol için mevcut durumu kaydet
  lastTouchButtonState = reading;
}

void loop() {
  static unsigned long lastWiFiCheck = 0;
  if (strlen(ssid) > 0 && WiFi.status() != WL_CONNECTED && millis() - lastWiFiCheck > 30000) {
    Serial.println("loop: WiFi bağlantısı koptu, yeniden bağlanılıyor...");
    WiFi.begin(ssid, password);
    lastWiFiCheck = millis();
  }
  dnsServer.processNextRequest();
  server.handleClient();
  webSocket.loop();
  checkHourlyReset();
  checkTouchButton();
}

String sanitizeString(String input) {
  // Türkçe karakterleri dönüştür
  input.replace("ş", "s");
  input.replace("Ş", "S");
  input.replace("ç", "c");
  input.replace("Ç", "C");
  input.replace("ğ", "g");
  input.replace("Ğ", "G");
  input.replace("ı", "i");
  input.replace("İ", "I");
  input.replace("ö", "o");
  input.replace("Ö", "O");
  input.replace("ü", "u");
  input.replace("Ü", "U");

  input.toLowerCase();

  String output = "";
  bool lastCharWasHyphen = false;

  // Sadece izin verilen karakterleri al, diğerlerini '-' yap
  for (int i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      output += c;
      lastCharWasHyphen = false;
    } else {
      // Birden fazla '-' yan yana gelmesini engelle
      if (!lastCharWasHyphen) {
        output += '-';
        lastCharWasHyphen = true;
      }
    }
  }

  // Baştaki ve sondaki '-' karakterlerini temizle
  while (output.startsWith("-")) {
    output.remove(0, 1);
  }
  while (output.endsWith("-")) {
    output.remove(output.length() - 1);
  }

  if (output.length() == 0) {
    output = "horus";
  }

  return output;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("webSocketEvent: Client [%u] connected\n", num);
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("webSocketEvent: Client [%u] disconnected\n", num);
  } else if (type == WStype_TEXT) {
    String message = String((char*)payload);
    Serial.printf("webSocketEvent: Message from client [%u]: %s\n", num, message.c_str());
    if (message == "status_request") {
      updateWebSocket();
    } else if (message == "ota_check_request") {
      xTaskCreatePinnedToCore(
        checkOTAUpdateTask,
        "OTAUpdateTask",
        10000,
        NULL,
        1,
        NULL,
        0
      );
    }
  }
}
void saveOtherHorusList() {
  StaticJsonDocument<512> doc;
  JsonArray devices = doc.createNestedArray("devices");
  for (int i = 0; i < otherHorusCount; i++) {
    devices.add(otherHorusList[i]);
  }

  File file = LittleFS.open("/other_horus.json", "w");
  if (!file) {
    Serial.println("saveOtherHorusList: Dosya açma hatası.");
    return;
  }
  serializeJson(doc, file);
  file.close();
  Serial.println("saveOtherHorusList: Diğer Horus cihazları kaydedildi.");
}

void loadOtherHorusList() {
  File file = LittleFS.open("/other_horus.json", "r");
  if (!file) {
    Serial.println("loadOtherHorusList: Dosya bulunamadı, varsayılan liste kullanılıyor.");
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.printf("loadOtherHorusList: JSON ayrıştırma hatası: %s\n", error.c_str());
    file.close();
    return;
  }
  
  JsonArray devices = doc["devices"].as<JsonArray>();
  otherHorusCount = 0;
  for (JsonVariant v : devices) {
    if (otherHorusCount < MAX_OTHER_HORUS) {
      strncpy(otherHorusList[otherHorusCount], v.as<const char*>(), 31);
      otherHorusList[otherHorusCount][31] = '\0';
      otherHorusCount++;
    }
  }
  file.close();
  Serial.println("loadOtherHorusList: Diğer Horus cihazları yüklendi.");
}
void stepMotor(int step) {
  digitalWrite(IN1, steps[step][0] ? HIGH : LOW);
  digitalWrite(IN2, steps[step][1] ? HIGH : LOW);
  digitalWrite(IN3, steps[step][2] ? HIGH : LOW);
  digitalWrite(IN4, steps[step][3] ? HIGH : LOW);
}

void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  running = false;
  if (motorTaskHandle != NULL) {
    vTaskSuspend(motorTaskHandle);
  }
  Serial.println("stopMotor: Motor stopped.");
  StaticJsonDocument<256> doc;
  doc["motorStatus"] = "Motor durduruldu.";
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

void startMotor() {
  running = true;
  calculatedStepDelay = (turnDuration * 1000.0) / stepsPerTurn;
  calculatedStepDelay = constrain(calculatedStepDelay, minStepDelay, maxStepDelay);
  Serial.printf("startMotor: Starting motor, stepDelay=%.2fms\n", calculatedStepDelay);
  if (motorTaskHandle != NULL) {
    vTaskResume(motorTaskHandle);
  }
  StaticJsonDocument<256> doc;
  doc["motorStatus"] = "Motor başlatıldı.";
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

float calculateStepDelay(int stepIndex, float baseDelay) {
  if (stepIndex < rampSteps) {
    float progress = (float)stepIndex / rampSteps;
    return maxStepDelay - (maxStepDelay - baseDelay) * progress;
  } else if (stepIndex >= stepsPerTurn - rampSteps) {
    float progress = (float)(stepsPerTurn - stepIndex) / rampSteps;
    return maxStepDelay - (maxStepDelay - baseDelay) * progress;
  }
  return baseDelay;
}

void runMotorTask(void *parameter) {
  static int stepCount = 0;
  for (;;) {
    if (running) {
      if (millis() - lastStepTime >= calculateStepDelay(stepCount, calculatedStepDelay)) {
        if (direction == 1 || (direction == 3 && forward)) {
          currentStepIndex = (currentStepIndex + 1) % 8;
        } else {
          currentStepIndex = (currentStepIndex - 1 + 8) % 8;
        }
        stepMotor(currentStepIndex);
        lastStepTime = millis();
        stepCount++;
        if (stepCount >= stepsPerTurn) {
          stepCount = 0;
          completedTurns++;
          if (direction == 3) forward = !forward;
          Serial.printf("runMotorTask: Turn completed, total turns: %d\n", completedTurns);
          StaticJsonDocument<256> doc;
          doc["motorStatus"] = "Motor çalışıyor, tur: " + String(completedTurns);
          String json;
          serializeJson(doc, json);
          webSocket.broadcastTXT(json);
          updateWebSocket();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void readSettings() {
  int address = 1; // Bayraktan sonra başla
  EEPROM.readBytes(address, ssid, sizeof(ssid));
  address += sizeof(ssid);
  EEPROM.readBytes(address, password, sizeof(password));
  address += sizeof(password);
  EEPROM.readBytes(address, custom_name, sizeof(custom_name));
  address += sizeof(custom_name);
  EEPROM.get(address, turnsPerDay);
  address += sizeof(turnsPerDay);
  EEPROM.get(address, turnDuration);
  address += sizeof(turnDuration);
  EEPROM.get(address, direction);

  // Veriyi geçerli bir metin yap
  ssid[sizeof(ssid) - 1] = '\0';
  password[sizeof(password) - 1] = '\0';
  custom_name[sizeof(custom_name) - 1] = '\0';

  // SSID'nin geçerli olup olmadığını kontrol et
  bool validSSID = true;
  for (int i = 0; i < strlen(ssid); i++) {
    if (!isPrintable(ssid[i])) {
      validSSID = false;
      break;
    }
  }
  if (!validSSID || strlen(ssid) == 0) {
    ssid[0] = '\0'; // Geçersiz veya boşsa sıfırla
  }

  if (turnsPerDay < 600 || turnsPerDay > 1200 || isnan(turnsPerDay)) turnsPerDay = 600;
  if (turnDuration < 10.0 || turnDuration > 15.0 || isnan(turnDuration)) turnDuration = 15.0;
  if (direction < 1 || direction > 3) direction = 1;

  hourlyTurns = turnsPerDay / 24;
  calculatedStepDelay = (turnDuration * 1000.0) / stepsPerTurn;
  calculatedStepDelay = constrain(calculatedStepDelay, minStepDelay, maxStepDelay);
  Serial.printf("readSettings: TPD=%d, Duration=%.2f, Direction=%d, StepDelay=%.2fms, SSID=%s\n",
                turnsPerDay, turnDuration, direction, calculatedStepDelay, ssid);
}

void writeMotorSettings() {
  int address = sizeof(ssid) + sizeof(password) + sizeof(custom_name);
  EEPROM.put(address, turnsPerDay);
  address += sizeof(turnsPerDay);
  EEPROM.put(address, turnDuration);
  address += sizeof(turnDuration);
  EEPROM.put(address, direction);
  EEPROM.commit();
  Serial.printf("writeMotorSettings: TPD=%d, Duration=%.2f, Direction=%d\n", turnsPerDay, turnDuration, direction);
}

void writeWiFiSettings() {
  int address = 0;
  EEPROM.writeBytes(address, ssid, sizeof(ssid));
  address += sizeof(ssid);
  EEPROM.writeBytes(address, password, sizeof(password));
  address += sizeof(password);
  EEPROM.writeBytes(address, custom_name, sizeof(custom_name));
  EEPROM.commit();
  Serial.println("writeWiFiSettings: WiFi settings saved, restarting...");
}

void setupWiFi() {
  Serial.println("setupWiFi: Initializing...");
  readSettings();
  
  WiFi.mode(WIFI_AP_STA);
  vTaskDelay(pdMS_TO_TICKS(10));
  WiFi.macAddress(baseMac);
  sprintf(mac_suffix, "%02x%02x", baseMac[4], baseMac[5]);

  // mDNS adını oluştur
  String sanitizedName = sanitizeString(String(custom_name));
  snprintf(mDNS_hostname, sizeof(mDNS_hostname), "%s-%s", sanitizedName.c_str(), mac_suffix);

  // AP Modunu başlat
  char apSsid[32];
  snprintf(apSsid, sizeof(apSsid), "Horus-%s", mac_suffix);
  WiFi.softAP(apSsid, default_password);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("setupWiFi: AP started: %s, IP: %s\n", apSsid, apIP.toString().c_str());
  dnsServer.start(53, "*", apIP);
  
  // Kayıtlı WiFi varsa bağlanmayı dene
  if (strlen(ssid) > 0) {
    Serial.printf("setupWiFi: Connecting to %s\n", ssid);
    WiFi.begin(ssid, password);
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
      vTaskDelay(pdMS_TO_TICKS(500));
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nsetupWiFi: Connected to %s, IP: %s\n", ssid, WiFi.localIP().toString().c_str());
    } else {
      Serial.println("\nsetupWiFi: Failed to connect to saved WiFi. Keeping saved credentials.");
    }
  } else {
    Serial.println("setupWiFi: No saved WiFi credentials, running in AP mode only.");
  }
}

void setupMDNS() {
  if (mDNS.begin(mDNS_hostname)) {
    Serial.printf("setupMDNS: Started: %s.local\n", mDNS_hostname);
    mDNS.addService("http", "tcp", 80);
    mDNS.addService("ws", "tcp", 81);
  } else {
    Serial.println("setupMDNS: failed to start!");
  }
}

void setupWebServer() {
  server.on("/manifest.json", HTTP_GET, []() {
    String manifest = R"JSON_CONTENT(
  {
    "name": "Horus by Wyntro",
    "short_name": "Horus",
    "description": "Horus motor kontrol uygulaması",
    "start_url": "/",
    "display": "standalone",
    "background_color": "#111827",
    "theme_color": "#3b82f6",
    "scope": "/",
    "icons": [
      {
        "src": "/icon-192x192.png",
        "sizes": "192x192",
        "type": "image/png",
        "purpose": "any"
      },
      {
        "src": "/icon-512x512.png",
        "sizes": "512x512",
        "type": "image/png",
        "purpose": "any maskable"
      }
    ],
    "screenshots": [
      {
        "src": "/screenshot1.png",
        "sizes": "1280x720",
        "type": "image/png",
        "form_factor": "wide"
      }
    ]
  }
  )JSON_CONTENT";
    server.send(200, "application/json", manifest);
  });

  server.on("/sw.js", HTTP_GET, []() {
    String sw = R"SW_SCRIPT(
  const CACHE_NAME = 'horus-v1';
  const urlsToCache = [
    '/',
    '/manifest.json',
    '/icon-192x192.png',
    '/icon-512x512.png'
  ];

  self.addEventListener('install', (event) => {
    event.waitUntil(
      caches.open(CACHE_NAME)
        .then((cache) => cache.addAll(urlsToCache))
    );
  });

  self.addEventListener('fetch', (event) => {
    event.respondWith(
      caches.match(event.request)
        .then((response) => response || fetch(event.request))
    );
  });
    )SW_SCRIPT";
    server.send(200, "application/javascript", sw);
  });

  server.on("/style.css", HTTP_GET, []() {
    File file = LittleFS.open("/style.css", "r");
    if (!file) {
      server.send(404, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "text/css");
    file.close();
  });

  server.on("/script.js", HTTP_GET, []() {
    File file = LittleFS.open("/script.js", "r");
    if (!file) {
      server.send(404, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "application/javascript");
    file.close();
  });

  server.on("/icon-512x512.png", HTTP_GET, []() {
    File file = LittleFS.open("/icon-512x512.png", "r");
    if (!file) {
      server.send(404, "text/plain", "Icon not found");
      return;
    }
    server.streamFile(file, "image/png");
    file.close();
  });

  server.on("/set", HTTP_GET, handleSet);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save_wifi", HTTP_POST, handleSaveWiFi);
  
  server.on("/add_other_horus", HTTP_POST, []() {
    if (otherHorusCount >= MAX_OTHER_HORUS) {
      server.send(200, "text/plain", "Error: Maximum number of devices reached.");
      return;
    }
    String mdns_name = server.arg("mdns_name");
    if (mdns_name.length() > 0 && mdns_name.length() < 32) {
      strncpy(otherHorusList[otherHorusCount], mdns_name.c_str(), 31);
      otherHorusList[otherHorusCount][31] = '\0';
      otherHorusCount++;
      saveOtherHorusList();
      server.send(200, "text/plain", "OK");
    } else {
     server.send(200, "text/plain", "Error: Invalid mDNS name.");
    }
  });

  server.on("/status", HTTP_GET, handleStatus);
  
  server.on("/check_update", HTTP_GET, []() {
    xTaskCreate(
        checkOTAUpdateTask,
        "CheckOTAUpdateTask",
        8192,
        NULL,
        1,
        NULL);
    server.send(200, "text/plain", "OTA check started.");
  });

  server.on("/manual_update", HTTP_GET, []() {
    File file = LittleFS.open("/update.html", "r");
    if (file) {
      server.streamFile(file, "text/html");
      file.close();
    } else {
      server.send(404, "text/plain", "Update page not found");
    }
  });

  server.on("/manual_update", HTTP_POST, []() {
    server.client().setTimeout(30000);
  }, handleManualUpdate);

  server.onNotFound([]() {
    File file = LittleFS.open("/index.html", "r");
    if (file) {
      server.streamFile(file, "text/html");
      file.close();
    } else {
      server.send(404, "text/plain", "Index page not found");
    }
  });

  server.begin();
  Serial.println("setupWebServer: Web server started.");
  Serial.println("setupWebServer: Web socket server started.");
}

void handleSet() {
  if (server.hasArg("tpd")) turnsPerDay = server.arg("tpd").toInt();
  if (server.hasArg("duration")) turnDuration = server.arg("duration").toFloat();
  if (server.hasArg("dir")) direction = server.arg("dir").toInt();
  if (turnsPerDay < 600 || turnsPerDay > 1200) turnsPerDay = 600;
  if (turnDuration < 10.0 || turnDuration > 15.0) turnDuration = 15.0;
  if (direction < 1 || direction > 3) direction = 1;
  hourlyTurns = turnsPerDay / 24;
  calculatedStepDelay = (turnDuration * 1000.0) / stepsPerTurn;
  calculatedStepDelay = constrain(calculatedStepDelay, minStepDelay, maxStepDelay);
  Serial.printf("handleSet: TPD=%d, Duration=%.2f, Direction=%d, StepDelay=%.2fms\n",
                turnsPerDay, turnDuration, direction, calculatedStepDelay);
  writeMotorSettings();
  if (server.hasArg("action")) {
    String action = server.arg("action");
    Serial.println("handleSet: Action=" + action);
    if (action == "start") {
      startMotor();
    } else if (action == "stop") {
      stopMotor();
    } else if (action == "reset") {
      resetMotor();
    } else {
      StaticJsonDocument<256> doc;
      doc["motorStatus"] = "Hata: Geçersiz komut.";
      String json;
      serializeJson(doc, json);
      webSocket.broadcastTXT(json);
    }
  }
  updateWebSocket();
  server.send(200, "text/plain", "OK");
}

void handleSaveWiFi() {
  bool restartRequired = false;

  if (server.hasArg("ssid")) {
    strncpy(ssid, server.arg("ssid").c_str(), sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0'; 

    strncpy(password, server.arg("password").c_str(), sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
    
    restartRequired = true;
  }

  if (server.hasArg("name")) {
    String old_name = String(custom_name);
    String new_name = server.arg("name");
    
    strncpy(custom_name, new_name.c_str(), sizeof(custom_name) - 1);
    custom_name[sizeof(custom_name) - 1] = '\0';

    if (new_name != old_name) {
      String sanitizedName = sanitizeString(new_name);
      char mac_suffix[5];
      sprintf(mac_suffix, "%02x%02x", baseMac[4], baseMac[5]);

      if (sanitizedName.length() > 0) {
        snprintf(mDNS_hostname, sizeof(mDNS_hostname), "%s-%s", sanitizedName.c_str(), mac_suffix);
      } else {
        snprintf(mDNS_hostname, sizeof(mDNS_hostname), "horus-%s", mac_suffix);
      }
      
      MDNS.end();
      setupMDNS();
    }
  }

  writeWiFiSettings();
  server.send(200, "text/plain", "OK");

  if (restartRequired) {
    Serial.println("handleSaveWiFi: WiFi settings saved, restarting...");
    delay(1000);
    ESP.restart();
  } else {
    Serial.println("handleSaveWiFi: Device name updated. New mDNS: " + String(mDNS_hostname));
    updateWebSocket();
  }
}

void handleScan() {
  Serial.println("handleScan: Ağ taraması başlatılıyor (sync)...");
    
  WiFi.mode(WIFI_AP_STA); // Hem AP hem STA modunda çalış
  int networksFound = WiFi.scanNetworks();
    
  if (networksFound == WIFI_SCAN_FAILED) {
    Serial.println("handleScan: Tarama başarısız.");
    server.send(200, "application/json", "{\"status\":\"Scan failed\"}");
      return;
  }
    
  Serial.printf("handleScan: Bulunan ağlar: %d\n", networksFound);
    
  // Her bir SSID ve RSSI değerini logla
  for (int i = 0; i < networksFound; i++) {
    Serial.println("SSID: " + WiFi.SSID(i) + ", RSSI: " + String(WiFi.RSSI(i)));
  }
    
  // JSON oluştur
  scanDoc.clear();
  JsonArray networks = scanDoc.createNestedArray("networks");
  if (!networks) {
    Serial.println("handleScan: JsonArray oluşturma başarısız!");
    server.send(200, "application/json", "{\"status\":\"JSON array creation failed\"}");
    return;
  }
    
  for (int i = 0; i < networksFound; i++) {
    JsonObject network = networks.createNestedObject();
    if (!network) {
      Serial.println("handleScan: JsonObject oluşturma başarısız!");
      continue;
    }
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
  }
    
  String response;
  serializeJson(scanDoc, response);
  Serial.println("handleScan: Gönderilen JSON: " + response);
  server.sendHeader("Access-Control-Allow-Origin", "*"); // CORS için
  server.send(200, "application/json", response);
    
  WiFi.scanDelete(); // Hafızayı temizle
}

void handleStatus() {
  StaticJsonDocument<256> doc;
  doc["status"] = running ? "Çalışıyor" : "Durduruldu";
  doc["completedTurns"] = completedTurns;
  doc["hourlyTurns"] = hourlyTurns;
  doc["turnsPerDay"] = turnsPerDay;
  doc["turnDuration"] = turnDuration;
  doc["direction"] = direction;
  doc["customName"] = custom_name;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
  Serial.println("handleStatus: Status sent.");
}

void handleManualUpdate() {
  StaticJsonDocument<256> statusDoc;
  String json;

  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("handleManualUpdate: Starting firmware upload, free heap: " + String(ESP.getFreeHeap()) + " bytes");
    statusDoc["otaStatus"] = "Dosya yükleniyor...";
    serializeJson(statusDoc, json);
    webSocket.broadcastTXT(json);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      statusDoc["otaStatus"] = "Hata: Güncelleme başlatılamadı.";
      serializeJson(statusDoc, json);
      webSocket.broadcastTXT(json);
      Serial.println("handleManualUpdate: Update.begin failed.");
      server.send(500, "text/plain", "Update begin failed.");
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      statusDoc["otaStatus"] = "Hata: Dosya yazma başarısız.";
      serializeJson(statusDoc, json);
      webSocket.broadcastTXT(json);
      Serial.println("handleManualUpdate: Failed to write firmware.");
      server.send(500, "text/plain", "Failed to write firmware.");
      return;
    }
    Serial.printf("handleManualUpdate: Wrote %d bytes\n", upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      statusDoc["otaStatus"] = "Güncelleme başarılı! Yeniden başlatılıyor...";
      serializeJson(statusDoc, json);
      webSocket.broadcastTXT(json);
      Serial.println("handleManualUpdate: Firmware update completed, restarting...");
      server.send(200, "text/plain", "Firmware updated successfully.");
      delay(1000);
      ESP.restart();
    } else {
      statusDoc["otaStatus"] = "Hata: Güncelleme tamamlanamadı.";
      serializeJson(statusDoc, json);
      webSocket.broadcastTXT(json);
      Serial.println("handleManualUpdate: Update failed to finalize.");
      server.send(500, "text/plain", "Update failed to finalize.");
    }
  }
}

void resetMotor() {
  stopMotor();
  turnsPerDay = 600;
  turnDuration = 15.0;
  direction = 1;
  completedTurns = 0;
  hourlyTurns = turnsPerDay / 24;
  currentStepIndex = 0;
  lastStepTime = millis();
  calculatedStepDelay = (turnDuration * 1000.0) / stepsPerTurn;
  calculatedStepDelay = constrain(calculatedStepDelay, minStepDelay, maxStepDelay);
  Serial.printf("resetMotor: TPD=%d, Duration=%.2f, Direction=%d, StepDelay=%.2fms\n",
                turnsPerDay, turnDuration, direction, calculatedStepDelay);
  writeMotorSettings();
  updateWebSocket();
}

void checkHourlyReset() {
  unsigned long currentTime = millis();
  if (currentTime - lastHourTime >= 3600000) {
    completedTurns = 0;
    lastHourTime = currentTime;
    updateWebSocket();
    Serial.println("checkHourlyReset: Hourly reset, completedTurns=0");
  }
}

bool isNewVersionAvailable(String latest, String current) {
  latest.replace("v", "");
  current.replace("v", "");
  int latestMajor = 0, latestMinor = 0, latestPatch = 0;
  int currentMajor = 0, currentMinor = 0, currentPatch = 0;
  sscanf(latest.c_str(), "%d.%d.%d", &latestMajor, &latestMinor, &latestPatch);
  sscanf(current.c_str(), "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);
  if (latestMajor > currentMajor) return true;
  if (latestMajor < currentMajor) return false;
  if (latestMinor > currentMinor) return true;
  if (latestMinor < currentMinor) return false;
  if (latestPatch > currentPatch) return true;
  return false;
}

void checkOTAUpdateTask(void *parameter) {
  Serial.println("checkOTAUpdateTask: Started.");
  StaticJsonDocument<256> statusDoc;
  String json;

  if (WiFi.status() != WL_CONNECTED) {
    statusDoc["otaStatus"] = "Hata: İnternet bağlantısı yok, lütfen WiFi ağına bağlanın.";
    statusDoc["updateAvailable"] = false;
    serializeJson(statusDoc, json);
    webSocket.broadcastTXT(json);
    Serial.println("checkOTAUpdateTask: No WiFi connection.");
    vTaskDelete(NULL);
    return;
  }

  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(5);
  http.addHeader("Accept", "application/vnd.github.v3+json");
  http.addHeader("User-Agent", "ESP32-WyntroHorus2/1.0");

  Serial.println("checkOTAUpdateTask: Fetching latest release from " + String(github_url));
  if (!http.begin(github_url)) {
    statusDoc["otaStatus"] = "Hata: HTTP bağlantısı başlatılamadı.";
    statusDoc["updateAvailable"] = false;
    serializeJson(statusDoc, json);
    webSocket.broadcastTXT(json);
    Serial.println("checkOTAUpdateTask: Failed to begin HTTP connection.");
    http.end();
    vTaskDelete(NULL);
    return;
  }

  int httpCode = http.GET();
  Serial.printf("checkOTAUpdateTask: HTTP response code: %d\n", httpCode);

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    if (payload.length() == 0) {
      statusDoc["otaStatus"] = "Hata: GitHub'dan boş yanıt alındı.";
      statusDoc["updateAvailable"] = false;
      serializeJson(statusDoc, json);
      webSocket.broadcastTXT(json);
      Serial.println("checkOTAUpdateTask: Received empty response from GitHub.");
      http.end();
      vTaskDelete(NULL);
      return;
    }

    DynamicJsonDocument doc(2048); // Increased size to handle assets
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      String latestVersion = doc["tag_name"].as<String>();
      String currentVersion = String(FIRMWARE_VERSION);
      Serial.println("checkOTAUpdateTask: Latest version: " + latestVersion + ", Current version: " + currentVersion);

      if (isNewVersionAvailable(latestVersion, currentVersion)) {
        statusDoc["otaStatus"] = "Yeni sürüm mevcut: " + latestVersion;
        statusDoc["updateAvailable"] = true;
        serializeJson(statusDoc, json);
        webSocket.broadcastTXT(json);

        String firmwareUrl;
        String filesystemUrl;

        JsonArray assets = doc["assets"].as<JsonArray>();
        for (JsonVariant asset : assets) {
          String name = asset["name"].as<String>();
          if (name.endsWith(".bin")) {
            if (name.startsWith("wyntrohorus2")) {
              firmwareUrl = asset["browser_download_url"].as<String>();
            } else if (name == "filesystem.bin") {
              filesystemUrl = asset["browser_download_url"].as<String>();
            }
          }
        }

        if (firmwareUrl.length() > 0) {
          Serial.println("checkOTAUpdateTask: Updating FIRMWARE from " + firmwareUrl);
          statusDoc["otaStatus"] = "Firmware indiriliyor: " + latestVersion;
          serializeJson(statusDoc, json);
          webSocket.broadcastTXT(json);

          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            Serial.println("Firmware update failed to begin!");
            statusDoc["otaStatus"] = "Hata: Firmware güncelleme başlatılamadı.";
            serializeJson(statusDoc, json);
            webSocket.broadcastTXT(json);
            vTaskDelete(NULL);
            return;
          }
          
          http.begin(firmwareUrl);
          int httpCodeBin = http.GET();
          if (httpCodeBin == HTTP_CODE_OK) {
            WiFiClient *client = http.getStreamPtr();
            size_t written = Update.writeStream(*client);
            if (written == Update.size()) {
              Serial.println("Firmware written successfully");
            } else {
              Serial.println("Firmware written failed");
            }
          } else {
            Serial.println("Firmware download failed");
          }
          http.end();

          if (!Update.end(false)) { // Do not reboot
            Serial.printf("Firmware update failed with error code: %u\n", Update.getError());
            statusDoc["otaStatus"] = "Hata: Firmware güncelleme tamamlanamadı.";
            serializeJson(statusDoc, json);
            webSocket.broadcastTXT(json);
            vTaskDelete(NULL);
            return;
          }
          Serial.println("Firmware update finished successfully.");

          // Now update filesystem
          if (filesystemUrl.length() > 0) {
              Serial.println("checkOTAUpdateTask: Updating FILESYSTEM from " + filesystemUrl);
              statusDoc["otaStatus"] = "Dosya sistemi indiriliyor...";
              serializeJson(statusDoc, json);
              webSocket.broadcastTXT(json);

              if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
                  Serial.println("Filesystem update failed to begin!");
                  statusDoc["otaStatus"] = "Hata: Dosya sistemi güncelleme başlatılamadı.";
                  serializeJson(statusDoc, json);
                  webSocket.broadcastTXT(json);
                  vTaskDelete(NULL);
                  return;
              }
              
              http.begin(filesystemUrl);
              httpCodeBin = http.GET();
              if (httpCodeBin == HTTP_CODE_OK) {
                WiFiClient *client = http.getStreamPtr();
                size_t written = Update.writeStream(*client);
                if (written == Update.size()) {
                  Serial.println("Filesystem written successfully");
                } else {
                  Serial.println("Filesystem written failed");
                }
              } else {
                Serial.println("Filesystem download failed");
              }
              http.end();

              if (!Update.end(true)) { // Reboot on success
                  Serial.printf("Filesystem update failed with error code: %u\n", Update.getError());
                  statusDoc["otaStatus"] = "Hata: Dosya sistemi güncelleme tamamlanamadı.";
                  serializeJson(statusDoc, json);
                  webSocket.broadcastTXT(json);
                  vTaskDelete(NULL);
                  return;
              }
              Serial.println("Filesystem update finished. Rebooting...");
              writeWiFiSettings();
              writeMotorSettings();
              ESP.restart();
          } else {
            Serial.println("No filesystem.bin found, rebooting after firmware update.");
            ESP.restart();
          }
        } else {
          statusDoc["otaStatus"] = "Hata: .bin dosyası bulunamadı.";
          statusDoc["updateAvailable"] = false;
          serializeJson(statusDoc, json);
          webSocket.broadcastTXT(json);
        }
      } else {
        statusDoc["otaStatus"] = "En son sürüme sahipsiniz.";
        statusDoc["updateAvailable"] = false;
        serializeJson(statusDoc, json);
        webSocket.broadcastTXT(json);
      }
    } else {
      statusDoc["otaStatus"] = "Hata: JSON ayrıştırma hatası: " + String(error.c_str());
      statusDoc["updateAvailable"] = false;
      serializeJson(statusDoc, json);
      webSocket.broadcastTXT(json);
    }
  } else {
    statusDoc["otaStatus"] = "Hata: OTA kontrol başarısız, HTTP " + String(httpCode);
    statusDoc["updateAvailable"] = false;
    serializeJson(statusDoc, json);
    webSocket.broadcastTXT(json);
  }

  http.end();
  vTaskDelete(NULL);
}
void updateWebSocket() {
  StaticJsonDocument<512> doc;
  doc["motorStatus"] = running ? "Çalışıyor" : "Durduruldu";
  doc["completedTurns"] = completedTurns;
  doc["tpd"] = turnsPerDay;
  doc["duration"] = turnDuration;
  doc["direction"] = direction;
  doc["version"] = FIRMWARE_VERSION;
  doc["customName"] = custom_name; 
  doc["mDNSHostname"] = mDNS_hostname;

  if (WiFi.status() == WL_CONNECTED) {
    doc["ip"] = WiFi.localIP().toString();
  } else {
    doc["ip"] = WiFi.softAPIP().toString();
  }

  JsonArray otherHorus = doc.createNestedArray("otherHorus");
  for (int i = 0; i < otherHorusCount; i++) {
    otherHorus.add(otherHorusList[i]);
  }

  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}
