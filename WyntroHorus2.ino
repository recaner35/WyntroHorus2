#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <WebSocketsServer.h>
#include <NetworkClientSecure.h>
#include <EEPROM.h>
#include <ElegantOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

// OTA AyarlarÄ±
const char* github_url = "https://api.github.com/repos/recaner35/WyntroHorus2/releases/latest";
const char* github_token = getenv("GITHUB_TOKEN"); // BURAYA KENDÄ° TOKENâ€™INI EKLE
const char* FIRMWARE_VERSION = "v1.0.17";

// WiFi AyarlarÄ±
const char* default_ssid = "HorusAP";
const char* default_password = "12345678";
char ssid[32] = "";
char password[64] = "";
char custom_name[21] = "";
char mDNS_hostname[32] = "";

// Motor AyarlarÄ±
int turnsPerDay = 600;
float turnDuration = 10.0;
int direction = 1;
bool running = false;
int completedTurns = 0;
unsigned long lastHourTime = 0;
int hourlyTurns = turnsPerDay / 24;

// Pin TanÄ±mlarÄ±
const int motorPin1 = 26;
const int motorPin2 = 27;
const int motorPin3 = 14;
const int motorPin4 = 12;
const int stepsPerRevolution = 2048;

// Global Nesneler
WebServer server(80);
WebSocketsServer webSocket(81);

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(motorPin1, OUTPUT);
  pinMode(motorPin2, OUTPUT);
  pinMode(motorPin3, OUTPUT);
  pinMode(motorPin4, OUTPUT);
  readSettings();
  setupWiFi();
  setupMDNS();
  setupWebServer();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  ElegantOTA.begin(&server);
  ElegantOTA.onStart([]() { running = false; stopMotor(); });
}

void loop() {
  webSocket.loop();
  server.handleClient();
  ElegantOTA.loop();
  if (running) {
    runMotor();
  }
  checkHourlyReset();
}

void setupWiFi() {
  if (strlen(ssid) > 0 && WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.softAP(default_ssid, default_password);
    Serial.println("Hotspot modunda baÅŸlatÄ±ldÄ±: " + String(default_ssid));
  } else {
    Serial.println("WiFiâ€™ye baÄŸlanÄ±ldÄ±: " + String(ssid));
  }
}

void setupMDNS() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  snprintf(mDNS_hostname, sizeof(mDNS_hostname), "horus-%s", mac.substring(8).c_str());
  if (MDNS.begin(mDNS_hostname)) {
    Serial.println("mDNS baÅŸlatÄ±ldÄ±: " + String(mDNS_hostname) + ".local");
  }
}

void setupWebServer() {
  server.on("/", []() { server.send(200, "text/html", htmlPage()); });
  server.on("/set", handleSet);
  server.on("/set_name", handleSetName);
  server.on("/wifi", handleWiFi);
  server.on("/reset_name", handleResetName);
  server.on("/reset_wifi", handleResetWiFi);
  server.on("/reset_motor", handleResetMotor);
  server.on("/scan_wifi", handleScanWiFi);
  server.on("/check_ota", handleCheckOTA);
  server.on("/status", []() {
    StaticJsonDocument<256> doc;
    String currentSSID = WiFi.SSID() != "" ? WiFi.SSID() : default_ssid;
    doc["status"] = running ? "Ã‡alÄ±ÅŸÄ±yor" : "Durduruldu";
    doc["completedTurns"] = completedTurns;
    doc["hourlyTurns"] = hourlyTurns;
    doc["currentSSID"] = escapeJsonString(currentSSID);
    doc["connectionStatus"] = (WiFi.status() == WL_CONNECTED) ? "BaÄŸlandÄ±" : "Hotspot modunda";
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["turnsPerDay"] = turnsPerDay;
    doc["turnDuration"] = turnDuration;
    doc["direction"] = direction;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });
  server.begin();
}

String escapeHtmlString(String input) {
  input.replace("&", "&amp;");
  input.replace("<", "&lt;");
  input.replace(">", "&gt;");
  input.replace("\"", "&quot;");
  input.replace("'", "&#39;");
  return input;
}

String escapeJsonString(String input) {
  input.replace("\"", "\\\"");
  input.replace("\\", "\\\\");
  return input;
}

void readSettings() {
  EEPROM.get(0, ssid);
  EEPROM.get(32, password);
  EEPROM.get(96, custom_name);
  EEPROM.get(117, turnsPerDay);
  EEPROM.get(121, turnDuration);
  EEPROM.get(125, direction);
  if (turnsPerDay < 600 || turnsPerDay > 1200) turnsPerDay = 600;
  if (turnDuration < 10.0 || turnDuration > 15.0) turnDuration = 10.0;
  if (direction < 1 || direction > 3) direction = 1;
  hourlyTurns = turnsPerDay / 24;
}

void writeSettings() {
  EEPROM.put(0, ssid);
  EEPROM.put(32, password);
  EEPROM.put(96, custom_name);
  writeMotorSettings();
  EEPROM.commit();
}

void writeMotorSettings() {
  EEPROM.put(117, turnsPerDay);
  EEPROM.put(121, turnDuration);
  EEPROM.put(125, direction);
  EEPROM.commit();
}

