#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <WebSocketsServer.h>
#include <ElegantOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <EEPROM.h>

// OTA Settings
const char* github_url = "https://api.github.com/repos/recaner35/WyntroHorus2/releases/latest";
const char* FIRMWARE_VERSION = "v1.0.22"; // Güncellenen firmware sürümü

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
static int currentStep = 0;
static unsigned long lastStepTime = 0;
static bool forward = true;
float calculatedStepDelay = 0; // Motor adım gecikmesini hesaplamak için değişken

// Pin Definitions
const int motorPin1 = 26;
const int motorPin2 = 27;
const int motorPin3 = 14;
const int motorPin4 = 12;
const int stepsPerRevolution = 2048;

// Global Objects
WebServer server(80);
WebSocketsServer webSocket(81);

// Motor kontrolü için görev kolu (task handle)
TaskHandle_t motorTaskHandle = NULL;

// Fonksiyon prototipleri
void readSettings();
void writeMotorSettings();
void writeWiFiSettings();
void setupWiFi();
void setupMDNS();
void setupWebServer();
void handleSet();
void handleScan();
void handleSaveWiFi();
void stopMotor();
void startMotor();
void runMotorTask(void *parameter); // Yeni motor kontrol görevi
void stepMotor(int step);
void checkHourlyReset();
void resetMotor();
void updateWebSocket();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
String htmlPage();
bool isNewVersionAvailable(String latest, String current);
void checkOTAUpdateTask(void *parameter);

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

  // OTA Event Handling for better debugging
  ElegantOTA.onStart([]() {
    stopMotor();
    Serial.println("OTA started, motor stopped.");
  });
  ElegantOTA.onEnd([](bool success) {
    if (success) {
      Serial.println("OTA update completed successfully!");
    } else {
      Serial.println("OTA update failed!");
    }
  });

  // Başlangıçta motor görevini oluştur
  xTaskCreatePinnedToCore(
      runMotorTask,
      "MotorTask",
      2048, // Stack size
      NULL,
      1,    // Priority
      &motorTaskHandle,
      0);   // Core 0'da çalıştır
}

void loop() {
  server.handleClient();
  webSocket.loop();
  ElegantOTA.loop();
  checkHourlyReset();
}

void stepMotor(int step) {
  // Motor adımı
  digitalWrite(motorPin1, step == 0 ? HIGH : LOW);
  digitalWrite(motorPin2, step == 1 ? HIGH : LOW);
  digitalWrite(motorPin3, step == 2 ? HIGH : LOW);
  digitalWrite(motorPin4, step == 3 ? HIGH : LOW);
}

void stopMotor() {
  // Motoru durdurmak için görevden çık
  running = false;
  vTaskSuspend(motorTaskHandle);
  digitalWrite(motorPin1, LOW);
  digitalWrite(motorPin2, LOW);
  digitalWrite(motorPin3, LOW);
  digitalWrite(motorPin4, LOW);
  Serial.println("Motor durduruldu.");
}

void startMotor() {
  // Motoru başlatmak için görevi devam ettir
  running = true;
  vTaskResume(motorTaskHandle);
  Serial.println("Motor başlatıldı.");
}

