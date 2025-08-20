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
const char* FIRMWARE_VERSION = "v1.0.20"; // Updated firmware version

// WiFi Settings
const char* default_ssid = "HorusAP";
const char* default_password = "12345678";
char ssid[32] = "";
char password[64] = "";
char custom_name[21] = "";
char mDNS_hostname[32] = "";

// Motor Settings
int turnsPerDay = 600;
float turnDuration = 15.0; // Updated for stability
int direction = 1;
bool running = false;
int completedTurns = 0;
unsigned long lastHourTime = 0;
int hourlyTurns = turnsPerDay / 24;
static int currentStep = 0;
static unsigned long lastStepTime = 0;
static bool forward = true;

// Pin Definitions
const int motorPin1 = 26;
const int motorPin2 = 27;
const int motorPin3 = 14;
const int motorPin4 = 12;
const int stepsPerRevolution = 2048;

// Global Objects
WebServer server(80);
WebSocketsServer webSocket(81);

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
void handleCheckOTA();
void stopMotor();
void runMotor();
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
    running = false;
    stopMotor();
    Serial.println("OTA started, motor stopped.");
  });
  ElegantOTA.onEnd([](bool success) {
    if (success) {
      Serial.println("OTA update completed successfully!");
    } else {
      Serial.println("OTA update failed!");
      // The line below was removed because ElegantOTA.get is not supported in newer versions.
      // Serial.printf("Error Code: %u\n", ElegantOTA.get -> _error); 
    }
  });

  // Uncomment to enable progress reporting in Serial Monitor
  // ElegantOTA.onProgress([](size_t current, size_t final) {
  //   Serial.printf("Progress: %u%%\n", (current * 100) / final);
  // });
}

void loop() {
  server.handleClient();
  webSocket.loop();
  ElegantOTA.loop(); // ElegantOTA must be in the main loop to handle the upload
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
  const float stepDelay = 5.0; // Fixed 5ms step delay
  if (running && millis() - lastStepTime >= stepDelay) {
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
  
  // Default values
  if (turnsPerDay < 600 || turnsPerDay > 1200) turnsPerDay = 600;
  if (turnDuration < 10.0 || turnDuration > 15.0) turnDuration = 15.0;
  if (direction < 1 || direction > 3) direction = 1;
  hourlyTurns = turnsPerDay / 24;
  
  Serial.println("Settings read: TPD=" + String(turnsPerDay) + ", Duration=" + String(turnDuration) + ", Dir=" + String(direction));
}

void writeMotorSettings() {
  int address = sizeof(ssid) + sizeof(password) + sizeof(custom_name);
  EEPROM.put(address, turnsPerDay);
  address += sizeof(turnsPerDay);
  EEPROM.put(address, turnDuration);
  address += sizeof(turnDuration);
  EEPROM.put(address, direction);
  EEPROM.commit();
  Serial.println("Motor settings saved: TPD=" + String(turnsPerDay) + ", Duration=" + String(turnDuration) + ", Dir=" + String(direction));
}

void writeWiFiSettings() {
  int address = 0;
  EEPROM.put(address, ssid);
  address += sizeof(ssid);
  EEPROM.put(address, password);
  address += sizeof(password);
  EEPROM.put(address, custom_name);
  EEPROM.commit();
  Serial.println("WiFi settings saved: SSID=" + String(ssid) + ", Custom Name=" + String(custom_name));
}

void setupWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(default_ssid, default_password);
  Serial.println("AP started: " + String(default_ssid));
  if (strlen(ssid) > 0 && strlen(password) > 0) {
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to WiFi: " + String(ssid) + ", IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("\nWiFi connection failed, continuing in AP mode.");
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
    Serial.println("mDNS started: " + String(mDNS_hostname) + ".local");
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
      4096, // Stack size
      NULL,
      1, // Priority
      NULL);
    server.send(200, "text/plain", "OTA check started.");
  });
  server.begin();
  Serial.println("Web server started.");
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
  String options = "";
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    String ssid_scan = WiFi.SSID(i);
    options += "<option value=\"" + ssid_scan + "\">" + ssid_scan + " (RSSI: " + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  Serial.println("WiFi scan completed: " + String(n) + " networks found.");
  server.send(200, "text/plain", options);
}

