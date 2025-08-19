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

// OTA Ayarları
const char* github_url = "https://api.github.com/repos/recaner35/WyntroHorus2/releases/latest";
const char* github_token = getenv("GITHUB_TOKEN");
const char* FIRMWARE_VERSION = "v1.0.18";

// WiFi Ayarları
const char* default_ssid = "HorusAP";
const char* default_password = "12345678";
char ssid[32] = "";
char password[64] = "";
char custom_name[21] = "";
char mDNS_hostname[32] = "";

// Motor Ayarları
int turnsPerDay = 600;
float turnDuration = 15.0; // Titremeyi önlemek için 15.0
int direction = 1; // 1: Saat yönü, 2: Saat yönü ters, 3: İkisi
bool running = false;
int completedTurns = 0;
unsigned long lastHourTime = 0;
int hourlyTurns = turnsPerDay / 24;
static int currentStep = 0;
static unsigned long lastStepTime = 0;
static bool forward = true;

// Pin Tanımları
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
  ElegantOTA.onStart([]() {
    running = false;
    stopMotor();
    Serial.println("OTA başlatıldı, motor durduruldu");
  });
}

void loop() {
  static unsigned long lastWebSocketTime = 0;
  static unsigned long lastServerTime = 0;
  static unsigned long lastOTATime = 0;
  unsigned long currentTime = millis();

  if (currentTime - lastWebSocketTime >= 50) {
    webSocket.loop();
    lastWebSocketTime = currentTime;
  }

  if (currentTime - lastServerTime >= 50) {
    server.handleClient();
    lastServerTime = currentTime;
  }

  if (currentTime - lastOTATime >= 100) {
    ElegantOTA.loop();
    lastOTATime = currentTime;
  }

  if (running) {
    runMotor();
  }

  checkHourlyReset();
}

void stepMotor(int step) {
  digitalWrite(motorPin1, step == 0 ? HIGH : LOW);
  digitalWrite(motorPin2, step == 1 ? HIGH : LOW);
  digitalWrite(motorPin3, step == 2 ? HIGH : LOW);
  digitalWrite(motorPin4, step == 3 ? HIGH : LOW);
}

void stopMotor() {
  digitalWrite(motorPin1, LOW);
  digitalWrite(motorPin2, LOW);
  digitalWrite(motorPin3, LOW);
  digitalWrite(motorPin4, LOW);
}

void runMotor() {
  const float stepDelay = 5.0; // Sabit 5ms adım gecikmesi
  if (running && millis() - lastStepTime >= stepDelay) {
    Serial.println("runMotor: Adım: " + String(currentStep) + ", Running: " + String(running) + ", Direction: " + String(direction) + ", turnDuration: " + String(turnDuration));
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
      StaticJsonDocument<256> doc;
      doc["motorStatus"] = "Motor çalışıyor, tur: " + String(completedTurns);
      String json;
      serializeJson(doc, json);
      webSocket.broadcastTXT(json);
    }
  } else if (!running) {
    Serial.println("runMotor: Motor durduruldu, running: false");
  }
}

void readSettings() {
  turnsPerDay = EEPROM.readInt(0);
  turnDuration = EEPROM.readFloat(4);
  direction = EEPROM.read(8);
  if (turnsPerDay < 600 || turnsPerDay > 1200) turnsPerDay = 600;
  if (turnDuration < 10.0 || turnDuration > 15.0) turnDuration = 15.0;
  if (direction < 1 || direction > 3) direction = 1;
  hourlyTurns = turnsPerDay / 24;
  EEPROM.get(100, ssid);
  EEPROM.get(132, password);
  EEPROM.get(196, custom_name);
  if (strlen(ssid) == 0) strcpy(ssid, default_ssid);
  if (strlen(password) == 0) strcpy(password, default_password);
  Serial.println("Ayarlar okundu: TPD=" + String(turnsPerDay) + ", Duration=" + String(turnDuration) + ", Dir=" + String(direction));
}

void writeMotorSettings() {
  EEPROM.writeInt(0, turnsPerDay);
  EEPROM.writeFloat(4, turnDuration);
  EEPROM.write(8, direction);
  EEPROM.commit();
  Serial.println("Motor ayarları kaydedildi: TPD=" + String(turnsPerDay) + ", Duration=" + String(turnDuration) + ", Dir=" + String(direction));
}

