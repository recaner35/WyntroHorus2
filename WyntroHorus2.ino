#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h> // LittleFS kütüphanesi eklendi
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <Update.h>

// OTA Settings
const char* github_url = "https://api.github.com/repos/recaner35/WyntroHorus2/releases/latest";
const char* FIRMWARE_VERSION = "v1.0.57"; // Seri monitör çıktınıza göre sürümü güncelledim

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
  WiFi.mode(WIFI_AP_STA);
  setupMDNS();
  if (!WiFi.softAP(mDNS_hostname, default_password)) {
    Serial.println("setupWiFi: Failed to start AP!");
    while (true);
  }
  Serial.println("setupWiFi: AP started: " + String(mDNS_hostname) + ", IP: " + WiFi.softAPIP().toString());
  if (strlen(ssid) > 0 && strlen(password) >= 8) {
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nsetupWiFi: Connected to " + String(ssid) + ", IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("\nsetupWiFi: Connection failed, continuing in AP mode.");
    }
  } else {
    Serial.println("setupWiFi: Invalid WiFi credentials, running in AP mode only.");
  }
}

String sanitizeString(String input) {
  String result = input;
  // Türkçe karakterleri dönüştür
  result.replace("ç", "c");
  result.replace("Ç", "C");
  result.replace("ş", "s");
  result.replace("Ş", "S");
  result.replace("ı", "i");
  result.replace("İ", "I");
  result.replace("ğ", "g");
  result.replace("Ğ", "G");
  result.replace("ü", "u");
  result.replace("Ü", "U");
  result.replace("ö", "o");
  result.replace("Ö", "O");
  // Boşlukları tire ile değiştir
  result.replace(" ", "-");
  // Diğer tüm özel karakterleri tire ile değiştir
  String sanitized = "";
  for (int i = 0; i < result.length(); i++) {
    char c = result[i];
    if (isAlphaNumeric(c) || c == '-') {
      sanitized += c;
    } else {
      sanitized += "-";
    }
  }
  // Birden fazla tireyi tek tireye indirge
  while (sanitized.indexOf("--") != -1) {
    sanitized.replace("--", "-");
  }
  // Başta ve sonda tire varsa kaldır
  sanitized.trim();
  while (sanitized.startsWith("-")) sanitized.remove(0, 1);
  while (sanitized.endsWith("-")) sanitized.remove(sanitized.length() - 1, 1);
  return sanitized;
}

