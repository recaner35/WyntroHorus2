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

// Diğer Horus cihazlarının listesi
const int MAX_OTHER_HORUS = 5;
char otherHorusList[MAX_OTHER_HORUS][32];
int otherHorusCount = 0;

// OTA Settings
const char* github_url = "https://api.github.com/repos/recaner35/WyntroHorus2/releases/latest";
const char* FIRMWARE_VERSION = "v1.0.59";

// WiFi Settings
const char* default_ssid = "HorusAP";
const char* default_password = "12345678";
char ssid[32] = "";
char password[64] = "";
char custom_name[21] = "";
char mDNS_hostname[32] = "";

// Motor Settings
int turnsPerDay = 600;
float turnDuration = 15.0;
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
  loadSettings();
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
  doc["tpd"] = turnsPerDay;
  doc["duration"] = turnDuration;
  doc["direction"] = direction;
  doc["customName"] = custom_name;
  doc["completedTurns"] = completedTurns;
  doc["hourlyTurns"] = hourlyTurns;
  doc["status"] = running ? "Çalışıyor" : "Durduruldu";
  if (WiFi.status() == WL_CONNECTED) {
    doc["ip"] = WiFi.localIP().toString();
    doc["ap"] = false;
  } else {
    doc["ip"] = WiFi.softAPIP().toString();
    doc["ap"] = true;
  }
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
  Serial.println("updateWebSocket: Sent update.");
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
        }

        body.light {
            --bg-color: #f3f4f6;
            --text-color: #1f2937;
            --primary-color: #2563eb;
            --secondary-color: #4b5563;
            --card-bg: #ffffff;
            --border-color: #d1d5db;
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
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            justify-content: space-between;
        }

        .status-item {
            flex: 1 1 calc(50% - 10px);
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

        body.light .form-group input[type="number"], body.light .form-group select, body.light .form-group input[type="text"] {
            background-color: #e5e7eb;
            color: #1f2937;
        }

        .form-group input[type="number"]:focus, .form-group select:focus, .form-group input[type="text"]:focus {
            outline: none;
            border-color: var(--primary-color);
            box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.5);
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
    </style>
</head>
<body>

<div class="container">
    <div class="card">
        <h1>Horus</h1>
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
            <div class="status-item">
                <span>IP Adresi</span>
                <h3 id="ipAddress"></h3>
            </div>
        </div>
    </div>

    <div class="card">
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

    <div class="card">
        <h2>WiFi Ayarları</h2>
        <div class="form-group">
            <label for="ssid">Ağ Adı (SSID)</label>
            <select id="ssidSelect"></select>
        </div>
        <div class="form-group">
            <label for="password">Şifre</label>
            <input type="text" id="passwordInput" placeholder="Şifrenizi buraya girin">
        </div>
        <div class="form-group">
            <label for="name">Cihaz Adı (Opsiyonel)</label>
            <input type="text" id="nameInput" placeholder="Cihaz Adı">
        </div>
        <div class="button-group">
            <button class="button primary" onclick="saveWiFiSettings()">Kaydet ve Yeniden Başlat</button>
            <button class="button secondary" onclick="scanNetworks()">Ağları Tara</button>
        </div>
    </div>

    <div class="card">
        <h2>Cihaz Güncelleme</h2>
        <div class="form-group">
            <div class="status-item">
                <span>Güncel Sürüm</span>
                <h3 id="currentVersion">Yükleniyor...</h3>
            </div>
            <div class="status-item">
                <span>Web arayüzü:</span>
                <h3 id="ipAddress"></h3>
            </div>
        </div>
        <div class="button-group">
            <button class="button primary" onclick="checkOTAUpdate()">Güncelleme Kontrol Et</button>
            <button class="button secondary" onclick="window.location.href='/manual_update'">Manuel Güncelleme</button>
        </div>
        <p id="message_box" style="display:none; text-align: center;"></p>
    </div>

    <div class="card">
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

            if (doc.otaStatus) {
                const msgBox = document.getElementById('message_box');
                msgBox.innerText = doc.otaStatus;
                msgBox.style.color = (doc.otaStatus.includes('Hata') || doc.otaStatus.includes('başarısız')) ? 'red' : 'green';
                msgBox.style.display = 'block';
                if (!doc.otaStatus.includes("indiriliyor")) {
                    setTimeout(() => { msgBox.style.display = 'none'; }, 5000);
                }
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
        const name = document.getElementById('nameInput').value;
        fetch('/save_wifi', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}&name=${encodeURIComponent(name)}`
        })
        .then(response => response.text())
        .then(data => {
            console.log(data);
        })
        .catch(error => console.error('Hata:', error));
    }

    function scanNetworks() {
        const scanButton = document.querySelector('.card:nth-of-type(3) .button.secondary');
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