void handleSet() {
  if (server.hasArg("tpd")) turnsPerDay = server.arg("tpd").toInt();
  if (server.hasArg("duration")) turnDuration = server.arg("duration").toFloat();
  if (server.hasArg("dir")) direction = server.arg("dir").toInt();
  if (turnsPerDay < 600 || turnsPerDay > 1200) turnsPerDay = 600;
  if (turnDuration < 10.0 || turnDuration > 15.0) turnDuration = 10.0;
  if (direction < 1 || direction > 3) direction = 1;
  hourlyTurns = turnsPerDay / 24;
  writeMotorSettings();
  if (server.hasArg("action")) {
    if (server.arg("action") == "start") {
      running = true;
    } else if (server.arg("action") == "stop") {
      running = false;
      stopMotor();
    }
  }
  updateWebSocket();
  server.send(200, "text/plain", "OK");
}

void handleSetName() {
  if (server.hasArg("custom_name")) {
    String newName = server.arg("custom_name");
    if (newName.length() > 0 && newName.length() <= 20) {
      strncpy(custom_name, newName.c_str(), sizeof(custom_name) - 1);
      custom_name[sizeof(custom_name) - 1] = '\0';
      writeSettings();
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "GeÃ§ersiz cihaz ismi");
    }
  } else {
    server.send(400, "text/plain", "Cihaz ismi eksik");
  }
}

void handleResetName() {
  custom_name[0] = '\0';
  writeSettings();
  server.send(200, "text/plain", "OK");
}

void handleWiFi() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    strncpy(ssid, server.arg("ssid").c_str(), sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    strncpy(password, server.arg("pass").c_str(), sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
    writeSettings();
    WiFi.disconnect();
    setupWiFi();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "SSID veya ÅŸifre eksik");
  }
}

void handleResetWiFi() {
  ssid[0] = '\0';
  password[0] = '\0';
  writeSettings();
  WiFi.disconnect();
  setupWiFi();
  server.send(200, "text/plain", "OK");
}

void handleResetMotor() {
  resetMotorSettings();
  server.send(200, "text/plain", "OK");
}

void handleScanWiFi() {
  scanWiFiNetworks();
  server.send(200, "text/plain", "OK");
}

void handleCheckOTA() {
  checkOTAUpdate();
  server.send(200, "text/plain", "OK");
}

void stopMotor() {
  digitalWrite(motorPin1, LOW);
  digitalWrite(motorPin2, LOW);
  digitalWrite(motorPin3, LOW);
  digitalWrite(motorPin4, LOW);
}

void runMotor() {
  static int currentStep = 0;
  static unsigned long lastStepTime = 0;
  static bool forward = true;
  if (millis() - lastStepTime >= turnDuration * 1000 / stepsPerRevolution) {
    if (direction == 1 || (direction == 3 && forward)) {
      stepMotor(currentStep % 4);
    } else {
      stepMotor(3 - (currentStep % 4));
    }
    currentStep++;
    lastStepTime = millis();
    if (currentStep >= stepsPerRevolution) {
      currentStep = 0;
      completedTurns++;
      if (direction == 3) forward = !forward;
      updateWebSocket();
    }
  }
}

void stepMotor(int step) {
  switch (step) {
    case 0: digitalWrite(motorPin1, HIGH); digitalWrite(motorPin2, LOW); digitalWrite(motorPin3, LOW); digitalWrite(motorPin4, LOW); break;
    case 1: digitalWrite(motorPin1, LOW); digitalWrite(motorPin2, HIGH); digitalWrite(motorPin3, LOW); digitalWrite(motorPin4, LOW); break;
    case 2: digitalWrite(motorPin1, LOW); digitalWrite(motorPin2, LOW); digitalWrite(motorPin3, HIGH); digitalWrite(motorPin4, LOW); break;
    case 3: digitalWrite(motorPin1, LOW); digitalWrite(motorPin2, LOW); digitalWrite(motorPin3, LOW); digitalWrite(motorPin4, HIGH); break;
  }
}

void checkHourlyReset() {
  unsigned long currentTime = millis();
  if (currentTime - lastHourTime >= 3600000) {
    if (completedTurns >= hourlyTurns) {
      completedTurns = 0;
      lastHourTime = currentTime;
      updateWebSocket();
    }
  }
}

void resetMotorSettings() {
  turnsPerDay = 600;
  turnDuration = 10.0;
  direction = 1;
  running = false;
  completedTurns = 0;
  lastHourTime = 0;
  hourlyTurns = turnsPerDay / 24;
  stopMotor();
  writeMotorSettings();
  Serial.println("Motor ayarlarÄ± sÄ±fÄ±rlandÄ±: TPD=600, Duration=10.0, Dir=1, Running=false");
  updateWebSocket(); // SÄ±fÄ±rlama sonrasÄ± gÃ¼ncelleme gÃ¶nder
}

String scanWiFiNetworks() {
  Serial.println("WiFi tarama baÅŸlatÄ±lÄ±yor...");
  int n = WiFi.scanNetworks();
  Serial.print("Bulunan aÄŸ sayÄ±sÄ±: ");
  Serial.println(n);
  String options = "<option value=\"\">AÄŸ SeÃ§in</option>";
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid != "") {
      Serial.print("AÄŸ bulundu: ");
      Serial.println(ssid);
      options += "<option value=\"" + escapeHtmlString(ssid) + "\">" + escapeHtmlString(ssid) + "</option>";
    }
  }
  if (n == 0) {
    Serial.println("HiÃ§ aÄŸ bulunamadÄ±!");
    options += "<option value=\"\">HiÃ§ aÄŸ bulunamadÄ±</option>";
  }
  StaticJsonDocument<512> doc;
  doc["wifiOptions"] = options;
  String json;
  serializeJson(doc, json);
  Serial.println("WebSocketâ€™a gÃ¶nderilen WiFi seÃ§enekleri: " + json);
  webSocket.broadcastTXT(json);
  return options;
}

