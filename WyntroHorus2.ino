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
const char* FIRMWARE_VERSION = "v1.0.68";

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

void setup() {
  Serial.begin(115200);
  vTaskDelay(pdMS_TO_TICKS(100));
  
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
  stopMotor();
  setupWiFi();
  setupMDNS();
  setupWebServer();
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  xTaskCreate(runMotorTask, "MotorTask", 4096, NULL, 1, NULL);
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  webSocket.loop();
  checkHourlyReset();
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
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void readSettings() {
  int address = 0;
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
  if (turnsPerDay < 600 || turnsPerDay > 1200 || isnan(turnsPerDay)) turnsPerDay = 600;
  if (turnDuration < 10.0 || turnDuration > 15.0 || isnan(turnDuration)) turnDuration = 15.0;
  if (direction < 1 || direction > 3) direction = 1;
  if (strlen(ssid) > 31) ssid[0] = '\0';
  if (strlen(password) > 63) password[0] = '\0';
  if (strlen(custom_name) > 20) custom_name[0] = '\0';
  hourlyTurns = turnsPerDay / 24;
  calculatedStepDelay = (turnDuration * 1000.0) / stepsPerTurn;
  calculatedStepDelay = constrain(calculatedStepDelay, minStepDelay, maxStepDelay);
  Serial.printf("readSettings: TPD=%d, Duration=%.2f, Direction=%d, StepDelay=%.2fms\n",
                turnsPerDay, turnDuration, direction, calculatedStepDelay);
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

  // MAC adresini bir kez oku
  WiFi.mode(WIFI_STA); // STA modunda sabit MAC için
  vTaskDelay(pdMS_TO_TICKS(10)); // WiFi modülünün başlaması için kısa bir gecikme
  WiFi.macAddress(baseMac); // Base MAC adresini al
  sprintf(mac_suffix, "%02x%02x", baseMac[4], baseMac[5]); // Küçük harf için %x

  // mDNS ismini oluştur
  if (strcmp(custom_name, "") != 0) {
    String sanitizedName = sanitizeString(String(custom_name));
    snprintf(mDNS_hostname, sizeof(mDNS_hostname), "%s-%s", sanitizedName.c_str(), mac_suffix);
  } else {
    snprintf(mDNS_hostname, sizeof(mDNS_hostname), "horus-%s", mac_suffix);
  }

  if (strcmp(ssid, "") == 0) {
    Serial.println("setupWiFi: Invalid WiFi credentials, running in AP mode only.");

    char apSsid[32];
    snprintf(apSsid, sizeof(apSsid), "Horus-%s", mac_suffix);

    WiFi.mode(WIFI_AP); // AP moduna geç
    vTaskDelay(pdMS_TO_TICKS(10)); // Mod geçişi için kısa bir gecikme
    WiFi.softAP(apSsid, default_password);
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("setupWiFi: AP started: %s, IP: %s\n", apSsid, apIP.toString().c_str());

    dnsServer.start(53, "*", apIP); // Captive portal için DNS sunucusunu başlat
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.printf("setupWiFi: Connecting to %s", ssid);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      vTaskDelay(pdMS_TO_TICKS(500)); // WDT'yi sıfırlamak için vTaskDelay kullan
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nsetupWiFi: Connected to %s, IP: %s\n", ssid, WiFi.localIP().toString().c_str());
      // mDNS ana bilgisayar adını ayarla
      if (strcmp(custom_name, "") != 0) {
        String sanitizedName = sanitizeString(String(custom_name));
        strncpy(mDNS_hostname, sanitizedName.c_str(), sizeof(mDNS_hostname));
      } else {
        char macStr[13];
        sprintf(macStr, "%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
        sprintf(mDNS_hostname, "horus-%s", macStr + 8);
      }
    } else {
      Serial.println("\nsetupWiFi: Failed to connect, running in AP mode.");
      WiFi.mode(WIFI_AP);
      vTaskDelay(pdMS_TO_TICKS(10)); // Mod geçişi için kısa bir gecikme
      char apSsid[32];
      sprintf(apSsid, "Horus-%s", mac_suffix);
      WiFi.softAP(apSsid, default_password);
      IPAddress apIP = WiFi.softAPIP();
      Serial.printf("setupWiFi: AP started: %s, IP: %s\n", apSsid, apIP.toString().c_str());

      dnsServer.start(53, "*", apIP); // Bağlantı başarısız olduğunda DNS sunucusunu başlat

      // mDNS ana bilgisayar adını AP adına göre ayarla
      sprintf(mDNS_hostname, "horus-%s", mac_suffix);
    }
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
  
  server.on("/manual_update", HTTP_GET, []() { server.send(200, "text/html", manualUpdatePage()); });
  server.on("/manual_update", HTTP_POST, []() { server.client().setTimeout(30000); }, handleManualUpdate);
  
  server.onNotFound([]() {
    server.send(200, "text/html", htmlPage());
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

void handleScan() {
  String options = "";
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    String ssid_scan = WiFi.SSID(i);
    options += "<option value=\"" + ssid_scan + "\">" + ssid_scan + "</option>";
  }
  Serial.println("handleScan: WiFi scan completed, found " + String(n) + " networks.");
  server.send(200, "text/plain", options);
}

void handleSaveWiFi() {
  bool restartRequired = false;

  // Sadece WiFi bilgileri gönderildiyse restart gerekir
  if (server.hasArg("ssid")) {
    strncpy(ssid, server.arg("ssid").c_str(), sizeof(ssid));
    strncpy(password, server.arg("password").c_str(), sizeof(password));
    restartRequired = true;
  }

  // İsim her durumda güncellenebilir
  if (server.hasArg("name")) {
    String old_name = String(custom_name);
    String new_name = server.arg("name");
    
    strncpy(custom_name, new_name.c_str(), sizeof(custom_name));

    // Eğer isim gerçekten değiştiyse mDNS'i yeniden başlat
    if (new_name != old_name) {
      String sanitizedName = sanitizeString(new_name);
      
      char mac_suffix[5];
      sprintf(mac_suffix, "%02x%02x", baseMac[4], baseMac[5]); // Küçük harf için %x

      if (sanitizedName.length() > 0) {
        // Eğer bir isim girildiyse, ismin sonuna MAC ekle
        snprintf(mDNS_hostname, sizeof(mDNS_hostname), "%s-%s", sanitizedName.c_str(), mac_suffix);
      } else {
        // Eğer isim boş ise, standart ismi kullan
        snprintf(mDNS_hostname, sizeof(mDNS_hostname), "horus-%s", mac_suffix);
      }
      
      MDNS.end();
      setupMDNS();
    }
  }

  writeWiFiSettings(); // Ayarları EEPROM'a yaz
  server.send(200, "text/plain", "OK");

  if (restartRequired) {
    Serial.println("handleSaveWiFi: WiFi settings saved, restarting...");
    delay(1000);
    ESP.restart();
  } else {
    Serial.println("handleSaveWiFi: Device name updated. New mDNS: " + String(mDNS_hostname));
    updateWebSocket(); // Arayüzü yeni isimle güncelle
  }
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
    
    // Yanıtın boş olup olmadığını kontrol edin
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

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      String latestVersion = doc["tag_name"].as<String>();
      String currentVersion = String(FIRMWARE_VERSION);
      Serial.println("checkOTAUpdateTask: Latest version: " + latestVersion + ", Current version: " + currentVersion);

      if (isNewVersionAvailable(latestVersion, currentVersion)) {
        statusDoc["otaStatus"] = "Yeni sürüm mevcut: " + latestVersion;
        statusDoc["updateAvailable"] = true;
        String binUrl;
        for (JsonVariant asset : doc["assets"].as<JsonArray>()) {
          String name = asset["name"].as<String>();
          if (name.endsWith(".bin")) {
            binUrl = asset["browser_download_url"].as<String>();
            break;
          }
        }

        if (binUrl.length() > 0) {
          Serial.println("checkOTAUpdateTask: Downloading firmware from " + binUrl);
          statusDoc["otaStatus"] = "Güncelleme indiriliyor: " + latestVersion;
          serializeJson(statusDoc, json);
          webSocket.broadcastTXT(json);
          http.end();

          // Yönlendirme hatasını çözmek için yeni bir HTTPClient nesnesi kullanacağız
          HTTPClient http_bin;
          http_bin.setTimeout(15000);
          http_bin.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
          http_bin.setRedirectLimit(5);
          http_bin.addHeader("User-Agent", "ESP32-WyntroHorus2/1.0");

          http_bin.begin(binUrl);
          Serial.println("checkOTAUpdateTask: Initiating HTTP GET for firmware...");
          int httpCodeBin = http_bin.GET();
          Serial.printf("checkOTAUpdateTask: Firmware HTTP response code: %d\n", httpCodeBin);

          if (httpCodeBin == HTTP_CODE_OK) {
            size_t size = http_bin.getSize();
            Serial.println("checkOTAUpdateTask: File size: " + String(size));

            if (size <= 0) {
              statusDoc["otaStatus"] = "Hata: Dosya boyutu bilinmiyor.";
              statusDoc["updateAvailable"] = false;
              serializeJson(statusDoc, json);
              webSocket.broadcastTXT(json);
              Serial.println("checkOTAUpdateTask: Unknown file size.");
              http_bin.end();
              vTaskDelete(NULL);
              return;
            }

            if (Update.begin(size)) {
              Serial.println("checkOTAUpdateTask: Starting OTA update, size: " + String(size));
              WiFiClient *client = http_bin.getStreamPtr();
              size_t written = Update.writeStream(*client);
              
              if (written == size) {
                Serial.println("checkOTAUpdateTask: Firmware written successfully.");
                if (Update.end(true)) {
                  statusDoc["otaStatus"] = "Güncelleme başarılı! Yeniden başlatılıyor...";
                  statusDoc["updateAvailable"] = false;
                  serializeJson(statusDoc, json);
                  webSocket.broadcastTXT(json);
                  Serial.println("checkOTAUpdateTask: Update completed, restarting...");
                  delay(1000);
                  ESP.restart();
                } else {
                  statusDoc["otaStatus"] = "Hata: Güncelleme tamamlanamadı.";
                  statusDoc["updateAvailable"] = false;
                  Serial.println("checkOTAUpdateTask: Update failed to finalize.");
                }
              } else {
                statusDoc["otaStatus"] = "Hata: Dosya yazma başarısız.";
                statusDoc["updateAvailable"] = false;
                Serial.println("checkOTAUpdateTask: Failed to write firmware, written: " + String(written));
              }
            } else {
              statusDoc["otaStatus"] = "Hata: Güncelleme başlatılamadı, yetersiz alan.";
              statusDoc["updateAvailable"] = false;
              Serial.println("checkOTAUpdateTask: Update.begin failed, size: " + String(size));
            }
          } else {
            statusDoc["otaStatus"] = "Hata: Dosya indirilemedi, HTTP " + String(httpCodeBin);
            statusDoc["updateAvailable"] = false;
            serializeJson(statusDoc, json);
            webSocket.broadcastTXT(json);
            Serial.println("checkOTAUpdateTask: Failed to download firmware, HTTP code: " + String(httpCodeBin));
          }
          http_bin.end();
        } else {
          statusDoc["otaStatus"] = "Hata: .bin dosyası bulunamadı.";
          statusDoc["updateAvailable"] = false;
          Serial.println("checkOTAUpdateTask: No .bin file found in release.");
        }
      } else {
        statusDoc["otaStatus"] = "En son sürüme sahipsiniz.";
        statusDoc["updateAvailable"] = false;
        Serial.println("checkOTAUpdateTask: No new version available.");
      }
    } else {
      statusDoc["otaStatus"] = "Hata: JSON ayrıştırma hatası: " + String(error.c_str());
      statusDoc["updateAvailable"] = false;
      Serial.println("checkOTAUpdateTask: JSON parse error: " + String(error.c_str()));
    }
  } else {
    statusDoc["otaStatus"] = "Hata: OTA kontrol başarısız, HTTP " + String(httpCode);
    statusDoc["updateAvailable"] = false;
    Serial.println("checkOTAUpdateTask: HTTP error: " + String(httpCode));
  }

  http.end();
  serializeJson(statusDoc, json);
  webSocket.broadcastTXT(json);
  Serial.println("checkOTAUpdateTask: Completed.");
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

  if (WiFi.getMode() == WIFI_AP) {
    doc["ip"] = WiFi.softAPIP().toString();
  } else {
    doc["ip"] = WiFi.localIP().toString();
  }

  JsonArray otherHorus = doc.createNestedArray("otherHorus");
  for (int i = 0; i < otherHorusCount; i++) {
    otherHorus.add(otherHorusList[i]);
  }

  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

String htmlPage() {
  String page = R"PAGE_HTML(
    <!DOCTYPE html>
    <html lang="tr">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
        <title>Horus</title>
        <meta name="apple-mobile-web-app-capable" content="yes">
        <link rel="manifest" href="/manifest.json">
        <link rel="apple-touch-icon" href="/icon-192x192.png">
        <style>
            :root {
                --bg-color: #111827;
                --text-color: #f9fafb;
                --primary-color: #3b82f6;
                --secondary-color: #d1d5db;
                --card-bg: #1f2937;
                --border-color: #4b5563;
                --tab-bg: #374151;
                --tab-text: #f9fafb;
                --tab-active-bg: var(--primary-color);
            }
            body.light {
                --bg-color: #f3f4f6;
                --text-color: #1f2937;
                --primary-color: #2563eb;
                --secondary-color: #4b5563;
                --card-bg: #ffffff;
                --border-color: #d1d5db;
                --tab-bg: #e5e7eb;
                --tab-text: #1f2937;
                --tab-active-bg: var(--primary-color);
            }
            body {
                font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                background-color: var(--bg-color);
                color: var(--text-color);
                margin: 0;
                padding: 20px;
                display: flex;
                flex-direction: column;
                justify-content: center;
                align-items: center;
                min-height: 100vh;
            }
            .container {
                width: 100%;
                max-width: 600px;
            }
            .card {
                background-color: var(--card-bg);
                border-radius: 12px;
                padding: 20px;
                margin-bottom: 20px;
                box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
                border: 1px solid var(--border-color);
            }
            h1, h2 {
                color: var(--primary-color);
                text-align: center;
            }
            h1 {
                font-size: 2.5em;
            }
            .status-section {
                display: grid;
                grid-template-columns: repeat(2, 1fr);
                gap: 10px;
            }
            .status-item {
                background-color: var(--card-bg);
                border-radius: 8px;
                padding: 15px;
                border: 1px solid var(--border-color);
                text-align: center;
            }
            .status-item span {
                display: block;
                font-size: 14px;
                color: var(--secondary-color);
            }
            .status-item h3 {
                margin: 5px 0 0;
                font-size: 20px;
                color: var(--text-color);
                font-weight: bold;
            }
            .form-group {
                margin-bottom: 15px;
            }
            .form-group label {
                display: block;
                margin-bottom: 5px;
                color: var(--secondary-color);
            }
            .form-group input[type="number"], .form-group select, .form-group input[type="text"] {
                width: 100%;
                padding: 10px;
                background-color: #374151;
                border: 1px solid var(--border-color);
                border-radius: 6px;
                color: var(--text-color);
                box-sizing: border-box;
            }
            .form-group.inline-group {
                display: flex;
                gap: 10px;
                align-items: center;
            }
            .form-group.inline-group input[type="text"] {
                flex-grow: 1;
            }
            body.light .form-group input[type="number"], body.light .form-group select, body.light .form-group input[type="text"] {
                background-color: #e5e7eb;
                color: #1f2937;
            }
            .form-group input[type="number"]:focus, .form-group select:focus, .form-group input[type="text"]:focus {
                outline: none;
                border-color: var(--primary-color);
                box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.5);
            }
            .form-group input[type="range"] {
                width: 100%;
                margin: 0 auto;
                display: block;
            }
            .radio-group {
                display: flex;
                flex-wrap: wrap;
                gap: 10px;
                margin-bottom: 15px;
            }
            .radio-item {
                flex: 1 1 0;
                text-align: center;
            }
            .radio-item input[type="radio"] {
                display: none;
            }
            .radio-item label {
                display: block;
                padding: 10px;
                background-color: #374151;
                border: 1px solid var(--border-color);
                border-radius: 6px;
                cursor: pointer;
                transition: all 0.2s ease;
            }
            body.light .radio-item label {
                background-color: #e5e7eb;
                color: #1f2937;
            }
            .radio-item input[type="radio"]:checked + label {
                background-color: var(--primary-color);
                border-color: var(--primary-color);
                color: var(--text-color);
            }
            .button-group {
                display: flex;
                flex-wrap: wrap;
                gap: 10px;
                margin-top: 20px;
            }
            .button {
                flex: 1 1 auto;
                padding: 12px 16px;
                border-radius: 6px;
                font-size: 16px;
                font-weight: bold;
                cursor: pointer;
                transition: background-color 0.3s ease;
                text-align: center;
                border: none;
                color: var(--text-color);
            }
            .button.primary {
                background-color: var(--primary-color);
            }
            .button.primary:hover {
                background-color: #2563eb;
            }
            .button.secondary {
                background-color: #4b5563;
            }
            .button.secondary:hover {
                background-color: #6b7280;
            }
            body.light .button.secondary {
                background-color: #e5e7eb;
                color: #1f2937;
            }
            body.light .button.secondary:hover {
                background-color: #d1d5db;
            }
            #message_box {
                margin-top: 15px;
                padding: 10px;
                background-color: #374151;
                border: 1px solid #4b5563;
                border-radius: 6px;
                color: #f9fafb;
                text-align: center;
                display: none;
            }
            body.light #message_box {
                background-color: #e5e7eb;
                color: #1f2937;
                border-color: #d1d5db;
            }
            .other-horus-list {
                margin-top: 15px;
            }
            .other-horus-item {
                display: flex;
                align-items: center;
                justify-content: space-between;
                padding: 10px;
                background-color: #374151;
                border: 1px solid var(--border-color);
                border-radius: 6px;
                margin-bottom: 5px;
            }
            body.light .other-horus-item {
                background-color: #e5e7eb;
                color: #1f2937;
            }
            .other-horus-item span {
                flex-grow: 1;
                text-align: left;
                padding-right: 10px;
            }
            .other-horus-item .button {
                padding: 8px 12px;
                font-size: 14px;
            }
            .tabs-container {
                display: flex;
                flex-wrap: wrap;
                border-bottom: 1px solid var(--border-color);
                margin-bottom: 20px;
                gap: 5px;
            }
            .tab {
                padding: 10px 15px;
                cursor: pointer;
                background-color: var(--tab-bg);
                color: var(--tab-text);
                border: 1px solid var(--border-color);
                border-bottom: none;
                border-top-left-radius: 8px;
                border-top-right-radius: 8px;
                transition: background-color 0.3s;
            }
            .tab.active {
                background-color: var(--primary-color);
                color: #f9fafb;
                border-color: var(--primary-color);
            }
            .tab-content {
                display: none;
            }
            .tab-content.active {
                display: block;
            }
            .card h1 {
                display: none;
            }
            .logo-svg {
                width: 100%;
                max-width: 300px;
                height: auto;
                display: block;
                margin: 0 auto;
                fill: var(--text-color);
            }
            .logo-svg path {
                fill: currentColor;
            }
            .footer-text {
                text-align: center;
                margin-top: 20px;
                font-size: 0.8em;
                color: var(--border-color);
            }
        </style>
    </head>
    <body>
    <div class="container">
        <div class="card">
            <svg class="logo-svg" viewBox="0 0 2475.04 1237.52" xml:space="preserve"><path class="fil0" d="M715.78 686.79l48.51 0 25.46 0c3.64,0 6.63,-2.91 6.63,-6.38 0,-3.56 -2.99,-6.39 -6.63,-6.39 -10.26,0 -18.75,-8.17 -18.91,-18.03l0 -277.95c0.16,-10.02 8.65,-18.11 18.91,-18.11 3.64,0 6.63,-2.91 6.63,-6.39 0,-3.55 -2.99,-6.38 -6.63,-6.38l-46.81 0 0 0 -27.16 0c-3.64,0 -6.55,2.83 -6.55,6.38 0,3.48 2.91,6.39 6.55,6.39 10.43,0 18.92,8.17 18.92,18.27l0.08 107.28c0,5.74 1.78,11.16 4.85,15.77l-183.93 0 0 -123.05c0,-10.1 8.49,-18.27 18.92,-18.27 3.64,0 6.55,-2.91 6.55,-6.39 0,-3.55 -2.91,-6.38 -6.55,-6.38l-46.81 0 -0.08 0 -27.08 0c-3.64,0 -6.63,2.83 -6.63,6.38 0,3.48 2.99,6.39 6.63,6.39 10.35,0 18.83,8.17 18.92,18.19l0 277.63c-0.09,10.1 -8.57,18.27 -18.92,18.27 -3.64,0 -6.63,2.83 -6.63,6.39 0,3.47 2.99,6.38 6.63,6.38l73.97 0c3.64,0 6.55,-2.91 6.55,-6.38 0,-3.56 -2.91,-6.39 -6.55,-6.39 -10.43,0 -18.92,-8.25 -19,-18.27l0 -126.04c0,-5.74 -1.69,-11.16 -4.85,-15.77l184.01 0 0 140.11 -0.08 0 0 1.7c0,10.02 -8.41,18.27 -18.92,18.27 -3.64,0 -6.55,2.83 -6.55,6.39 0,3.47 2.91,6.38 6.55,6.38l0 0zm272.86 6.23c1.05,0 2.02,-0.08 3.07,-0.08 0,0 3.64,-0.09 4.04,-0.17l1.38 0 0.16 -0.08c89.82,-4.6 159.99,-81.57 159.99,-175.68 0,-97.17 -75.67,-176.16 -168.64,-176.16 -2.43,0 -4.93,0.08 -7.12,0.24l-2.42 0 -0.08 0.08c-89.42,5.18 -159.03,81.98 -159.03,175.84 0,97.02 75.67,176.01 168.65,176.01l0 0zm-136.8 -169.62c3.64,0 6.63,-2.91 6.63,-6.39 0,-18.83 2.51,-37.27 7.52,-54.81 17.71,-62.49 63.22,-105.02 116.02,-108.33 2.18,-0.17 4.44,-0.24 6.63,-0.24 83.51,0 152.07,69.93 155.3,156.92l-18.59 0c-3.64,0 -6.63,2.91 -6.63,6.46 0,24.1 -4.13,47.3 -12.13,69.05 -20.45,54.89 -63.14,90.95 -111.32,93.94l-3.16 0.16c-1.13,0 -2.34,0.08 -3.47,0.08 -83.52,0 -152.08,-69.85 -155.31,-156.84l18.51 0zm602.95 163.39c3.64,0 6.55,-2.83 6.55,-6.38 0,-3.56 -2.91,-6.39 -6.55,-6.39 -0.96,0 -1.93,-0.08 -2.99,-0.08 -14.14,-0.97 -26.68,-8.57 -33.71,-20.54l-0.08 -0.08 -0.73 -1.29 -0.08 -0.08 -68.23 -110.68 -0.49 -0.65c-4.85,-6.87 -10.91,-12.69 -17.78,-17.38 50.77,-1.86 91.43,-40.67 91.43,-88.04 0,-48.1 -42.12,-87.4 -93.46,-87.96l-0.16 -0.08 -141.48 0c-3.64,0 -6.63,2.83 -6.63,6.38 0,3.48 2.99,6.39 6.63,6.39 10.43,0 18.92,8.17 18.92,18.27l0 277.55c-0.08,10.1 -8.57,18.27 -18.92,18.27 -3.64,0 -6.63,2.83 -6.63,6.39 0,3.47 2.99,6.38 6.63,6.38l73.9 0c3.63,0 6.62,-2.91 6.62,-6.38 0,-3.56 -2.99,-6.39 -6.62,-6.39 -10.43,0 -18.92,-8.25 -18.92,-18.27l0 -116.66c0,-5.74 -1.78,-11.16 -4.86,-15.77l50.38 0 0.08 0 1.69 0c4.13,0 8.09,2.02 10.27,5.1l66.13 107.84c17.63,30.4 49.56,49.24 85.21,50.37 1.3,0.08 2.59,0.16 3.88,0.16l0 0zm-126.84 -176.24l-85.94 0 0 -134.86c0,-5.73 -1.7,-11.23 -4.86,-15.76l87.89 0 0 0 1.69 0c31.37,0 56.92,33.79 56.92,75.27 0,41.07 -24.9,74.54 -55.62,75.35l-0.08 0zm265.09 182.47c23.69,0 46.33,-6.79 65.16,-19.49 1.62,-0.89 3.15,-1.94 4.45,-3.07 4.28,-2.99 8.25,-6.31 11.8,-9.7 22.24,-21.26 34.53,-49.64 34.53,-79.88l0 -202.76c0.16,-10.02 8.56,-18.19 18.91,-18.19 3.64,0 6.63,-2.83 6.63,-6.39 0,-3.47 -2.99,-6.38 -6.63,-6.38l-51.01 0c-3.64,0 -6.63,2.91 -6.63,6.38 0,3.56 2.99,6.39 6.63,6.39 10.43,0 18.92,8.25 18.92,18.35l0 202.6c0,26.76 -10.92,51.91 -31.05,71.15 -2.75,2.67 -5.74,5.17 -8.97,7.52l-1.13 0.8c-1.22,0.9 -2.35,1.62 -3.48,2.35 -14.47,9.3 -31.37,14.15 -48.83,14.15 -23.93,0 -46.33,-8.98 -63.06,-25.14 -16.9,-16.34 -26.12,-37.92 -26.12,-60.88l0 -210.85 0 -0.08 0 -1.62c0,-10.1 8.49,-18.35 18.92,-18.35 3.64,0 6.63,-2.83 6.63,-6.39 0,-3.47 -2.99,-6.38 -6.63,-6.38l-73.89 0c-3.64,0 -6.63,2.91 -6.63,6.38 0,3.56 2.99,6.39 6.63,6.39 10.35,0 18.84,8.17 18.92,18.27l0 202.68c0,61.85 51.98,112.14 115.93,112.14l0 0zm275.69 0c1.94,0 3.8,-0.08 5.74,-0.17 1.37,0 2.75,-0.08 4.12,-0.24l2.75 -0.24 0.97 -0.16 0.89 0 0.32 -0.08c55.63,-5.9 97.5,-45.2 97.5,-91.52 0,-34.12 -22.71,-65.32 -59.26,-81.41l-95.15 -42.21c-25.07,-11.96 -40.67,-33.79 -40.67,-57.07 0,-34.04 31.85,-62.41 74.06,-65.89l3.55 -0.24c1.7,-0.09 3.4,-0.09 5.18,-0.09 1.7,0 3.47,0 5.01,0.09l3.56 0.24c41.07,3.39 72.92,30.88 74.13,63.87 0.09,1.86 0.97,3.56 2.43,4.68 1.13,0.98 2.67,1.46 4.12,1.46l0.65 0 13.9 -2.26c1.78,-0.32 3.4,-1.38 4.45,-2.83l1.46 -2.67 -0.57 -2.34c-9.86,-39.38 -50.77,-69.29 -99.6,-72.68l-3.88 -0.25c-1.86,-0.08 -3.72,-0.08 -5.66,-0.08 -1.94,0 -3.8,0 -5.82,0.08l-3.8 0.25c-57.16,3.96 -101.95,44.06 -101.95,91.27 0,32.42 21.18,62.74 55.46,79.23l96.21 42.52c26.92,11.81 43.65,34.36 43.65,58.86 0,32.91 -30.55,61.44 -70.74,66.38 -0.48,0 -4.2,0.4 -4.2,0.4l-3.64 0.16c-1.7,0.08 -3.47,0.08 -5.17,0.08 -1.7,0 -3.48,0 -5.02,-0.08l-3.71 -0.16c-42.77,-3.56 -75.03,-32.34 -75.03,-66.78 0,-1.62 0.08,-2.91 0.24,-4.2 0.25,-1.86 -0.4,-3.8 -1.77,-5.26 -1.22,-1.21 -2.91,-1.86 -5.26,-1.86 0,0 -15.2,1.3 -15.2,1.3 -1.78,0.16 -3.47,0.97 -4.61,2.34l-1.69 2.43 0.32 2.34c5.5,43.98 48.42,78.66 102.11,82.38l3.96 0.24c1.86,0.09 3.72,0.17 5.66,0.17l0 0z"/><path class="fil0" d="M497.36 892.87l79.55 0c28.34,0 51.42,-20.82 51.42,-46.41 0,-20.86 -15.28,-38.56 -36.46,-44.34 15.89,-6.87 26.52,-21.22 26.52,-37.39 0,-22.52 -20.14,-40.79 -45.8,-41.64 -0.37,0 -0.69,-0.04 -0.97,-0.04l-0.04 0 -74.22 0c-1.82,0 -3.32,1.42 -3.32,3.2 0,1.73 1.5,3.19 3.32,3.19 5.21,0 9.46,4.08 9.46,9.13l0 138.78c-0.04,5.05 -4.29,9.13 -9.46,9.13 -1.82,0 -3.32,1.42 -3.32,3.19 0,1.74 1.5,3.2 3.32,3.2l0 0zm80.2 -6.39l-52.71 0 0 -72.15c0,-2.87 -0.89,-5.58 -2.43,-7.89l54.49 0c17.75,0 32.18,17.95 32.18,40.02 0,21.79 -14.07,39.58 -31.53,40.02l0 0zm-52.67 -86.42l0 -62.74c0,-2.87 -0.89,-5.62 -2.47,-7.88l47.78 0 0 0 0.81 0c0.36,0 0.77,0 1.13,0.04 15.16,0.93 27,16.41 27,35.25 0,18.88 -11.84,34.4 -27,35.33 -0.36,0 -0.77,0 -1.17,0l-46.08 0zm206.84 92.81c1.82,0 3.32,-1.46 3.32,-3.2 0,-1.77 -1.5,-3.19 -3.32,-3.19 -5.17,0 -9.42,-4.08 -9.46,-9.09l0 -51.1 48.79 -85.17 0.37 -0.08 0.36 -1.13c3.4,-5.7 11.04,-10.47 16.82,-10.47 1.82,0 3.27,-1.46 3.27,-3.19 0,-1.78 -1.45,-3.2 -3.27,-3.2l-25.55 0c-1.82,0 -3.27,1.42 -3.27,3.2 0,1.73 1.45,3.19 3.27,3.19 1.09,0 3.03,0.2 3.84,1.45 0.77,1.3 0.4,3.64 -0.97,6.07l-46.12 80.48 -46.08 -80.44c-1.42,-2.47 -1.83,-4.81 -1.02,-6.11 0.77,-1.29 2.71,-1.49 3.8,-1.49 1.82,0 3.32,-1.42 3.32,-3.15 0,-1.78 -1.5,-3.2 -3.32,-3.2l-38.6 0c-1.82,0 -3.32,1.42 -3.32,3.2 0,1.73 1.5,3.19 3.32,3.19 5.82,0 13.58,4.89 16.94,10.67l49.35 86.14 0 51.1c0,5.01 -4.24,9.13 -9.46,9.13 -1.81,0 -3.31,1.42 -3.31,3.19 0,1.74 1.5,3.2 3.31,3.2l36.99 0zm308.14 3.11l0.49 0c1.37,0 2.63,-0.81 3.11,-2.06l6.15 -15.89 53.52 -138.32c2.14,-5.58 8.77,-10.27 14.47,-10.27 1.82,0 3.27,-1.46 3.27,-3.19 0,-1.78 -1.45,-3.2 -3.27,-3.2l-25.55 0c-1.82,0 -3.27,1.42 -3.27,3.2 0,1.73 1.45,3.19 3.27,3.19 2.15,0 3.84,0.69 4.73,1.94 1.05,1.45 1.09,3.68 0.16,6.06l-50.44 130.41 -50.49 -130.41c-0.93,-2.38 -0.85,-4.61 0.2,-6.06 0.89,-1.25 2.59,-1.94 4.69,-1.94 1.82,0 3.31,-1.46 3.31,-3.19 0,-1.78 -1.49,-3.2 -3.31,-3.2l-11.32 0 0 0 -26.44 0c-1.82,0 -3.31,1.42 -3.31,3.2 0,1.73 1.49,3.19 3.31,3.19 5.66,0 12.29,4.69 14.43,10.27l16.3 42 -33.35 86.14 -50.49 -130.41c-0.93,-2.42 -0.85,-4.61 0.16,-6.06 0.93,-1.25 2.59,-1.94 4.73,-1.94 1.82,0 3.31,-1.46 3.31,-3.19 0,-1.78 -1.49,-3.2 -3.31,-3.2l-11.36 0 0 0 -26.4 0c-1.82,0 -3.31,1.42 -3.31,3.2 0,1.73 1.49,3.19 3.31,3.19 5.66,0 12.29,4.69 14.43,10.27l59.67 154.09 0.44 1.37 0.77 0.21c0.04,0.04 0.04,0.04 0.08,0.08l0.45 0.52 1.37 0c1.38,0 2.59,-0.81 3.11,-2.06l39.9 -103.08 39.86 103 0.44 1.29 0.25 0.08 0.08 0.17 1.21 0.4 0.64 0.2 0 0zm188.09 -3.11c1.82,0 3.32,-1.46 3.32,-3.2 0,-1.77 -1.5,-3.19 -3.32,-3.19 -5.17,0 -9.41,-4.08 -9.46,-9.09l0 -51.1 48.8 -85.17 0.36 -0.08 0.36 -1.13c3.4,-5.7 11.04,-10.47 16.82,-10.47 1.82,0 3.27,-1.46 3.27,-3.19 0,-1.78 -1.45,-3.2 -3.27,-3.2l-25.55 0c-1.82,0 -3.27,1.42 -3.27,3.2 0,1.73 1.45,3.19 3.27,3.19 1.09,0 3.03,0.2 3.84,1.45 0.77,1.3 0.41,3.64 -0.97,6.07l-46.12 80.48 -46.08 -80.44c-1.42,-2.47 -1.82,-4.81 -1.01,-6.11 0.76,-1.29 2.7,-1.49 3.8,-1.49 1.81,0 3.31,-1.42 3.31,-3.15 0,-1.78 -1.5,-3.2 -3.31,-3.2l-38.61 0c-1.82,0 -3.31,1.42 -3.31,3.2 0,1.73 1.49,3.19 3.31,3.19 5.82,0 13.58,4.89 16.94,10.67l49.36 86.14 0 51.1c0,5.01 -4.25,9.13 -9.46,9.13 -1.82,0 -3.32,1.42 -3.32,3.19 0,1.74 1.5,3.2 3.32,3.2l36.98 0zm224.23 3.8l0.6 0 1.74 -0.57 0.29 -0.68c0.44,-0.53 0.68,-1.22 0.68,-1.94l0 -154.91c0,-5.05 4.25,-9.13 9.46,-9.13 1.82,0 3.32,-1.46 3.32,-3.19 0,-1.78 -1.5,-3.2 -3.32,-3.2l-25.5 0c-1.87,0 -3.32,1.42 -3.32,3.2 0,1.73 1.45,3.19 3.32,3.19 5.21,0 9.45,4.08 9.45,9.13l0 127.05 -110.35 -141.4c-0.65,-0.8 -1.62,-1.25 -2.67,-1.25l-13.7 0.04 -0.32 0.04 -12.98 0c-1.82,0 -3.32,1.42 -3.32,3.2 0,1.73 1.5,3.19 3.32,3.19 2.59,0 5.98,1.01 9.46,2.79l0 145.24c0,5.01 -4.25,9.13 -9.46,9.13 -1.82,0 -3.32,1.42 -3.32,3.2 0,1.73 1.5,3.19 3.32,3.19l25.51 0c1.82,0 3.31,-1.46 3.31,-3.19 0,-1.78 -1.49,-3.2 -3.31,-3.2 -5.22,0 -9.46,-4.12 -9.46,-9.13l0 -140.75c1.41,1.21 2.66,2.46 3.68,3.75l120.78 154.7 0.61 0.97 0.64 0.17c0.49,0.24 0.97,0.36 1.5,0.36l0.04 0zm130.04 -3.8c1.82,0 3.31,-1.46 3.31,-3.2 0,-1.77 -1.49,-3.19 -3.31,-3.19 -5.18,0 -9.42,-4.08 -9.46,-9.09l0 -147.95 32.06 0c13.09,0 26.23,10.35 29.3,23.04 0.32,1.46 1.66,2.46 3.2,2.46l0.44 0 0.32 -0.07c1.62,-0.37 2.67,-1.83 2.55,-3.4l0 -0.12 -6.22 -25.87 -0.21 -0.61 -0.24 -0.32c-0.16,-0.25 -0.32,-0.45 -0.53,-0.65l-1.13 -0.85 -62.01 0 -0.04 0 -12.24 0 -61.73 0c-0.81,0 -1.58,0.32 -2.22,0.89l-0.41 0.36 -0.12 0.25 -0.32 0.36 -6.19 25.87 -0.16 0.45 0 0.28c-0.12,1.53 0.93,2.99 2.51,3.36l0.44 0.07 0.37 0c1.53,0 2.87,-1 3.19,-2.46 3.07,-12.69 16.21,-23.04 29.31,-23.04l32.05 0 0 147.91c0,5.05 -4.28,9.13 -9.5,9.13 -1.82,0 -3.27,1.42 -3.27,3.19 0,1.74 1.45,3.2 3.27,3.2l36.99 0zm214.65 0c1.81,0 3.27,-1.42 3.27,-3.2 0,-1.77 -1.46,-3.19 -3.27,-3.19 -0.49,0 -0.98,-0.04 -1.5,-0.04 -7.07,-0.48 -13.34,-4.28 -16.86,-10.26l-0.04 -0.05 -0.36 -0.64 -0.04 -0.04 -34.12 -55.34 -0.24 -0.33c-2.43,-3.43 -5.46,-6.34 -8.89,-8.69 25.38,-0.93 45.71,-20.33 45.71,-44.02 0,-24.05 -21.06,-43.7 -46.73,-43.98l-0.08 -0.04 -70.74 0c-1.82,0 -3.31,1.42 -3.31,3.2 0,1.73 1.49,3.19 3.31,3.19 5.22,0 9.46,4.08 9.46,9.13l0 138.78c-0.04,5.05 -4.28,9.13 -9.46,9.13 -1.82,0 -3.31,1.42 -3.31,3.19 0,1.74 1.49,3.2 3.31,3.2l36.95 0c1.82,0 3.32,-1.46 3.32,-3.2 0,-1.77 -1.5,-3.19 -3.32,-3.19 -5.22,0 -9.46,-4.12 -9.46,-9.13l0 -58.33c0,-2.87 -0.89,-5.58 -2.42,-7.89l25.18 0 0.04 0 0.85 0c2.06,0 4.04,1.01 5.13,2.55l33.07 53.92c8.81,15.2 24.78,24.62 42.6,25.19 0.65,0.04 1.3,0.08 1.95,0.08l0 0zm-63.43 -88.12l-42.97 0 0 -67.43c0,-2.87 -0.85,-5.62 -2.42,-7.88l43.94 0 0 0 0.85 0c15.68,0 28.45,16.89 28.45,37.63 0,20.54 -12.45,37.27 -27.81,37.68l-0.04 0zm163.23 91.23c0.52,0 1.01,-0.04 1.53,-0.04 0,0 1.82,-0.04 2.03,-0.08l0.68 0 0.08 -0.04c44.91,-2.31 80,-40.79 80,-87.84 0,-48.59 -37.83,-88.08 -84.32,-88.08 -1.21,0 -2.47,0.04 -3.56,0.12l-1.21 0 -0.04 0.04c-44.71,2.59 -79.51,40.99 -79.51,87.92 0,48.51 37.83,88 84.32,88l0 0zm-68.4 -84.8c1.82,0 3.32,-1.46 3.32,-3.2 0,-9.42 1.25,-18.63 3.76,-27.41 8.85,-31.24 31.61,-52.51 58,-54.16 1.09,-0.08 2.23,-0.12 3.32,-0.12 41.76,0 76.04,34.96 77.65,78.46l-9.3 0c-1.82,0 -3.31,1.45 -3.31,3.23 0,12.05 -2.06,23.65 -6.06,34.52 -10.23,27.45 -31.57,45.48 -55.67,46.97l-1.57 0.08c-0.57,0 -1.17,0.05 -1.74,0.05 -41.76,0 -76.04,-34.93 -77.65,-78.42l9.25 0z"/></svg>
            <div class="status-section">
                <div class="status-item">
                    <span data-lang-key="motorStatusLabel">Motor Durumu</span>
                    <h3 id="motorStatus">Durduruldu</h3>
                </div>
                <div class="status-item">
                    <span data-lang-key="completedTurnsLabel">Tamamlanan Turlar</span>
                    <h3 id="completedTurns">0</h3>
                </div>
                <div class="status-item">
                    <span data-lang-key="turnsPerDayLabel">Günde Dönüş</span>
                    <h3 id="turnsPerDay">600</h3>
                </div>
                <div class="status-item">
                    <span data-lang-key="turnDurationLabel">Dönüş Süresi</span>
                    <h3 id="turnDuration">15 s</h3>
                </div>
            </div>
        </div>
        <div class="card">
            <div class="tabs-container">
                <div class="tab" id="tab-settings" onclick="showTab('settings')" data-lang-key="tabSettings">Ayarlar</div>
                <div class="tab" id="tab-wifi" onclick="showTab('wifi')" data-lang-key="tabWifi">WiFi Ayarları</div>
                <div class="tab" id="tab-otherHorus" onclick="showTab('otherHorus')" data-lang-key="tabOtherHorus">Diğer Horus Cihazları</div>
                <div class="tab" id="tab-theme" onclick="showTab('theme')" data-lang-key="tabTheme">Tema</div>
                <div class="tab" id="tab-language" onclick="showTab('language')" data-lang-key="tabLanguage">Dil</div>
                <div class="tab" id="tab-ota" onclick="showTab('ota')" data-lang-key="tabOta">Cihaz Güncelleme</div>
            </div>
            <div id="settingsTab" class="tab-content">
                <h2 data-lang-key="settingsTitle">Ayarlar</h2>
                <div class="form-group">
                    <label for="turnsPerDay" data-lang-key="turnsPerDayInputLabel">Günde Dönüş Sayısı</label>
                    <input type="range" id="turnsPerDayInput" min="600" max="1200" step="100" value="600" oninput="document.getElementById('turnsPerDayValue').innerText = this.value;">
                    <p style="text-align: center; margin: 5px 0 0;"><span id="turnsPerDayValue">600</span> <span data-lang-key="tourUnit">Tur</span></p>
                </div>
                <div class="form-group">
                    <label for="turnDuration" data-lang-key="turnDurationInputLabel">Dönüş Süresi (saniye)</label>
                    <input type="range" id="turnDurationInput" min="10" max="15" step="0.5" value="15" oninput="document.getElementById('turnDurationValue').innerText = this.value;">
                    <p style="text-align: center; margin: 5px 0 0;"><span id="turnDurationValue">15.0</span> <span data-lang-key="secondUnit">s</span></p>
                </div>
                <div class="form-group">
                    <label data-lang-key="directionLabel">Yön</label>
                    <div class="radio-group">
                        <div class="radio-item">
                            <input type="radio" id="direction1" name="direction" value="1" checked>
                            <label for="direction1" data-lang-key="directionForward">Sadece İleri</label>
                        </div>
                        <div class="radio-item">
                            <input type="radio" id="direction2" name="direction" value="2">
                            <label for="direction2" data-lang-key="directionBackward">Sadece Geri</label>
                        </div>
                        <div class="radio-item">
                            <input type="radio" id="direction3" name="direction" value="3">
                            <label for="direction3" data-lang-key="directionBoth">İleri ve Geri</label>
                        </div>
                    </div>
                </div>
                <div class="button-group">
                    <button class="button primary" onclick="sendSettings('start')" data-lang-key="startButton">Başlat</button>
                    <button class="button secondary" onclick="sendSettings('stop')" data-lang-key="stopButton">Durdur</button>
                    <button class="button secondary" onclick="sendSettings('reset')" data-lang-key="resetButton">Sıfırla</button>
                </div>
            </div>
            <div id="wifiTab" class="tab-content">
                <h2 data-lang-key="wifiSettingsTitle">WiFi Ayarları</h2>
                <div class="form-group">
                    <label for="ssid" data-lang-key="networkNameLabel">Ağ Adı (SSID)</label>
                    <select id="ssidSelect"></select>
                </div>
                <div class="form-group">
                    <label for="password" data-lang-key="passwordLabel">Şifre</label>
                    <input type="text" id="passwordInput" data-lang-key="passwordPlaceholder" placeholder="Şifrenizi buraya girin">
                </div>
                <div class="button-group">
                    <button class="button primary" onclick="saveWiFiSettings()" data-lang-key="saveAndRestartButton">Kaydet ve Yeniden Başlat</button>
                    <button class="button secondary" onclick="scanNetworks()" data-lang-key="scanNetworksButton">Ağları Tara</button>
                </div>
            </div>
            <div id="otherHorusTab" class="tab-content">
                <h2 data-lang-key="otherHorusTitle">Diğer Horus Cihazları</h2>
                <div class="form-group inline-group">
                    <label for="otherHorusName" style="display:none;" data-lang-key="mdnsNameLabel">MDNS Adı (Örn: horus-D99D)</label>
                    <input type="text" id="otherHorusName" data-lang-key="mdnsNamePlaceholder" placeholder="MDNS Adı (Örn: horus-D99D)">
                    <button class="button primary" onclick="addOtherHorus()" data-lang-key="addButton">Ekle</button>
                </div>
                <div class="other-horus-list" id="otherHorusList">
                </div>
            </div>
            <div id="themeTab" class="tab-content">
                <h2 data-lang-key="themeTitle">Tema</h2>
                <div class="radio-group">
                    <div class="radio-item">
                        <input type="radio" id="themeSystem" name="theme" value="system" checked>
                        <label for="themeSystem" data-lang-key="themeSystem">Sistem</label>
                    </div>
                    <div class="radio-item">
                        <input type="radio" id="themeDark" name="theme" value="dark">
                        <label for="themeDark" data-lang-key="themeDark">Karanlık</label>
                    </div>
                    <div class="radio-item">
                        <input type="radio" id="themeLight" name="theme" value="light">
                        <label for="themeLight" data-lang-key="themeLight">Aydınlık</label>
                    </div>
                </div>
            </div>
            <div id="languageTab" class="tab-content">
                <h2 data-lang-key="languageTitle">Dil</h2>
                <div class="form-group">
                    <label for="languageSelect" data-lang-key="languageSelectLabel">Arayüz Dili</label>
                    <select id="languageSelect" onchange="setLanguage(this.value)">
                        <option value="tr">Türkçe</option>
                        <option value="en">English</option>
                        <option value="de">Deutsch</option>
                        <option value="fr">Français</option>
                        <option value="it">Italiano</option>
                        <option value="rm">Romansh</option>
                        <option value="es">Español</option>
                        <option value="mk">Македонски</option>
                        <option value="sq">Shqip</option>
                        <option value="bs">Bosanski</option>
                        <option value="sr">Српски</option>
                        <option value="rup">Armãneashce</option>
                        <option value="rom">Romani</option>
                        <option value="zh-CN">简体中文</option>
                        <option value="ja">日本語</option>
                        <option value="az">Azərbaycan dili</option>
                    </select>
                </div>
            </div>
            <div id="otaTab" class="tab-content">
                <h2 data-lang-key="otaTitle">Cihaz Güncelleme</h2>
                <div class="status-section" style="margin-bottom: 20px;">
                    <div class="status-item">
                        <span data-lang-key="currentVersionLabel">Güncel Sürüm</span>
                        <h3 id="currentVersion" data-lang-key="loading">Yükleniyor...</h3>
                    </div>
                    <div class="status-item">
                        <span data-lang-key="webInterfaceLabel">Web Arayüzü:</span>
                        <h3 id="ipAddress"></h3>
                    </div>
                    <div class="status-item">
                        <span data-lang-key="deviceNameLabel">Cihaz Adı</span>
                        <h3 id="deviceName" data-lang-key="loading">Yükleniyor...</h3>
                    </div>
                </div>
                <div class="form-group">
                    <label for="name" data-lang-key="optionalDeviceNameLabel">Cihaz Adı (Opsiyonel)</label>
                    <input type="text" id="nameInput" data-lang-key="deviceNamePlaceholder" placeholder="Cihaz Adı">
                </div>
                <div class="button-group">
                    <button class="button primary" onclick="saveDeviceName()" data-lang-key="saveButton">Kaydet</button>
                    <button class="button secondary" onclick="resetDeviceName()" data-lang-key="resetButton">Sıfırla</button>
                </div>
                <p id="message_box" style="display:none; text-align: center;"></p>
                <div class="button-group" style="margin-top: 5px;">
                    <button class="button primary" onclick="checkOTAUpdate()" data-lang-key="checkUpdateButton">Güncelleme Kontrol Et</button>
                    <button class="button secondary" onclick="window.location.href='/manual_update'" data-lang-key="manualUpdateButton">Manuel Güncelleme</button>
                </div>
            </div>
        </div>
        <button id="pwa_install_button" style="display: none; margin: 10px; padding: 10px; background-color: #3b82f6; color: white; border: none; border-radius: 5px;" data-lang-key="installAppButton">Uygulamayı Yükle</button>
    </div>
    <p class="footer-text" data-lang-key="footerText">Caner Kocacık tarafından tasarlanmıştır.</p>
    <script>
        var ws;
        var reconnectInterval;
        let deferredPrompt;

        const translations = {
            'tr': {
                motorStatusLabel: 'Motor Durumu',
                completedTurnsLabel: 'Tamamlanan Turlar',
                turnsPerDayLabel: 'Günde Dönüş',
                turnDurationLabel: 'Dönüş Süresi',
                tabSettings: 'Ayarlar',
                tabWifi: 'WiFi Ayarları',
                tabOtherHorus: 'Diğer Horus Cihazları',
                tabTheme: 'Tema',
                tabLanguage: 'Dil',
                tabOta: 'Cihaz Güncelleme',
                settingsTitle: 'Ayarlar',
                turnsPerDayInputLabel: 'Günde Dönüş Sayısı',
                tourUnit: 'Tur',
                turnDurationInputLabel: 'Dönüş Süresi (saniye)',
                secondUnit: 's',
                directionLabel: 'Yön',
                directionForward: 'Sadece İleri',
                directionBackward: 'Sadece Geri',
                directionBoth: 'İleri ve Geri',
                startButton: 'Başlat',
                stopButton: 'Durdur',
                resetButton: 'Sıfırla',
                wifiSettingsTitle: 'WiFi Ayarları',
                networkNameLabel: 'Ağ Adı (SSID)',
                passwordLabel: 'Şifre',
                passwordPlaceholder: 'Şifrenizi buraya girin',
                saveAndRestartButton: 'Kaydet ve Yeniden Başlat',
                scanNetworksButton: 'Ağları Tara',
                otherHorusTitle: 'Diğer Horus Cihazları',
                mdnsNameLabel: 'MDNS Adı (Örn: horus-D99D)',
                mdnsNamePlaceholder: 'MDNS Adı (Örn: horus-D99D)',
                addButton: 'Ekle',
                themeTitle: 'Tema',
                themeSystem: 'Sistem',
                themeDark: 'Karanlık',
                themeLight: 'Aydınlık',
                languageTitle: 'Dil',
                languageSelectLabel: 'Arayüz Dili',
                otaTitle: 'Cihaz Güncelleme',
                currentVersionLabel: 'Güncel Sürüm',
                loading: 'Yükleniyor...',
                webInterfaceLabel: 'Web Arayüzü:',
                deviceNameLabel: 'Cihaz Adı',
                optionalDeviceNameLabel: 'Cihaz Adı (Opsiyonel)',
                deviceNamePlaceholder: 'Cihaz Adı',
                saveButton: 'Kaydet',
                checkUpdateButton: 'Güncelleme Kontrol Et',
                manualUpdateButton: 'Manuel Güncelleme',
                installAppButton: 'Uygulamayı Yükle',
                footerText: 'Caner Kocacık tarafından tasarlanmıştır.',
                motorStatusRunning: 'Çalışıyor',
                motorStatusStopped: 'Durduruldu',
                alertEnterMdns: 'Lütfen bir MDNS adı girin.',
                alertDeviceAdded: 'Cihaz eklendi: ',
                alertDeviceAddError: 'Cihaz eklenirken bir hata oluştu.',
                alertCommandSuccess: ' cihazı için komut başarıyla gönderildi.',
                alertConnectionError: ' cihazına bağlanılamadı.',
                scanningNetworks: 'Taranıyor...',
                otaChecking: 'Güncelleme kontrol ediliyor...',
                otaErrorConnect: 'Hata: Sunucuya bağlanılamadı.',
                noOtherDevices: 'Henüz başka cihaz eklenmemiş.'
            },
            'en': {
                motorStatusLabel: 'Motor Status',
                completedTurnsLabel: 'Completed Turns',
                turnsPerDayLabel: 'Turns Per Day',
                turnDurationLabel: 'Turn Duration',
                tabSettings: 'Settings',
                tabWifi: 'WiFi Settings',
                tabOtherHorus: 'Other Horus Devices',
                tabTheme: 'Theme',
                tabLanguage: 'Language',
                tabOta: 'Device Update',
                settingsTitle: 'Settings',
                turnsPerDayInputLabel: 'Turns Per Day',
                tourUnit: 'Turns',
                turnDurationInputLabel: 'Turn Duration (seconds)',
                secondUnit: 's',
                directionLabel: 'Direction',
                directionForward: 'Forward Only',
                directionBackward: 'Backward Only',
                directionBoth: 'Forward and Backward',
                startButton: 'Start',
                stopButton: 'Stop',
                resetButton: 'Reset',
                wifiSettingsTitle: 'WiFi Settings',
                networkNameLabel: 'Network Name (SSID)',
                passwordLabel: 'Password',
                passwordPlaceholder: 'Enter your password here',
                saveAndRestartButton: 'Save and Restart',
                scanNetworksButton: 'Scan Networks',
                otherHorusTitle: 'Other Horus Devices',
                mdnsNameLabel: 'MDNS Name (e.g., horus-D99D)',
                mdnsNamePlaceholder: 'MDNS Name (e.g., horus-D99D)',
                addButton: 'Add',
                themeTitle: 'Theme',
                themeSystem: 'System',
                themeDark: 'Dark',
                themeLight: 'Light',
                languageTitle: 'Language',
                languageSelectLabel: 'Interface Language',
                otaTitle: 'Device Update',
                currentVersionLabel: 'Current Version',
                loading: 'Loading...',
                webInterfaceLabel: 'Web Interface:',
                deviceNameLabel: 'Device Name',
                optionalDeviceNameLabel: 'Device Name (Optional)',
                deviceNamePlaceholder: 'Device Name',
                saveButton: 'Save',
                checkUpdateButton: 'Check for Updates',
                manualUpdateButton: 'Manual Update',
                installAppButton: 'Install App',
                footerText: 'Designed by Caner Kocacık.',
                motorStatusRunning: 'Running',
                motorStatusStopped: 'Stopped',
                alertEnterMdns: 'Please enter an MDNS name.',
                alertDeviceAdded: 'Device added: ',
                alertDeviceAddError: 'An error occurred while adding the device.',
                alertCommandSuccess: ' command sent successfully to ',
                alertConnectionError: 'Could not connect to ',
                scanningNetworks: 'Scanning...',
                otaChecking: 'Checking for update...',
                otaErrorConnect: 'Error: Could not connect to the server.',
                noOtherDevices: 'No other devices have been added yet.'
            },
            'de': {
                motorStatusLabel: 'Motorstatus',
                completedTurnsLabel: 'Abgeschlossene Umdrehungen',
                turnsPerDayLabel: 'Umdrehungen pro Tag',
                turnDurationLabel: 'Dauer der Umdrehung',
                tabSettings: 'Einstellungen',
                tabWifi: 'WLAN-Einstellungen',
                tabOtherHorus: 'Andere Horus-Geräte',
                tabTheme: 'Thema',
                tabLanguage: 'Sprache',
                tabOta: 'Geräte-Update',
                settingsTitle: 'Einstellungen',
                turnsPerDayInputLabel: 'Umdrehungen pro Tag',
                tourUnit: 'Umdr.',
                turnDurationInputLabel: 'Dauer der Umdrehung (Sekunden)',
                secondUnit: 's',
                directionLabel: 'Richtung',
                directionForward: 'Nur Vorwärts',
                directionBackward: 'Nur Rückwärts',
                directionBoth: 'Vorwärts und Rückwärts',
                startButton: 'Start',
                stopButton: 'Stopp',
                resetButton: 'Zurücksetzen',
                wifiSettingsTitle: 'WLAN-Einstellungen',
                networkNameLabel: 'Netzwerkname (SSID)',
                passwordLabel: 'Passwort',
                passwordPlaceholder: 'Geben Sie hier Ihr Passwort ein',
                saveAndRestartButton: 'Speichern und Neustarten',
                scanNetworksButton: 'Netzwerke suchen',
                otherHorusTitle: 'Andere Horus-Geräte',
                mdnsNameLabel: 'MDNS-Name (z.B. horus-D99D)',
                mdnsNamePlaceholder: 'MDNS-Name (z.B. horus-D99D)',
                addButton: 'Hinzufügen',
                themeTitle: 'Thema',
                themeSystem: 'System',
                themeDark: 'Dunkel',
                themeLight: 'Hell',
                languageTitle: 'Sprache',
                languageSelectLabel: 'Oberflächensprache',
                otaTitle: 'Geräte-Update',
                currentVersionLabel: 'Aktuelle Version',
                loading: 'Wird geladen...',
                webInterfaceLabel: 'Web-Oberfläche:',
                deviceNameLabel: 'Gerätename',
                optionalDeviceNameLabel: 'Gerätename (Optional)',
                deviceNamePlaceholder: 'Gerätename',
                saveButton: 'Speichern',
                checkUpdateButton: 'Nach Updates suchen',
                manualUpdateButton: 'Manuelles Update',
                installAppButton: 'App installieren',
                footerText: 'Entworfen von Caner Kocacık.',
                motorStatusRunning: 'Läuft',
                motorStatusStopped: 'Gestoppt',
                alertEnterMdns: 'Bitte geben Sie einen MDNS-Namen ein.',
                alertDeviceAdded: 'Gerät hinzugefügt: ',
                alertDeviceAddError: 'Beim Hinzufügen des Geräts ist ein Fehler aufgetreten.',
                alertCommandSuccess: ' Befehl erfolgreich an Gerät gesendet: ',
                alertConnectionError: 'Verbindung zum Gerät fehlgeschlagen: ',
                scanningNetworks: 'Scannen...',
                otaChecking: 'Suche nach Updates...',
                otaErrorConnect: 'Fehler: Verbindung zum Server konnte nicht hergestellt werden.',
                noOtherDevices: 'Es wurden noch keine anderen Geräte hinzugefügt.'
            },
            'fr': {
                motorStatusLabel: 'État du Moteur',
                completedTurnsLabel: 'Tours Terminés',
                turnsPerDayLabel: 'Tours par Jour',
                turnDurationLabel: 'Durée du Tour',
                tabSettings: 'Paramètres',
                tabWifi: 'Paramètres WiFi',
                tabOtherHorus: 'Autres Appareils Horus',
                tabTheme: 'Thème',
                tabLanguage: 'Langue',
                tabOta: 'Mise à Jour',
                settingsTitle: 'Paramètres',
                turnsPerDayInputLabel: 'Tours par Jour',
                tourUnit: 'Tours',
                turnDurationInputLabel: 'Durée du Tour (secondes)',
                secondUnit: 's',
                directionLabel: 'Direction',
                directionForward: 'Avant Seulement',
                directionBackward: 'Arrière Seulement',
                directionBoth: 'Avant et Arrière',
                startButton: 'Démarrer',
                stopButton: 'Arrêter',
                resetButton: 'Réinitialiser',
                wifiSettingsTitle: 'Paramètres WiFi',
                networkNameLabel: 'Nom du Réseau (SSID)',
                passwordLabel: 'Mot de passe',
                passwordPlaceholder: 'Entrez votre mot de passe ici',
                saveAndRestartButton: 'Enregistrer et Redémarrer',
                scanNetworksButton: 'Scanner les Réseaux',
                otherHorusTitle: 'Autres Appareils Horus',
                mdnsNameLabel: 'Nom MDNS (ex: horus-D99D)',
                mdnsNamePlaceholder: 'Nom MDNS (ex: horus-D99D)',
                addButton: 'Ajouter',
                themeTitle: 'Thème',
                themeSystem: 'Système',
                themeDark: 'Sombre',
                themeLight: 'Clair',
                languageTitle: 'Langue',
                languageSelectLabel: 'Langue de l\'interface',
                otaTitle: 'Mise à Jour de l\'appareil',
                currentVersionLabel: 'Version Actuelle',
                loading: 'Chargement...',
                webInterfaceLabel: 'Interface Web:',
                deviceNameLabel: 'Nom de l\'appareil',
                optionalDeviceNameLabel: 'Nom de l\'appareil (Optionnel)',
                deviceNamePlaceholder: 'Nom de l\'appareil',
                saveButton: 'Enregistrer',
                checkUpdateButton: 'Vérifier les Mises à Jour',
                manualUpdateButton: 'Mise à Jour Manuelle',
                installAppButton: 'Installer l\'Application',
                footerText: 'Conçu par Caner Kocacık.',
                motorStatusRunning: 'En marche',
                motorStatusStopped: 'Arrêté',
                alertEnterMdns: 'Veuillez entrer un nom MDNS.',
                alertDeviceAdded: 'Appareil ajouté : ',
                alertDeviceAddError: 'Une erreur s\'est produite lors de l\'ajout de l\'appareil.',
                alertCommandSuccess: ' commande envoyée avec succès à l\'appareil ',
                alertConnectionError: 'Impossible de se connecter à l\'appareil ',
                scanningNetworks: 'Balayage...',
                otaChecking: 'Vérification de la mise à jour...',
                otaErrorConnect: 'Erreur : Impossible de se connecter au serveur.',
                noOtherDevices: 'Aucun autre appareil n\'a encore été ajouté.'
            },
            'it': {
                motorStatusLabel: 'Stato Motore',
                completedTurnsLabel: 'Giri Completati',
                turnsPerDayLabel: 'Giri al Giorno',
                turnDurationLabel: 'Durata Giro',
                tabSettings: 'Impostazioni',
                tabWifi: 'Impostazioni WiFi',
                tabOtherHorus: 'Altri Dispositivi Horus',
                tabTheme: 'Tema',
                tabLanguage: 'Lingua',
                tabOta: 'Aggiornamento',
                settingsTitle: 'Impostazioni',
                turnsPerDayInputLabel: 'Giri al Giorno',
                tourUnit: 'Giri',
                turnDurationInputLabel: 'Durata Giro (secondi)',
                secondUnit: 's',
                directionLabel: 'Direzione',
                directionForward: 'Solo Avanti',
                directionBackward: 'Solo Indietro',
                directionBoth: 'Avanti e Indietro',
                startButton: 'Avvia',
                stopButton: 'Ferma',
                resetButton: 'Resetta',
                wifiSettingsTitle: 'Impostazioni WiFi',
                networkNameLabel: 'Nome Rete (SSID)',
                passwordLabel: 'Password',
                passwordPlaceholder: 'Inserisci qui la tua password',
                saveAndRestartButton: 'Salva e Riavvia',
                scanNetworksButton: 'Scansiona Reti',
                otherHorusTitle: 'Altri Dispositivi Horus',
                mdnsNameLabel: 'Nome MDNS (es: horus-D99D)',
                mdnsNamePlaceholder: 'Nome MDNS (es: horus-D99D)',
                addButton: 'Aggiungi',
                themeTitle: 'Tema',
                themeSystem: 'Sistema',
                themeDark: 'Scuro',
                themeLight: 'Chiaro',
                languageTitle: 'Lingua',
                languageSelectLabel: 'Lingua Interfaccia',
                otaTitle: 'Aggiornamento Dispositivo',
                currentVersionLabel: 'Versione Corrente',
                loading: 'Caricamento...',
                webInterfaceLabel: 'Interfaccia Web:',
                deviceNameLabel: 'Nome Dispositivo',
                optionalDeviceNameLabel: 'Nome Dispositivo (Opzionale)',
                deviceNamePlaceholder: 'Nome Dispositivo',
                saveButton: 'Salva',
                checkUpdateButton: 'Controlla Aggiornamenti',
                manualUpdateButton: 'Aggiornamento Manuale',
                installAppButton: 'Installa App',
                footerText: 'Progettato da Caner Kocacık.',
                motorStatusRunning: 'In funzione',
                motorStatusStopped: 'Fermato',
                alertEnterMdns: 'Inserisci un nome MDNS.',
                alertDeviceAdded: 'Dispositivo aggiunto: ',
                alertDeviceAddError: 'Si è verificato un errore durante l\'aggiunta del dispositivo.',
                alertCommandSuccess: ' comando inviato con successo al dispositivo ',
                alertConnectionError: 'Impossibile connettersi al dispositivo ',
                scanningNetworks: 'Scansione...',
                otaChecking: 'Controllo aggiornamenti...',
                otaErrorConnect: 'Errore: Impossibile connettersi al server.',
                noOtherDevices: 'Nessun altro dispositivo è stato ancora aggiunto.'
            },
            'es': {
                motorStatusLabel: 'Estado del Motor',
                completedTurnsLabel: 'Vueltas Completadas',
                turnsPerDayLabel: 'Vueltas por Día',
                turnDurationLabel: 'Duración de la Vuelta',
                tabSettings: 'Configuración',
                tabWifi: 'Configuración de WiFi',
                tabOtherHorus: 'Otros Dispositivos Horus',
                tabTheme: 'Tema',
                tabLanguage: 'Idioma',
                tabOta: 'Actualización',
                settingsTitle: 'Configuración',
                turnsPerDayInputLabel: 'Vueltas por Día',
                tourUnit: 'Vueltas',
                turnDurationInputLabel: 'Duración de la Vuelta (segundos)',
                secondUnit: 's',
                directionLabel: 'Dirección',
                directionForward: 'Solo Adelante',
                directionBackward: 'Solo Atrás',
                directionBoth: 'Adelante y Atrás',
                startButton: 'Iniciar',
                stopButton: 'Detener',
                resetButton: 'Reiniciar',
                wifiSettingsTitle: 'Configuración de WiFi',
                networkNameLabel: 'Nombre de Red (SSID)',
                passwordLabel: 'Contraseña',
                passwordPlaceholder: 'Ingrese su contraseña aquí',
                saveAndRestartButton: 'Guardar y Reiniciar',
                scanNetworksButton: 'Escanear Redes',
                otherHorusTitle: 'Otros Dispositivos Horus',
                mdnsNameLabel: 'Nombre MDNS (ej: horus-D99D)',
                mdnsNamePlaceholder: 'Nombre MDNS (ej: horus-D99D)',
                addButton: 'Añadir',
                themeTitle: 'Tema',
                themeSystem: 'Sistema',
                themeDark: 'Oscuro',
                themeLight: 'Claro',
                languageTitle: 'Idioma',
                languageSelectLabel: 'Idioma de la Interfaz',
                otaTitle: 'Actualización del Dispositivo',
                currentVersionLabel: 'Versión Actual',
                loading: 'Cargando...',
                webInterfaceLabel: 'Interfaz Web:',
                deviceNameLabel: 'Nombre del Dispositivo',
                optionalDeviceNameLabel: 'Nombre del Dispositivo (Opcional)',
                deviceNamePlaceholder: 'Nombre del Dispositivo',
                saveButton: 'Guardar',
                checkUpdateButton: 'Buscar Actualizaciones',
                manualUpdateButton: 'Actualización Manual',
                installAppButton: 'Instalar Aplicación',
                footerText: 'Diseñado por Caner Kocacık.',
                motorStatusRunning: 'Funcionando',
                motorStatusStopped: 'Detenido',
                alertEnterMdns: 'Por favor, ingrese un nombre MDNS.',
                alertDeviceAdded: 'Dispositivo añadido: ',
                alertDeviceAddError: 'Ocurrió un error al añadir el dispositivo.',
                alertCommandSuccess: ' comando enviado con éxito al dispositivo ',
                alertConnectionError: 'No se pudo conectar al dispositivo ',
                scanningNetworks: 'Escaneando...',
                otaChecking: 'Buscando actualizaciones...',
                otaErrorConnect: 'Error: No se pudo conectar al servidor.',
                noOtherDevices: 'Aún no se han agregado otros dispositivos.'
            },
            'zh-CN': {
                motorStatusLabel: '电机状态',
                completedTurnsLabel: '完成圈数',
                turnsPerDayLabel: '每日圈数',
                turnDurationLabel: '每圈时长',
                tabSettings: '设置',
                tabWifi: 'WiFi设置',
                tabOtherHorus: '其他Horus设备',
                tabTheme: '主题',
                tabLanguage: '语言',
                tabOta: '设备更新',
                settingsTitle: '设置',
                turnsPerDayInputLabel: '每日圈数',
                tourUnit: '圈',
                turnDurationInputLabel: '每圈时长 (秒)',
                secondUnit: '秒',
                directionLabel: '方向',
                directionForward: '仅向前',
                directionBackward: '仅向后',
                directionBoth: '向前和向后',
                startButton: '开始',
                stopButton: '停止',
                resetButton: '重置',
                wifiSettingsTitle: 'WiFi设置',
                networkNameLabel: '网络名称 (SSID)',
                passwordLabel: '密码',
                passwordPlaceholder: '在此输入您的密码',
                saveAndRestartButton: '保存并重启',
                scanNetworksButton: '扫描网络',
                otherHorusTitle: '其他Horus设备',
                mdnsNameLabel: 'MDNS名称 (例如: horus-D99D)',
                mdnsNamePlaceholder: 'MDNS名称 (例如: horus-D99D)',
                addButton: '添加',
                themeTitle: '主题',
                themeSystem: '系统',
                themeDark: '深色',
                themeLight: '浅色',
                languageTitle: '语言',
                languageSelectLabel: '界面语言',
                otaTitle: '设备更新',
                currentVersionLabel: '当前版本',
                loading: '加载中...',
                webInterfaceLabel: 'Web界面:',
                deviceNameLabel: '设备名称',
                optionalDeviceNameLabel: '设备名称 (可选)',
                deviceNamePlaceholder: '设备名称',
                saveButton: '保存',
                checkUpdateButton: '检查更新',
                manualUpdateButton: '手动更新',
                installAppButton: '安装应用',
                footerText: '由Caner Kocacık设计。',
                motorStatusRunning: '运行中',
                motorStatusStopped: '已停止',
                alertEnterMdns: '请输入MDNS名称。',
                alertDeviceAdded: '设备已添加: ',
                alertDeviceAddError: '添加设备时发生错误。',
                alertCommandSuccess: ' 命令已成功发送至设备 ',
                alertConnectionError: '无法连接到设备 ',
                scanningNetworks: '扫描中...',
                otaChecking: '正在检查更新...',
                otaErrorConnect: '错误：无法连接到服务器。',
                noOtherDevices: '尚未添加其他设备。'
            },
            'ja': {
                motorStatusLabel: 'モーター状態',
                completedTurnsLabel: '完了した回転数',
                turnsPerDayLabel: '1日の回転数',
                turnDurationLabel: '回転時間',
                tabSettings: '設定',
                tabWifi: 'WiFi設定',
                tabOtherHorus: '他のHorusデバイス',
                tabTheme: 'テーマ',
                tabLanguage: '言語',
                tabOta: 'デバイス更新',
                settingsTitle: '設定',
                turnsPerDayInputLabel: '1日の回転数',
                tourUnit: '回転',
                turnDurationInputLabel: '回転時間 (秒)',
                secondUnit: '秒',
                directionLabel: '方向',
                directionForward: '正転のみ',
                directionBackward: '逆転のみ',
                directionBoth: '正転と逆転',
                startButton: '開始',
                stopButton: '停止',
                resetButton: 'リセット',
                wifiSettingsTitle: 'WiFi設定',
                networkNameLabel: 'ネットワーク名 (SSID)',
                passwordLabel: 'パスワード',
                passwordPlaceholder: 'ここにパスワードを入力',
                saveAndRestartButton: '保存して再起動',
                scanNetworksButton: 'ネットワークをスキャン',
                otherHorusTitle: '他のHorusデバイス',
                mdnsNameLabel: 'MDNS名 (例: horus-D99D)',
                mdnsNamePlaceholder: 'MDNS名 (例: horus-D99D)',
                addButton: '追加',
                themeTitle: 'テーマ',
                themeSystem: 'システム',
                themeDark: 'ダーク',
                themeLight: 'ライト',
                languageTitle: '言語',
                languageSelectLabel: 'インターフェース言語',
                otaTitle: 'デバイスの更新',
                currentVersionLabel: '現在のバージョン',
                loading: '読み込み中...',
                webInterfaceLabel: 'Webインターフェース:',
                deviceNameLabel: 'デバイス名',
                optionalDeviceNameLabel: 'デバイス名 (任意)',
                deviceNamePlaceholder: 'デバイス名',
                saveButton: '保存',
                checkUpdateButton: '更新を確認',
                manualUpdateButton: '手動更新',
                installAppButton: 'アプリをインストール',
                footerText: 'Caner Kocacıkによるデザイン。',
                motorStatusRunning: '実行中',
                motorStatusStopped: '停止',
                alertEnterMdns: 'MDNS名を入力してください。',
                alertDeviceAdded: 'デバイスが追加されました: ',
                alertDeviceAddError: 'デバイスの追加中にエラーが発生しました。',
                alertCommandSuccess: ' コマンドがデバイスに正常に送信されました ',
                alertConnectionError: 'デバイスに接続できませんでした ',
                scanningNetworks: 'スキャン中...',
                otaChecking: '更新を確認しています...',
                otaErrorConnect: 'エラー：サーバーに接続できませんでした。',
                noOtherDevices: '他のデバイスはまだ追加されていません。'
            },
            'rm': {
                motorStatusLabel: 'Stadi dal motor',
                completedTurnsLabel: 'Rotaziuns terminadas',
                turnsPerDayLabel: 'Rotaziuns per di',
                turnDurationLabel: 'Durada da la rotaziun',
                tabSettings: 'Parameter',
                tabWifi: 'Parameter WLAN',
                tabOtherHorus: 'Auters apparats Horus',
                tabTheme: 'Tema',
                tabLanguage: 'Lingua',
                tabOta: 'Actualisaziun',
                settingsTitle: 'Parameter',
                turnsPerDayInputLabel: 'Rotaziuns per di',
                tourUnit: 'Rotaziuns',
                turnDurationInputLabel: 'Durada da la rotaziun (secundas)',
                secondUnit: 's',
                directionLabel: 'Direcziun',
                directionForward: 'Mo enavant',
                directionBackward: 'Mo enavos',
                directionBoth: 'Enavant ed enavos',
                startButton: 'Cumenzar',
                stopButton: 'Finir',
                resetButton: 'Resetar',
                wifiSettingsTitle: 'Parameter WLAN',
                networkNameLabel: 'Num da la rait (SSID)',
                passwordLabel: 'Pled-clav',
                passwordPlaceholder: 'Endatar il pled-clav',
                saveAndRestartButton: 'Memorisar e reaviar',
                scanNetworksButton: 'Tschertgar raits',
                otherHorusTitle: 'Auters apparats Horus',
                mdnsNameLabel: 'Num MDNS (p.ex. horus-D99D)',
                mdnsNamePlaceholder: 'Num MDNS (p.ex. horus-D99D)',
                addButton: 'Agiuntar',
                themeTitle: 'Tema',
                themeSystem: 'Sistem',
                themeDark: 'Stgir',
                themeLight: 'Cler',
                languageTitle: 'Lingua',
                languageSelectLabel: 'Lingua da l\'interfatscha',
                otaTitle: 'Actualisaziun da l\'apparat',
                currentVersionLabel: 'Versiun actuala',
                loading: 'Chargiar...',
                webInterfaceLabel: 'Interfatscha web:',
                deviceNameLabel: 'Num da l\'apparat',
                optionalDeviceNameLabel: 'Num da l\'apparat (opziunal)',
                deviceNamePlaceholder: 'Num da l\'apparat',
                saveButton: 'Memorisar',
                checkUpdateButton: 'Controllar actualisaziuns',
                manualUpdateButton: 'Actualisaziun manuala',
                installAppButton: 'Installar l\'applicaziun',
                footerText: 'Designà da Caner Kocacık.',
                motorStatusRunning: 'En funcziun',
                motorStatusStopped: 'Farmà',
                alertEnterMdns: 'Endatar in num MDNS, per plaschair.',
                alertDeviceAdded: 'Apparat agiuntà: ',
                alertDeviceAddError: 'Errur durant agiuntar l\'apparat.',
                alertCommandSuccess: ' Cumond tramess cun success a l\'apparat ',
                alertConnectionError: 'Na possì betg connectar a l\'apparat ',
                scanningNetworks: 'Tschertgar...',
                otaChecking: 'Controllar actualisaziuns...',
                otaErrorConnect: 'Errur: Na possì betg connectar al server.',
                noOtherDevices: 'Anc nagins auters apparats agiuntads.'
            },
            'mk': {
                motorStatusLabel: 'Статус на моторот',
                completedTurnsLabel: 'Завршени вртења',
                turnsPerDayLabel: 'Вртења на ден',
                turnDurationLabel: 'Времетраење на вртење',
                tabSettings: 'Поставки',
                tabWifi: 'WiFi Поставки',
                tabOtherHorus: 'Други Horus уреди',
                tabTheme: 'Тема',
                tabLanguage: 'Јазик',
                tabOta: 'Ажурирање',
                settingsTitle: 'Поставки',
                turnsPerDayInputLabel: 'Вртења на ден',
                tourUnit: 'Вртења',
                turnDurationInputLabel: 'Времетраење на вртење (секунди)',
                secondUnit: 's',
                directionLabel: 'Насока',
                directionForward: 'Само напред',
                directionBackward: 'Само назад',
                directionBoth: 'Напред и назад',
                startButton: 'Старт',
                stopButton: 'Стоп',
                resetButton: 'Ресетирај',
                wifiSettingsTitle: 'WiFi Поставки',
                networkNameLabel: 'Име на мрежа (SSID)',
                passwordLabel: 'Лозинка',
                passwordPlaceholder: 'Внесете ја вашата лозинка тука',
                saveAndRestartButton: 'Зачувај и рестартирај',
                scanNetworksButton: 'Скенирај мрежи',
                otherHorusTitle: 'Други Horus уреди',
                mdnsNameLabel: 'MDNS име (пр. horus-D99D)',
                mdnsNamePlaceholder: 'MDNS име (пр. horus-D99D)',
                addButton: 'Додај',
                themeTitle: 'Тема',
                themeSystem: 'Систем',
                themeDark: 'Темна',
                themeLight: 'Светла',
                languageTitle: 'Јазик',
                languageSelectLabel: 'Јазик на интерфејс',
                otaTitle: 'Ажурирање на уредот',
                currentVersionLabel: 'Моментална верзија',
                loading: 'Вчитување...',
                webInterfaceLabel: 'Веб интерфејс:',
                deviceNameLabel: 'Име на уред',
                optionalDeviceNameLabel: 'Име на уред (опционално)',
                deviceNamePlaceholder: 'Име на уред',
                saveButton: 'Зачувај',
                checkUpdateButton: 'Провери за ажурирања',
                manualUpdateButton: 'Рачно ажурирање',
                installAppButton: 'Инсталирај апликација',
                footerText: 'Дизајнирано од Caner Kocacık.',
                motorStatusRunning: 'Работи',
                motorStatusStopped: 'Запрен',
                alertEnterMdns: 'Ве молиме внесете MDNS име.',
                alertDeviceAdded: 'Уредот е додаден: ',
                alertDeviceAddError: 'Настана грешка при додавање на уредот.',
                alertCommandSuccess: ' командата е успешно испратена до уредот ',
                alertConnectionError: 'Не може да се поврзе со уредот ',
                scanningNetworks: 'Скенирање...',
                otaChecking: 'Проверка за ажурирање...',
                otaErrorConnect: 'Грешка: Не може да се поврзе со серверот.',
                noOtherDevices: 'Сè уште не се додадени други уреди.'
            },
            'sq': {
                motorStatusLabel: 'Statusi i Motorit',
                completedTurnsLabel: 'Rrotullime të Përfunduara',
                turnsPerDayLabel: 'Rrotullime në Ditë',
                turnDurationLabel: 'Kohëzgjatja e Rrotullimit',
                tabSettings: 'Cilësimet',
                tabWifi: 'Cilësimet e WiFi',
                tabOtherHorus: 'Pajisje të tjera Horus',
                tabTheme: 'Tema',
                tabLanguage: 'Gjuha',
                tabOta: 'Përditësimi',
                settingsTitle: 'Cilësimet',
                turnsPerDayInputLabel: 'Rrotullime në Ditë',
                tourUnit: 'Rrotullime',
                turnDurationInputLabel: 'Kohëzgjatja e Rrotullimit (sekonda)',
                secondUnit: 's',
                directionLabel: 'Drejtimi',
                directionForward: 'Vetëm Përpara',
                directionBackward: 'Vetëm Prapa',
                directionBoth: 'Përpara dhe Prapa',
                startButton: 'Nis',
                stopButton: 'Ndalo',
                resetButton: 'Rivendos',
                wifiSettingsTitle: 'Cilësimet e WiFi',
                networkNameLabel: 'Emri i Rrjetit (SSID)',
                passwordLabel: 'Fjalëkalimi',
                passwordPlaceholder: 'Shkruani fjalëkalimin tuaj këtu',
                saveAndRestartButton: 'Ruaj dhe Rinis',
                scanNetworksButton: 'Skano Rrjetet',
                otherHorusTitle: 'Pajisje të tjera Horus',
                mdnsNameLabel: 'Emri MDNS (p.sh. horus-D99D)',
                mdnsNamePlaceholder: 'Emri MDNS (p.sh. horus-D99D)',
                addButton: 'Shto',
                themeTitle: 'Tema',
                themeSystem: 'Sistemi',
                themeDark: 'E errët',
                themeLight: 'E çelët',
                languageTitle: 'Gjuha',
                languageSelectLabel: 'Gjuha e Ndërfaqes',
                otaTitle: 'Përditësimi i Pajisjes',
                currentVersionLabel: 'Versioni Aktual',
                loading: 'Duke u ngarkuar...',
                webInterfaceLabel: 'Ndërfaqja e Uebit:',
                deviceNameLabel: 'Emri i Pajisjes',
                optionalDeviceNameLabel: 'Emri i Pajisjes (Opsionale)',
                deviceNamePlaceholder: 'Emri i Pajisjes',
                saveButton: 'Ruaj',
                checkUpdateButton: 'Kontrollo për Përditësime',
                manualUpdateButton: 'Përditësim Manual',
                installAppButton: 'Instalo Aplikacionin',
                footerText: 'Projektuar nga Caner Kocacık.',
                motorStatusRunning: 'Në punë',
                motorStatusStopped: 'Ndalur',
                alertEnterMdns: 'Ju lutemi vendosni një emër MDNS.',
                alertDeviceAdded: 'Pajisja u shtua: ',
                alertDeviceAddError: 'Ndodhi një gabim gjatë shtimit të pajisjes.',
                alertCommandSuccess: ' komanda u dërgua me sukses te pajisja ',
                alertConnectionError: 'Nuk mund të lidhej me pajisjen ',
                scanningNetworks: 'Duke skanuar...',
                otaChecking: 'Duke kontrolluar për përditësim...',
                otaErrorConnect: 'Gabim: Nuk mund të lidhej me serverin.',
                noOtherDevices: 'Asnjë pajisje tjetër nuk është shtuar ende.'
            },
            'bs': {
                motorStatusLabel: 'Status Motora',
                completedTurnsLabel: 'Završeni Okreti',
                turnsPerDayLabel: 'Okreta Dnevno',
                turnDurationLabel: 'Trajanje Okreta',
                tabSettings: 'Postavke',
                tabWifi: 'WiFi Postavke',
                tabOtherHorus: 'Drugi Horus Uređaji',
                tabTheme: 'Tema',
                tabLanguage: 'Jezik',
                tabOta: 'Ažuriranje',
                settingsTitle: 'Postavke',
                turnsPerDayInputLabel: 'Okreta Dnevno',
                tourUnit: 'Okreta',
                turnDurationInputLabel: 'Trajanje Okreta (sekunde)',
                secondUnit: 's',
                directionLabel: 'Smjer',
                directionForward: 'Samo Naprijed',
                directionBackward: 'Samo Nazad',
                directionBoth: 'Naprijed i Nazad',
                startButton: 'Pokreni',
                stopButton: 'Zaustavi',
                resetButton: 'Resetuj',
                wifiSettingsTitle: 'WiFi Postavke',
                networkNameLabel: 'Naziv Mreže (SSID)',
                passwordLabel: 'Lozinka',
                passwordPlaceholder: 'Unesite vašu lozinku ovdje',
                saveAndRestartButton: 'Sačuvaj i Ponovo Pokreni',
                scanNetworksButton: 'Skeniraj Mreže',
                otherHorusTitle: 'Drugi Horus Uređaji',
                mdnsNameLabel: 'MDNS Naziv (npr. horus-D99D)',
                mdnsNamePlaceholder: 'MDNS Naziv (npr. horus-D99D)',
                addButton: 'Dodaj',
                themeTitle: 'Tema',
                themeSystem: 'Sistem',
                themeDark: 'Tamna',
                themeLight: 'Svijetla',
                languageTitle: 'Jezik',
                languageSelectLabel: 'Jezik Interfejsa',
                otaTitle: 'Ažuriranje Uređaja',
                currentVersionLabel: 'Trenutna Verzija',
                loading: 'Učitavanje...',
                webInterfaceLabel: 'Web Interfejs:',
                deviceNameLabel: 'Naziv Uređaja',
                optionalDeviceNameLabel: 'Naziv Uređaja (Opcionalno)',
                deviceNamePlaceholder: 'Naziv Uređaja',
                saveButton: 'Sačuvaj',
                checkUpdateButton: 'Provjeri Ažuriranja',
                manualUpdateButton: 'Ručno Ažuriranje',
                installAppButton: 'Instaliraj Aplikaciju',
                footerText: 'Dizajnirao Caner Kocacık.',
                motorStatusRunning: 'Radi',
                motorStatusStopped: 'Zaustavljen',
                alertEnterMdns: 'Molimo unesite MDNS naziv.',
                alertDeviceAdded: 'Uređaj dodan: ',
                alertDeviceAddError: 'Došlo je do greške prilikom dodavanja uređaja.',
                alertCommandSuccess: ' komanda uspješno poslana na uređaj ',
                alertConnectionError: 'Nije moguće povezati se na uređaj ',
                scanningNetworks: 'Skeniranje...',
                otaChecking: 'Provjera ažuriranja...',
                otaErrorConnect: 'Greška: Nije moguće povezati se na server.',
                noOtherDevices: 'Još uvijek nema dodanih drugih uređaja.'
            },
            'sr': {
                motorStatusLabel: 'Status Motora',
                completedTurnsLabel: 'Završeni Okreti',
                turnsPerDayLabel: 'Okreta Dnevno',
                turnDurationLabel: 'Trajanje Okreta',
                tabSettings: 'Podešavanja',
                tabWifi: 'WiFi Podešavanja',
                tabOtherHorus: 'Drugi Horus Uređaji',
                tabTheme: 'Tema',
                tabLanguage: 'Jezik',
                tabOta: 'Ažuriranje',
                settingsTitle: 'Podešavanja',
                turnsPerDayInputLabel: 'Okreta Dnevno',
                tourUnit: 'Okreta',
                turnDurationInputLabel: 'Trajanje Okreta (sekunde)',
                secondUnit: 's',
                directionLabel: 'Smer',
                directionForward: 'Samo Napred',
                directionBackward: 'Samo Nazad',
                directionBoth: 'Napred i Nazad',
                startButton: 'Pokreni',
                stopButton: 'Zaustavi',
                resetButton: 'Resetuj',
                wifiSettingsTitle: 'WiFi Podešavanja',
                networkNameLabel: 'Naziv Mreže (SSID)',
                passwordLabel: 'Lozinka',
                passwordPlaceholder: 'Unesite vašu lozinku ovde',
                saveAndRestartButton: 'Sačuvaj i Ponovo Pokreni',
                scanNetworksButton: 'Skeniraj Mreže',
                otherHorusTitle: 'Drugi Horus Uređaji',
                mdnsNameLabel: 'MDNS Naziv (npr. horus-D99D)',
                mdnsNamePlaceholder: 'MDNS Naziv (npr. horus-D99D)',
                addButton: 'Dodaj',
                themeTitle: 'Tema',
                themeSystem: 'Sistem',
                themeDark: 'Tamna',
                themeLight: 'Svetla',
                languageTitle: 'Jezik',
                languageSelectLabel: 'Jezik Interfejsa',
                otaTitle: 'Ažuriranje Uređaja',
                currentVersionLabel: 'Trenutna Verzija',
                loading: 'Učitavanje...',
                webInterfaceLabel: 'Web Interfejs:',
                deviceNameLabel: 'Naziv Uređaja',
                optionalDeviceNameLabel: 'Naziv Uređaja (Opciono)',
                deviceNamePlaceholder: 'Naziv Uređaja',
                saveButton: 'Sačuvaj',
                checkUpdateButton: 'Proveri Ažuriranja',
                manualUpdateButton: 'Ručno Ažuriranje',
                installAppButton: 'Instaliraj Aplikaciju',
                footerText: 'Dizajnirao Caner Kocacık.',
                motorStatusRunning: 'Radi',
                motorStatusStopped: 'Zaustavljen',
                alertEnterMdns: 'Molimo unesite MDNS naziv.',
                alertDeviceAdded: 'Uređaj dodat: ',
                alertDeviceAddError: 'Došlo je do greške prilikom dodavanja uređaja.',
                alertCommandSuccess: ' komanda uspešno poslata na uređaj ',
                alertConnectionError: 'Nije moguće povezati se na uređaj ',
                scanningNetworks: 'Skeniranje...',
                otaChecking: 'Provera ažuriranja...',
                otaErrorConnect: 'Greška: Nije moguće povezati se na server.',
                noOtherDevices: 'Još uvek nema dodatih drugih uređaja.'
            },
            'rup': {
                motorStatusLabel: 'Starea a Motorlui',
                completedTurnsLabel: 'Turnuri Completati',
                turnsPerDayLabel: 'Turnuri pi Zi',
                turnDurationLabel: 'Durata a Turnului',
                tabSettings: 'Pricădeanji',
                tabWifi: 'Pricădeanji WiFi',
                tabOtherHorus: 'Alti Horus Aparati',
                tabTheme: 'Tema',
                tabLanguage: 'Limba',
                tabOta: 'Noutati',
                settingsTitle: 'Pricădeanji',
                turnsPerDayInputLabel: 'Turnuri pi Zi',
                tourUnit: 'Turnuri',
                turnDurationInputLabel: 'Durata a Turnului (secundi)',
                secondUnit: 's',
                directionLabel: 'Direcția',
                directionForward: 'Ma Nainti',
                directionBackward: 'Ma Năpoi',
                directionBoth: 'Nainti și Năpoi',
                startButton: 'Apucă',
                stopButton: 'Astavă',
                resetButton: 'Dă Năpoi',
                wifiSettingsTitle: 'Pricădeanji WiFi',
                networkNameLabel: 'Numa a Rețelei (SSID)',
                passwordLabel: 'Parola',
                passwordPlaceholder: 'Bagă parola ta aclo',
                saveAndRestartButton: 'Scrie și Dă Năpoi',
                scanNetworksButton: 'Scaneadză Rețeli',
                otherHorusTitle: 'Alti Horus Aparati',
                mdnsNameLabel: 'Numa MDNS (ex: horus-D99D)',
                mdnsNamePlaceholder: 'Numa MDNS (ex: horus-D99D)',
                addButton: 'Adaugă',
                themeTitle: 'Tema',
                themeSystem: 'Sistem',
                themeDark: 'Neagră',
                themeLight: 'Albă',
                languageTitle: 'Limba',
                languageSelectLabel: 'Limba a Interfeței',
                otaTitle: 'Noutati a Aparatlui',
                currentVersionLabel: 'Versia di Tora',
                loading: 'Ncărcari...',
                webInterfaceLabel: 'Interfața Web:',
                deviceNameLabel: 'Numa a Aparatlui',
                optionalDeviceNameLabel: 'Numa a Aparatlui (Opțional)',
                deviceNamePlaceholder: 'Numa a Aparatlui',
                saveButton: 'Scrie',
                checkUpdateButton: 'Caftă Noutati',
                manualUpdateButton: 'Noutati Manuali',
                installAppButton: 'Instaleadză Aplicația',
                footerText: 'Făcut di Caner Kocacık.',
                motorStatusRunning: 'Meargi',
                motorStatusStopped: 'Astat',
                alertEnterMdns: 'Vă rugăm să introduceți un nume MDNS.',
                alertDeviceAdded: 'Aparatul adăugat: ',
                alertDeviceAddError: 'A apărut o eroare la adăugarea aparatului.',
                alertCommandSuccess: ' comanda trimisă cu succes la aparat ',
                alertConnectionError: 'Nu s-a putut conecta la aparat ',
                scanningNetworks: 'Scanari...',
                otaChecking: 'Căutari noutati...',
                otaErrorConnect: 'Eroare: Nu s-a putut conecta la server.',
                noOtherDevices: 'Nu s-au adăugat încă alte aparate.'
            },
            'rom': {
                motorStatusLabel: 'Motorosko Statuso',
                completedTurnsLabel: 'Pherde Vurti',
                turnsPerDayLabel: 'Vurti pe Dives',
                turnDurationLabel: 'Vurtako Vaxt',
                tabSettings: 'Postavke',
                tabWifi: 'WiFi Postavke',
                tabOtherHorus: 'Aver Horus Masinki',
                tabTheme: 'Tema',
                tabLanguage: 'Chhib',
                tabOta: 'Nevipe',
                settingsTitle: 'Postavke',
                turnsPerDayInputLabel: 'Vurti pe Dives',
                tourUnit: 'Vurti',
                turnDurationInputLabel: 'Vurtako Vaxt (sekunde)',
                secondUnit: 's',
                directionLabel: 'Righ',
                directionForward: 'Numa Anglal',
                directionBackward: 'Numa Palal',
                directionBoth: 'Anglal thaj Palal',
                startButton: 'Startuj',
                stopButton: 'Stopir',
                resetButton: 'Resetir',
                wifiSettingsTitle: 'WiFi Postavke',
                networkNameLabel: 'Mrezako Alav (SSID)',
                passwordLabel: 'Lozinka',
                passwordPlaceholder: 'Chiv to lozinka akate',
                saveAndRestartButton: 'Spasir thaj Restartir',
                scanNetworksButton: 'Skenir Mreze',
                otherHorusTitle: 'Aver Horus Masinki',
                mdnsNameLabel: 'MDNS Alav (npr. horus-D99D)',
                mdnsNamePlaceholder: 'MDNS Alav (npr. horus-D99D)',
                addButton: 'Dodajin',
                themeTitle: 'Tema',
                themeSystem: 'Sistem',
                themeDark: 'Kalo',
                themeLight: 'Parhno',
                languageTitle: 'Chhib',
                languageSelectLabel: 'Interfejsosko Chhib',
                otaTitle: 'Masinkako Nevipe',
                currentVersionLabel: 'Akutno Verzija',
                loading: 'Ladiripe...',
                webInterfaceLabel: 'Web Interfejso:',
                deviceNameLabel: 'Masinkako Alav',
                optionalDeviceNameLabel: 'Masinkako Alav (Opcionalno)',
                deviceNamePlaceholder: 'Masinkako Alav',
                saveButton: 'Spasir',
                checkUpdateButton: 'Dikh Nevipe',
                manualUpdateButton: 'Manuelno Nevipe',
                installAppButton: 'Instalir Aplikacija',
                footerText: 'Kerdo katar Caner Kocacık.',
                motorStatusRunning: 'Phirel',
                motorStatusStopped: 'Stopirime',
                alertEnterMdns: 'Molimo te, chiv MDNS alav.',
                alertDeviceAdded: 'Masinka dodajime: ',
                alertDeviceAddError: 'Greska ked dodajisarda masinka.',
                alertCommandSuccess: ' komanda uspjesno bichhaldi ki masinka ',
                alertConnectionError: 'Nashti povezime pe masinka ',
                scanningNetworks: 'Skeniripe...',
                otaChecking: 'Dikhipe nevipasko...',
                otaErrorConnect: 'Greska: Nashti povezime pe servero.',
                noOtherDevices: 'Nane aver masinki dodajime.'
            },
            'az': {
                motorStatusLabel: 'Motor Vəziyyəti',
                completedTurnsLabel: 'Tamamlanmış Dövrələr',
                turnsPerDayLabel: 'Gündəlik Dövrə',
                turnDurationLabel: 'Dövrə Müddəti',
                tabSettings: 'Parametrlər',
                tabWifi: 'WiFi Parametrləri',
                tabOtherHorus: 'Digər Horus Cihazları',
                tabTheme: 'Mövzu',
                tabLanguage: 'Dil',
                tabOta: 'Cihaz Yeniləməsi',
                settingsTitle: 'Parametrlər',
                turnsPerDayInputLabel: 'Gündəlik Dövrə Sayı',
                tourUnit: 'Dövrə',
                turnDurationInputLabel: 'Dövrə Müddəti (saniyə)',
                secondUnit: 'san',
                directionLabel: 'İstiqamət',
                directionForward: 'Yalnız İrəli',
                directionBackward: 'Yalnız Geri',
                directionBoth: 'İrəli və Geri',
                startButton: 'Başlat',
                stopButton: 'Dayandır',
                resetButton: 'Sıfırla',
                wifiSettingsTitle: 'WiFi Parametrləri',
                networkNameLabel: 'Şəbəkə Adı (SSID)',
                passwordLabel: 'Şifrə',
                passwordPlaceholder: 'Şifrənizi bura daxil edin',
                saveAndRestartButton: 'Yadda Saxla və Yenidən Başlat',
                scanNetworksButton: 'Şəbəkələri Tara',
                otherHorusTitle: 'Digər Horus Cihazları',
                mdnsNameLabel: 'MDNS Adı (məs: horus-D99D)',
                mdnsNamePlaceholder: 'MDNS Adı (məs: horus-D99D)',
                addButton: 'Əlavə et',
                themeTitle: 'Mövzu',
                themeSystem: 'Sistem',
                themeDark: 'Tünd',
                themeLight: 'Açıq',
                languageTitle: 'Dil',
                languageSelectLabel: 'İnterfeys Dili',
                otaTitle: 'Cihaz Yeniləməsi',
                currentVersionLabel: 'Mövcud Versiya',
                loading: 'Yüklənir...',
                webInterfaceLabel: 'Veb İnterfeysi:',
                deviceNameLabel: 'Cihaz Adı',
                optionalDeviceNameLabel: 'Cihaz Adı (İstəyə Bağlı)',
                deviceNamePlaceholder: 'Cihaz Adı',
                saveButton: 'Yadda Saxla',
                checkUpdateButton: 'Yeniləmələri Yoxla',
                manualUpdateButton: 'Əl ilə Yeniləmə',
                installAppButton: 'Tətbiqi Yüklə',
                footerText: 'Caner Kocacık tərəfindən hazırlanmışdır.',
                motorStatusRunning: 'İşləyir',
                motorStatusStopped: 'Dayandırılıb',
                alertEnterMdns: 'Zəhmət olmasa bir MDNS adı daxil edin.',
                alertDeviceAdded: 'Cihaz əlavə edildi: ',
                alertDeviceAddError: 'Cihaz əlavə edilərkən xəta baş verdi.',
                alertCommandSuccess: ' əmri cihaza uğurla göndərildi ',
                alertConnectionError: 'Cihaza qoşulmaq mümkün olmadı ',
                scanningNetworks: 'Taranır...',
                otaChecking: 'Yeniləmə yoxlanılır...',
                otaErrorConnect: 'Xəta: Serverə qoşulmaq mümkün olmadı.',
                noOtherDevices: 'Hələ başqa cihaz əlavə edilməyib.'
            }
        };

        function setLanguage(lang) {
            const langStrings = translations[lang] || translations['tr'];
            document.querySelectorAll('[data-lang-key]').forEach(elem => {
                const key = elem.getAttribute('data-lang-key');
                if (langStrings[key]) {
                    if (elem.placeholder !== undefined) {
                        elem.placeholder = langStrings[key];
                    } else {
                        elem.innerText = langStrings[key];
                    }
                }
            });
            localStorage.setItem('language', lang);
            document.documentElement.lang = lang;
        }

        function loadLanguage() {
            const savedLang = localStorage.getItem('language') || 'tr';
            document.getElementById('languageSelect').value = savedLang;
            setLanguage(savedLang);
        }

        function getTranslation(key, lang = localStorage.getItem('language') || 'tr') {
            return (translations[lang] && translations[lang][key]) || translations['tr'][key];
        }

        function setTheme(theme) {
            if (theme === 'dark') {
                document.body.classList.remove('light');
                localStorage.setItem('theme', 'dark');
            } else if (theme === 'light') {
                document.body.classList.add('light');
                localStorage.setItem('theme', 'light');
            } else {
                if (window.matchMedia('(prefers-color-scheme: light)').matches) {
                    document.body.classList.add('light');
                } else {
                    document.body.classList.remove('light');
                }
                localStorage.removeItem('theme');
            }
        }

        function loadTheme() {
            const savedTheme = localStorage.getItem('theme');
            if (savedTheme) {
                document.getElementById('theme' + savedTheme.charAt(0).toUpperCase() + savedTheme.slice(1)).checked = true;
                setTheme(savedTheme);
            } else {
                document.getElementById('themeSystem').checked = true;
                setTheme('system');
            }
        }

        function showTab(tabId) {
            document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
            document.querySelectorAll('.tab').forEach(tab => tab.classList.remove('active'));
            document.getElementById(tabId + 'Tab').classList.add('active');
            document.getElementById('tab-' + tabId).classList.add('active');
        }

        function handleMessage(data) {
            try {
                var doc = JSON.parse(data);
                if (doc.tpd) {
                    document.getElementById('turnsPerDay').innerText = doc.tpd;
                    document.getElementById('turnsPerDayValue').innerText = doc.tpd;
                    document.getElementById('turnsPerDayInput').value = doc.tpd;
                }
                if (doc.duration) {
                    document.getElementById('turnDuration').innerText = doc.duration + ' ' + getTranslation('secondUnit');
                    document.getElementById('turnDurationValue').innerText = doc.duration;
                    document.getElementById('turnDurationInput').value = doc.duration;
                }
                if (doc.direction) {
                    const radio = document.getElementById('direction' + doc.direction);
                    if (radio) radio.checked = true;
                }
                if (doc.customName && doc.customName.length > 0) {
                    document.getElementById('deviceName').innerText = doc.customName;
                    document.getElementById('nameInput').value = doc.customName;
                    document.getElementById('nameInput').placeholder = '';
                } else {
                    document.getElementById('deviceName').innerText = doc.mDNSHostname;
                    document.getElementById('nameInput').value = '';
                    document.getElementById('nameInput').placeholder = doc.mDNSHostname;
                }
                if (doc.ip && doc.mDNSHostname) {
                    document.getElementById('ipAddress').innerHTML = doc.ip + '<br><small style="font-size: 0.8em;">' + doc.mDNSHostname + '.local</small>';
                }
                if (doc.motorStatus) {
                    let statusKey = 'motorStatusStopped';
                    if (doc.motorStatus.includes('Çalışıyor') || doc.motorStatus.includes('çalışıyor')) {
                        statusKey = 'motorStatusRunning';
                    }
                    document.getElementById('motorStatus').innerText = getTranslation(statusKey);
                }
                if (doc.completedTurns) document.getElementById('completedTurns').innerText = doc.completedTurns;
                if (doc.version) document.getElementById('currentVersion').innerText = doc.version;
                if (doc.otaStatus) {
                    const msgBox = document.getElementById('message_box');
                    msgBox.innerText = doc.otaStatus; // OTA messages are kept in original language from server for now
                    msgBox.style.color = (doc.otaStatus.includes('Hata') || doc.otaStatus.includes('başarısız')) ? 'red' : 'green';
                    msgBox.style.display = 'block';
                    if (!doc.otaStatus.includes("indiriliyor")) {
                        setTimeout(() => { msgBox.style.display = 'none'; }, 5000);
                    }
                }
                if (doc.otherHorus) {
                    renderOtherHorusList(doc.otherHorus);
                }
            } catch(e) {
                console.error("JSON parse error:", e);
            }
        }

        function connectWebSocket() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                return;
            }
            ws = new WebSocket('ws://' + window.location.hostname + ':81/');
            ws.onopen = function() {
                console.log('WebSocket connection opened.');
                clearInterval(reconnectInterval);
                ws.send('status_request');
            };
            ws.onmessage = function(event) {
                console.log('Message received:', event.data);
                handleMessage(event.data);
            };
            ws.onclose = function() {
                console.log('WebSocket connection closed, reconnecting...');
                if (!reconnectInterval) {
                    reconnectInterval = setInterval(connectWebSocket, 5000);
                }
            };
            ws.onerror = function(error) {
                console.error('WebSocket error:', error);
                ws.close();
            };
        }

        function renderOtherHorusList(devices) {
            const listContainer = document.getElementById('otherHorusList');
            listContainer.innerHTML = '';
            if (devices.length === 0) {
                listContainer.innerHTML = `<p style="text-align: center; color: var(--secondary-color);">${getTranslation('noOtherDevices')}</p>`;
                return;
            }
            devices.forEach(device => {
                const item = document.createElement('div');
                item.className = 'other-horus-item';
                item.innerHTML = `
                    <span>${device}.local</span>
                    <div class="button-group">
                        <button class="button primary" onclick="controlOtherHorus('${device}', 'start')">${getTranslation('startButton')}</button>
                        <button class="button secondary" onclick="controlOtherHorus('${device}', 'stop')">${getTranslation('stopButton')}</button>
                        <button class="button secondary" onclick="controlOtherHorus('${device}', 'reset')">${getTranslation('resetButton')}</button>
                    </div>
                `;
                listContainer.appendChild(item);
            });
        }

        function addOtherHorus() {
            const mdnsName = document.getElementById('otherHorusName').value;
            if (!mdnsName) {
                alert(getTranslation('alertEnterMdns'));
                return;
            }
            fetch('/add_other_horus', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `mdns_name=${encodeURIComponent(mdnsName)}`
            })
            .then(response => response.text())
            .then(data => {
                console.log("Add device response:", data);
                alert(getTranslation('alertDeviceAdded') + mdnsName);
                document.getElementById('otherHorusName').value = "";
                requestStatusUpdate();
            })
            .catch(error => {
                console.error('Add device error:', error);
                alert(getTranslation('alertDeviceAddError'));
            });
        }

        function controlOtherHorus(mdnsName, action) {
            console.log(`Sending ${action} command to ${mdnsName}...`);
            fetch(`http://${mdnsName}.local/set?action=${action}`)
                .then(response => response.text())
                .then(data => {
                    console.log(`Response from ${mdnsName}:`, data);
                    alert(`${mdnsName}.local: ${getTranslation('alertCommandSuccess')}`);
                })
                .catch(error => {
                    console.error(`Control device (${mdnsName}) error:`, error);
                    alert(`${getTranslation('alertConnectionError')} ${mdnsName}.local.`);
                });
        }

        function sendSettings(action) {
            const turnsPerDay = document.getElementById('turnsPerDayInput').value;
            const turnDuration = document.getElementById('turnDurationInput').value;
            const direction = document.querySelector('input[name="direction"]:checked').value;
            let url = `/set?tpd=${turnsPerDay}&duration=${turnDuration}&dir=${direction}`;
            if (action) {
                url += `&action=${action}`;
            }
            fetch(url)
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                    if (ws && ws.readyState === WebSocket.OPEN) {
                        ws.send('status_request');
                    }
                })
                .catch(error => console.error('Error:', error));
        }

        function requestStatusUpdate() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send('status_request');
            }
        }

        function saveWiFiSettings() {
            const ssid = document.getElementById('ssidSelect').value;
            const password = document.getElementById('passwordInput').value;
            fetch('/save_wifi', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`
            })
            .then(response => response.text())
            .then(data => {
                console.log(data);
            })
            .catch(error => console.error('Error:', error));
        }

        function saveDeviceName() {
            const name = document.getElementById('nameInput').value;
            fetch('/save_wifi', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `name=${encodeURIComponent(name)}`
            })
            .then(response => response.text())
            .then(data => {
                console.log(data);
            })
            .catch(error => console.error('Error:', error));
        }

        function resetDeviceName() {
            document.getElementById('nameInput').value = "";
            saveDeviceName();
        }

        function scanNetworks() {
            const scanButton = document.querySelector('#wifiTab .button.secondary');
            const originalText = scanButton.innerText;
            scanButton.innerText = getTranslation('scanningNetworks');
            scanButton.disabled = true;
            fetch('/scan')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('ssidSelect').innerHTML = data;
                    scanButton.innerText = originalText;
                    scanButton.disabled = false;
                })
                .catch(error => {
                    console.error('Error:', error);
                    scanButton.innerText = originalText;
                    scanButton.disabled = false;
                });
        }

        function checkOTAUpdate() {
            const msgBox = document.getElementById('message_box');
            msgBox.innerText = getTranslation('otaChecking');
            msgBox.style.color = 'yellow';
            msgBox.style.display = 'block';
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send('ota_check_request');
            } else {
                msgBox.innerText = getTranslation('otaErrorConnect');
                msgBox.style.color = 'red';
                setTimeout(() => { msgBox.style.display = 'none'; }, 5000);
            }
        }

        document.querySelectorAll('input[name="theme"]').forEach(radio => {
            radio.addEventListener('change', (event) => {
                setTheme(event.target.value);
            });
        });

        window.addEventListener('beforeinstallprompt', (e) => {
            e.preventDefault();
            deferredPrompt = e;
            document.getElementById('pwa_install_button').style.display = 'block';
            console.log('Install prompt ready');
        });
        window.addEventListener('appinstalled', () => {
            console.log('PWA installed successfully.');
            document.getElementById('pwa_install_button').style.display = 'none';
        });
        document.getElementById('pwa_install_button').addEventListener('click', async () => {
            if (deferredPrompt) {
                deferredPrompt.prompt();
                const { outcome } = await deferredPrompt.userChoice;
                console.log(outcome === 'accepted' ? 'User accepted install' : 'User dismissed install');
                deferredPrompt = null;
                document.getElementById('pwa_install_button').style.display = 'none';
            }
        });

        window.onload = function() {
            loadTheme();
            loadLanguage();
            connectWebSocket();
            showTab('settings');
            if ('serviceWorker' in navigator) {
                navigator.serviceWorker.register('/sw.js').then((reg) => {
                    console.log('Service Worker registered:', reg);
                }).catch((error) => {
                    console.error('Service Worker registration failed:', error);
                });
            }
        };
    </script>
    </body>
    </html>
)PAGE_HTML";
  return page;
}

String manualUpdatePage() {
  String page = R"UPDATE_HTML(
<!DOCTYPE html>
<html lang="tr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Manuel Güncelleme</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            text-align: center;
        }
        .container {
            max-width: 500px;
            margin: auto;
            padding: 20px;
            border: 1px solid #ccc;
            border-radius: 10px;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        #message_box {
            margin-top: 20px;
            padding: 10px;
            border: 1px solid transparent;
            border-radius: 5px;
        }
        .success { color: green; border-color: green; }
        .error { color: red; border-color: red; }
        input[type="file"] { margin-top: 10px; }
        button { margin-top: 10px; padding: 10px 20px; cursor: pointer; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Manuel Güncelleme</h1>
        <p>Firmware dosyasını (.bin) seçin ve yükle'ye basın.</p>
        <input type="file" id="firmwareFile" accept=".bin">
        <br>
        <button onclick="uploadFirmware()">Yükle</button>
        <div id="message_box"></div>
    </div>

    <script>
        var ws;
        var reconnectInterval;
		let deferredPrompt;
        
        function connectWebSocket() {
            if (ws && ws.readyState === WebSocket.OPEN) return;
            ws = new WebSocket('ws://' + window.location.hostname + ':81/');
            ws.onopen = function() {
                console.log('WebSocket bağlantısı açıldı.');
                clearInterval(reconnectInterval);
            };
            ws.onmessage = function(event) {
                console.log('Gelen mesaj:', event.data);
                handleMessage(event.data);
            };
            ws.onclose = function() {
                console.log('WebSocket bağlantısı kesildi, yeniden bağlanılıyor...');
                if (!reconnectInterval) {
                    reconnectInterval = setInterval(connectWebSocket, 5000);
                }
            };
            ws.onerror = function(error) {
                console.error('WebSocket hatası:', error);
                ws.close();
            };
        }
        function handleMessage(data) {
            try {
                var doc = JSON.parse(data);
                if (doc.otaStatus) {
                    const msgBox = document.getElementById('message_box');
                    msgBox.innerText = doc.otaStatus;
                    msgBox.className = (doc.otaStatus.includes('Hata') || doc.otaStatus.includes('başarısız')) ? 'error' : 'success';
                }
            } catch(e) { console.error("JSON ayrıştırma hatası:", e); }
        }
        function uploadFirmware() {
            let fileInput = document.getElementById('firmwareFile');
            if (fileInput.files.length === 0) {
                document.getElementById('message_box').innerText = 'Lütfen bir dosya seçin.';
                return;
            }
            let formData = new FormData();
            formData.append('firmware', fileInput.files[0]);
            fetch('/manual_update', { method: 'POST', body: formData })
            .then(response => response.text())
            .then(data => {
                document.getElementById('message_box').innerText = 'Güncelleme gönderildi. Cihaz yeniden başlatılıyor...';
                document.getElementById('message_box').className = 'success';
            })
            .catch(error => {
                document.getElementById('message_box').innerText = 'Güncelleme gönderilirken hata oluştu.';
                document.getElementById('message_box').className = 'error';
            });
        }
        window.onload = function() {
            connectWebSocket();
        };
    </script>
</body>
</html>
)UPDATE_HTML";
  return page;
}
