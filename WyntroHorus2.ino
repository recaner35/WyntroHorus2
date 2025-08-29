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
const char* FIRMWARE_VERSION = "v1.0.50";

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
  EEPROM.begin(512);
  if (!LittleFS.begin()) { // LittleFS başlatılıyor
    Serial.println("setup: Failed to mount LittleFS!");
    return;
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

  // Motor task'ını oluştur
  BaseType_t taskCreated = xTaskCreate(
      runMotorTask,
      "MotorTask",
      4096,
      NULL,
      1,
      &motorTaskHandle);
  if (taskCreated != pdPASS) {
    Serial.println("setup: Failed to create MotorTask!");
  } else {
    Serial.println("setup: MotorTask created successfully.");
    vTaskSuspend(motorTaskHandle); // Başlangıçta askıya al
  }
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
  setupMDNS(); // Ensure mDNS_hostname is set before starting AP
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
  String mac = WiFi.macAddress();
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
                  serializeJson(statusDoc, json);
                  webSocket.broadcastTXT(json);
                  Serial.println("checkOTAUpdateTask: Update completed, restarting...");
                  delay(1000);
                  ESP.restart();
                } else {
                  statusDoc["otaStatus"] = "Hata: Güncelleme tamamlanamadı.";
                  Serial.println("checkOTAUpdateTask: Update failed to finalize.");
                }
              } else {
                statusDoc["otaStatus"] = "Hata: Dosya yazma başarısız.";
                Serial.println("checkOTAUpdateTask: Failed to write firmware, written: " + String(written));
              }
            } else {
              statusDoc["otaStatus"] = "Hata: Güncelleme başlatılamadı, yetersiz alan.";
              Serial.println("checkOTAUpdateTask: Update.begin failed, size: " + String(size));
            }
          } else {
            statusDoc["otaStatus"] = "Hata: Dosya indirilemedi, HTTP " + String(httpCodeBin);
            serializeJson(statusDoc, json);
            webSocket.broadcastTXT(json);
            Serial.println("checkOTAUpdateTask: Failed to download firmware, HTTP code: " + String(httpCodeBin));
          }
          http.end();
        } else {
          statusDoc["otaStatus"] = "Hata: .bin dosyası bulunamadı.";
          Serial.println("checkOTAUpdateTask: No .bin file found in release.");
        }
      } else {
        statusDoc["otaStatus"] = "Firmware güncel: " + currentVersion;
        Serial.println("checkOTAUpdateTask: Firmware is up to date: " + currentVersion);
      }
    } else {
      statusDoc["otaStatus"] = "Hata: JSON ayrıştırma hatası: " + String(error.c_str());
      Serial.println("checkOTAUpdateTask: JSON parse error: " + String(error.c_str()));
    }
  } else {
    statusDoc["otaStatus"] = "Hata: OTA kontrol başarısız, HTTP " + String(httpCode);
    Serial.println("checkOTAUpdateTask: HTTP error: " + String(httpCode));
  }
  http.end();
  serializeJson(statusDoc, json);
  webSocket.broadcastTXT(json);
  Serial.println("checkOTAUpdateTask: Completed.");
  vTaskDelete(NULL);
}

void updateWebSocket() {
  StaticJsonDocument<256> doc;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["status"] = running ? "Çalışıyor" : "Durduruldu";
  doc["completedTurns"] = completedTurns;
  doc["hourlyTurns"] = hourlyTurns;
  doc["turnsPerDay"] = turnsPerDay;
  doc["turnDuration"] = turnDuration;
  doc["direction"] = direction;
  doc["customName"] = custom_name;
  doc["currentSSID"] = WiFi.SSID() != "" ? WiFi.SSID() : String(mDNS_hostname);
  doc["connectionStatus"] = WiFi.status() == WL_CONNECTED ? "Bağlandı" : "Hotspot modunda";
  doc["mDNS"] = String(mDNS_hostname) + ".local";
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
  Serial.println("updateWebSocket: Sent update.");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("webSocketEvent: Client [%u] disconnected\n", num);
      break;
    case WStype_CONNECTED:
      Serial.printf("webSocketEvent: Client [%u] connected\n", num);
      updateWebSocket();
      break;
    case WStype_TEXT:
      break;
  }
}

String htmlPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="tr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Horus by Wyntro</title>
    <link rel="manifest" href="/manifest.json">
    <meta name="theme-color" content="#3b82f6">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="default">
    <meta name="apple-mobile-web-app-title" content="Horus">
    <link rel="apple-touch-icon" href="/icon-192x192.png">
    <link rel="icon" href="/icon-192x192.png">
    <script src="https://cdn.tailwindcss.com"></script>
    <script>
        tailwind.config = {
            darkMode: 'class',
            theme: {
                extend: {
                    colors: {
                        gray: {
                            100: '#f3f4f6', 600: '#4b5563', 700: '#374151',
                            800: '#1f2937', 900: '#111827'
                        },
                        blue: { 500: '#3b82f6', 600: '#2563eb' },
                        red: { 500: '#ef4444', 600: '#dc2626' },
                        green: { 500: '#22c55e', 600: '#16a34a' },
                        purple: { 500: '#8b5cf6', 600: '#7c3aed' },
                        yellow: { 500: '#eab308', 600: '#ca8a04' }
                    }
                }
            },
            corePlugins: {
                preflight: true, container: false, accessibility: false
            },
            safelist: [
                'bg-gray-100', 'bg-gray-800', 'bg-gray-900', 'text-gray-100', 'text-gray-900',
                'bg-blue-500', 'hover:bg-blue-600', 'bg-red-500', 'hover:bg-red-600',
                'bg-green-500', 'hover:bg-green-600', 'bg-purple-500', 'hover:bg-purple-600',
                'bg-yellow-500', 'hover:bg-yellow-600', 'bg-gray-500', 'hover:bg-gray-600',
                'dark:bg-gray-900', 'dark:bg-gray-800', 'dark:bg-gray-700', 
                'dark:border-gray-600', 'dark:text-gray-100'
            ]
        };
        console.warn = function() {};
    </script>
    <style>
        @keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
        .animate-spin-slow { animation: spin 2s linear infinite; }
        .hidden { display: none; }
        .tab-content.active { display: block; }
        .tab-content { display: none; }
        .theme-toggle { display: flex; align-items: center; gap: 8px; }
        .theme-toggle input { display: none; }
        .theme-toggle label { 
            display: inline-flex; align-items: center; justify-content: center;
            width: 40px; height: 40px; cursor: pointer; 
            border-radius: 50%; transition: background-color 0.3s, box-shadow 0.3s;
            font-size: 20px;
        }
        #theme-system + label { background-color: #6b7280; }
        #theme-dark + label { background-color: #1f2937; }
        #theme-light + label { background-color: #f59e0b; }
        #theme-system:checked + label { background-color: #4b5563; box-shadow: 0 0 8px rgba(0,0,0,0.5); }
        #theme-dark:checked + label { background-color: #111827; box-shadow: 0 0 8px rgba(0,0,0,0.5); }
        #theme-light:checked + label { background-color: #d97706; box-shadow: 0 0 8px rgba(0,0,0,0.5); }
        body.dark { 
            background-color: #111827;
            color: #f3f4f6;
        }
        .dark .bg-white { 
            background-color: #1f2937;
        }
    </style>