void handleSaveWiFi() {
  if (server.hasArg("ssid")) strncpy(ssid, server.arg("ssid").c_str(), sizeof(ssid));
  if (server.hasArg("password")) strncpy(password, server.arg("password").c_str(), sizeof(password));
  if (server.hasArg("name")) strncpy(custom_name, server.arg("name").c_str(), sizeof(custom_name));
  writeWiFiSettings();
  server.send(200, "text/plain", "OK");
  Serial.println("WiFi settings saved, restarting...");
  ESP.restart();
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
  Serial.println("Motor settings reset: TPD=600, Duration=15.0, Dir=1, Running=false");
}

void checkHourlyReset() {
  unsigned long currentTime = millis();
  if (currentTime - lastHourTime >= 3600000) { // 1 hour (3,600,000 ms)
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
  Serial.println("checkOTAUpdateTask started.");
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(github_url);
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

      if (isNewVersionAvailable(latestVersion, currentVersion)) {
        statusDoc["otaStatus"] = "Yeni sürüm mevcut: " + latestVersion;
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
  Serial.println("checkOTAUpdateTask finished.");
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
  Serial.println("WebSocket updated: " + json);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket client [%u] disconnected\n", num);
      break;
    case WStype_CONNECTED:
      Serial.printf("WebSocket client [%u] connected\n", num);
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
            <div class="flex justify-center space-x-2">
                <button onclick="checkUpdate()" class="bg-yellow-500 hover:bg-yellow-600 text-white font-bold py-2 px-4 rounded transition-colors duration-200">Güncellemeleri Kontrol Et</button>
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
                if (data.status) {
                    document.getElementById('status').innerText = data.status;
                    document.getElementById('motor_spinner').classList.toggle('hidden', data.status !== 'Çalışıyor');
                }
                if (data.completedTurns != null) document.getElementById('completedTurns').innerText = data.completedTurns;
                if (data.hourlyTurns != null) document.getElementById('hourlyTurns').innerText = data.hourlyTurns;
                if (data.turnsPerDay != null) {
                    document.getElementById('tpd').value = data.turnsPerDay;
                    document.getElementById('tpd_val').innerText = data.turnsPerDay;
                }
                if (data.turnDuration != null) {
                    document.getElementById('duration').value = data.turnDuration;
                    document.getElementById('duration_val').innerText = data.turnDuration;
                }
                if (data.direction != null) document.getElementById('dir').value = data.direction;
                if (data.firmwareVersion) document.getElementById('version').innerText = data.firmwareVersion;
                if (data.otaStatus) {
                    document.getElementById('ota_status').innerText = data.otaStatus;
                    const otaStatusElement = document.getElementById('ota_status');
                    if (data.otaStatus.includes("güncel")) {
                        otaStatusElement.style.color = 'green';
                    } else if (data.otaStatus.includes("Yeni sürüm")) {
                        otaStatusElement.style.color = 'orange';
                    } else {
                        otaStatusElement.style.color = 'red';
                    }
                }
                if (data.customName != null) document.getElementById('deviceName').innerText = data.customName;
                if (data.currentSSID != null) document.getElementById('currentSSID').innerText = data.currentSSID;
                if (data.connectionStatus != null) document.getElementById('connectionStatus').innerText = data.connectionStatus;
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
                    showMessage(`Command sent: ${action}`);
                })
                .catch(error => {
                    console.error('Error:', error);
                    showMessage('Error sending command.', 'error');
                });
        }
        
        function scanWiFi() {
            fetch('/scan')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('ssid').innerHTML = data;
                    console.log('WiFi options loaded');
                    showMessage('WiFi networks scanned.');
                })
                .catch(error => {
                    console.error('Error:', error);
                    showMessage('WiFi scan error.', 'error');
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
                showMessage('WiFi settings saved! Device restarting.', 'info');
            })
            .catch(error => {
                console.error('Error:', error);
                showMessage('Error saving WiFi settings.', 'error');
            });
        }

        function checkUpdate() {
            document.getElementById('ota_status').innerText = "Checking for updates...";
            document.getElementById('ota_status').style.color = 'black';
            fetch('/check_update')
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                })
                .catch(error => {
                    console.error('Error:', error);
                    showMessage('Could not start update check.', 'error');
                });
        }
        
        // Open the motor tab by default on load
        window.onload = function() {
            openTab('motor');
        }
    </script>
</body>
</html>
)rawliteral";
  return page;
}