void checkOTAUpdate() {
  HTTPClient http;
  http.begin(github_url);
  if (github_token && strlen(github_token) > 0) {
    http.addHeader("Authorization", String("token ") + github_token);
  } else {
    Serial.println("GitHub token eksik, anonim istek gÃ¶nderiliyor.");
  }
  http.addHeader("Accept", "application/vnd.github.v3+json");
  int httpCode = http.GET();

  StaticJsonDocument<256> statusDoc;
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      String latestVersion = doc["tag_name"].as<String>();
      String currentVersion = String(FIRMWARE_VERSION);
      latestVersion.replace("v", "");
      currentVersion.replace("v", "");
      if (latestVersion > currentVersion) {
        statusDoc["otaStatus"] = "Yeni sÃ¼rÃ¼m mevcut: " + latestVersion;
      } else {
        statusDoc["otaStatus"] = "Firmware gÃ¼ncel: " + currentVersion;
      }
    } else {
      statusDoc["otaStatus"] = "OTA kontrol hatasÄ±: JSON parse hatasÄ± - " + String(error.c_str());
      Serial.println("JSON parse hatasÄ±: " + String(error.c_str()));
    }
  } else {
    statusDoc["otaStatus"] = "OTA kontrol hatasÄ±: HTTP " + String(httpCode);
    Serial.println("HTTP hatasÄ±: " + String(httpCode));
    if (httpCode == 401) {
      Serial.println("Yetkilendirme hatasÄ±: GitHub tokenâ€™Ä± kontrol edin.");
    } else if (httpCode == 403) {
      Serial.println("API rate limit aÅŸÄ±lmÄ±ÅŸ olabilir, token gerekli veya rate limit beklenmeli.");
    }
  }
  String json;
  serializeJson(statusDoc, json);
  webSocket.broadcastTXT(json);
  http.end();
}

void updateWebSocket() {
  StaticJsonDocument<256> doc;
  String currentSSID = WiFi.SSID() != "" ? WiFi.SSID() : default_ssid;
  doc["status"] = running ? "Ã‡alÄ±ÅŸÄ±yor" : "Durduruldu";
  doc["completedTurns"] = completedTurns;
  doc["hourlyTurns"] = hourlyTurns;
  doc["currentSSID"] = escapeJsonString(currentSSID);
  doc["connectionStatus"] = (WiFi.status() == WL_CONNECTED) ? "BaÄŸlandÄ±" : "Hotspot modunda";
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["turnsPerDay"] = turnsPerDay;
  doc["turnDuration"] = turnDuration;
  doc["direction"] = direction;

  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
  Serial.println("WebSocket gÃ¼ncelleme gÃ¶nderildi: " + json);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_DISCONNECTED) {
    Serial.printf("WebSocket client #%u disconnected\n", num);
  } else if (type == WStype_CONNECTED) {
    Serial.printf("WebSocket client #%u connected\n", num);
    updateWebSocket();
  }
}

