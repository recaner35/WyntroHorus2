#include <WiFi.h>
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

// mDNS nesnesini tanımla
MDNSResponder mDNS;

// Diğer Horus cihazlarının listesi
const int MAX_OTHER_HORUS = 5;
char otherHorusList[MAX_OTHER_HORUS][32];
int otherHorusCount = 0;

// OTA Settings
const char* github_url = "https://api.github.com/repos/recaner35/WyntroHorus2/releases/latest";
const char* FIRMWARE_VERSION = "v1.0.60";

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

// Function prototypes
void readSettings();
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
  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed, even after format!");
  } else {
    Serial.println("LittleFS mounted successfully!");
  }

  LittleFS.begin();
  readSettings();
  loadOtherHorusList(); // Cihaz listesini yükle
  setupWiFi();
  setupMDNS();
  setupWebServer();
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  stopMotor();
  readSettings();
  setupWiFi();
  setupMDNS();
  setupWebServer();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  xTaskCreate(runMotorTask, "MotorTask", 4096, NULL, 1, NULL);
}

void loop() {
  server.handleClient();
  webSocket.loop();
  checkHourlyReset();
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
  readSettings(); // Ayarları buradan okuyun
  
  if (strcmp(ssid, "") == 0) {
    Serial.println("setupWiFi: Invalid WiFi credentials, running in AP mode only.");
    byte mac[6];
    WiFi.macAddress(mac);
    char macStr[13];
    sprintf(macStr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // AP adını MAC adresinin son 4 hanesine göre oluştur
    char apSsid[32];
    sprintf(apSsid, "Horus-%s%s", macStr + 8, macStr + 10);
    
    WiFi.softAP(apSsid, default_password);
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("setupWiFi: AP started: %s, IP: %s\n", apSsid, apIP.toString().c_str());
    
    // mDNS ana bilgisayar adını AP adına göre ayarla
    sprintf(mDNS_hostname, "horus-%s%s", macStr + 8, macStr + 10);

  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.printf("setupWiFi: Connecting to %s", ssid);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nsetupWiFi: Connected to %s, IP: %s\n", ssid, WiFi.localIP().toString().c_str());
      
      // mDNS ana bilgisayar adını ayarla
      if (strcmp(custom_name, "") != 0) {
        strncpy(mDNS_hostname, custom_name, sizeof(mDNS_hostname) - 1);
        mDNS_hostname[sizeof(mDNS_hostname) - 1] = '\0';
      } else {
        byte mac[6];
        WiFi.macAddress(mac);
        char macStr[13];
        sprintf(macStr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        sprintf(mDNS_hostname, "horus-%s%s", macStr + 8, macStr + 10);
      }
      
    } else {
      Serial.println("\nsetupWiFi: Failed to connect, running in AP mode.");
      // Eğer bağlantı başarısız olursa AP moduna dön
      WiFi.mode(WIFI_AP);
      byte mac[6];
      WiFi.macAddress(mac);
      char macStr[13];
      sprintf(macStr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      char apSsid[32];
      sprintf(apSsid, "Horus-%s%s", macStr + 8, macStr + 10);
      WiFi.softAP(apSsid, default_password);
      IPAddress apIP = WiFi.softAPIP();
      Serial.printf("setupWiFi: AP started: %s, IP: %s\n", apSsid, apIP.toString().c_str());
      
      // mDNS ana bilgisayar adını AP adına göre ayarla
      sprintf(mDNS_hostname, "horus-%s%s", macStr + 8, macStr + 10);
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
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", htmlPage()); });
  server.on("/manifest.json", HTTP_GET, []() {
    String manifest = R"rawliteral(
{
  "name": "Horus by Wyntro",
  "short_name": "Horus",
  "start_url": "/",
  "display": "standalone",
  "background_color": "#111827",
  "theme_color": "#3b82f6",
  "icons": [
    {
      "src": "/icon-192x192.png",
      "sizes": "192x192",
      "type": "image/png"
    },
    {
      "src": "/icon-512x512.png",
      "sizes": "512x512",
      "type": "image/png"
    }
  ]
}
      server.on("/add_other_horus", HTTP_POST, []() {
    if (otherHorusCount >= MAX_OTHER_HORUS) {
      server.send(200, "text/plain", "Error: Maximum number of devices reached.");
      return;
    }
    String mdns_name = server.arg("mdns_name");
    if (mdns_name.length() > 0 && mdns_name.length() < 32) {
      // Yalnızca MDNS adını kaydet
      strncpy(otherHorusList[otherHorusCount], mdns_name.c_str(), 31);
      otherHorusList[otherHorusCount][31] = '\0';
      otherHorusCount++;
      saveOtherHorusList();
      server.send(200, "text/plain", "OK");
    } else {
      server.send(200, "text/plain", "Error: Invalid mDNS name.");
    }
});
    )rawliteral";
    server.send(200, "application/json", manifest);
  });
  server.on("/sw.js", HTTP_GET, []() {
    String sw = R"rawliteral(
self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open('horus-v1').then((cache) => {
      return cache.addAll([
        '/',
        '/manifest.json',
        '/icon-192x192.png',
        '/icon-512x512.png'
      ]);
    })
  );
});

self.addEventListener('fetch', (event) => {
  event.respondWith(
    caches.match(event.request).then((response) => {
      return response || fetch(event.request);
    })
  );
});
    )rawliteral";
    server.send(200, "application/javascript", sw);
  });
  server.on("/icon-192x192.png", HTTP_GET, []() {
    File file = LittleFS.open("/icon-192x192.png", "r");
    if (!file) {
      server.send(404, "text/plain", "Icon not found");
      return;
    }
    server.streamFile(file, "image/png");
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
  server.on("/manual_update", HTTP_GET, []() { server.send(200, "text/html", manualUpdatePage()); });
  server.on("/manual_update", HTTP_POST, []() { server.client().setTimeout(30000); }, handleManualUpdate);
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
  String old_name = String(custom_name);
  if (server.hasArg("ssid")) strncpy(ssid, server.arg("ssid").c_str(), sizeof(ssid));
  if (server.hasArg("password")) strncpy(password, server.arg("password").c_str(), sizeof(password));
  if (server.hasArg("name")) strncpy(custom_name, server.arg("name").c_str(), sizeof(custom_name));
  writeWiFiSettings();
  if (String(custom_name) != old_name) {
    MDNS.end();
    setupMDNS();
  }
  server.send(200, "text/plain", "OK");
  Serial.println("handleSaveWiFi: WiFi settings saved, restarting...");
  delay(1000);
  ESP.restart();
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

  if (!server.hasArg("firmware")) {
    statusDoc["otaStatus"] = "Hata: Dosya seçilmedi.";
    serializeJson(statusDoc, json);
    webSocket.broadcastTXT(json);
    server.send(400, "text/plain", "No file uploaded.");
    Serial.println("handleManualUpdate: No file uploaded.");
    return;
  }

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
  doc["ip"] = WiFi.localIP().toString();
  doc["version"] = FIRMWARE_VERSION;

  // Diğer Horus cihazlarının listesini ekle
  JsonArray otherHorus = doc.createNestedArray("otherHorus");
  for (int i = 0; i < otherHorusCount; i++) {
    otherHorus.add(otherHorusList[i]);
  }

  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

String htmlPage() {
  String page = R"rawliteral(
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
            justify-content: center;
            align-items: flex-start;
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
        display: none; /* 'Horus' başlığını gizle */
    }
    .logo-svg {
        width: 100%;
        max-width: 300px;
        height: auto;
        display: block;
        margin: 0 auto;
        fill: var(--text-color); /* SVG'nin rengini tema değişkeninden alır */
    }
    .logo-svg path {
        fill: currentColor;
    }
    </style>
</head>
<body>

<div class="container">
    <div class="card">
        <svg class="logo-svg" version="1.1" xmlns="http://www.w3.org/2000/svg" xml:space="preserve" width="2479px" height="1240px" style="shape-rendering:geometricPrecision; text-rendering:geometricPrecision; image-rendering:optimizeQuality; fill-rule:evenodd; clip-rule:evenodd"
        viewBox="0 0 2475.04 1237.52"
        xmlns:xlink="http://www.w3.org/1999/xlink"
        xmlns:xodm="http://www.corel.com/coreldraw/odm/2003">
         <defs>
          <style type="text/css">
           <![CDATA[
           .fil1 {fill:none}
           .fil0 {fill:currentColor;fill-rule:nonzero}
           ]]>
          </style>
         </defs>
         <g id="Layer_x0020_1">
          <metadata id="CorelCorpID_0Corel-Layer"/>
          <g id="_2045456349696">
            <g>
            <path class="fil0" d="M715.78 686.79l48.51 0 25.46 0c3.64,0 6.63,-2.91 6.63,-6.38 0,-3.56 -2.99,-6.39 -6.63,-6.39 -10.26,0 -18.75,-8.17 -18.91,-18.03l0 -277.95c0.16,-10.02 8.65,-18.11 18.91,-18.11 3.64,0 6.63,-2.91 6.63,-6.39 0,-3.55 -2.99,-6.38 -6.63,-6.38l-46.81 0 0 0 -27.16 0c-3.64,0 -6.55,2.83 -6.55,6.38 0,3.48 2.91,6.39 6.55,6.39 10.43,0 18.92,8.17 18.92,18.27l0.08 107.28c0,5.74 1.78,11.16 4.85,15.77l-183.93 0 0 -123.05c0,-10.1 8.49,-18.27 18.92,-18.27 3.64,0 6.55,-2.91 6.55,-6.39 0,-3.55 -2.91,-6.38 -6.55,-6.38l-46.81 0 -0.08 0 -27.08 0c-3.64,0 -6.63,2.83 -6.63,6.38 0,3.48 2.99,6.39 6.63,6.39 10.35,0 18.83,8.17 18.92,18.19l0 277.63c-0.09,10.1 -8.57,18.27 -18.92,18.27 -3.64,0 -6.63,2.83 -6.63,6.39 0,3.47 2.99,6.38 6.63,6.38l73.97 0c3.64,0 6.55,-2.91 6.55,-6.38 0,-3.56 -2.91,-6.39 -6.55,-6.39 -10.43,0 -18.92,-8.25 -19,-18.27l0 -126.04c0,-5.74 -1.69,-11.16 -4.85,-15.77l184.01 0 0 140.11 -0.08 0 0 1.7c0,10.02 -8.41,18.27 -18.92,18.27 -3.64,0 -6.55,2.83 -6.55,6.39 0,3.47 2.91,6.38 6.55,6.38l0 0zm272.86 6.23c1.05,0 2.02,-0.08 3.07,-0.08 0,0 3.64,-0.09 4.04,-0.17l1.38 0 0.16 -0.08c89.82,-4.6 159.99,-81.57 159.99,-175.68 0,-97.17 -75.67,-176.16 -168.64,-176.16 -2.43,0 -4.93,0.08 -7.12,0.24l-2.42 0 -0.08 0.08c-89.42,5.18 -159.03,81.98 -159.03,175.84 0,97.02 75.67,176.01 168.65,176.01l0 0zm-136.8 -169.62c3.64,0 6.63,-2.91 6.63,-6.39 0,-18.83 2.51,-37.27 7.52,-54.81 17.71,-62.49 63.22,-105.02 116.02,-108.33 2.18,-0.17 4.44,-0.24 6.63,-0.24 83.51,0 152.07,69.93 155.3,156.92l-18.59 0c-3.64,0 -6.63,2.91 -6.63,6.46 0,24.1 -4.13,47.3 -12.13,69.05 -20.45,54.89 -63.14,90.95 -111.32,93.94l-3.16 0.16c-1.13,0 -2.34,0.08 -3.47,0.08 -83.52,0 -152.08,-69.85 -155.31,-156.84l18.51 0zm602.95 163.39c3.64,0 6.55,-2.83 6.55,-6.38 0,-3.56 -2.91,-6.39 -6.55,-6.39 -0.96,0 -1.93,-0.08 -2.99,-0.08 -14.14,-0.97 -26.68,-8.57 -33.71,-20.54l-0.08 -0.08 -0.73 -1.29 -0.08 -0.08 -68.23 -110.68 -0.49 -0.65c-4.85,-6.87 -10.91,-12.69 -17.78,-17.38 50.77,-1.86 91.43,-40.67 91.43,-88.04 0,-48.1 -42.12,-87.4 -93.46,-87.96l-0.16 -0.08 -141.48 0c-3.64,0 -6.63,2.83 -6.63,6.38 0,3.48 2.99,6.39 6.63,6.39 10.43,0 18.92,8.17 18.92,18.27l0 277.55c-0.08,10.1 -8.57,18.27 -18.92,18.27 -3.64,0 -6.63,2.83 -6.63,6.39 0,3.47 2.99,6.38 6.63,6.38l73.9 0c3.63,0 6.62,-2.91 6.62,-6.38 0,-3.56 -2.99,-6.39 -6.62,-6.39 -10.43,0 -18.92,-8.25 -18.92,-18.27l0 -116.66c0,-5.74 -1.78,-11.16 -4.86,-15.77l50.38 0 0.08 0 1.69 0c4.13,0 8.09,2.02 10.27,5.1l66.13 107.84c17.63,30.4 49.56,49.24 85.21,50.37 1.3,0.08 2.59,0.16 3.88,0.16l0 0zm-126.84 -176.24l-85.94 0 0 -134.86c0,-5.73 -1.7,-11.23 -4.86,-15.76l87.89 0 0 0 1.69 0c31.37,0 56.92,33.79 56.92,75.27 0,41.07 -24.9,74.54 -55.62,75.35l-0.08 0zm265.09 182.47c23.69,0 46.33,-6.79 65.16,-19.49 1.62,-0.89 3.15,-1.94 4.45,-3.07 4.28,-2.99 8.25,-6.31 11.8,-9.7 22.24,-21.26 34.53,-49.64 34.53,-79.88l0 -202.76c0.16,-10.02 8.56,-18.19 18.91,-18.19 3.64,0 6.63,-2.83 6.63,-6.39 0,-3.47 -2.99,-6.38 -6.63,-6.38l-51.01 0c-3.64,0 -6.63,2.91 -6.63,6.38 0,3.56 2.99,6.39 6.63,6.39 10.43,0 18.92,8.25 18.92,18.35l0 202.6c0,26.76 -10.92,51.91 -31.05,71.15 -2.75,2.67 -5.74,5.17 -8.97,7.52l-1.13 0.8c-1.22,0.9 -2.35,1.62 -3.48,2.35 -14.47,9.3 -31.37,14.15 -48.83,14.15 -23.93,0 -46.33,-8.98 -63.06,-25.14 -16.9,-16.34 -26.12,-37.92 -26.12,-60.88l0 -210.85 0 -0.08 0 -1.62c0,-10.1 8.49,-18.35 18.92,-18.35 3.64,0 6.63,-2.83 6.63,-6.39 0,-3.47 -2.99,-6.38 -6.63,-6.38l-73.89 0c-3.64,0 -6.63,2.91 -6.63,6.38 0,3.56 2.99,6.39 6.63,6.39 10.35,0 18.84,8.17 18.92,18.27l0 202.68c0,61.85 51.98,112.14 115.93,112.14l0 0zm275.69 0c1.94,0 3.8,-0.08 5.74,-0.17 1.37,0 2.75,-0.08 4.12,-0.24l2.75 -0.24 0.97 -0.16 0.89 0 0.32 -0.08c55.63,-5.9 97.5,-45.2 97.5,-91.52 0,-34.12 -22.71,-65.32 -59.26,-81.41l-95.15 -42.21c-25.07,-11.96 -40.67,-33.79 -40.67,-57.07 0,-34.04 31.85,-62.41 74.06,-65.89l3.55 -0.24c1.7,-0.09 3.4,-0.09 5.18,-0.09 1.7,0 3.47,0 5.01,0.09l3.56 0.24c41.07,3.39 72.92,30.88 74.13,63.87 0.09,1.86 0.97,3.56 2.43,4.68 1.13,0.98 2.67,1.46 4.12,1.46l0.65 0 13.9 -2.26c1.78,-0.32 3.4,-1.38 4.45,-2.83l1.46 -2.67 -0.57 -2.34c-9.86,-39.38 -50.77,-69.29 -99.6,-72.68l-3.88 -0.25c-1.86,-0.08 -3.72,-0.08 -5.66,-0.08 -1.94,0 -3.8,0 -5.82,0.08l-3.8 0.25c-57.16,3.96 -101.95,44.06 -101.95,91.27 0,32.42 21.18,62.74 55.46,79.23l96.21 42.52c26.92,11.81 43.65,34.36 43.65,58.86 0,32.91 -30.55,61.44 -70.74,66.38 -0.48,0 -4.2,0.4 -4.2,0.4l-3.64 0.16c-1.7,0.08 -3.47,0.08 -5.17,0.08 -1.7,0 -3.48,0 -5.02,-0.08l-3.71 -0.16c-42.77,-3.56 -75.03,-32.34 -75.03,-66.78 0,-1.62 0.08,-2.91 0.24,-4.2 0.25,-1.86 -0.4,-3.8 -1.77,-5.26 -1.22,-1.21 -2.91,-1.86 -5.26,-1.86 0,0 -15.2,1.3 -15.2,1.3 -1.78,0.16 -3.47,0.97 -4.61,2.34l-1.69 2.43 0.32 2.34c5.5,43.98 48.42,78.66 102.11,82.38l3.96 0.24c1.86,0.09 3.72,0.17 5.66,0.17l0 0z"/>
            <path class="fil0" d="M497.36 892.87l79.55 0c28.34,0 51.42,-20.82 51.42,-46.41 0,-20.86 -15.28,-38.56 -36.46,-44.34 15.89,-6.87 26.52,-21.22 26.52,-37.39 0,-22.52 -20.14,-40.79 -45.8,-41.64 -0.37,0 -0.69,-0.04 -0.97,-0.04l-0.04 0 -74.22 0c-1.82,0 -3.32,1.42 -3.32,3.2 0,1.73 1.5,3.19 3.32,3.19 5.21,0 9.46,4.08 9.46,9.13l0 138.78c-0.04,5.05 -4.29,9.13 -9.46,9.13 -1.82,0 -3.32,1.42 -3.32,3.19 0,1.74 1.5,3.2 3.32,3.2l0 0zm80.2 -6.39l-52.71 0 0 -72.15c0,-2.87 -0.89,-5.58 -2.43,-7.89l54.49 0c17.75,0 32.18,17.95 32.18,40.02 0,21.79 -14.07,39.58 -31.53,40.02l0 0zm-52.67 -86.42l0 -62.74c0,-2.87 -0.89,-5.62 -2.47,-7.88l47.78 0 0 0 0.81 0c0.36,0 0.77,0 1.13,0.04 15.16,0.93 27,16.41 27,35.25 0,18.88 -11.84,34.4 -27,35.33 -0.36,0 -0.77,0 -1.17,0l-46.08 0zm206.84 92.81c1.82,0 3.32,-1.46 3.32,-3.2 0,-1.77 -1.5,-3.19 -3.32,-3.19 -5.17,0 -9.42,-4.08 -9.46,-9.09l0 -51.1 48.79 -85.17 0.37 -0.08 0.36 -1.13c3.4,-5.7 11.04,-10.47 16.82,-10.47 1.82,0 3.27,-1.46 3.27,-3.19 0,-1.78 -1.45,-3.2 -3.27,-3.2l-25.55 0c-1.82,0 -3.27,1.42 -3.27,3.2 0,1.73 1.45,3.19 3.27,3.19 1.09,0 3.03,0.2 3.84,1.45 0.77,1.3 0.4,3.64 -0.97,6.07l-46.12 80.48 -46.08 -80.44c-1.42,-2.47 -1.83,-4.81 -1.02,-6.11 0.77,-1.29 2.71,-1.49 3.8,-1.49 1.82,0 3.32,-1.42 3.32,-3.15 0,-1.78 -1.5,-3.2 -3.32,-3.2l-38.6 0c-1.82,0 -3.32,1.42 -3.32,3.2 0,1.73 1.5,3.19 3.32,3.19 5.82,0 13.58,4.89 16.94,10.67l49.35 86.14 0 51.1c0,5.01 -4.24,9.13 -9.46,9.13 -1.81,0 -3.31,1.42 -3.31,3.19 0,1.74 1.5,3.2 3.31,3.2l36.99 0zm308.14 3.11l0.49 0c1.37,0 2.63,-0.81 3.11,-2.06l6.15 -15.89 53.52 -138.32c2.14,-5.58 8.77,-10.27 14.47,-10.27 1.82,0 3.27,-1.46 3.27,-3.19 0,-1.78 -1.45,-3.2 -3.27,-3.2l-25.55 0c-1.82,0 -3.27,1.42 -3.27,3.2 0,1.73 1.45,3.19 3.27,3.19 2.15,0 3.84,0.69 4.73,1.94 1.05,1.45 1.09,3.68 0.16,6.06l-50.44 130.41 -50.49 -130.41c-0.93,-2.38 -0.85,-4.61 0.2,-6.06 0.89,-1.25 2.59,-1.94 4.69,-1.94 1.82,0 3.31,-1.46 3.31,-3.19 0,-1.78 -1.49,-3.2 -3.31,-3.2l-11.32 0 0 0 -26.44 0c-1.82,0 -3.31,1.42 -3.31,3.2 0,1.73 1.49,3.19 3.31,3.19 5.66,0 12.29,4.69 14.43,10.27l16.3 42 -33.35 86.14 -50.49 -130.41c-0.93,-2.42 -0.85,-4.61 0.16,-6.06 0.93,-1.25 2.59,-1.94 4.73,-1.94 1.82,0 3.31,-1.46 3.31,-3.19 0,-1.78 -1.49,-3.2 -3.31,-3.2l-11.36 0 0 0 -26.4 0c-1.82,0 -3.31,1.42 -3.31,3.2 0,1.73 1.49,3.19 3.31,3.19 5.66,0 12.29,4.69 14.43,10.27l59.67 154.09 0.44 1.37 0.77 0.21c0.04,0.04 0.04,0.04 0.08,0.08l0.45 0.52 1.37 0c1.38,0 2.59,-0.81 3.11,-2.06l39.9 -103.08 39.86 103 0.44 1.29 0.25 0.08 0.08 0.17 1.21 0.4 0.64 0.2 0 0zm188.09 -3.11c1.82,0 3.32,-1.46 3.32,-3.2 0,-1.77 -1.5,-3.19 -3.32,-3.19 -5.17,0 -9.41,-4.08 -9.46,-9.09l0 -51.1 48.8 -85.17 0.36 -0.08 0.36 -1.13c3.4,-5.7 11.04,-10.47 16.82,-10.47 1.82,0 3.27,-1.46 3.27,-3.19 0,-1.78 -1.45,-3.2 -3.27,-3.2l-25.55 0c-1.82,0 -3.27,1.42 -3.27,3.2 0,1.73 1.45,3.19 3.27,3.19 1.09,0 3.03,0.2 3.84,1.45 0.77,1.3 0.41,3.64 -0.97,6.07l-46.12 80.48 -46.08 -80.44c-1.42,-2.47 -1.82,-4.81 -1.01,-6.11 0.76,-1.29 2.7,-1.49 3.8,-1.49 1.81,0 3.31,-1.42 3.31,-3.15 0,-1.78 -1.5,-3.2 -3.31,-3.2l-38.61 0c-1.82,0 -3.31,1.42 -3.31,3.2 0,1.73 1.49,3.19 3.31,3.19 5.82,0 13.58,4.89 16.94,10.67l49.36 86.14 0 51.1c0,5.01 -4.25,9.13 -9.46,9.13 -1.82,0 -3.32,1.42 -3.32,3.19 0,1.74 1.5,3.2 3.32,3.2l36.98 0zm224.23 3.8l0.6 0 1.74 -0.57 0.29 -0.68c0.44,-0.53 0.68,-1.22 0.68,-1.94l0 -154.91c0,-5.05 4.25,-9.13 9.46,-9.13 1.82,0 3.32,-1.46 3.32,-3.19 0,-1.78 -1.5,-3.2 -3.32,-3.2l-25.5 0c-1.87,0 -3.32,1.42 -3.32,3.2 0,1.73 1.45,3.19 3.32,3.19 5.21,0 9.45,4.08 9.45,9.13l0 127.05 -110.35 -141.4c-0.65,-0.8 -1.62,-1.25 -2.67,-1.25l-13.7 0.04 -0.32 0.04 -12.98 0c-1.82,0 -3.32,1.42 -3.32,3.2 0,1.73 1.5,3.19 3.32,3.19 2.59,0 5.98,1.01 9.46,2.79l0 145.24c0,5.01 -4.25,9.13 -9.46,9.13 -1.82,0 -3.32,1.42 -3.32,3.2 0,1.73 1.5,3.19 3.32,3.19l25.51 0c1.82,0 3.31,-1.46 3.31,-3.19 0,-1.78 -1.49,-3.2 -3.31,-3.2 -5.22,0 -9.46,-4.12 -9.46,-9.13l0 -140.75c1.41,1.21 2.66,2.46 3.68,3.75l120.78 154.7 0.61 0.97 0.64 0.17c0.49,0.24 0.97,0.36 1.5,0.36l0.04 0zm130.04 -3.8c1.82,0 3.31,-1.46 3.31,-3.2 0,-1.77 -1.49,-3.19 -3.31,-3.19 -5.18,0 -9.42,-4.08 -9.46,-9.09l0 -147.95 32.06 0c13.09,0 26.23,10.35 29.3,23.04 0.32,1.46 1.66,2.46 3.2,2.46l0.44 0 0.32 -0.07c1.62,-0.37 2.67,-1.83 2.55,-3.4l0 -0.12 -6.22 -25.87 -0.21 -0.61 -0.24 -0.32c-0.16,-0.25 -0.32,-0.45 -0.53,-0.65l-1.13 -0.85 -62.01 0 -0.04 0 -12.24 0 -61.73 0c-0.81,0 -1.58,0.32 -2.22,0.89l-0.41 0.36 -0.12 0.25 -0.32 0.36 -6.19 25.87 -0.16 0.45 0 0.28c-0.12,1.53 0.93,2.99 2.51,3.36l0.44 0.07 0.37 0c1.53,0 2.87,-1 3.19,-2.46 3.07,-12.69 16.21,-23.04 29.31,-23.04l32.05 0 0 147.91c0,5.05 -4.28,9.13 -9.5,9.13 -1.82,0 -3.27,1.42 -3.27,3.19 0,1.74 1.45,3.2 3.27,3.2l36.99 0zm214.65 0c1.81,0 3.27,-1.42 3.27,-3.2 0,-1.77 -1.46,-3.19 -3.27,-3.19 -0.49,0 -0.98,-0.04 -1.5,-0.04 -7.07,-0.48 -13.34,-4.28 -16.86,-10.26l-0.04 -0.05 -0.36 -0.64 -0.04 -0.04 -34.12 -55.34 -0.24 -0.33c-2.43,-3.43 -5.46,-6.34 -8.89,-8.69 25.38,-0.93 45.71,-20.33 45.71,-44.02 0,-24.05 -21.06,-43.7 -46.73,-43.98l-0.08 -0.04 -70.74 0c-1.82,0 -3.31,1.42 -3.31,3.2 0,1.73 1.49,3.19 3.31,3.19 5.22,0 9.46,4.08 9.46,9.13l0 138.78c-0.04,5.05 -4.28,9.13 -9.46,9.13 -1.82,0 -3.31,1.42 -3.31,3.19 0,1.74 1.49,3.2 3.31,3.2l36.95 0c1.82,0 3.32,-1.46 3.32,-3.2 0,-1.77 -1.5,-3.19 -3.32,-3.19 -5.22,0 -9.46,-4.12 -9.46,-9.13l0 -58.33c0,-2.87 -0.89,-5.58 -2.42,-7.89l25.18 0 0.04 0 0.85 0c2.06,0 4.04,1.01 5.13,2.55l33.07 53.92c8.81,15.2 24.78,24.62 42.6,25.19 0.65,0.04 1.3,0.08 1.95,0.08l0 0zm-63.43 -88.12l-42.97 0 0 -67.43c0,-2.87 -0.85,-5.62 -2.42,-7.88l43.94 0 0 0 0.85 0c15.68,0 28.45,16.89 28.45,37.63 0,20.54 -12.45,37.27 -27.81,37.68l-0.04 0zm163.23 91.23c0.52,0 1.01,-0.04 1.53,-0.04 0,0 1.82,-0.04 2.03,-0.08l0.68 0 0.08 -0.04c44.91,-2.31 80,-40.79 80,-87.84 0,-48.59 -37.83,-88.08 -84.32,-88.08 -1.21,0 -2.47,0.04 -3.56,0.12l-1.21 0 -0.04 0.04c-44.71,2.59 -79.51,40.99 -79.51,87.92 0,48.51 37.83,88 84.32,88l0 0zm-68.4 -84.8c1.82,0 3.32,-1.46 3.32,-3.2 0,-9.42 1.25,-18.63 3.76,-27.41 8.85,-31.24 31.61,-52.51 58,-54.16 1.09,-0.08 2.23,-0.12 3.32,-0.12 41.76,0 76.04,34.96 77.65,78.46l-9.3 0c-1.82,0 -3.31,1.45 -3.31,3.23 0,12.05 -2.06,23.65 -6.06,34.52 -10.23,27.45 -31.57,45.48 -55.67,46.97l-1.57 0.08c-0.57,0 -1.17,0.05 -1.74,0.05 -41.76,0 -76.04,-34.93 -77.65,-78.42l9.25 0z"/>
            </g>
            <polygon class="fil1" points="0,0 2475.04,0 2475.04,1237.52 0,1237.52 "/>
          </g>
         </g>
        </svg>
        <div class="status-section">
            <div class="status-item">
                <span>Motor Durumu</span>
                <h3 id="motorStatus">Durduruldu</h3>
            </div>
            <div class="status-item">
                <span>Tamamlanan Turlar</span>
                <h3 id="completedTurns">0</h3>
            </div>
            <div class="status-item">
                <span>Günde Dönüş</span>
                <h3 id="turnsPerDay">600</h3>
            </div>
            <div class="status-item">
                <span>Dönüş Süresi</span>
                <h3 id="turnDuration">15 s</h3>
            </div>
        </div>
    </div>

    <div class="card">
        <div class="tabs-container">
            <div class="tab" id="tab-settings" onclick="showTab('settings')">Ayarlar</div>
            <div class="tab" id="tab-wifi" onclick="showTab('wifi')">WiFi Ayarları</div>
            <div class="tab" id="tab-otherHorus" onclick="showTab('otherHorus')">Diğer Horus Cihazları</div>
            <div class="tab" id="tab-theme" onclick="showTab('theme')">Tema</div>
            <div class="tab" id="tab-ota" onclick="showTab('ota')">Cihaz Güncelleme</div>
        </div>
        
        <div id="settingsTab" class="tab-content">
            <h2>Ayarlar</h2>
            <div class="form-group">
                <label for="turnsPerDay">Günde Dönüş Sayısı</label>
                <input type="range" id="turnsPerDayInput" min="600" max="1200" step="100" value="600" oninput="document.getElementById('turnsPerDayValue').innerText = this.value;">
                <p style="text-align: center; margin: 5px 0 0;"><span id="turnsPerDayValue">600</span> Tur</p>
            </div>
            <div class="form-group">
                <label for="turnDuration">Dönüş Süresi (saniye)</label>
                <input type="range" id="turnDurationInput" min="10" max="15" step="0.5" value="15" oninput="document.getElementById('turnDurationValue').innerText = this.value;">
                <p style="text-align: center; margin: 5px 0 0;"><span id="turnDurationValue">15.0</span> s</p>
            </div>
            <div class="form-group">
                <label>Yön</label>
                <div class="radio-group">
                    <div class="radio-item">
                        <input type="radio" id="direction1" name="direction" value="1" checked>
                        <label for="direction1">Sadece İleri</label>
                    </div>
                    <div class="radio-item">
                        <input type="radio" id="direction2" name="direction" value="2">
                        <label for="direction2">Sadece Geri</label>
                    </div>
                    <div class="radio-item">
                        <input type="radio" id="direction3" name="direction" value="3">
                        <label for="direction3">İleri ve Geri</label>
                    </div>
                </div>
            </div>
            <div class="button-group">
                <button class="button primary" onclick="sendSettings('start')">Başlat</button>
                <button class="button secondary" onclick="sendSettings('stop')">Durdur</button>
                <button class="button secondary" onclick="sendSettings('reset')">Sıfırla</button>
            </div>
        </div>
        
        <div id="wifiTab" class="tab-content">
            <h2>WiFi Ayarları</h2>
            <div class="form-group">
                <label for="ssid">Ağ Adı (SSID)</label>
                <select id="ssidSelect"></select>
            </div>
            <div class="form-group">
                <label for="password">Şifre</label>
                <input type="text" id="passwordInput" placeholder="Şifrenizi buraya girin">
            </div>
            <div class="button-group">
                <button class="button primary" onclick="saveWiFiSettings()">Kaydet ve Yeniden Başlat</button>
                <button class="button secondary" onclick="scanNetworks()">Ağları Tara</button>
            </div>
        </div>

        <div id="otherHorusTab" class="tab-content">
            <h2>Diğer Horus Cihazları</h2>
            <div class="form-group inline-group">
                <label for="otherHorusName" style="display:none;">MDNS Adı (Örn: horus-D99D)</label>
                <input type="text" id="otherHorusName" placeholder="MDNS Adı (Örn: horus-D99D)">
                <button class="button primary" onclick="addOtherHorus()">Ekle</button>
            </div>
            <div class="other-horus-list" id="otherHorusList">
                </div>
        </div>

        <div id="themeTab" class="tab-content">
            <h2>Tema</h2>
            <div class="radio-group">
                <div class="radio-item">
                    <input type="radio" id="themeSystem" name="theme" value="system" checked>
                    <label for="themeSystem">Sistem</label>
                </div>
                <div class="radio-item">
                    <input type="radio" id="themeDark" name="theme" value="dark">
                    <label for="themeDark">Karanlık</label>
                </div>
                <div class="radio-item">
                    <input type="radio" id="themeLight" name="theme" value="light">
                    <label for="themeLight">Aydınlık</label>
                </div>
            </div>
        </div>
        
        <div id="otaTab" class="tab-content">
            <h2>Cihaz Güncelleme</h2>
            <div class="status-section" style="margin-bottom: 20px;">
                <div class="status-item">
                    <span>Güncel Sürüm</span>
                    <h3 id="currentVersion">Yükleniyor...</h3>
                </div>
                <div class="status-item">
                    <span>Web Arayüzü:</span>
                    <h3 id="ipAddress"></h3>
                </div>
                <div class="status-item">
                    <span>Cihaz Adı</span>
                    <h3 id="deviceName">Yükleniyor...</h3>
                </div>
            </div>
            <div class="form-group">
                <label for="name">Cihaz Adı (Opsiyonel)</label>
                <input type="text" id="nameInput" placeholder="Cihaz Adı">
            </div>
            <div class="button-group">
                <button class="button primary" onclick="saveDeviceName()">Kaydet</button>
                <button class="button secondary" onclick="resetDeviceName()">Sıfırla</button>
            </div>
            <p id="message_box" style="display:none; text-align: center;"></p>
            <div class="button-group" style="margin-top: 5px;">
                <button class="button primary" onclick="checkOTAUpdate()">Güncelleme Kontrol Et</button>
                <button class="button secondary" onclick="window.location.href='/manual_update'">Manuel Güncelleme</button>
            </div>
        </div>

    </div>
</div>

<script>
    var ws;
    var reconnectInterval;

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
            if (doc.tpd) document.getElementById('turnsPerDayValue').innerText = doc.tpd;
            if (doc.duration) document.getElementById('turnDurationValue').innerText = doc.duration;
            if (doc.tpd) document.getElementById('turnsPerDayInput').value = doc.tpd;
            if (doc.duration) document.getElementById('turnDurationInput').value = doc.duration;
            if (doc.direction) {
                const radio = document.getElementById('direction' + doc.direction);
                if (radio) radio.checked = true;
            }
            if (doc.customName) document.getElementById('nameInput').value = doc.customName;
            if (doc.motorStatus) document.getElementById('motorStatus').innerText = doc.motorStatus;
            if (doc.completedTurns) document.getElementById('completedTurns').innerText = doc.completedTurns;
            if (doc.status) document.getElementById('motorStatus').innerText = doc.status;
            if (doc.hourlyTurns) document.getElementById('hourlyTurns').innerText = doc.hourlyTurns;
            if (doc.ip) document.getElementById('ipAddress').innerText = doc.ip;
            if (doc.version) document.getElementById('currentVersion').innerText = doc.version;
            if (doc.customName) document.getElementById('deviceName').innerText = doc.customName;
            

            if (doc.otaStatus) {
                const msgBox = document.getElementById('message_box');
                msgBox.innerText = doc.otaStatus;
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
            console.error("JSON ayrıştırma hatası:", e);
        }
    }

    function connectWebSocket() {
        if (ws && ws.readyState === WebSocket.OPEN) {
            return;
        }

        ws = new WebSocket('ws://' + window.location.hostname + ':81/');

        ws.onopen = function() {
            console.log('WebSocket bağlantısı açıldı.');
            clearInterval(reconnectInterval);
            ws.send('status_request');
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

    function renderOtherHorusList(devices) {
        const listContainer = document.getElementById('otherHorusList');
        listContainer.innerHTML = '';
        if (devices.length === 0) {
            listContainer.innerHTML = '<p style="text-align: center; color: var(--secondary-color);">Henüz başka cihaz eklenmemiş.</p>';
            return;
        }
        devices.forEach(device => {
            const item = document.createElement('div');
            item.className = 'other-horus-item';
            item.innerHTML = `
                <span>${device}.local</span>
                <div class="button-group">
                    <button class="button primary" onclick="controlOtherHorus('${device}', 'start')">Başlat</button>
                    <button class="button secondary" onclick="controlOtherHorus('${device}', 'stop')">Durdur</button>
                    <button class="button secondary" onclick="controlOtherHorus('${device}', 'reset')">Sıfırla</button>
                </div>
            `;
            listContainer.appendChild(item);
        });
    }

    function addOtherHorus() {
        const mdnsName = document.getElementById('otherHorusName').value;
        if (!mdnsName) {
            alert("Lütfen bir MDNS adı girin.");
            return;
        }
        
        fetch('/add_other_horus', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `mdns_name=${encodeURIComponent(mdnsName)}`
        })
        .then(response => response.text())
        .then(data => {
            console.log("Cihaz ekleme yanıtı:", data);
            alert("Cihaz eklendi: " + mdnsName);
            document.getElementById('otherHorusName').value = "";
            requestStatusUpdate();
        })
        .catch(error => {
            console.error('Cihaz ekleme hatası:', error);
            alert('Cihaz eklenirken bir hata oluştu.');
        });
    }

    function controlOtherHorus(mdnsName, action) {
        console.log(`Cihaz ${mdnsName} için ${action} komutu gönderiliyor...`);
        fetch(`http://${mdnsName}.local/set?action=${action}`)
            .then(response => response.text())
            .then(data => {
                console.log(`Yanıt (${mdnsName}):`, data);
                alert(`${mdnsName}.local cihazı için komut başarıyla gönderildi.`);
            })
            .catch(error => {
                console.error(`Cihaz (${mdnsName}) kontrol hatası:`, error);
                alert(`${mdnsName}.local cihazına bağlanılamadı.`);
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
            .catch(error => console.error('Hata:', error));
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
        .catch(error => console.error('Hata:', error));
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
        .catch(error => console.error('Hata:', error));
    }
    
    function resetDeviceName() {
        document.getElementById('nameInput').value = "";
        saveDeviceName();
    }

    function scanNetworks() {
        const scanButton = document.querySelector('#wifiTab .button.secondary');
        const originalText = scanButton.innerText;
        scanButton.innerText = "Taranıyor...";
        scanButton.disabled = true;

        fetch('/scan')
            .then(response => response.text())
            .then(data => {
                document.getElementById('ssidSelect').innerHTML = data;
                scanButton.innerText = originalText;
                scanButton.disabled = false;
            })
            .catch(error => {
                console.error('Hata:', error);
                scanButton.innerText = originalText;
                scanButton.disabled = false;
            });
    }

    function checkOTAUpdate() {
        const msgBox = document.getElementById('message_box');
        msgBox.innerText = "Güncelleme kontrol ediliyor...";
        msgBox.style.color = 'yellow';
        msgBox.style.display = 'block';

        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send('ota_check_request');
        } else {
            msgBox.innerText = "Hata: Sunucuya bağlanılamadı.";
            msgBox.style.color = 'red';
            setTimeout(() => { msgBox.style.display = 'none'; }, 5000);
        }
    }
    
    document.querySelectorAll('input[name="theme"]').forEach(radio => {
        radio.addEventListener('change', (event) => {
            setTheme(event.target.value);
        });
    });

    window.onload = function() {
        loadTheme();
        connectWebSocket();
        showTab('settings'); // İlk sekme olarak Ayarlar'ı göster
        if ('serviceWorker' in navigator) {
            navigator.serviceWorker.register('/sw.js').then((reg) => {
                console.log('Service Worker registered:', reg);
            }).catch((error) => {
                console.error('Service Worker registration failed:', error);
            });
        }
    };
    let deferredPrompt;
	const installButton = document.getElementById('pwa_install_button');

	window.addEventListener('beforeinstallprompt', (e) => {
    	e.preventDefault();
       	deferredPrompt = e;
   	 	installButton.style.display = 'block';
});

	installButton.addEventListener('click', async () => {
    	if (deferredPrompt) {
     	   deferredPrompt.prompt();
     	   const { outcome } = await deferredPrompt.userChoice;
        if (outcome === 'accepted') {
            console.log('Kullanıcı uygulamayı yüklemeyi kabul etti.');
        } else {
            console.log('Kullanıcı uygulamayı yüklemeyi reddetti.');
        }
        deferredPrompt = null;
        installButton.style.display = 'none';
    }
});

window.addEventListener('appinstalled', () => {
    console.log('PWA başarıyla yüklendi.');
    installButton.style.display = 'none';
});
</script>
<button id="pwa_install_button" class="btn primary" style="display:none;">Uygulamayı Yükle</button>
</body>
</html>
)rawliteral";
  return page;
}

String manualUpdatePage() {
  String page = R"rawliteral(
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
            background-color: #111827;
            color: #f9fafb;
        }
        .container {
            max-width: 500px;
            margin: auto;
            padding: 20px;
            border: 1px solid #4b5563;
            border-radius: 10px;
            background-color: #1f2937;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        #message_box {
            margin-top: 20px;
            padding: 10px;
            border: 1px solid transparent;
            border-radius: 5px;
        }
        .success {
            color: green;
            border-color: green;
        }
        .error {
            color: red;
            border-color: red;
        }
        input[type="file"] {
            margin-top: 10px;
            padding: 8px;
            background-color: #374151;
            border: 1px solid #4b5563;
            color: #f9fafb;
            border-radius: 5px;
        }
        button {
            margin-top: 10px;
            padding: 10px 20px;
            cursor: pointer;
            background-color: #3b82f6;
            color: white;
            border: none;
            border-radius: 5px;
            font-weight: bold;
            transition: background-color 0.3s ease;
        }
        button:hover {
            background-color: #2563eb;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Manuel Güncelleme</h1>
        <p>Firmware dosyasını (.bin) seçin ve yükle'ye basın.</p>
        <input type="file" id="firmwareFile" accept=".bin">
        <br>
        <button id="uploadButton" onclick="uploadFirmware()">Yükle</button>
        <div id="message_box"></div>
    </div>

    <script>
        document.getElementById('firmwareFile').addEventListener('change', function() {
            const msgBox = document.getElementById('message_box');
            if (this.files.length > 0) {
                msgBox.innerText = this.files[0].name + " seçildi.";
                msgBox.className = 'success';
            } else {
                msgBox.innerText = "Dosya seçilmedi.";
                msgBox.className = 'error';
            }
        });

                function uploadFirmware() {
            const msgBox = document.getElementById('message_box');
            const fileInput = document.getElementById('firmwareFile');
            const uploadButton = document.getElementById('uploadButton');

            if (fileInput.files.length === 0) {
                msgBox.innerText = 'Lütfen bir dosya seçin.';
                msgBox.className = 'error';
                return;
            }

            msgBox.innerText = 'Yükleniyor...';
            msgBox.className = 'success';
            uploadButton.disabled = true;

            const formData = new FormData();
            formData.append('firmware', fileInput.files[0]);

            fetch('/manual_update', {
                method: 'POST',
                body: formData
            })
            .then(response => {
                if (response.status >= 400) {
                    throw new Error('Sunucu hatası: ' + response.status);
                }
                return response.text();
            })
            .then(data => {
                console.log('Sunucudan yanıt:', data);
                msgBox.innerText = 'Güncelleme başarıyla gönderildi!';
                msgBox.className = 'success';
                setTimeout(() => {
                    msgBox.innerText = '';
                }, 5000);
            })
            .catch(error => {
                console.error('Yükleme hatası:', error);
                msgBox.innerText = 'Yükleme sırasında bir hata oluştu: ' + error.message;
                msgBox.className = 'error';
                uploadButton.disabled = false;
            });
        }
    </script>
</body>
</html>
)rawliteral";
  return page;
}