void writeWiFiSettings() {
  EEPROM.put(100, ssid);
  EEPROM.put(132, password);
  EEPROM.put(196, custom_name);
  EEPROM.commit();
  Serial.println("WiFi ayarları kaydedildi: SSID=" + String(ssid) + ", Custom Name=" + String(custom_name));
}

void setupWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(default_ssid, default_password);
  Serial.println("AP başlatıldı: " + String(default_ssid));
  if (strlen(ssid) > 0 && strlen(password) > 0) {
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi’ye bağlanıldı: " + String(ssid) + ", IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("\nWiFi bağlantısı başarısız, AP modunda devam ediliyor.");
    }
  }
}

void setupMDNS() {
  strcpy(mDNS_hostname, "horus");
  if (strlen(custom_name) > 0) {
    strncat(mDNS_hostname, custom_name, sizeof(mDNS_hostname) - strlen(mDNS_hostname) - 1);
  } else {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    strncat(mDNS_hostname, mac.c_str() + 8, sizeof(mDNS_hostname) - strlen(mDNS_hostname) - 1);
  }
  if (MDNS.begin(mDNS_hostname)) {
    Serial.println("mDNS başlatıldı: " + String(mDNS_hostname) + ".local");
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", htmlPage()); });
  server.on("/set", HTTP_GET, handleSet);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save_wifi", HTTP_POST, handleSaveWiFi);
  server.on("/check_update", HTTP_GET, []() { checkOTAUpdate(); server.send(200, "text/plain", "OK"); });
  server.begin();
  Serial.println("Web sunucusu başlatıldı");
}

String escapeHtmlString(String input) {
  input.replace("&", "&amp;");
  input.replace("<", "&lt;");
  input.replace(">", "&gt;");
  input.replace("\"", "&quot;");
  input.replace("'", "&#39;");
  return input;
}

void handleSet() {
  if (server.hasArg("tpd")) turnsPerDay = server.arg("tpd").toInt();
  if (server.hasArg("duration")) turnDuration = server.arg("duration").toFloat();
  if (server.hasArg("dir")) direction = server.arg("dir").toInt();
  if (turnsPerDay < 600 || turnsPerDay > 1200) turnsPerDay = 600;
  if (turnDuration < 10.0 || turnDuration > 15.0) turnDuration = 15.0;
  if (direction < 1 || direction > 3) direction = 1;
  hourlyTurns = turnsPerDay / 24;
  writeMotorSettings();
  if (server.hasArg("action")) {
    if (server.arg("action") == "start") {
      running = true;
      Serial.println("handleSet: running=true");
    } else if (server.arg("action") == "stop") {
      running = false;
      stopMotor();
      Serial.println("handleSet: running=false");
    } else if (server.arg("action") == "reset") {
      resetMotor();
    }
  }
  updateWebSocket();
  server.send(200, "text/plain", "OK");
}

void handleScan() {
  String options = scanWiFiNetworks();
  server.send(200, "text/plain", options);
}

void handleSaveWiFi() {
  if (server.hasArg("ssid")) strncpy(ssid, server.arg("ssid").c_str(), sizeof(ssid));
  if (server.hasArg("password")) strncpy(password, server.arg("password").c_str(), sizeof(password));
  if (server.hasArg("name")) strncpy(custom_name, server.arg("name").c_str(), sizeof(custom_name));
  writeWiFiSettings();
  server.send(200, "text/plain", "OK");
  Serial.println("WiFi ayarları kaydedildi, yeniden başlatılıyor...");
  ESP.restart();
}

String scanWiFiNetworks() {
  String options = "";
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    options += "<option value=\"" + escapeHtmlString(ssid) + "\">" + escapeHtmlString(ssid) + " (RSSI: " + WiFi.RSSI(i) + " dBm)</option>";
  }
  Serial.println("WiFi tarama tamamlandı: " + String(n) + " ağ bulundu");
  return options;
}

void resetMotor() {
  running = false;
  stopMotor();
  turnsPerDay = 600;
  turnDuration = 15.0;
  direction = 1;
  completedTurns = 0;
  hourlyTurns = turnsPerDay / 24;
  currentStep = 0;
  lastStepTime = millis();
  writeMotorSettings();
  updateWebSocket();
  Serial.println("Motor ayarları sıfırlandı: TPD=600, Duration=15.0, Dir=1, Running=false");
}