String htmlPage() {
  String status = running ? "Ã‡alÄ±ÅŸÄ±yor" : "Durduruldu";
  String spinnerClass = running ? "" : "hidden";
  String completed = String(completedTurns);
  String hourly = String(hourlyTurns);
  float progress = hourlyTurns > 0 ? (completedTurns / (float)hourlyTurns * 100) : 0;
  String currentSSID = WiFi.SSID() != "" ? WiFi.SSID() : default_ssid;
  String connectionStatus = (WiFi.status() == WL_CONNECTED) ? "BaÄŸlandÄ±" : "Hotspot modunda";
  String otaStatus = "";
  String tpd = String(turnsPerDay);
  String duration = String(turnDuration, 1);
  String dir1Checked = (direction == 1) ? "checked" : "";
  String dir2Checked = (direction == 2) ? "checked" : "";
  String dir3Checked = (direction == 3) ? "checked" : "";
  String currentCustomName = custom_name != "" ? custom_name : "Cihaz Ä°smi Girin";

  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Horus by Wyntro</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <script src="https://kit.fontawesome.com/a076d05399.js" crossorigin="anonymous"></script>
  <style>
    .collapsible { transition: all 0.2s ease; }
    .collapsible.active + .content { display: block; }
    .content { display: none; }
    .device-content { display: none; }
    .device-content.active { display: block; }
    .error { color: #dc2626; }
    .fa-spin { animation: fa-spin 2s infinite linear; }
  </style>
</head>
<body class="bg-gray-100 dark:bg-gray-900 text-gray-900 dark:text-gray-100 min-h-screen flex items-center justify-center">
  <div class="container max-w-2xl mx-auto p-4 bg-white dark:bg-gray-800 rounded-lg shadow-lg">
    <h1 class="text-3xl font-bold text-center mb-4 text-blue-600 dark:text-blue-400 w-24 mx-auto">Horus by Wyntro</h1>
    <p class="text-center text-lg" id="device_id">Cihaz: )rawliteral" + escapeHtmlString(mDNS_hostname) + R"rawliteral(.local</p>
    <p class="text-center font-semibold text-blue-600 dark:text-blue-400" id="status">Durum: )rawliteral" + status + R"rawliteral( <i class="fas fa-spinner fa-spin )rawliteral" + spinnerClass + R"rawliteral(" id="motor_spinner"></i></p>
    <div class="mb-4">
      <p class="text-center">Tamamlanan Turlar: <span id="turns">)rawliteral" + completed + R"rawliteral( / )rawliteral" + hourly + R"rawliteral(</span></p>
      <div class="w-full bg-gray-200 dark:bg-gray-700 rounded-full h-2.5 mt-2">
        <div class="bg-blue-600 dark:bg-blue-400 h-2.5 rounded-full" style="width: )rawliteral" + String(progress) + R"rawliteral(%;" id="progress_bar"></div>
      </div>
    </div>
    <p class="text-center" id="wifi">BaÄŸlÄ± WiFi: )rawliteral" + escapeHtmlString(currentSSID) + R"rawliteral(</p>
    <p class="text-center" id="connection_status">BaÄŸlantÄ± Durumu: )rawliteral" + connectionStatus + R"rawliteral(</p>
    <p class="text-center" id="ota_status">)rawliteral" + otaStatus + R"rawliteral(</p>
    <div class="space-y-4">
      <div>
        <label class="block text-sm font-medium">GÃ¼nlÃ¼k Tur: <span id="tpd_val">)rawliteral" + tpd + R"rawliteral(</span></label>
        <input type="range" id="tpd" min="600" max="1200" step="1" value=")rawliteral" + tpd + R"rawliteral(" oninput="tpd_val.innerText=this.value; validateTpd(this)" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">
        <p class="text-red-500 hidden" id="tpd_error">GÃ¼nlÃ¼k tur 600-1200 arasÄ±nda olmalÄ±.</p>
      </div>
      <div>
        <label class="block text-sm font-medium">Tur SÃ¼resi (saniye): <span id="duration_val">)rawliteral" + duration + R"rawliteral(</span></label>
        <input type="range" id="duration" min="10" max="15" step="0.1" value=")rawliteral" + duration + R"rawliteral(" oninput="duration_val.innerText=this.value; validateDuration(this)" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">
        <p class="text-red-500 hidden" id="duration_error">Tur sÃ¼resi 10-15 saniye arasÄ±nda olmalÄ±.</p>
      </div>
      <div>
        <label class="block text-sm font-medium">DÃ¶nÃ¼ÅŸ YÃ¶nÃ¼:</label>
        <div class="flex justify-center space-x-4">
          <label><input type="radio" name="dir" value="1" )rawliteral" + dir1Checked + R"rawliteral(> Saat YÃ¶nÃ¼</label>
          <label><input type="radio" name="dir" value="2" )rawliteral" + dir2Checked + R"rawliteral(> Saat YÃ¶nÃ¼nÃ¼n Tersi</label>
          <label><input type="radio" name="dir" value="3" )rawliteral" + dir3Checked + R"rawliteral(> Ä°leri - Geri</label>
        </div>
      </div>
      <div class="flex justify-center space-x-4">
        <button onclick="sendCommand('start')" class="bg-blue-600 dark:bg-blue-500 text-white px-4 py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-play mr-2"></i>BaÅŸlat</button>
        <button onclick="sendCommand('stop')" class="bg-red-600 dark:bg-red-500 text-white px-4 py-2 rounded-md hover:bg-red-700 dark:hover:bg-blue-600"><i class="fas fa-stop mr-2"></i>Durdur</button>
      </div>
    </div>
    <button class="collapsible w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md mt-4 hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-cog mr-2"></i>Ayarlar MenÃ¼sÃ¼</button>
    <div class="content mt-2 space-y-4">
      <h3 class="text-xl font-semibold">Cihaz Ä°smi AyarlarÄ±</h3>
      <div class="space-y-2">
        <label class="block text-sm font-medium">Cihaz Ä°smi (1-20 karakter, sadece harf veya rakam):</label>
        <input type="text" id="custom_name" placeholder=")rawliteral" + escapeHtmlString(currentCustomName) + R"rawliteral(" maxlength="20" class="w-full p-2 border dark:border-gray-600 rounded-md bg-gray-100 dark:bg-gray-700">
        <button onclick="setName()" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-save mr-2"></i>Ä°smi Kaydet</button>
      </div>
      <button onclick="resetName()" class="w-full bg-gray-600 dark:bg-gray-500 text-white py-2 rounded-md hover:bg-gray-700 dark:hover:bg-gray-600"><i class="fas fa-undo mr-2"></i>Cihaz Ä°smini SÄ±fÄ±rla</button>
      <h3 class="text-xl font-semibold">WiFi AyarlarÄ±</h3>
      <button id="scan_wifi_button" onclick="scanWiFi()" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-wifi mr-2"></i>AÄŸlarÄ± Tara</button>
      <p id="scan_status" class="text-center"></p>
      <div class="space-y-2">
        <label class="block text-sm font-medium">WiFi SSID:</label>
        <select id="ssid" class="w-full p-2 border dark:border-gray-600 rounded-md bg-gray-100 dark:bg-gray-700">
          <option value="">AÄŸ SeÃ§in</option>
        </select>
        <label class="block text-sm font-medium">WiFi Åifre:</label>
        <input type="password" id="pass" placeholder="WiFi Åifresi" class="w-full p-2 border dark:border-gray-600 rounded-md bg-gray-100 dark:bg-gray-700">
        <button onclick="saveWiFi()" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-save mr-2"></i>WiFi Kaydet</button>
      </div>
      <button onclick="resetWiFi()" class="w-full bg-gray-600 dark:bg-gray-500 text-white py-2 rounded-md hover:bg-gray-700 dark:hover:bg-gray-600"><i class="fas fa-undo mr-2"></i>WiFi AyarlarÄ±nÄ± SÄ±fÄ±rla</button>
      <button onclick="resetMotor()" class="w-full bg-gray-600 dark:bg-gray-500 text-white py-2 rounded-md hover:bg-gray-700 dark:hover:bg-gray-600"><i class="fas fa-undo mr-2"></i>Motor AyarlarÄ±nÄ± SÄ±fÄ±rla</button>
      <h3 class="text-xl font-semibold">GÃ¼ncelleme</h3>
      <button onclick="goToUpdate()" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-upload mr-2"></i>OTA GÃ¼ncelleme SayfasÄ±</button>
      <button onclick="checkOTA()" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-sync-alt mr-2"></i>GÃ¼ncellemeyi Åimdi Kontrol Et</button>
    </div>
    <button class="collapsible w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md mt-4 hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-network-wired mr-2"></i>DiÄŸer Cihazlar</button>
    <div class="content mt-2 space-y-4">
      <h3 class="text-xl font-semibold">DiÄŸer Horus by Wyntro CihazlarÄ±</h3>
      <input type="text" id="device_hostname" placeholder="Cihaz hostname (Ã¶rn: MyWinder2-d99d.local)" class="w-full p-2 border dark:border-gray-600 rounded-md bg-gray-100 dark:bg-gray-700">
      <button onclick="addDevice()" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-plus mr-2"></i>Ekle</button>
      <p id="device_status" class="text-center"></p>
      <div id="device_list"></div>
    </div>
  </div>
  <script>
    function sendCommand(action) {
      const tpd = document.getElementById('tpd').value;
      const duration = document.getElementById('duration').value;
      const dir = document.querySelector('input[name="dir"]:checked')?.value || '1';
      fetch(`/set?tpd=${tpd}&duration=${duration}&dir=${dir}&action=${action}`)
        .then(response => response.text())
        .then(data => console.log(`Komut gÃ¶nderildi: ${action}`))
        .catch(error => console.error('Komut gÃ¶nderim hatasÄ±:', error));
    }

    function setName() {
      const custom_name = document.getElementById('custom_name').value.trim();
      if (custom_name.length > 0 && custom_name.length <= 20) {
        fetch(`/set_name?custom_name=${encodeURIComponent(custom_name)}`)
          .then(response => response.text())
          .then(data => console.log('Cihaz ismi ayarlandÄ±:', custom_name))
          .catch(error => console.error('Cihaz ismi ayarlanamadÄ±:', error));
      } else {
        alert('Cihaz ismi 1-20 karakter arasÄ±nda olmalÄ±!');
      }
    }

    function resetName() {
      fetch('/reset_name')
        .then(response => response.text())
        .then(data => console.log('Cihaz ismi sÄ±fÄ±rlandÄ±'))
        .catch(error => console.error('Cihaz ismi sÄ±fÄ±rlanamadÄ±:', error));
    }

    function saveWiFi() {
      const ssid = document.getElementById('ssid').value;
      const pass = document.getElementById('pass').value;
      if (ssid) {
        fetch(`/wifi?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}`)
          .then(response => response.text())
          .then(data => console.log('WiFi ayarlarÄ± kaydedildi:', ssid))
          .catch(error => console.error('WiFi ayarlarÄ± kaydedilemedi:', error));
      } else {
        alert('LÃ¼tfen bir WiFi aÄŸÄ± seÃ§in!');
      }
    }

    function resetWiFi() {
      fetch('/reset_wifi')
        .then(response => response.text())
        .then(data => console.log('WiFi ayarlarÄ± sÄ±fÄ±rlandÄ±'))
        .catch(error => console.error('WiFi ayarlarÄ± sÄ±fÄ±rlanamadÄ±:', error));
    }

    function resetMotor() {
      fetch('/reset_motor')
        .then(response => response.text())
        .then(data => {
          console.log('Motor ayarlarÄ± sÄ±fÄ±rlandÄ±');
          // ArayÃ¼zÃ¼ manuel gÃ¼ncelle
          document.getElementById('tpd').value = 600;
          document.getElementById('tpd_val').innerText = 600;
          document.getElementById('duration').value = 10.0;
          document.getElementById('duration_val').innerText = 10.0;
          document.querySelector('input[name="dir"][value="1"]').checked = true;
          document.getElementById('status').innerText = 'Durum: Durduruldu';
          document.getElementById('motor_spinner').classList.add('hidden');
          document.getElementById('turns').innerText = '0 / 25';
          document.getElementById('progress_bar').style.width = '0%';
        })
        .catch(error => console.error('Motor ayarlarÄ± sÄ±fÄ±rlanamadÄ±:', error));
    }

    function goToUpdate() {
      window.location.href = '/update';
    }

    function checkOTA() {
      fetch('/check_ota')
        .then(response => response.text())
        .then(data => console.log('OTA kontrol edildi'))
        .catch(error => console.error('OTA kontrol hatasÄ±:', error));
    }

    document.addEventListener('DOMContentLoaded', function() {
      console.log('DOM tamamen yÃ¼klendi.');
      document.querySelectorAll('.collapsible').forEach(button => {
        button.addEventListener('click', function() {
          this.classList.toggle('active');
          this.nextElementSibling.classList.toggle('active');
          localStorage.setItem('collapsibleState_' + this.innerText, this.classList.contains('active') ? 'open' : 'closed');
        });
      });

      document.querySelectorAll('.collapsible').forEach(button => {
        if (localStorage.getItem('collapsibleState_' + button.innerText) === 'open') {
          button.classList.add('active');
          button.nextElementSibling.classList.add('active');
        }
      });

      initWebSocket();
      updateDeviceList();
    });

    function validateTpd(input) {
      const error = document.getElementById('tpd_error');
      if (error) {
        console.log('validateTpd: tpd_error elemanÄ± bulundu.');
        if (input.value < 600 || input.value > 1200) {
          error.classList.remove('hidden');
        } else {
          error.classList.add('hidden');
        }
      } else {
        console.error('validateTpd: tpd_error elemanÄ± bulunamadÄ±!');
      }
    }

    function validateDuration(input) {
      const error = document.getElementById('duration_error');
      if (error) {
        console.log('validateDuration: duration_error elemanÄ± bulundu.');
        if (input.value < 10 || input.value > 15) {
          error.classList.remove('hidden');
        } else {
          error.classList.add('hidden');
        }
      } else {
        console.error('validateDuration: duration_error elemanÄ± bulunamadÄ±!');
      }
    }

    function scanWiFi() {
      console.log('scanWiFi fonksiyonu Ã§aÄŸrÄ±ldÄ±.');
      const scanStatus = document.getElementById('scan_status');
      const ssidSelect = document.getElementById('ssid');
      if (!scanStatus || !ssidSelect) {
        console.error('scan_status veya ssid elemanÄ± bulunamadÄ±!');
        if (scanStatus) {
          scanStatus.innerText = 'Tarama baÅŸarÄ±sÄ±z: Gerekli HTML elemanlarÄ± eksik.';
        }
        return;
      }
      scanStatus.innerText = 'Tarama yapÄ±lÄ±yor, lÃ¼tfen 10-15 saniye bekleyin...';
      ssidSelect.innerHTML = '<option value="">AÄŸ SeÃ§in</option>';
      fetch('/scan_wifi', { timeout: 30000 })
        .then(response => {
          if (!response.ok) throw new Error('HTTP hatasÄ±: ' + response.status);
          return response.text();
        })
        .then(data => {
          console.log('WiFi tarama yanÄ±tÄ±:', data);
          if (data === 'OK') {
            scanStatus.innerText = 'Tarama baÅŸlatÄ±ldÄ±, sonuÃ§lar bekleniyor...';
          } else {
            scanStatus.innerText = 'Tarama baÅŸarÄ±sÄ±z: Sunucudan beklenmeyen yanÄ±t - ' + data;
          }
        })
        .catch(error => {
          console.error('WiFi tarama hatasÄ±:', error);
          scanStatus.innerText = 'Tarama baÅŸlatÄ±lÄ±rken hata: ' + error.message;
        });
    }

    let ws;
    function initWebSocket() {
      console.log('WebSocket baÅŸlatÄ±lÄ±yor: ws://' + window.location.hostname + ':81/');
      ws = new WebSocket('ws://' + window.location.hostname + ':81/');
      ws.onmessage = function(event) {
        console.log('WebSocket mesaj alÄ±ndÄ±:', event.data);
        const scanStatus = document.getElementById('scan_status');
        if (!scanStatus) {
          console.error('scan_status elemanÄ± bulunamadÄ±!');
          return;
        }
        try {
          const data = JSON.parse(event.data);
          console.log('JSON parse edildi:', data);
          if (data.status) {
            const statusEl = document.getElementById('status');
            if (statusEl) {
              statusEl.innerText = 'Durum: ' + data.status;
              const motorSpinner = document.getElementById('motor_spinner');
              if (motorSpinner) {
                motorSpinner.classList.toggle('hidden', data.status !== 'Ã‡alÄ±ÅŸÄ±yor');
              } else {
                console.error('motor_spinner elemanÄ± bulunamadÄ±!');
              }
            } else {
              console.error('status elemanÄ± bulunamadÄ±!');
            }
          }
          if (data.completedTurns !== undefined && data.hourlyTurns !== undefined) {
            const turnsEl = document.getElementById('turns');
            const progressBar = document.getElementById('progress_bar');
            if (turnsEl && progressBar) {
              turnsEl.innerText = data.completedTurns + ' / ' + data.hourlyTurns;
              const progress = (data.completedTurns / data.hourlyTurns * 100).toFixed(1);
              progressBar.style.width = progress + '%';
            } else {
              console.error('turns veya progress_bar elemanÄ± bulunamadÄ±!');
            }
          }
          if (data.currentSSID) {
            const wifiEl = document.getElementById('wifi');
            if (wifiEl) {
              wifiEl.innerText = 'BaÄŸlÄ± WiFi: ' + data.currentSSID;
            } else {
              console.error('wifi elemanÄ± bulunamadÄ±!');
            }
          }
          if (data.connectionStatus) {
            const connEl = document.getElementById('connection_status');
            if (connEl) {
              connEl.innerText = 'BaÄŸlantÄ± Durumu: ' + data.connectionStatus;
            } else {
              console.error('connection_status elemanÄ± bulunamadÄ±!');
            }
          }
          if (data.wifiOptions) {
            const ssidSelect = document.getElementById('ssid');
            if (ssidSelect) {
              ssidSelect.innerHTML = data.wifiOptions;
              scanStatus.innerText = 'Tarama tamamlandÄ±, aÄŸlar yÃ¼klendi.';
              console.log('WiFi seÃ§enekleri yÃ¼klendi:', data.wifiOptions);
            } else {
              console.error('ssid elemanÄ± bulunamadÄ±!');
              scanStatus.innerText = 'Tarama baÅŸarÄ±sÄ±z: WiFi seÃ§im elemanÄ± bulunamadÄ±.';
            }
          }
          if (data.otaStatus) {
            const otaEl = document.getElementById('ota_status');
            if (otaEl) {
              otaEl.innerText = data.otaStatus;
            } else {
              console.error('ota_status elemanÄ± bulunamadÄ±!');
            }
          }
        } catch (error) {
          console.error('WebSocket JSON parse hatasÄ±:', error.message);
          scanStatus.innerText = 'Tarama baÅŸarÄ±sÄ±z: Veri iÅŸleme hatasÄ± - ' + error.message;
        }
      };
      ws.onclose = function() {
        console.log('WebSocket baÄŸlantÄ±sÄ± kesildi, 2 saniye sonra yeniden baÄŸlanÄ±lÄ±yor...');
        setTimeout(initWebSocket, 2000);
      };
      ws.onerror = function(error) {
        console.error('WebSocket hatasÄ±:', error);
        if (scanStatus) {
          scanStatus.innerText = 'WebSocket hatasÄ±: BaÄŸlantÄ± sorunu.';
        }
      };
    }

    let devices = JSON.parse(localStorage.getItem('watchWinderDevices')) || [];

    function saveDevices() {
      localStorage.setItem('watchWinderDevices', JSON.stringify(devices));
    }

    function addDevice() {
      console.log('addDevice fonksiyonu Ã§aÄŸrÄ±ldÄ±.');
      const hostname = document.getElementById('device_hostname').value.trim();
      const deviceStatus = document.getElementById('device_status');
      if (!deviceStatus) {
        console.error('device_status elemanÄ± bulunamadÄ±!');
        return;
      }
      if (hostname && hostname !== window.location.hostname && !devices.includes(hostname)) {
        devices.push(hostname);
        saveDevices();
        document.getElementById('device_hostname').value = '';
        deviceStatus.innerText = 'Cihaz eklendi: ' + hostname;
        updateDeviceList();
        fetchDeviceStatus(hostname);
      } else {
        deviceStatus.innerText = 'Hata: GeÃ§erli bir hostname girin, kendi cihazÄ±nÄ±zÄ± ekleyemezsiniz veya cihaz zaten ekli.';
      }
    }

    function removeDevice(hostname) {
      console.log('removeDevice Ã§aÄŸrÄ±ldÄ±:', hostname);
      devices = devices.filter(device => device !== hostname);
      saveDevices();
      const deviceStatus = document.getElementById('device_status');
      if (deviceStatus) {
        deviceStatus.innerText = 'Cihaz silindi: ' + hostname;
      } else {
        console.error('device_status elemanÄ± bulunamadÄ±!');
      }
      updateDeviceList();
    }

    function fetchDeviceStatus(hostname) {
      console.log('fetchDeviceStatus Ã§aÄŸrÄ±ldÄ±:', hostname);
      fetch(`http://${hostname}/status`, { mode: 'cors' })
        .then(response => {
          if (!response.ok) throw new Error('Cihaz yanÄ±t vermiyor');
          return response.json();
        })
        .then(data => {
          updateDeviceUI(hostname, data);
        })
        .catch(error => {
          updateDeviceUI(hostname, { error: error.message });
        });
    }

    function updateDeviceUI(hostname, data) {
      console.log('updateDeviceUI Ã§aÄŸrÄ±ldÄ±:', hostname, data);
      const deviceDiv = document.getElementById(`device_${hostname}`);
      if (!deviceDiv) {
        console.error(`device_${hostname} elemanÄ± bulunamadÄ±!`);
        return;
      }
      let html = `<h3 class="device-header text-lg font-semibold bg-blue-600 dark:bg-blue-500 text-white p-2 rounded-md cursor-pointer">${hostname}</h3>`;
      html += `<div class="device-content p-4 bg-gray-100 dark:bg-gray-700 rounded-md mt-2" id="content_${hostname}">`;
      html += `<button class="w-full bg-red-600 dark:bg-red-500 text-white py-2 rounded-md hover:bg-red-700 dark:hover:bg-red-600 mb-2" onclick="removeDevice('${hostname}')"><i class="fas fa-trash mr-2"></i>Sil</button>`;
      if (data.error) {
        html += `<p class="text-red-500">Hata: ${data.error}</p>`;
      } else {
        html += `<p class="font-semibold">Durum: ${data.status} <i class="fas fa-spinner fa-spin ${data.status === 'Ã‡alÄ±ÅŸÄ±yor' ? '' : 'hidden'}" id="motor_spinner_${hostname}"></i></p>`;
        html += `<p>Tamamlanan Turlar: ${data.completedTurns} / ${data.hourlyTurns}</p>`;
        html += `<p>BaÄŸlÄ± WiFi: ${data.currentSSID}</p>`;
        html += `<p>BaÄŸlantÄ± Durumu: ${data.connectionStatus}</p>`;
        html += `<p>Firmware: ${data.firmwareVersion}</p>`;
        html += `<div class="space-y-2">`;
        html += `<label class="block text-sm font-medium">GÃ¼nlÃ¼k Tur: <span id="tpd_val_${hostname}">${data.turnsPerDay}</span></label>`;
        html += `<input type="range" id="tpd_${hostname}" min="600" max="1200" step="1" value="${data.turnsPerDay}" oninput="tpd_val_${hostname}.innerText=this.value" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">`;
        html += `<label class="block text-sm font-medium">Tur SÃ¼resi (saniye): <span id="duration_val_${hostname}">${data.turnDuration}</span></label>`;
        html += `<input type="range" id="duration_${hostname}" min="10" max="15" step="0.1" value="${data.turnDuration}" oninput="duration_val_${hostname}.innerText=this.value" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">`;
        html += `<label class="block text-sm font-medium">DÃ¶nÃ¼ÅŸ YÃ¶nÃ¼:</label>`;
        html += `<div class="flex justify-center space-x-4">`;
        html += `<label><input type="radio" name="dir_${hostname}" value="1" ${data.direction == 1 ? 'checked' : ''}> Saat YÃ¶nÃ¼</label>`;
        html += `<label><input type="radio" name="dir_${hostname}" value="2" ${data.direction == 2 ? 'checked' : ''}> Saat YÃ¶nÃ¼nÃ¼n Tersi</label>`;
        html += `<label><input type="radio" name="dir_${hostname}" value="3" ${data.direction == 3 ? 'checked' : ''}> Ä°leri - Geri</label>`;
        html += `</div>`;
        html += `<div class="space-y-2">`;
        html += `<button onclick="sendDeviceCommand('${hostname}', 'start')" class="bg-blue-600 dark:bg-blue-500 text-white px-4 py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-play mr-2"></i>BaÅŸlat</button>`;
        html += `<button onclick="sendDeviceCommand('${hostname}', 'stop')" class="bg-red-600 dark:bg-red-500 text-white px-4 py-2 rounded-md hover:bg-red-700 dark:hover:bg-red-600"><i class="fas fa-stop mr-2"></i>Durdur</button>`;
        html += `</div>`;
        html += `</div>`;
      }
      html += `</div>`;
      deviceDiv.innerHTML = html;

      deviceDiv.querySelector('.device-header').addEventListener('click', function() {
        const content = document.getElementById(`content_${hostname}`);
        if (content) {
          content.classList.toggle('active');
          localStorage.setItem(`device_state_${hostname}`, content.classList.contains('active') ? 'open' : 'closed');
        } else {
          console.error(`content_${hostname} elemanÄ± bulunamadÄ±!`);
        }
      });

      setTimeout(() => {
        if (devices.includes(hostname)) {
          fetchDeviceStatus(hostname);
        }
      }, 5000);
    }

    function sendDeviceCommand(hostname, action) {
      const tpd = document.getElementById(`tpd_${hostname}`).value;
      const duration = document.getElementById(`duration_${hostname}`).value;
      const dir = document.querySelector(`input[name="dir_${hostname}"]:checked`)?.value || '1';
      fetch(`http://${hostname}/set?tpd=${tpd}&duration=${duration}&dir=${dir}&action=${action}`, { mode: 'cors' })
        .then(response => response.text())
        .then(data => console.log(`Cihaz komutu gÃ¶nderildi: ${hostname}, ${action}`))
        .catch(error => console.error(`Cihaz komutu gÃ¶nderilemedi: ${hostname}, ${error}`));
    }

    function updateDeviceList() {
      console.log('updateDeviceList Ã§aÄŸrÄ±ldÄ±.');
      const deviceList = document.getElementById('device_list');
      if (!deviceList) {
        console.error('device_list elemanÄ± bulunamadÄ±!');
        return;
      }
      deviceList.innerHTML = '';
      devices.forEach(hostname => {
        const deviceDiv = document.createElement('div');
        deviceDiv.className = 'device mt-4';
        deviceDiv.id = `device_${hostname}`;
        deviceList.appendChild(deviceDiv);
        fetchDeviceStatus(hostname);
        const state = localStorage.getItem(`device_state_${hostname}`);
        if (state === 'open') {
          setTimeout(() => {
            const content = document.getElementById(`content_${hostname}`);
            if (content) {
              content.classList.add('active');
            } else {
              console.error(`content_${hostname} elemanÄ± bulunamadÄ±!`);
            }
          }, 100);
        }
      });
    }
  </script>
</body>
</html>
)rawliteral";
  return page;
}