</head>
<body class="bg-gray-100 dark:bg-gray-900 text-gray-900 dark:text-gray-100 min-h-screen flex flex-col items-center justify-center p-4">
    <div class="bg-white dark:bg-gray-800 p-6 rounded-lg shadow-lg w-full max-w-lg">
        <div class="flex justify-between items-center mb-4">
            <h1 class="text-2xl font-bold text-center">Horus by Wyntro</h1>
            <div class="theme-toggle">
                <input type="radio" id="theme-system" name="theme" value="system">
                <label for="theme-system">🌓</label>
                <input type="radio" id="theme-dark" name="theme" value="dark">
                <label for="theme-dark">🌙</label>
                <input type="radio" id="theme-light" name="theme" value="light">
                <label for="theme-light">☀️</label>
            </div>
        </div>

        <div class="flex justify-center mb-4 space-x-2 flex-wrap">
            <button onclick="openTab('motor')" class="tab-button bg-blue-500 hover:bg-blue-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="settings">Ayarlar</button>
            <button onclick="openTab('wifi')" class="tab-button bg-gray-500 hover:bg-gray-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="wifi">WiFi</button>
            <button onclick="openTab('devices')" class="tab-button bg-gray-500 hover:bg-gray-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="devices">Cihazlar</button>
            <button onclick="openTab('about')" class="tab-button bg-gray-500 hover:bg-gray-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="about">Hakkında</button>
        </div>

        <div id="motor" class="tab-content active space-y-4">
            <p class="text-center" data-translate="status">Durum: <span id="status">Durduruldu</span> <span id="motor_spinner" class="hidden animate-spin-slow">🔄</span></p>
            <p class="text-center" data-translate="completed_turns">Tamamlanan Turlar: <span id="completedTurns">0</span></p>
            <p class="text-center" data-translate="hourly_turns">Saatlik Turlar: <span id="hourlyTurns">0</span></p>
            <p id="motor_status" class="text-center"></p>
            <div>
                <label class="block text-sm font-medium" data-translate="turns_per_day">Günlük Tur Sayısı: <span id="tpd_val">600</span></label>
                <input type="range" id="tpd" min="600" max="1200" value="600" oninput="tpd_val.innerText=this.value" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">
            </div>
            <div>
                <label class="block text-sm font-medium" data-translate="turn_duration">Tur Süresi (s): <span id="duration_val">15.0</span></label>
                <input type="range" id="duration" min="10" max="15" step="0.1" value="15.0" oninput="duration_val.innerText=this.value" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">
            </div>
            <div>
                <label class="block text-sm font-medium" data-translate="direction">Dönüş Yönü</label>
                <div class="flex justify-between space-x-2">
                    <label class="flex items-center">
                        <input type="radio" name="dir" value="1" checked class="mr-2">
                        <span data-translate="clockwise">Saat Yönü</span>
                    </label>
                    <label class="flex items-center">
                        <input type="radio" name="dir" value="2" class="mr-2">
                        <span data-translate="counter_clockwise">Saat Yönü Ters</span>
                    </label>
                    <label class="flex items-center">
                        <input type="radio" name="dir" value="3" class="mr-2">
                        <span data-translate="both">Çift Yönlü</span>
                    </label>
                </div>
            </div>
            <div class="flex justify-center space-x-2">
                <button onclick="sendCommand('start')" class="bg-blue-500 hover:bg-blue-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="start">Başlat</button>
                <button onclick="sendCommand('stop')" class="bg-red-500 hover:bg-red-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="stop">Durdur</button>
                <button onclick="sendCommand('reset')" class="bg-gray-500 hover:bg-gray-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="reset_settings">Ayarları Sıfırla</button>
            </div>
        </div>

        <div id="wifi" class="tab-content space-y-4">
            <p class="text-center" id="wifi_info" data-translate="connected">Bağlı: <span id="currentSSID">-</span></p>
            <p class="text-center" id="conn_status" data-translate="connection_status">Durum: <span id="connectionStatus">-</span></p>
            <div>
                <label class="block text-sm font-medium" data-translate="network_name">Ağ Adı</label>
                <select id="ssid" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600 text-gray-900 dark:text-gray-100"></select>
            </div>
            <div>
                <label class="block text-sm font-medium" data-translate="password">Şifre</label>
                <input type="password" id="wifi_password" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600 text-gray-900 dark:text-gray-100">
            </div>
            <div class="flex justify-center space-x-2">
                <button id="scanButton" onclick="scanWiFi()" class="bg-green-500 hover:bg-green-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="scan_networks">Ağları Tara</button>
                <button onclick="saveWiFi()" class="bg-purple-500 hover:bg-purple-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="save_restart">Kaydet & Yeniden Başlat</button>
            </div>
        </div>

        <div id="devices" class="tab-content space-y-4">
            <div>
                <label class="block text-sm font-medium" data-translate="add_device">Cihaz Ekle (örn: horus-1234.local)</label>
                <input type="text" id="deviceDomain" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600 text-gray-900 dark:text-gray-100" placeholder="horus-1234.local">
            </div>
            <div class="flex justify-center">
                <button onclick="addDevice()" class="bg-green-500 hover:bg-green-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="add">Ekle</button>
            </div>
            <div id="deviceList" class="space-y-2"></div>
        </div>

        <div id="about" class="tab-content space-y-4">
            <p class="text-center" data-translate="firmware_version">Firmware Sürümü: <span id="version">-</span></p>
            <p class="text-center" id="ota_status"></p>
            <p class="text-center" data-translate="device_name">Cihaz Adı: <span id="deviceName">-</span></p>
            <p class="text-center" data-translate="mdns_domain">mDNS Domain: <span id="mDNS">-</span></p>
            <div>
                <label class="block text-sm font-medium" data-translate="device_name">Cihaz Adı</label>
                <input type="text" id="customName" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600 text-gray-900 dark:text-gray-100">
            </div>
            <div class="flex justify-center space-x-2">
                <button onclick="saveDeviceName()" class="bg-purple-500 hover:bg-purple-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="save">Kaydet</button>
                <button onclick="resetDeviceName()" class="bg-gray-500 hover:bg-gray-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="reset_device_name">Cihaz Adını Sıfırla</button>
            </div>
            <div class="flex flex-col items-center space-y-2">
                <button id="checkUpdateButton" onclick="checkUpdate()" class="bg-yellow-500 hover:bg-yellow-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200" data-translate="check_updates">Güncellemeleri Kontrol Et</button>
            </div>
        </div>

        <p id="motor_status" class="text-center mt-4 text-sm font-semibold"></p>
        <p id="message_box" class="text-center mt-4 text-sm font-semibold"></p>
    </div>

    <script>
        function waitForTailwind(callback) {
            if (typeof tailwind !== 'undefined') {
                console.log('waitForTailwind: Tailwind loaded');
                callback();
            } else {
                console.log('waitForTailwind: Waiting for Tailwind...');
                setTimeout(() => waitForTailwind(callback), 100);
            }
        }

        let ws = new WebSocket('ws://' + window.location.hostname + ':81/');
        let devices = JSON.parse(localStorage.getItem('horusDevices')) || [];

        function applyTheme(theme) {
            console.log('applyTheme: Applying theme:', theme);
            const body = document.body;
            localStorage.setItem('theme', theme);
            const themeInput = document.querySelector(`input[name="theme"][value="${theme}"]`);
            if (themeInput) {
                themeInput.checked = true;
            } else {
                console.error('applyTheme: Theme input not found for', theme);
            }
            if (theme === 'system') {
                console.log('applyTheme: System theme, checking prefers-color-scheme');
                if (window.matchMedia('(prefers-color-scheme: dark)').matches) {
                    body.classList.add('dark');
                    console.log('applyTheme: System prefers dark');
                } else {
                    body.classList.remove('dark');
                    console.log('applyTheme: System prefers light');
                }
            } else if (theme === 'dark') {
                body.classList.add('dark');
                console.log('applyTheme: Dark theme applied');
            } else {
                body.classList.remove('dark');
                console.log('applyTheme: Light theme applied');
            }
        }

        function initTheme() {
            console.log('initTheme: Starting theme initialization');
            const savedTheme = localStorage.getItem('theme') || 'system';
            console.log('initTheme: Initializing with theme:', savedTheme);
            applyTheme(savedTheme);
            const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
            mediaQuery.addEventListener('change', (e) => {
                console.log('initTheme: Media query changed, prefers dark:', e.matches);
                if (localStorage.getItem('theme') === 'system') {
                    applyTheme('system');
                }
            });
        }

        document.querySelectorAll('input[name="theme"]').forEach(input => {
            input.addEventListener('change', (e) => {
                console.log('Theme input changed to:', e.target.value);
                applyTheme(e.target.value);
            });
        });

        ws.onmessage = function(event) {
            console.log('WebSocket message received: ' + event.data);
            try {
                let data = JSON.parse(event.data);
                const statusElement = document.getElementById('status');
                const motorSpinnerElement = document.getElementById('motor_spinner');
                const completedTurnsElement = document.getElementById('completedTurns');
                const hourlyTurnsElement = document.getElementById('hourlyTurns');
                const tpdElement = document.getElementById('tpd');
                const tpdValElement = document.getElementById('tpd_val');
                const durationElement = document.getElementById('duration');
                const durationValElement = document.getElementById('duration_val');
                const versionElement = document.getElementById('version');
                const otaStatusElement = document.getElementById('ota_status');
                const deviceNameElement = document.getElementById('deviceName');
                const mDNSElement = document.getElementById('mDNS');
                const currentSSIDElement = document.getElementById('currentSSID');
                const connectionStatusElement = document.getElementById('connectionStatus');
                const motorStatusElement = document.getElementById('motor_status');
                const customNameElement = document.getElementById('customName');

                if (data.status) {
                    statusElement.innerText = data.status;
                    motorSpinnerElement.classList.toggle('hidden', data.status !== 'Çalışıyor');
                }
                if (data.completedTurns != null) completedTurnsElement.innerText = data.completedTurns;
                if (data.hourlyTurns != null) hourlyTurnsElement.innerText = data.hourlyTurns;
                if (data.turnsPerDay != null) {
                    tpdElement.value = data.turnsPerDay;
                    tpdValElement.innerText = data.turnsPerDay;
                }
                if (data.turnDuration != null) {
                    durationElement.value = data.turnDuration;
                    durationValElement.innerText = data.turnDuration;
                }
                if (data.direction != null) {
                    document.querySelector(`input[name="dir"][value="${data.direction}"]`).checked = true;
                }
                if (data.firmwareVersion) versionElement.innerText = data.firmwareVersion;
                if (data.otaStatus) {
                    otaStatusElement.innerText = data.otaStatus;
                    otaStatusElement.style.color = data.otaStatus.includes("güncel") ? 'green' :
                                                  data.otaStatus.includes("Yeni sürüm") ? 'orange' :
                                                  data.otaStatus.includes("başarılı") ? 'green' : 'red';
                    if (data.otaStatus.includes("başarılı") || data.otaStatus.includes("güncel") || data.otaStatus.includes("Hata")) {
                        document.getElementById('checkUpdateButton').innerText = 'Güncellemeleri Kontrol Et';
                        document.getElementById('checkUpdateButton').disabled = false;
                    }
                }
                if (data.customName != null) {
                    deviceNameElement.innerText = data.customName;
                    customNameElement.value = data.customName;
                }
                if (data.mDNS) mDNSElement.innerText = data.mDNS;
                if (data.currentSSID != null) currentSSIDElement.innerText = data.currentSSID;
                if (data.connectionStatus != null) connectionStatusElement.innerText = data.connectionStatus;
            } catch (e) {
                console.error("JSON parse error:", e);
                console.log("Received data:", event.data);
                showMessage('WebSocket veri hatası.', 'error');
            }
        };

        function openTab(tabName) {
            const tabs = document.querySelectorAll('.tab-content');
            tabs.forEach(tab => tab.classList.remove('active'));
            document.getElementById(tabName).classList.add('active');
            const buttons = document.querySelectorAll('.tab-button');
            buttons.forEach(btn => btn.classList.remove('bg-blue-500', 'hover:bg-blue-600'));
            buttons.forEach(btn => btn.classList.add('bg-gray-500', 'hover:bg-gray-600'));
            const activeTabButton = document.querySelector(`[onclick="openTab('${tabName}')"]`);
            if (activeTabButton) {
                activeTabButton.classList.remove('bg-gray-500', 'hover:bg-gray-600');
                activeTabButton.classList.add('bg-blue-500', 'hover:bg-blue-600');
            }
            if (tabName === 'devices') updateDeviceList();
        }

        function showMessage(msg, type = 'info') {
            const messageBox = document.getElementById('message_box');
            messageBox.innerText = msg;
            messageBox.style.color = type === 'error' ? 'red' : 'green';
            setTimeout(() => { messageBox.innerText = ''; }, 5000);
        }

        function sendCommand(action) {
            let tpd = document.getElementById('tpd').value;
            let duration = document.getElementById('duration').value;
            let dir = document.querySelector('input[name="dir"]:checked').value;
            let url = `/set?tpd=${tpd}&duration=${duration}&dir=${dir}&action=${action}`;
            console.log(`sendCommand: Sending ${action} to ${url}`);
            fetch(url)
                .then(response => {
                    console.log(`sendCommand: Response status: ${response.status}`);
                    return response.text();
                })
                .then(data => {
                    console.log(`sendCommand: Response: ${data}`);
                })
                .catch(error => {
                    console.error('sendCommand: Error:', error);
                    showMessage('Komut gönderilirken hata oluştu.', 'error');
                });
        }

        function scanWiFi() {
            const scanButton = document.getElementById('scanButton');
            scanButton.innerText = 'Taranıyor...';
            scanButton.disabled = true;
            fetch('/scan')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('ssid').innerHTML = data;
                    console.log('WiFi seçenekleri yüklendi.');
                    scanButton.innerText = 'Yeniden Tara';
                    scanButton.disabled = false;
                })
                .catch(error => {
                    console.error('Hata:', error);
                    scanButton.innerText = 'Yeniden Tara';
                    scanButton.disabled = false;
                    showMessage('WiFi tarama hatası.', 'error');
                });
        }

        function saveWiFi() {
            let ssid = document.getElementById('ssid').value;
            let password = document.getElementById('wifi_password').value;
            fetch('/save_wifi', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`
            })
            .then(response => response.text())
            .then(data => {
                console.log(data);
                showMessage('WiFi ayarları kaydedildi! Cihaz yeniden başlatılıyor.', 'info');
            })
            .catch(error => {
                console.error('Hata:', error);
                showMessage('WiFi ayarları kaydedilirken hata oluştu.', 'error');
            });
        }

        function saveDeviceName() {
            let name = document.getElementById('customName').value;
            fetch('/save_wifi', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `name=${encodeURIComponent(name)}`
            })
            .then(response => response.text())
            .then(data => {
                console.log(data);
                showMessage('Cihaz adı kaydedildi! Cihaz yeniden başlatılıyor.', 'info');
            })
            .catch(error => {
                console.error('Hata:', error);
                showMessage('Cihaz adı kaydedilirken hata oluştu.', 'error');
            });
        }

        function resetDeviceName() {
            fetch('/save_wifi', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `name=`
            })
            .then(response => response.text())
            .then(data => {
                console.log(data);
                showMessage('Cihaz adı sıfırlandı! Cihaz yeniden başlatılıyor.', 'info');
            })
            .catch(error => {
                console.error('Hata:', error);
                showMessage('Cihaz adı sıfırlanırken hata oluştu.', 'error');
            });
        }

        function addDevice() {
            let domain = document.getElementById('deviceDomain').value.trim();
            if (!domain.match(/^[a-zA-Z0-9-]+\.[a-z]{2,}$/)) {
                showMessage('Geçersiz domain formatı! Örn: horus-1234.local', 'error');
                return;
            }
            if (!devices.includes(domain)) {
                devices.push(domain);
                localStorage.setItem('horusDevices', JSON.stringify(devices));
                showMessage(`Cihaz eklendi: ${domain}`);
                updateDeviceList();
            } else {
                showMessage('Bu cihaz zaten ekli!', 'error');
            }
            document.getElementById('deviceDomain').value = '';
        }

        function updateDeviceList() {
            const deviceList = document.getElementById('deviceList');
            deviceList.innerHTML = '';
            devices.forEach((domain, index) => {
                fetch(`http://${domain}/status`)
                    .then(response => response.json())
                    .then(data => {
                        const deviceDiv = document.createElement('div');
                        deviceDiv.className = 'border p-2 rounded dark:border-gray-600';
                        deviceDiv.innerHTML = `
                            <p><strong>${domain}</strong>: ${data.status} (Turlar: ${data.completedTurns}, Günlük: ${data.turnsPerDay}, Süre: ${data.turnDuration}s, Yön: ${data.direction == 1 ? 'Saat Yönü' : data.direction == 2 ? 'Saat Yönü Ters' : 'Çift Yönlü'})</p>
                            <div class="flex justify-between space-x-2 mt-2">
                                <button onclick="controlDevice('${domain}', 'start')" class="bg-blue-500 hover:bg-blue-600 text-white font-bold py-1 px-2 rounded text-sm" data-translate="start">Başlat</button>
                                <button onclick="controlDevice('${domain}', 'stop')" class="bg-red-500 hover:bg-red-600 text-white font-bold py-1 px-2 rounded text-sm" data-translate="stop">Durdur</button>
                                <button onclick="controlDevice('${domain}', 'reset')" class="bg-gray-500 hover:bg-gray-600 text-white font-bold py-1 px-2 rounded text-sm" data-translate="reset_settings">Sıfırla</button>
                                <button onclick="removeDevice(${index})" class="bg-red-600 hover:bg-red-700 text-white font-bold py-1 px-2 rounded text-sm" data-translate="remove">Kaldır</button>
                            </div>
                        `;
                        deviceList.appendChild(deviceDiv);
                    })
                    .catch(error => {
                        console.error(`Hata (${domain}):`, error);
                        const deviceDiv = document.createElement('div');
                        deviceDiv.className = 'border p-2 rounded dark:border-gray-600';
                        deviceDiv.innerHTML = `
                            <p><strong>${domain}</strong>: Bağlantı hatası</p>
                            <div class="flex justify-end mt-2">
                                <button onclick="removeDevice(${index})" class="bg-red-600 hover:bg-red-700 text-white font-bold py-1 px-2 rounded text-sm" data-translate="remove">Kaldır</button>
                            </div>
                        `;
                        deviceList.appendChild(deviceDiv);
                    });
            });
        }

        function controlDevice(domain, action) {
            let tpd = document.getElementById('tpd').value;
            let duration = document.getElementById('duration').value;
            let dir = document.querySelector('input[name="dir"]:checked').value;
            fetch(`http://${domain}/set?tpd=${tpd}&duration=${duration}&dir=${dir}&action=${action}`)
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                    updateDeviceList();
                })
                .catch(error => {
                    console.error('Hata:', error);
                    showMessage(`Komut gönderilirken hata oluştu (${domain}).`, 'error');
                });
        }

        function removeDevice(index) {
            devices.splice(index, 1);
            localStorage.setItem('horusDevices', JSON.stringify(devices));
            showMessage('Cihaz kaldırıldı.');
            updateDeviceList();
        }

        function checkUpdate() {
            const updateButton = document.getElementById('checkUpdateButton');
            updateButton.innerText = 'Kontrol Ediliyor...';
            updateButton.disabled = true;
            fetch('/check_update')
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                })
                .catch(error => {
                    console.error('Hata:', error);
                    updateButton.innerText = 'Güncellemeleri Kontrol Et';
                    updateButton.disabled = false;
                    showMessage('Güncelleme kontrolü başlatılamadı.', 'error');
                });
        }

        window.onload = function() {
            console.log('window.onload: Initializing');
            waitForTailwind(() => {
                console.log('window.onload: Tailwind ready, initializing tabs and theme');
                openTab('motor');
                updateDeviceList();
                initTheme();
                if ('serviceWorker' in navigator) {
                    navigator.serviceWorker.register('/sw.js').then((reg) => {
                        console.log('Service Worker registered:', reg);
                    }).catch((error) => {
                        console.error('Service Worker registration failed:', error);
                    });
                }
            });
        }
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
    <title>Horus - Manuel Güncelleme</title>
    <link rel="manifest" href="/manifest.json">
    <meta name="theme-color" content="#3b82f6">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="default">
    <meta name="apple-mobile-web-app-title" content="Horus">
    <link rel="apple-touch-icon" href="/icon-192x192.png">
    <link rel="icon" href="/icon-192x192.png">
    <script src="https://cdn.tailwindcss.com"></script>
    <script>
        tailwind.config = {
            theme: {
                extend: {
                    colors: {
                        gray: { 100: '#f3f4f6', 700: '#374151', 800: '#1f2937', 900: '#111827' },
                        blue: { 500: '#3b82f6', 600: '#2563eb' },
                        red: { 500: '#ef4444', 600: '#dc2626' }
                    }
                }
            },
            corePlugins: { preflight: true, container: false, accessibility: false },
            safelist: ['bg-gray-100', 'bg-gray-800', 'bg-gray-900', 'text-gray-100', 'text-gray-900',
                       'bg-blue-500', 'hover:bg-blue-600', 'bg-red-500', 'hover:bg-red-600',
                       'dark:bg-gray-700', 'dark:border-gray-600', 'dark:text-gray-100']
        };
        console.warn = function() {};
    </script>
    <style>
        @keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
        .animate-spin-slow { animation: spin 2s linear infinite; }
        .hidden { display: none; }
        body.dark { background-color: #111827; color: #f3f4f6; }
        .dark .bg-white { background-color: #1f2937; }
    </style>
</head>
<body class="bg-gray-100 dark:bg-gray-900 text-gray-900 dark:text-gray-100 min-h-screen flex flex-col items-center justify-center p-4">
    <div class="bg-white dark:bg-gray-800 p-6 rounded-lg shadow-lg w-full max-w-lg">
        <h1 class="text-2xl font-bold text-center mb-4">Horus - Manuel Güncelleme</h1>
        <div class="mb-4">
            <label class="block text-sm font-medium mb-2" for="firmware">Firmware Dosyası (.bin)</label>
            <input type="file" id="firmware" name="firmware" accept=".bin" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600 text-gray-900 dark:text-gray-100">
        </div>
        <div class="flex justify-center">
            <button onclick="uploadFirmware()" class="bg-blue-500 hover:bg-blue-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">Yükle</button>
        </div>
        <p id="ota_status" class="text-center mt-4 text-sm font-semibold"></p>
        <p id="message_box" class="text-center mt-4 text-sm font-semibold"></p>
    </div>

    <script>
        let ws = new WebSocket('ws://' + window.location.hostname + ':81/');
        ws.onmessage = function(event) {
            console.log('WebSocket message received: ' + event.data);
            try {
                let data = JSON.parse(event.data);
                if (data.otaStatus) {
                    document.getElementById('ota_status').innerText = data.otaStatus;
                    document.getElementById('ota_status').style.color = data.otaStatus.includes("başarılı") ? 'green' : 'red';
                }
            } catch (e) {
                console.error("JSON parse error:", e);
                document.getElementById('message_box').innerText = 'WebSocket veri hatası.';
                document.getElementById('message_box').style.color = 'red';
                setTimeout(() => { document.getElementById('message_box').innerText = ''; }, 5000);
            }
        };

        function uploadFirmware() {
            let fileInput = document.getElementById('firmware');
            if (!fileInput.files.length) {
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
            if ('serviceWorker' in navigator) {
                navigator.serviceWorker.register('/sw.js').then((reg) => {
                    console.log('Service Worker registered:', reg);
                }).catch((error) => {
                    console.error('Service Worker registration failed:', error);
                });
            }
        }
    </script>
</body>
</html>
)rawliteral";
  return page;
}