void runMotorTask(void *parameter) {
  for (;;) {
    if (running) {
      if (direction == 1 || (direction == 3 && forward)) {
        stepMotor(currentStep % 4);
      } else {
        stepMotor(3 - (currentStep % 4));
      }
      currentStep++;
      if (currentStep >= stepsPerRevolution) {
        currentStep = 0;
        completedTurns++;
        if (direction == 3) forward = !forward;
        updateWebSocket();
        Serial.printf("Turn completed. Total turns: %d\n", completedTurns);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(calculatedStepDelay));
  }
}

void readSettings() {
  EEPROM.begin(512);
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
  
  if (turnsPerDay < 600 || turnsPerDay > 1200) turnsPerDay = 600;
  if (turnDuration < 10.0 || turnDuration > 15.0) turnDuration = 15.0;
  if (direction < 1 || direction > 3) direction = 1;
  hourlyTurns = turnsPerDay / 24;
  
  calculatedStepDelay = (turnDuration * 1000.0) / stepsPerRevolution;
  Serial.printf("Ayarlar okundu: TPD=%d, Süre=%.2f, Yön=%d, Adım Gecikmesi=%.2f ms\n", turnsPerDay, turnDuration, direction, calculatedStepDelay);
}

void writeMotorSettings() {
  int address = sizeof(ssid) + sizeof(password) + sizeof(custom_name);
  EEPROM.put(address, turnsPerDay);
  address += sizeof(turnsPerDay);
  EEPROM.put(address, turnDuration);
  address += sizeof(turnDuration);
  EEPROM.put(address, direction);
  EEPROM.commit();
  Serial.printf("Motor ayarları kaydedildi: TPD=%d, Süre=%.2f, Yön=%d\n", turnsPerDay, turnDuration, direction);
}

void writeWiFiSettings() {
  int address = 0;
  EEPROM.put(address, ssid);
  address += sizeof(ssid);
  EEPROM.put(address, password);
  address += sizeof(password);
  EEPROM.put(address, custom_name);
  EEPROM.commit();
  Serial.println("WiFi ayarları kaydedildi, yeniden başlatılıyor...");
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
      Serial.println("\nWiFi'ye bağlandı: " + String(ssid) + ", IP: " + WiFi.localIP().toString());
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
  server.on("/check_update", HTTP_GET, []() {
    xTaskCreate(
      checkOTAUpdateTask,
      "CheckOTAUpdateTask",
      4096,
      NULL,
      1,
      NULL);
    server.send(200, "text/plain", "OTA kontrolü başlatıldı.");
  });
  server.begin();
  Serial.println("Web sunucusu başlatıldı.");
}

void handleSet() {
  if (server.hasArg("tpd")) turnsPerDay = server.arg("tpd").toInt();
  if (server.hasArg("duration")) turnDuration = server.arg("duration").toFloat();
  if (server.hasArg("dir")) direction = server.arg("dir").toInt();
  if (turnsPerDay < 600 || turnsPerDay > 1200) turnsPerDay = 600;
  if (turnDuration < 10.0 || turnDuration > 15.0) turnDuration = 15.0;
  if (direction < 1 || direction > 3) direction = 1;
  hourlyTurns = turnsPerDay / 24;
  
  calculatedStepDelay = (turnDuration * 1000.0) / stepsPerRevolution;
  Serial.printf("Yeni ayarlar: TPD=%d, Süre=%.2f, Yön=%d, Adım Gecikmesi=%.2f ms\n", turnsPerDay, turnDuration, direction, calculatedStepDelay);

  writeMotorSettings();
  if (server.hasArg("action")) {
    if (server.arg("action") == "start") {
      startMotor();
    } else if (server.arg("action") == "stop") {
      stopMotor();
    } else if (server.arg("action") == "reset") {
      resetMotor();
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
    options += "<option value=\"" + ssid_scan + "\">" + ssid_scan + " (RSSI: " + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  Serial.println("WiFi taraması tamamlandı: " + String(n) + " ağ bulundu.");
  server.send(200, "text/plain", options);
}

void handleSaveWiFi() {
  if (server.hasArg("ssid")) strncpy(ssid, server.arg("ssid").c_str(), sizeof(ssid));
  if (server.hasArg("password")) strncpy(password, server.arg("password").c_str(), sizeof(password));
  if (server.hasArg("name")) strncpy(custom_name, server.arg("name").c_str(), sizeof(custom_name));
  writeWiFiSettings();
  server.send(200, "text/plain", "OK");
  Serial.println("WiFi ayarları kaydedildi, cihaz yeniden başlatılıyor...");
  ESP.restart();
}

void resetMotor() {
  stopMotor();
  turnsPerDay = 600;
  turnDuration = 15.0;
  direction = 1;
  completedTurns = 0;
  hourlyTurns = turnsPerDay / 24;
  currentStep = 0;
  lastStepTime = millis();
  
  calculatedStepDelay = (turnDuration * 1000.0) / stepsPerRevolution;
  Serial.printf("Motor ayarları sıfırlandı: TPD=%d, Süre=%.2f, Yön=%d, Adım Gecikmesi=%.2f ms\n", turnsPerDay, turnDuration, direction, calculatedStepDelay);

  writeMotorSettings();
  updateWebSocket();
  Serial.println("Motor ayarları sıfırlandı.");
}

void checkHourlyReset() {
  unsigned long currentTime = millis();
  if (currentTime - lastHourTime >= 3600000) { // 1 saat (3,600,000 ms)
    completedTurns = 0;
    lastHourTime = currentTime;
    updateWebSocket();
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
  Serial.println("checkOTAUpdateTask başlatıldı.");
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(github_url);
  http.addHeader("Accept", "application/vnd.github.v3+json");
  int httpCode = http.GET();
  StaticJsonDocument<256> statusDoc;
  
  statusDoc["updateAvailable"] = false;

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      String latestVersion = doc["tag_name"].as<String>();
      String currentVersion = String(FIRMWARE_VERSION);

      if (isNewVersionAvailable(latestVersion, currentVersion)) {
        statusDoc["otaStatus"] = "Yeni sürüm mevcut: " + latestVersion;
        statusDoc["updateAvailable"] = true;
      } else {
        statusDoc["otaStatus"] = "Firmware güncel: " + currentVersion;
      }
    } else {
      statusDoc["otaStatus"] = "OTA kontrol hatası: JSON parse error.";
      Serial.println("JSON parse error: " + String(error.c_str()));
    }
  } else {
    statusDoc["otaStatus"] = "OTA kontrol hatası: HTTP " + String(httpCode);
    Serial.println("HTTP error: " + String(httpCode));
  }
  http.end();
  String json;
  serializeJson(statusDoc, json);
  webSocket.broadcastTXT(json);
  Serial.println("checkOTAUpdateTask tamamlandı.");
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
  doc["currentSSID"] = WiFi.SSID() != "" ? WiFi.SSID() : String(default_ssid);
  doc["connectionStatus"] = WiFi.status() == WL_CONNECTED ? "Bağlandı" : "Hotspot modunda";
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
  Serial.println("WebSocket güncellendi: " + json);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket client [%u] bağlantısı kesildi\n", num);
      break;
    case WStype_CONNECTED:
      Serial.printf("WebSocket client [%u] bağlandı\n", num);
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
    <style>
        @keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
        .animate-spin-slow { animation: spin 2s linear infinite; }
        .hidden { display: none; }
        .tab-content.active { display: block; }
        .tab-content { display: none; }
    </style>
</head>
<body class="bg-gray-100 dark:bg-gray-900 text-gray-900 dark:text-gray-100 min-h-screen flex flex-col items-center justify-center p-4">
    <div class="bg-white dark:bg-gray-800 p-6 rounded-lg shadow-lg w-full max-w-lg">
        <h1 class="text-2xl font-bold mb-4 text-center">Horus Kontrol Paneli</h1>

        <div class="flex justify-center mb-4 space-x-2">
            <button onclick="openTab('motor')" class="tab-button bg-blue-500 hover:bg-blue-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">Motor</button>
            <button onclick="openTab('wifi')" class="tab-button bg-gray-500 hover:bg-gray-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">WiFi</button>
            <button onclick="openTab('about')" class="tab-button bg-gray-500 hover:bg-gray-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">Hakkında</button>
        </div>

        <!-- Motor Tabı -->
        <div id="motor" class="tab-content active space-y-4">
            <p class="text-center">Durum: <span id="status">Durduruldu</span> <span id="motor_spinner" class="hidden animate-spin-slow">🔄</span></p>
            <p class="text-center">Tamamlanan Turlar: <span id="completedTurns">0</span></p>
            <p class="text-center">Saatlik Turlar: <span id="hourlyTurns">0</span></p>

            <div>
                <label class="block text-sm font-medium">Günlük Tur Sayısı: <span id="tpd_val">600</span></label>
                <input type="range" id="tpd" min="600" max="1200" value="600" oninput="tpd_val.innerText=this.value" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">
            </div>
            <div>
                <label class="block text-sm font-medium">Tur Süresi (s): <span id="duration_val">15.0</span></label>
                <input type="range" id="duration" min="10" max="15" step="0.1" value="15.0" oninput="duration_val.innerText=this.value" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">
            </div>
            <div>
                <label class="block text-sm font-medium">Dönüş Yönü</label>
                <select id="dir" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600">
                    <option value="1">Saat Yönü</option>
                    <option value="2">Saat Yönü Ters</option>
                    <option value="3">İkisi</option>
                </select>
            </div>
            
            <div class="flex justify-center space-x-2">
                <button onclick="sendCommand('start')" class="bg-blue-500 hover:bg-blue-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">Başlat</button>
                <button onclick="sendCommand('stop')" class="bg-red-500 hover:bg-red-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">Durdur</button>
                <button onclick="sendCommand('reset')" class="bg-gray-500 hover:bg-gray-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">Ayarları Sıfırla</button>
            </div>
        </div>

        <!-- WiFi Tabı -->
        <div id="wifi" class="tab-content space-y-4">
            <p class="text-center" id="wifi_info">Bağlı: <span id="currentSSID">-</span></p>
            <p class="text-center" id="conn_status">Durum: <span id="connectionStatus">-</span></p>
            <div>
                <label class="block text-sm font-medium">Ağ Adı</label>
                <select id="ssid" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600 text-gray-900 dark:text-gray-100"></select>
            </div>
            <div>
                <label class="block text-sm font-medium">Şifre</label>
                <input type="password" id="wifi_password" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600 text-gray-900 dark:text-gray-100">
            </div>
            <div>
                <label class="block text-sm font-medium">Cihaz Adı</label>
                <input type="text" id="customName" class="w-full p-2 border rounded dark:bg-gray-700 dark:border-gray-600 text-gray-900 dark:text-gray-100">
            </div>

            <div class="flex justify-center space-x-2">
                <button onclick="scanWiFi()" class="bg-green-500 hover:bg-green-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">Ağları Tara</button>
                <button onclick="saveWiFi()" class="bg-purple-500 hover:bg-purple-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">Kaydet & Yeniden Başlat</button>
            </div>
        </div>

        <!-- Hakkında Tabı -->
        <div id="about" class="tab-content space-y-4">
            <p class="text-center">Firmware Sürümü: <span id="version">-</span></p>
            <p class="text-center" id="ota_status"></p>
            <p class="text-center">Cihaz Adı: <span id="deviceName">-</span></p>
            <div class="flex flex-col items-center space-y-2">
                <button id="checkUpdateButton" onclick="checkUpdate()" class="bg-yellow-500 hover:bg-yellow-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">Güncellemeleri Kontrol Et</button>
                <button id="installUpdateButton" onclick="installUpdate()" class="bg-green-500 hover:bg-green-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200 hidden">Güncellemeyi Yükle</button>
            </div>
        </div>

        <p id="message_box" class="text-center mt-4 text-sm font-semibold"></p>
    </div>

    <script>
        let ws = new WebSocket('ws://' + window.location.hostname + ':81/');
        
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
                const dirElement = document.getElementById('dir');
                const versionElement = document.getElementById('version');
                const otaStatusElement = document.getElementById('ota_status');
                const deviceNameElement = document.getElementById('deviceName');
                const currentSSIDElement = document.getElementById('currentSSID');
                const connectionStatusElement = document.getElementById('connectionStatus');
                const checkUpdateButton = document.getElementById('checkUpdateButton');
                const installUpdateButton = document.getElementById('installUpdateButton');

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
                if (data.direction != null) dirElement.value = data.direction;
                if (data.firmwareVersion) versionElement.innerText = data.firmwareVersion;
                
                if (data.otaStatus) {
                    otaStatusElement.innerText = data.otaStatus;
                    if (data.otaStatus.includes("güncel")) {
                        otaStatusElement.style.color = 'green';
                    } else if (data.otaStatus.includes("Yeni sürüm")) {
                        otaStatusElement.style.color = 'orange';
                    } else {
                        otaStatusElement.style.color = 'red';
                    }
                }
                if (data.updateAvailable != null) {
                    if (data.updateAvailable) {
                        checkUpdateButton.classList.add('hidden');
                        installUpdateButton.classList.remove('hidden');
                    } else {
                        checkUpdateButton.classList.remove('hidden');
                        installUpdateButton.classList.add('hidden');
                    }
                }
                if (data.customName != null) deviceNameElement.innerText = data.customName;
                if (data.currentSSID != null) currentSSIDElement.innerText = data.currentSSID;
                if (data.connectionStatus != null) connectionStatusElement.innerText = data.connectionStatus;
            } catch (e) {
                console.error("JSON parse error:", e);
                console.log("Received data:", event.data);
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
        }

        function showMessage(msg, type = 'info') {
            const messageBox = document.getElementById('message_box');
            messageBox.innerText = msg;
            messageBox.style.color = type === 'error' ? 'red' : 'green';
            setTimeout(() => {
                messageBox.innerText = '';
            }, 5000);
        }

        function sendCommand(action) {
            let tpd = document.getElementById('tpd').value;
            let duration = document.getElementById('duration').value;
            let dir = document.getElementById('dir').value;
            fetch(`/set?tpd=${tpd}&duration=${duration}&dir=${dir}&action=${action}`)
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                    showMessage(`Komut gönderildi: ${action}`);
                })
                .catch(error => {
                    console.error('Hata:', error);
                    showMessage('Komut gönderilirken hata oluştu.', 'error');
                });
        }
        
        function scanWiFi() {
            fetch('/scan')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('ssid').innerHTML = data;
                    console.log('WiFi seçenekleri yüklendi.');
                    showMessage('WiFi ağları tarandı.');
                })
                .catch(error => {
                    console.error('Hata:', error);
                    showMessage('WiFi tarama hatası.', 'error');
                });
        }

        function saveWiFi() {
            let ssid = document.getElementById('ssid').value;
            let password = document.getElementById('wifi_password').value;
            let name = document.getElementById('customName').value;
            fetch('/save_wifi', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}&name=${encodeURIComponent(name)}`
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

        function checkUpdate() {
            document.getElementById('ota_status').innerText = "Güncellemeler kontrol ediliyor...";
            document.getElementById('ota_status').style.color = 'black';
            document.getElementById('installUpdateButton').classList.add('hidden');
            fetch('/check_update')
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                })
                .catch(error => {
                    console.error('Hata:', error);
                    showMessage('Güncelleme kontrolü başlatılamadı.', 'error');
                });
        }

        function installUpdate() {
            // Yönlendirme
            window.location.href = "/update";
        }
        
        // Yüklendiğinde motor sekmesini aç
        window.onload = function() {
            openTab('motor');
        }
    </script>
</body>
</html>
)rawliteral";
  return page;
}