void setupMDNS() {
  String mac = WiFi.softAPmacAddress();
  mac.replace(":", "");
  String macLast4 = mac.substring(mac.length() - 4);
  String sanitized_name = sanitizeString(String(custom_name));
  if (sanitized_name.length() > 0 && sanitized_name.length() <= 20) {
    strncpy(mDNS_hostname, (sanitized_name + "-" + macLast4).c_str(), sizeof(mDNS_hostname) - 1);
    mDNS_hostname[sizeof(mDNS_hostname) - 1] = '\0';
  } else {
    strncpy(mDNS_hostname, ("horus-" + macLast4).c_str(), sizeof(mDNS_hostname) - 1);
    mDNS_hostname[sizeof(mDNS_hostname) - 1] = '\0';
  }
  if (MDNS.begin(mDNS_hostname)) {
    Serial.println("setupMDNS: Started: " + String(mDNS_hostname) + ".local");
  } else {
    Serial.println("setupMDNS: Failed to start!");
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
  IPAddress githubIP;
  if (!WiFi.hostByName("api.github.com", githubIP)) {
    statusDoc["otaStatus"] = "Hata: DNS çözümlemesi başarısız.";
    statusDoc["updateAvailable"] = false;
    serializeJson(statusDoc, json);
    webSocket.broadcastTXT(json);
    Serial.println("checkOTAUpdateTask: DNS resolution failed for api.github.com");
    vTaskDelete(NULL);
    return;
  }
  Serial.println("checkOTAUpdateTask: DNS resolved, IP: " + githubIP.toString());
  http.begin(github_url);
  Serial.println("checkOTAUpdateTask: Fetching latest release from " + String(github_url));
  int httpCode = http.GET();
  Serial.printf("checkOTAUpdateTask: HTTP response code: %d\n", httpCode);
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
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
          http.begin(binUrl);
          http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
          http.setRedirectLimit(5);
          http.addHeader("User-Agent", "ESP32-WyntroHorus2/1.0");
          Serial.println("checkOTAUpdateTask: Initiating HTTP GET for firmware...");
          int httpCodeBin = http.GET();
          Serial.printf("checkOTAUpdateTask: Firmware HTTP response code: %d\n", httpCodeBin);
          if (httpCodeBin == HTTP_CODE_OK) {
            size_t size = http.getSize();
            Serial.println("checkOTAUpdateTask: File size: " + String(size));
            if (size <= 0) {
              statusDoc["otaStatus"] = "Hata: Dosya boyutu bilinmiyor.";
              statusDoc["updateAvailable"] = false;
              serializeJson(statusDoc, json);
              webSocket.broadcastTXT(json);
              Serial.println("checkOTAUpdateTask: Unknown file size.");
              http.end();
              vTaskDelete(NULL);
              return;
            }
            if (Update.begin(size)) {
              Serial.println("checkOTAUpdateTask: Starting OTA update, size: " + String(size));
              WiFiClient *client = http.getStreamPtr();
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
          http.end();
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

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("webSocketEvent: Client [%u] connected\n", num);
    updateWebSocket();
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("webSocketEvent: Client [%u] disconnected\n", num);
  } else if (type == WStype_TEXT) {
    String msg = String((char *)payload);
    Serial.printf("webSocketEvent: Received message: %s\n", msg.c_str());
    if (msg == "status_request") {
      updateWebSocket();
    } else if (msg == "ota_check_request") {
      xTaskCreate(checkOTAUpdateTask, "CheckOTAUpdateTask", 8192, NULL, 1, NULL);
    }
  }
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
    <title>Horus v1.0.57</title>
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

        .form-group input[type="number"],
        .form-group select,
        .form-group input[type="text"] {
            width: 100%;
            padding: 10px;
            background-color: #374151;
            border: 1px solid var(--border-color);
            border-radius: 6px;
            color: var(--text-color);
            box-sizing: border-box;
        }

        .form-group input[type="number"]:focus,
        .form-group select:focus,
        .form-group input[type="text"]:focus {
            outline: none;
            border-color: var(--primary-color);
            box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.5);
        }

        .button-group {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin-top: 20px;
        }
        
        .button {
            flex: 1 1 calc(33.333% - 10px);
            padding: 12px;
            border: none;
            border-radius: 6px;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
            transition: background-color 0.3s ease, transform 0.1s ease;
            color: white;
            text-align: center;
        }
        
        .button:active {
            transform: scale(0.98);
        }
        
        .primary {
            background-color: var(--primary-color);
        }
        
        .primary:hover {
            background-color: #2563eb;
        }
        
        .secondary {
            background-color: #6b7280;
        }
        
        .secondary:hover {
            background-color: #4b5563;
        }

        .warning {
            background-color: #dc2626;
        }

        .warning:hover {
            background-color: #b91c1c;
        }

        .info-box {
            background-color: #374151;
            border: 1px solid #4b5563;
            border-radius: 8px;
            padding: 15px;
            margin-top: 15px;
            font-size: 14px;
            color: var(--secondary-color);
            line-height: 1.5;
        }

        .message-box {
            min-height: 20px;
            margin-top: 10px;
            text-align: center;
            font-weight: bold;
        }

        .wifi-list {
            list-style: none;
            padding: 0;
            max-height: 200px;
            overflow-y: auto;
            border: 1px solid var(--border-color);
            border-radius: 6px;
            margin-top: 10px;
            background-color: #374151;
        }

        .wifi-list li {
            padding: 10px;
            border-bottom: 1px solid var(--border-color);
            cursor: pointer;
            transition: background-color 0.2s ease;
        }

        .wifi-list li:last-child {
            border-bottom: none;
        }

        .wifi-list li:hover {
            background-color: #4b5563;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Wyntro Horus v1.0.57</h1>

        <div class="card">
            <h2>Durum</h2>
            <div class="status-section">
                <div class="status-item">
                    <span>Motor Durumu</span>
                    <h3 id="motor_status">Yükleniyor...</h3>
                </div>
                <div class="status-item">
                    <span>Tamamlanan Turlar</span>
                    <h3 id="completed_turns">0</h3>
                </div>
                <div class="status-item">
                    <span>Günlük Tur Sayısı</span>
                    <h3 id="turns_per_day">0</h3>
                </div>
                <div class="status-item">
                    <span>Tur Süresi</span>
                    <h3 id="turn_duration">0</h3>
                </div>
                <div class="status-item">
                    <span>Dönüş Yönü</span>
                    <h3 id="direction">0</h3>
                </div>
                <div class="status-item">
                    <span>Saatlik Tur Sayısı</span>
                    <h3 id="hourly_turns">0</h3>
                </div>
            </div>
            <div class="info-box">
                <p><strong>Cihaz Adı:</strong> <span id="custom_name"></span></p>
                <p><strong>IP Adresi:</strong> <span id="ip_address"></span></p>
                <p><strong>AP Modu:</strong> <span id="ap_mode"></span></p>
            </div>
        </div>

        <div class="card">
            <h2>Motor Ayarları</h2>
            <div class="form-group">
                <label for="turns_per_day_input">Günlük Tur Sayısı (600 - 1200)</label>
                <input type="number" id="turns_per_day_input" min="600" max="1200">
            </div>
            <div class="form-group">
                <label for="turn_duration_input">Tur Süresi (saniye, 10 - 15)</label>
                <input type="number" id="turn_duration_input" step="0.5" min="10" max="15">
            </div>
            <div class="form-group">
                <label for="direction_input">Dönüş Yönü</label>
                <select id="direction_input">
                    <option value="1">1 (İleri)</option>
                    <option value="2">2 (Geri)</option>
                    <option value="3">3 (İleri-Geri)</option>
                </select>
            </div>
            <div class="button-group">
                <button onclick="setMotorSettings('start')" class="button primary">Başlat</button>
                <button onclick="setMotorSettings('stop')" class="button secondary">Durdur</button>
                <button onclick="setMotorSettings('reset')" class="button secondary">Sıfırla</button>
                <button onclick="setMotorSettings()" class="button primary" style="flex: 1 1 100%;">Ayarları Kaydet</button>
            </div>
        </div>

        <div class="card">
            <h2>WiFi Ayarları</h2>
            <div class="form-group">
                <label for="custom_name_input">Cihaz Adı</label>
                <input type="text" id="custom_name_input" maxlength="20">
            </div>
            <div class="form-group">
                <label for="ssid_input">WiFi Ağı</label>
                <input type="text" id="ssid_input" placeholder="Ağ adını girin veya listeden seçin">
                <ul id="wifi-list" class="wifi-list"></ul>
            </div>
            <div class="form-group">
                <label for="password_input">Şifre</label>
                <input type="password" id="password_input" placeholder="Şifre">
            </div>
            <div class="button-group">
                <button onclick="scanNetworks()" class="button secondary">Ağları Tara</button>
                <button onclick="saveWiFiSettings()" class="button primary">Kaydet ve Yeniden Başlat</button>
            </div>
        </div>

        <div class="card">
            <h2>OTA Güncelleme</h2>
            <div class="message-box" id="ota_message_box"></div>
            <div class="button-group">
                <button onclick="checkOTAUpdate()" class="button primary">Güncelleme Kontrol Et</button>
                <a href="/manual_update" class="button secondary" style="text-decoration: none;">Manuel Güncelleme</a>
            </div>
        </div>

    </div>

    <script>
        let ws;
        const wsUrl = `ws://${window.location.hostname}:81/ws`;
        
        function connectWebSocket() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                return;
            }
            console.log("WebSocket'e bağlanılıyor...");
            ws = new WebSocket(wsUrl);
            ws.onopen = () => {
                console.log("WebSocket bağlantısı açıldı.");
                requestStatusUpdate();
            };
            ws.onmessage = (event) => {
                console.log("Mesaj alındı:", event.data);
                handleMessage(event.data);
            };
            ws.onclose = () => {
                console.log("WebSocket bağlantısı kapandı. Yeniden bağlanılıyor...");
                setTimeout(connectWebSocket, 5000);
            };
            ws.onerror = (error) => {
                console.error("WebSocket hatası:", error);
            };
        }

        function handleMessage(data) {
            let messageBox = document.getElementById('ota_message_box');
            try {
                const doc = JSON.parse(data);
                
                if (doc.otaStatus) {
                    messageBox.innerText = doc.otaStatus;
                    messageBox.style.color = doc.otaStatus.includes("Hata") ? 'red' : 'green';
                }
                
                if (doc.status) {
                    document.getElementById('motor_status').innerText = doc.status;
                    document.getElementById('completed_turns').innerText = doc.completedTurns;
                    document.getElementById('turns_per_day').innerText = doc.turnsPerDay;
                    document.getElementById('turn_duration').innerText = doc.turnDuration;
                    document.getElementById('direction').innerText = doc.direction;
                    document.getElementById('hourly_turns').innerText = doc.hourlyTurns;
                    document.getElementById('custom_name').innerText = doc.customName;
                    document.getElementById('ip_address').innerText = doc.ip;
                    document.getElementById('ap_mode').innerText = doc.ap ? 'Evet' : 'Hayır';
                    
                    document.getElementById('turns_per_day_input').value = doc.turnsPerDay;
                    document.getElementById('turn_duration_input').value = doc.turnDuration;
                    document.getElementById('direction_input').value = doc.direction;
                    document.getElementById('custom_name_input').value = doc.customName;
                }
            } catch (e) {
                console.error("JSON ayrıştırma hatası:", e);
                messageBox.innerText = "WebSocket veri hatası.";
                messageBox.style.color = 'red';
            }
        }
        
        function requestStatusUpdate() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send("status_request");
            }
        }

        function setMotorSettings(action = '') {
            let tpd = document.getElementById('turns_per_day_input').value;
            let duration = document.getElementById('turn_duration_input').value;
            let direction = document.getElementById('direction_input').value;
            let url = `/set?tpd=${tpd}&duration=${duration}&dir=${direction}`;
            if (action) {
                url += `&action=${action}`;
            }
            fetch(url)
            .then(response => response.text())
            .then(data => console.log(data))
            .catch(error => console.error('Hata:', error));
        }

        function saveWiFiSettings() {
            let ssid = document.getElementById('ssid_input').value;
            let password = document.getElementById('password_input').value;
            let name = document.getElementById('custom_name_input').value;
            let formData = new FormData();
            formData.append('ssid', ssid);
            formData.append('password', password);
            formData.append('name', name);
            fetch('/save_wifi', {
                method: 'POST',
                body: formData
            })
            .then(response => response.text())
            .then(data => console.log(data))
            .catch(error => console.error('Hata:', error));
        }

        function scanNetworks() {
            fetch('/scan')
            .then(response => response.text())
            .then(data => {
                document.getElementById('wifi-list').innerHTML = data;
                let listItems = document.querySelectorAll('#wifi-list li');
                listItems.forEach(item => {
                    item.addEventListener('click', () => {
                        document.getElementById('ssid_input').value = item.textContent;
                    });
                });
            })
            .catch(error => console.error('Ağları tararken hata:', error));
        }

        function checkOTAUpdate() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send("ota_check_request");
            }
        }
        
        window.onload = function() {
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
        :root {
            --bg-color: #111827;
            --text-color: #f9fafb;
            --primary-color: #3b82f6;
            --secondary-color: #d1d5db;
            --card-bg: #1f2937;
            --border-color: #4b5563;
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
        h1 {
            color: var(--primary-color);
            text-align: center;
        }
        .form-group {
            margin-bottom: 15px;
        }
        .form-group label {
            display: block;
            margin-bottom: 5px;
            color: var(--secondary-color);
        }
        .form-group input[type="file"] {
            width: 100%;
            padding: 10px;
            background-color: #374151;
            border: 1px solid var(--border-color);
            border-radius: 6px;
            color: var(--text-color);
            box-sizing: border-box;
        }
        .button {
            width: 100%;
            padding: 12px;
            border: none;
            border-radius: 6px;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
            transition: background-color 0.3s ease, transform 0.1s ease;
            color: white;
            text-align: center;
            background-color: var(--primary-color);
        }
        .button:hover {
            background-color: #2563eb;
        }
        .button:active {
            transform: scale(0.98);
        }
        .message-box {
            min-height: 20px;
            margin-top: 20px;
            text-align: center;
            font-weight: bold;
        }
        .back-link {
            display: block;
            margin-top: 20px;
            text-align: center;
            color: var(--primary-color);
            text-decoration: none;
            font-weight: bold;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Manuel Güncelleme</h1>
        <div class="card">
            <p>Yeni firmware (.bin) dosyasını seçin ve yükle butonuna basın.</p>
            <div class="form-group">
                <label for="file_input">Firmware Dosyası Seçin</label>
                <input type="file" id="file_input" accept=".bin">
            </div>
            <button class="button" onclick="uploadFirmware()">Yükle</button>
            <div class="message-box" id="message_box"></div>
        </div>
        <a href="/" class="back-link">Ana Sayfaya Dön</a>
    </div>

    <script>
        let ws;
        const wsUrl = `ws://${window.location.hostname}:81/ws`;
        
        function connectWebSocket() {
            if (ws && ws.readyState === WebSocket.OPEN) {
                return;
            }
            console.log("WebSocket'e bağlanılıyor...");
            ws = new WebSocket(wsUrl);
            ws.onopen = () => {
                console.log("WebSocket bağlantısı açıldı.");
            };
            ws.onmessage = (event) => {
                console.log("Mesaj alındı:", event.data);
                handleMessage(event.data);
            };
            ws.onclose = () => {
                console.log("WebSocket bağlantısı kapandı. Yeniden bağlanılıyor...");
                setTimeout(connectWebSocket, 5000);
            };
            ws.onerror = (error) => {
                console.error("WebSocket hatası:", error);
            };
        }

        function handleMessage(data) {
            let messageBox = document.getElementById('message_box');
            try {
                const doc = JSON.parse(data);
                if (doc.otaStatus) {
                    messageBox.innerText = doc.otaStatus;
                    messageBox.style.color = doc.otaStatus.includes("Hata") ? 'red' : 'green';
                }
            } catch (e) {
                console.error("JSON ayrıştırma hatası:", e);
                messageBox.innerText = "WebSocket veri hatası.";
                messageBox.style.color = 'red';
            }
        }

        function uploadFirmware() {
            let fileInput = document.getElementById('file_input');
            if (fileInput.files.length === 0) {
                document.getElementById('message_box').innerText = 'Lütfen bir dosya seçin.';
                document.getElementById('message_box').style.color = 'red';
                setTimeout(() => { document.getElementById('message_box').innerText = ''; }, 5000);
                return;
            }
            let formData = new FormData();
            formData.append('firmware', fileInput.files[0]);
            fetch('/manual_update', {
                method: 'POST',
                body: formData
            })
            .then(response => response.text())
            .then(data => {
                console.log(data);
                document.getElementById('message_box').innerText = 'Güncelleme gönderildi.';
                document.getElementById('message_box').style.color = 'green';
                setTimeout(() => { document.getElementById('message_box').innerText = ''; }, 5000);
            })
            .catch(error => {
                console.error('Hata:', error);
                document.getElementById('message_box').innerText = 'Güncelleme gönderilirken hata oluştu.';
                document.getElementById('message_box').style.color = 'red';
                setTimeout(() => { document.getElementById('message_box').innerText = ''; }, 5000);
            });
        }

        window.onload = function() {
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