void checkHourlyReset() {
  unsigned long currentTime = millis();
  if (currentTime - lastHourTime >= 3600000) { // 1 saat
    if (completedTurns >= hourlyTurns) {
      running = false;
      stopMotor();
      completedTurns = 0;
    }
    lastHourTime = currentTime;
    updateWebSocket();
  }
}

void checkOTAUpdate() {
  Serial.println("checkOTAUpdate başladı, running: " + String(running));
  unsigned long startTime = millis();
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(github_url);
  if (github_token && strlen(github_token) > 0) {
    http.addHeader("Authorization", String("token ") + github_token);
  } else {
    Serial.println("GitHub token eksik, anonim istek gönderiliyor.");
  }
  http.addHeader("Accept", "application/vnd.github.v3+json");
  int httpCode = http.GET();
  Serial.println("checkOTAUpdate HTTP isteği tamamlandı, süre: " + String(millis() - startTime) + "ms, httpCode: " + String(httpCode));
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
        statusDoc["otaStatus"] = "Yeni sürüm mevcut: " + latestVersion;
      } else {
        statusDoc["otaStatus"] = "Firmware güncel: " + currentVersion;
      }
    } else {
      statusDoc["otaStatus"] = "OTA kontrol hatası: JSON parse hatası - " + String(error.c_str());
      Serial.println("JSON parse hatası: " + String(error.c_str()));
    }
  } else {
    statusDoc["otaStatus"] = "OTA kontrol hatası: HTTP " + String(httpCode);
    Serial.println("HTTP hatası: " + String(httpCode));
    if (httpCode == 401) {
      Serial.println("Yetkilendirme hatası: GitHub token’ı kontrol edin.");
    } else if (httpCode == 403) {
      Serial.println("API rate limit aşılmış olabilir, token gerekli veya rate limit beklenmeli.");
    }
  }
  String json;
  serializeJson(statusDoc, json);
  webSocket.broadcastTXT(json);
  Serial.println("checkOTAUpdate tamamlandı, running: " + String(running));
  if (running) {
    Serial.println("Motor durumu korunuyor, yeniden başlatılıyor...");
    stopMotor();
    currentStep = 0;
    lastStepTime = millis();
  }
  http.end();
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
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
  Serial.println("WebSocket güncellendi: " + json);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket istemcisi [%u] bağlantısı kesildi\n", num);
      break;
    case WStype_CONNECTED:
      Serial.printf("WebSocket istemcisi [%u] bağlandı\n", num);
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
    <title>Horus Kontrol</title>
    <script src="https://cdn.tailwindcss.com"></script>
  </head>
  <body class="bg-gray-100 dark:bg-gray-900 text-gray-900 dark:text-gray-100 min-h-screen flex flex-col items-center justify-center p-4">
    <div class="bg-white dark:bg-gray-800 p-6 rounded-lg shadow-lg w-full max-w-md">
      <h1 class="text-2xl font-bold mb-4 text-center">Horus Kontrol Paneli</h1>
      <p class="text-center">Sürüm: <span id="version">-</span></p>
      <p class="text-center">Durum: <span id="status">Durduruldu</span> <i id="motor_spinner" class="hidden animate-spin">⥁</i></p>
      <p id="motor_status" class="text-center"></p>
      <p class="text-center">Tamamlanan Turlar: <span id="completedTurns">0</span></p>
      <p class="text-center">Saatlik Turlar: <span id="hourlyTurns">0</span></p>
      <div class="mb-4">
        <label class="block text-sm font-medium">Günlük Tur Sayısı: <span id="tpd_val">600</span></label>
        <input type="range" id="tpd" min="600" max="1200" value="600" oninput="tpd_val.innerText=this.value" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">
      </div>
      <div class="mb-4">
        <label class="block text-sm font-medium">Tur Süresi (s): <span id="duration_val">15.0</span></label>
        <input type="range" id="duration" min="10" max="15" step="0.1" value="15.0" oninput="duration_val.innerText=this.value" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">
      </div>
      <div class="mb-4">
        <label class="block text-sm font-medium">Dönüş Yönü</label>
        <select id="dir" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600">
          <option value="1">Saat Yönü</option>
          <option value="2">Saat Yönü Ters</option>
          <option value="3">İkisi</option>
        </select>
      </div>
      <div class="flex justify-center space-x-2 mb-4">
        <button onclick="sendCommand('start')" class="bg-blue-500 hover:bg-blue-600 text-white font-bold py-2 px-4 rounded">Başlat</button>
        <button onclick="sendCommand('stop')" class="bg-red-500 hover:bg-red-600 text-white font-bold py-2 px-4 rounded">Durdur</button>
        <button onclick="sendCommand('reset')" class="bg-gray-500 hover:bg-gray-600 text-white font-bold py-2 px-4 rounded">Motor Ayarlarını Sıfırla</button>
      </div>
      <div class="mb-4">
        <label class="block text-sm font-medium">WiFi Ağı</label>
        <select id="ssid" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600"></select>
      </div>
      <div class="mb-4">
        <label class="block text-sm font-medium">WiFi Şifresi</label>
        <input type="password" id="wifi_password" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600">
      </div>
      <div class="mb-4">
        <label class="block text-sm font-medium">Cihaz Adı</label>
        <input type="text" id="name" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600">
      </div>
      <div class="flex justify-center space-x-2 mb-4">
        <button onclick="scanWiFi()" class="bg-green-500 hover:bg-green-600 text-white font-bold py-2 px-4 rounded">Ağları Tara</button>
        <button onclick="saveWiFi()" class="bg-purple-500 hover:bg-purple-600 text-white font-bold py-2 px-4 rounded">WiFi Kaydet</button>
      </div>
      <div class="flex justify-center">
        <button onclick="checkUpdate()" class="bg-yellow-500 hover:bg-yellow-600 text-white font-bold py-2 px-4 rounded">Güncellemeleri Kontrol Et</button>
      </div>
      <p id="ota_status" class="text-center mt-4"></p>
    </div>
    <script>
      let ws = new WebSocket('ws://' + window.location.hostname + ':81/');
      ws.onmessage = function(event) {
        console.log('WebSocket mesaj alındı: ' + event.data);
        let data = JSON.parse(event.data);
        if (data.firmwareVersion) document.getElementById('version').innerText = data.firmwareVersion;
        if (data.status) {
          document.getElementById('status').innerText = data.status;
          document.getElementById('motor_spinner').classList.toggle('hidden', data.status !== 'Çalışıyor');
        }
        if (data.completedTurns != null) document.getElementById('completedTurns').innerText = data.completedTurns;
        if (data.hourlyTurns) document.getElementById('hourlyTurns').innerText = data.hourlyTurns;
        if (data.turnsPerDay) {
          document.getElementById('tpd').value = data.turnsPerDay;
          document.getElementById('tpd_val').innerText = data.turnsPerDay;
        }
        if (data.turnDuration) {
          document.getElementById('duration').value = data.turnDuration;
          document.getElementById('duration_val').innerText = data.turnDuration;
        }
        if (data.direction) document.getElementById('dir').value = data.direction;
        if (data.otaStatus) document.getElementById('ota_status').innerText = data.otaStatus;
        if (data.motorStatus) document.getElementById('motor_status').innerText = data.motorStatus;
      };
      function sendCommand(action) {
        let tpd = document.getElementById('tpd').value;
        let duration = document.getElementById('duration').value;
        let dir = document.getElementById('dir').value;
        fetch(`/set?tpd=${tpd}&duration=${duration}&dir=${dir}&action=${action}`)
          .then(response => response.text())
          .then(data => console.log(data))
          .catch(error => console.error('Hata:', error));
      }
      function scanWiFi() {
        fetch('/scan')
          .then(response => response.text())
          .then(data => {
            document.getElementById('ssid').innerHTML = data;
            console.log('WiFi seçenekleri yüklendi');
          })
          .catch(error => console.error('Hata:', error));
      }
      function saveWiFi() {
        let ssid = document.getElementById('ssid').value;
        let password = document.getElementById('wifi_password').value;
        let name = document.getElementById('name').value;
        fetch('/save_wifi', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}&name=${encodeURIComponent(name)}`
        })
          .then(response => response.text())
          .then(data => {
            console.log(data);
            alert('WiFi ayarları kaydedildi! Cihaz yeniden başlatılacak.');
          })
          .catch(error => console.error('Hata:', error));
      }
      function checkUpdate() {
        fetch('/check_update')
          .then(response => response.text())
          .then(data => console.log(data))
          .catch(error => console.error('Hata:', error));
      }
    </script>
  </body>
  </html>
  )rawliteral";
  return page;
}
