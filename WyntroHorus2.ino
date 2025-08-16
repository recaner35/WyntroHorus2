#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <EEPROM.h>
#include <ElegantOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>

// WiFi ve Ağ Ayarları
String default_ssid; // Dinamik SSID: horus-<MAC-son-4>
String macSuffix;
const char* default_password = "12345678";
IPAddress apIP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);
String mDNS_hostname;
String custom_name;

// OTA Ayarları
#define FIRMWARE_VERSION "1.0.12"
const char* github_url = "https://api.github.com/repos/recaner35/Wyntro/releases/latest";
const char* github_token = "";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// EEPROM Ayarları
#define EEPROM_SIZE 140
#define SSID_ADDR 0
#define PASS_ADDR 32
#define TPD_ADDR 96
#define DURATION_ADDR 100
#define DIR_ADDR 104
#define RUNNING_ADDR 108
#define LAST_HOUR_ADDR 109
#define COMPLETED_TURNS_ADDR 113
#define DEVICE_ID_ADDR 117
#define CUSTOM_NAME_ADDR 120

// Motor Pinleri
const int IN1 = 17;
const int IN2 = 5;
const int IN3 = 18;
const int IN4 = 19;

// 8 adımlı motor dizisi
const int steps[8][4] = {
  {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
  {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1}
};

// Motor Sabitleri
const int stepsPerTurn = 4096;
const int rampSteps = 200;
const float minStepDelay = 2.0;
const float maxStepDelay = 10.0;

int direction = 1;
int turnsPerDay = 600;
float turnDuration = 10.0;
bool running = false;
bool isScanning = false;

int hourlyTurns = 25;
int completedTurns = 0;
int stepsRemaining = 0;
int currentStep = 0;
int stepDir = 1;
bool isTurning = false;

unsigned long lastStepTime = 0;
unsigned long lastHourTime = 0;
unsigned long lastUpdateTime = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastOTACheck = 0;
const unsigned long RECONNECT_INTERVAL = 300000;
const unsigned long OTA_CHECK_INTERVAL = 3600000;

// HTML için güvenli kaçırma fonksiyonu
String escapeHtmlString(const String &input) {
  String escaped = input;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}

// HTML Sayfası Oluşturucu
String htmlPage() {
  String status = running ? "Çalışıyor" : "Durduruldu";
  String tpd = String(turnsPerDay);
  String duration = String(turnDuration, 1);
  String completed = String(completedTurns);
  String hourly = String(hourlyTurns);
  String dir1Checked = (direction == 1) ? "checked" : "";
  String dir2Checked = (direction == 2) ? "checked" : "";
  String dir3Checked = (direction == 3) ? "checked" : "";
  String currentSSID = WiFi.SSID() != "" ? WiFi.SSID() : default_ssid;
  String connectionStatus = (WiFi.status() == WL_CONNECTED) ? "Bağlandı" : "Hotspot modunda";
  String otaStatus = "OTA: Kontrol ediliyor...";
  String currentCustomName = custom_name.length() > 0 ? escapeHtmlString(custom_name) : "Horus";
  float progress = (float)completedTurns / hourlyTurns * 100;
  String spinnerClass = running ? "" : "hidden";

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
    <p class="text-center" id="wifi">Bağlı WiFi: )rawliteral" + escapeHtmlString(currentSSID) + R"rawliteral(</p>
    <p class="text-center" id="connection_status">Bağlantı Durumu: )rawliteral" + connectionStatus + R"rawliteral(</p>
    <p class="text-center" id="ota_status">)rawliteral" + otaStatus + R"rawliteral(</p>
    <form action="/set" method="get" class="space-y-4">
      <div>
        <label class="block text-sm font-medium">Günlük Tur: <span id="tpd_val">)rawliteral" + tpd + R"rawliteral(</span></label>
        <input type="range" name="tpd" min="600" max="1200" step="1" value=")rawliteral" + tpd + R"rawliteral(" oninput="tpd_val.innerText=this.value; validateTpd(this)" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">
        <p class="text-red-500 hidden" id="tpd_error">Günlük tur 600-1200 arasında olmalı.</p>
      </div>
      <div>
        <label class="block text-sm font-medium">Tur Süresi (saniye): <span id="duration_val">)rawliteral" + duration + R"rawliteral(</span></label>
        <input type="range" name="duration" min="10" max="15" step="0.1" value=")rawliteral" + duration + R"rawliteral(" oninput="duration_val.innerText=this.value; validateDuration(this)" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer">
        <p class="text-red-500 hidden" id="duration_error">Tur süresi 10-15 saniye arasında olmalı.</p>
      </div>
      <div>
        <label class="block text-sm font-medium">Dönüş Yönü:</label>
        <div class="flex justify-center space-x-4">
          <label><input type="radio" name="dir" value="1" )rawliteral" + dir1Checked + R"rawliteral(> Saat Yönü</label>
          <label><input type="radio" name="dir" value="2" )rawliteral" + dir2Checked + R"rawliteral(> Saat Yönünün Tersi</label>
          <label><input type="radio" name="dir" value="3" )rawliteral" + dir3Checked + R"rawliteral(> İleri - Geri</label>
        </div>
      </div>
      <div class="flex justify-center space-x-4">
        <button type="submit" name="action" value="start" class="bg-blue-600 dark:bg-blue-500 text-white px-4 py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-play mr-2"></i>Başlat</button>
        <button type="submit" name="action" value="stop" class="bg-red-600 dark:bg-red-500 text-white px-4 py-2 rounded-md hover:bg-red-700 dark:hover:bg-blue-600"><i class="fas fa-stop mr-2"></i>Durdur</button>
      </div>
    </form>
    <button class="collapsible w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md mt-4 hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-cog mr-2"></i>Ayarlar Menüsü</button>
    <div class="content mt-2 space-y-4">
      <h3 class="text-xl font-semibold">Cihaz İsmi Ayarları</h3>
      <form action="/set_name" method="get" class="space-y-2">
        <label class="block text-sm font-medium">Cihaz İsmi (1-20 karakter, sadece harf veya rakam):</label>
        <input type="text" name="custom_name" placeholder=")rawliteral" + escapeHtmlString(currentCustomName) + R"rawliteral(" maxlength="20" class="w-full p-2 border dark:border-gray-600 rounded-md bg-gray-100 dark:bg-gray-700">
        <button type="submit" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-save mr-2"></i>İsmi Kaydet</button>
      </form>
      <form action="/reset_name" method="get">
        <button type="submit" class="w-full bg-gray-600 dark:bg-gray-500 text-white py-2 rounded-md hover:bg-gray-700 dark:hover:bg-gray-600"><i class="fas fa-undo mr-2"></i>Cihaz İsmini Sıfırla</button>
      </form>
      <h3 class="text-xl font-semibold">WiFi Ayarları</h3>
      <button id="scan_wifi_button" onclick="scanWiFi()" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-wifi mr-2"></i>Ağları Tara</button>
      <p id="scan_status" class="text-center"></p>
      <form action="/wifi" method="get" class="space-y-2">
        <label class="block text-sm font-medium">WiFi SSID:</label>
        <select name="ssid" id="wifi_select" class="w-full p-2 border dark:border-gray-600 rounded-md bg-gray-100 dark:bg-gray-700">
          <option value="">Ağ Seçin</option>
        </select>
        <label class="block text-sm font-medium">WiFi Şifre:</label>
        <input type="password" name="pass" placeholder="WiFi Şifresi" class="w-full p-2 border dark:border-gray-600 rounded-md bg-gray-100 dark:bg-gray-700">
        <button type="submit" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-save mr-2"></i>WiFi Kaydet</button>
      </form>
      <h3 class="text-xl font-semibold">Sıfırlama</h3>
      <form action="/reset_wifi" method="get">
        <button type="submit" class="w-full bg-gray-600 dark:bg-gray-500 text-white py-2 rounded-md hover:bg-gray-700 dark:hover:bg-gray-600"><i class="fas fa-undo mr-2"></i>WiFi Ayarlarını Sıfırla</button>
      </form>
      <form action="/reset_motor" method="get">
        <button type="submit" class="w-full bg-gray-600 dark:bg-gray-500 text-white py-2 rounded-md hover:bg-gray-700 dark:hover:bg-gray-600"><i class="fas fa-undo mr-2"></i>Motor Ayarlarını Sıfırla</button>
      </form>
      <h3 class="text-xl font-semibold">OTA Güncelleme</h3>
      <form action="/update" method="get">
        <button type="submit" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-upload mr-2"></i>OTA Güncelleme Sayfası</button>
      </form>
      <form action="/check_ota" method="get">
        <button type="submit" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-sync-alt mr-2"></i>OTA'yı Şimdi Kontrol Et</button>
      </form>
    </div>
    <button class="collapsible w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md mt-4 hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-network-wired mr-2"></i>Diğer Cihazlar</button>
    <div class="content mt-2 space-y-4">
      <h3 class="text-xl font-semibold">Diğer Horus by Wyntro Cihazları</h3>
      <input type="text" id="device_hostname" placeholder="Cihaz hostname (örn: MyWinder2-d99d.local)" class="w-full p-2 border dark:border-gray-600 rounded-md bg-gray-100 dark:bg-gray-700">
      <button onclick="addDevice()" class="w-full bg-blue-600 dark:bg-blue-500 text-white py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-plus mr-2"></i>Cihaz Ekle</button>
      <p id="device_status" class="text-center"></p>
      <div id="device_list"></div>
    </div>
  </div>
  <script>
    document.addEventListener('DOMContentLoaded', function() {
      console.log('DOM tamamen yüklendi.');
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
        console.log('validateTpd: tpd_error elemanı bulundu.');
        if (input.value < 600 || input.value > 1200) {
          error.classList.remove('hidden');
        } else {
          error.classList.add('hidden');
        }
      } else {
        console.error('validateTpd: tpd_error elemanı bulunamadı!');
      }
    }

    function validateDuration(input) {
      const error = document.getElementById('duration_error');
      if (error) {
        console.log('validateDuration: duration_error elemanı bulundu.');
        if (input.value < 10 || input.value > 15) {
          error.classList.remove('hidden');
        } else {
          error.classList.add('hidden');
        }
      } else {
        console.error('validateDuration: duration_error elemanı bulunamadı!');
      }
    }

    let ws;
    function initWebSocket() {
      console.log('WebSocket başlatılıyor: ws://' + window.location.hostname + ':81/');
      ws = new WebSocket('ws://' + window.location.hostname + ':81/');
      ws.onmessage = function(event) {
        console.log('WebSocket mesaj alındı:', event.data);
        const scanStatus = document.getElementById('scan_status');
        if (!scanStatus) {
          console.error('scan_status elemanı bulunamadı!');
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
                motorSpinner.classList.toggle('hidden', data.status !== 'Çalışıyor');
              } else {
                console.error('motor_spinner elemanı bulunamadı!');
              }
            } else {
              console.error('status elemanı bulunamadı!');
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
              console.error('turns veya progress_bar elemanı bulunamadı!');
            }
          }
          if (data.currentSSID) {
            const wifiEl = document.getElementById('wifi');
            if (wifiEl) {
              wifiEl.innerText = 'Bağlı WiFi: ' + data.currentSSID;
            } else {
              console.error('wifi elemanı bulunamadı!');
            }
          }
          if (data.connectionStatus) {
            const connEl = document.getElementById('connection_status');
            if (connEl) {
              connEl.innerText = 'Bağlantı Durumu: ' + data.connectionStatus;
            } else {
              console.error('connection_status elemanı bulunamadı!');
            }
          }
          if (data.wifiOptions) {
            const wifiSelect = document.getElementById('wifi_select');
            if (wifiSelect) {
              wifiSelect.innerHTML = data.wifiOptions;
              scanStatus.innerText = 'Tarama tamamlandı.';
            } else {
              console.error('wifi_select elemanı bulunamadı!');
              scanStatus.innerText = 'Tarama başarısız: WiFi seçim elemanı bulunamadı.';
            }
          }
          if (data.otaStatus) {
            const otaEl = document.getElementById('ota_status');
            if (otaEl) {
              otaEl.innerText = data.otaStatus;
            } else {
              console.error('ota_status elemanı bulunamadı!');
            }
          }
        } catch (error) {
          console.error('WebSocket JSON parse hatası:', error.message);
          scanStatus.innerText = 'Tarama başarısız: Veri işleme hatası - ' + error.message;
        }
      };
      ws.onclose = function() {
        console.log('WebSocket bağlantısı kesildi, 2 saniye sonra yeniden bağlanılıyor...');
        setTimeout(initWebSocket, 2000);
      };
      ws.onerror = function(error) {
        console.error('WebSocket hatası:', error);
        if (scanStatus) {
          scanStatus.innerText = 'WebSocket hatası: Bağlantı sorunu.';
        }
      };
    }

    function scanWiFi() {
      console.log('scanWiFi fonksiyonu çağrıldı.');
      const scanStatus = document.getElementById('scan_status');
      const wifiSelect = document.getElementById('wifi_select');
      if (!scanStatus || !wifiSelect) {
        console.error('scan_status veya wifi_select elemanı bulunamadı!');
        if (scanStatus) {
          scanStatus.innerText = 'Tarama başarısız: Gerekli HTML elemanları eksik.';
        }
        return;
      }
      scanStatus.innerText = 'Tarama yapılıyor, lütfen 10-15 saniye bekleyin...';
      wifiSelect.innerHTML = '<option value="">Ağ Seçin</option>';
      fetch('/scan_wifi', { timeout: 30000 })
        .then(response => {
          if (!response.ok) throw new Error('HTTP hatası: ' + response.status);
          return response.text();
        })
        .then(data => {
          console.log('Fetch verisi:', data);
          if (data === 'OK') {
            scanStatus.innerText = 'Tarama başlatıldı, sonuçlar bekleniyor...';
          } else {
            scanStatus.innerText = 'Tarama başarısız: Sunucudan beklenmeyen yanıt - ' + data;
          }
        })
        .catch(error => {
          console.error('Fetch hatası:', error);
          scanStatus.innerText = 'Tarama başlatılırken hata: ' + error.message;
        });
    }

    let devices = JSON.parse(localStorage.getItem('watchWinderDevices')) || [];

    function saveDevices() {
      localStorage.setItem('watchWinderDevices', JSON.stringify(devices));
    }

    function addDevice() {
      console.log('addDevice fonksiyonu çağrıldı.');
      const hostname = document.getElementById('device_hostname').value.trim();
      const deviceStatus = document.getElementById('device_status');
      if (!deviceStatus) {
        console.error('device_status elemanı bulunamadı!');
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
        deviceStatus.innerText = 'Hata: Geçerli bir hostname girin, kendi cihazınızı ekleyemezsiniz veya cihaz zaten ekli.';
      }
    }

    function removeDevice(hostname) {
      console.log('removeDevice çağrıldı:', hostname);
      devices = devices.filter(device => device !== hostname);
      saveDevices();
      const deviceStatus = document.getElementById('device_status');
      if (deviceStatus) {
        deviceStatus.innerText = 'Cihaz silindi: ' + hostname;
      } else {
        console.error('device_status elemanı bulunamadı!');
      }
      updateDeviceList();
    }

    function fetchDeviceStatus(hostname) {
      console.log('fetchDeviceStatus çağrıldı:', hostname);
      fetch(`http://${hostname}/status`, { mode: 'cors' })
        .then(response => {
          if (!response.ok) throw new Error('Cihaz yanıt vermiyor');
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
      console.log('updateDeviceUI çağrıldı:', hostname, data);
      const deviceDiv = document.getElementById(`device_${hostname}`);
      if (!deviceDiv) {
        console.error(`device_${hostname} elemanı bulunamadı!`);
        return;
      }
      let html = `<h3 class="device-header text-lg font-semibold bg-blue-600 dark:bg-blue-500 text-white p-2 rounded-md cursor-pointer">${hostname}</h3>`;
      html += `<div class="device-content p-4 bg-gray-100 dark:bg-gray-700 rounded-md mt-2" id="content_${hostname}">`;
      html += `<button class="w-full bg-red-600 dark:bg-red-500 text-white py-2 rounded-md hover:bg-red-700 dark:hover:bg-red-600 mb-2" onclick="removeDevice('${hostname}')"><i class="fas fa-trash mr-2"></i>Sil</button>`;
      if (data.error) {
        html += `<p class="text-red-500">Hata: ${data.error}</p>`;
      } else {
        html += `<p class="font-semibold">Durum: ${data.status} <i class="fas fa-spinner fa-spin ${data.status === 'Çalışıyor' ? '' : 'hidden'}" id="motor_spinner_${hostname}"></i></p>`;
        html += `<p>Tamamlanan Turlar: ${data.completedTurns} / ${data.hourlyTurns}</p>`;
        html += `<p>Bağlı WiFi: ${data.currentSSID}</p>`;
        html += `<p>Bağlantı Durumu: ${data.connectionStatus}</p>`;
        html += `<p>Firmware: ${data.firmwareVersion}</p>`;
        html += `<form id="form_${hostname}" action="http://${hostname}/set" method="get" class="space-y-2">`;
        html += `<label class="block text-sm font-medium">Günlük Tur: <span id="tpd_val_${hostname}">${data.turnsPerDay}</span></label>`;
        html += `<input type="range" name="tpd" min="600" max="1200" step="1" value="${data.turnsPerDay}" oninput="tpd_val_${hostname}.innerText=this.value" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer"><br>`;
        html += `<label class="block text-sm font-medium">Tur Süresi (saniye): <span id="duration_val_${hostname}">${data.turnDuration}</span></label>`;
        html += `<input type="range" name="duration" min="10" max="15" step="0.1" value="${data.turnDuration}" oninput="duration_val_${hostname}.innerText=this.value" class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg cursor-pointer"><br>`;
        html += `<label class="block text-sm font-medium">Dönüş Yönü:</label>`;
        html += `<div class="flex justify-center space-x-4">`;
        html += `<label><input type="radio" name="dir" value="1" ${data.direction == 1 ? 'checked' : ''}> Saat Yönü</label>`;
        html += `<label><input type="radio" name="dir" value="2" ${data.direction == 2 ? 'checked' : ''}> Saat Yönünün Tersi</label>`;
        html += `<label><input type="radio" name="dir" value="3" ${data.direction == 3 ? 'checked' : ''}> İleri - Geri</label>`;
        html += `</div>`;
        html += `<div class="flex justify-center space-x-4">`;
        html += `<button type="submit" name="action" value="start" class="bg-blue-600 dark:bg-blue-500 text-white px-4 py-2 rounded-md hover:bg-blue-700 dark:hover:bg-blue-600"><i class="fas fa-play mr-2"></i>Başlat</button>`;
        html += `<button type="submit" name="action" value="stop" class="bg-red-600 dark:bg-red-500 text-white px-4 py-2 rounded-md hover:bg-red-700 dark:hover:bg-red-600"><i class="fas fa-stop mr-2"></i>Durdur</button>`;
        html += `</div></form>`;
      }
      html += `</div>`;
      deviceDiv.innerHTML = html;

      deviceDiv.querySelector('.device-header').addEventListener('click', function() {
        const content = document.getElementById(`content_${hostname}`);
        if (content) {
          content.classList.toggle('active');
          localStorage.setItem(`device_state_${hostname}`, content.classList.contains('active') ? 'open' : 'closed');
        } else {
          console.error(`content_${hostname} elemanı bulunamadı!`);
        }
      });

      setTimeout(() => {
        if (devices.includes(hostname)) {
          fetchDeviceStatus(hostname);
        }
      }, 5000);
    }

    function updateDeviceList() {
      console.log('updateDeviceList çağrıldı.');
      const deviceList = document.getElementById('device_list');
      if (!deviceList) {
        console.error('device_list elemanı bulunamadı!');
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
              console.error(`content_${hostname} elemanı bulunamadı!`);
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

// Motor Adımını Uygula
void applyStep(int idx) {
  idx = (idx % 8 + 8) % 8;
  digitalWrite(IN1, steps[idx][0]);
  digitalWrite(IN2, steps[idx][1]);
  digitalWrite(IN3, steps[idx][2]);
  digitalWrite(IN4, steps[idx][3]);
}

// Motoru Durdur
void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// Hız rampalama için adım gecikmesini hesapla
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

// EEPROM'dan WiFi bilgilerini oku
void readWiFiCredentials(String &ssid, String &pass) {
  char ssidBuf[33];
  char passBuf[65];
  for (int i = 0; i < 32; i++) {
    ssidBuf[i] = EEPROM.read(SSID_ADDR + i);
  }
  ssidBuf[32] = '\0';
  for (int i = 0; i < 64; i++) {
    passBuf[i] = EEPROM.read(PASS_ADDR + i);
  }
  passBuf[64] = '\0';
  ssid = String(ssidBuf);
  pass = String(passBuf);
  ssid.trim();
  pass.trim();
}

// EEPROM'a WiFi bilgilerini yaz
void writeWiFiCredentials(String ssid, String pass) {
  Serial.println("EEPROM’a yazılan WiFi SSID: " + ssid);
  Serial.println(String("EEPROM’a yazılan WiFi Şifre: ") + (pass.length() > 0 ? "******" : "Boş"));
  for (int i = 0; i < 32; i++) {
    EEPROM.write(SSID_ADDR + i, i < ssid.length() ? ssid[i] : 0);
  }
  for (int i = 0; i < 64; i++) {
    EEPROM.write(PASS_ADDR + i, i < pass.length() ? pass[i] : 0);
  }
  EEPROM.commit();
}

// EEPROM'a kullanıcı ismini yaz
void writeCustomName(String name) {
  Serial.println("EEPROM’a yazılan custom_name: " + name);
  for (int i = 0; i < 20; i++) {
    EEPROM.write(CUSTOM_NAME_ADDR + i, i < name.length() ? name[i] : 0);
  }
  EEPROM.commit();
}

// EEPROM'dan kullanıcı ismini oku
void readCustomName(String &name) {
  char nameBuf[21];
  for (int i = 0; i < 20; i++) {
    nameBuf[i] = EEPROM.read(CUSTOM_NAME_ADDR + i);
  }
  nameBuf[20] = '\0';
  name = String(nameBuf);
  name.trim();
  Serial.print("EEPROM’dan okunan ham custom_name: ");
  for (int i = 0; i < 20; i++) {
    if (nameBuf[i] >= 32 && nameBuf[i] <= 126) {
      Serial.print(nameBuf[i]);
    } else {
      Serial.print("[0x");
      Serial.print(nameBuf[i], HEX);
      Serial.print("]");
    }
  }
  Serial.println();
  Serial.println("custom_name uzunluğu: " + String(name.length()));
  bool isValid = true;
  for (int i = 0; i < name.length(); i++) {
    char c = name[i];
    if (c < 32 || c > 126) {
      isValid = false;
      break;
    }
  }
  if (!isValid || name.length() > 20) {
    Serial.println("Geçersiz custom_name tespit edildi, sıfırlanıyor.");
    name = "";
    writeCustomName(name);
  }
}

// EEPROM'u sıfırla
void clearEEPROM() {
  Serial.println("EEPROM sıfırlanıyor...");
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("EEPROM sıfırlandı.");
}

// EEPROM'dan motor ayarlarını oku
void readMotorSettings() {
  EEPROM.get(TPD_ADDR, turnsPerDay);
  if (turnsPerDay < 600 || turnsPerDay > 1200) turnsPerDay = 600;
  EEPROM.get(DURATION_ADDR, turnDuration);
  if (turnDuration < 10.0 || turnDuration > 15.0) turnDuration = 10.0;
  EEPROM.get(DIR_ADDR, direction);
  if (direction < 1 || direction > 3) direction = 1;
  running = EEPROM.read(RUNNING_ADDR);
  EEPROM.get(LAST_HOUR_ADDR, lastHourTime);
  EEPROM.get(COMPLETED_TURNS_ADDR, completedTurns);
  hourlyTurns = turnsPerDay / 24;
}

// EEPROM'a motor ayarlarını yaz
void writeMotorSettings() {
  EEPROM.put(TPD_ADDR, turnsPerDay);
  EEPROM.put(DURATION_ADDR, turnDuration);
  EEPROM.put(DIR_ADDR, direction);
  EEPROM.write(RUNNING_ADDR, running);
  EEPROM.put(LAST_HOUR_ADDR, lastHourTime);
  EEPROM.put(COMPLETED_TURNS_ADDR, completedTurns);
  EEPROM.commit();
}

// Motor ayarlarını sıfırla
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
  Serial.println("Motor ayarları sıfırlandı: TPD=600, Duration=10.0, Dir=1, Running=false");
}

// WiFi ayarlarını sıfırla
void resetWiFiCredentials() {
  for (int i = 0; i < 96; i++) {
    EEPROM.write(SSID_ADDR + i, 0);
  }
  EEPROM.commit();
}

// Kullanıcı ismini sıfırla
void resetCustomName() {
  custom_name = "";
  writeCustomName(custom_name);
  mDNS_hostname = "horus-" + macSuffix;
  Serial.println("Cihaz ismi sıfırlandı, yeni isim: horus");
  Serial.println("Yeni mDNS Hostname: " + mDNS_hostname + ".local");
  MDNS.end();
  if (!MDNS.begin(mDNS_hostname.c_str())) {
    Serial.println("mDNS yeniden başlatılamadı!");
  } else {
    Serial.println("mDNS yeniden başlatıldı: http://" + mDNS_hostname + ".local");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  }
  WiFi.softAPdisconnect(true);
  WiFi.softAPConfig(apIP, gateway, subnet);
  WiFi.softAP(default_ssid.c_str(), default_password, 11);
  Serial.println("Hotspot güncellendi: " + default_ssid);
}

// JSON stringi için güvenli kaçırma fonksiyonu
String escapeJsonString(const String &input) {
  String escaped = input;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\n", "\\n");
  escaped.replace("\r", "\\r");
  escaped.replace("\t", "\\t");
  return escaped;
}

// WebSocket üzerinden durumu güncelle
void updateWebSocket() {
  StaticJsonDocument<256> doc;
  String currentSSID = WiFi.SSID() != "" ? WiFi.SSID() : default_ssid;
  doc["status"] = running ? "Çalışıyor" : "Durduruldu";
  doc["completedTurns"] = completedTurns;
  doc["hourlyTurns"] = hourlyTurns;
  doc["currentSSID"] = escapeJsonString(currentSSID);
  doc["connectionStatus"] = (WiFi.status() == WL_CONNECTED) ? "Bağlandı" : "Hotspot modunda";
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["turnsPerDay"] = turnsPerDay;
  doc["turnDuration"] = turnDuration;
  doc["direction"] = direction;

  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

// WiFi ağlarını tara
String scanWiFiNetworks() {
  Serial.println("WiFi tarama başlatılıyor...");
  int n = WiFi.scanNetworks();
  Serial.print("Bulunan ağ sayısı: ");
  Serial.println(n);
  String options = "<option value=\"\">Ağ Seçin</option>";
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid != "") {
      Serial.print("Ağ bulundu: ");
      Serial.println(ssid);
      options += "<option value=\"" + escapeHtmlString(ssid) + "\">" + escapeHtmlString(ssid) + "</option>";
    }
  }
  if (n == 0) {
    Serial.println("Hiç ağ bulunamadı!");
    options += "<option value=\"\">Hiç ağ bulunamadı</option>";
  }
  StaticJsonDocument<512> doc;
  doc["wifiOptions"] = options;
  String json;
  serializeJson(doc, json);
  Serial.println("WebSocket’a gönderilen WiFi seçenekleri: " + json);
  webSocket.broadcastTXT(json);
  return options;
}

// OTA güncellemesini kontrol et
void checkOTAUpdate() {
  HTTPClient http;
  http.begin(github_url);
  http.addHeader("Authorization", String("token ") + github_token);
  int httpCode = http.GET();

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
        String otaStatus = "Yeni sürüm mevcut: " + latestVersion;
        StaticJsonDocument<256> statusDoc;
        statusDoc["otaStatus"] = otaStatus;
        String json;
        serializeJson(statusDoc, json);
        webSocket.broadcastTXT(json);
      } else {
        StaticJsonDocument<256> statusDoc;
        statusDoc["otaStatus"] = "Firmware güncel: " + currentVersion;
        String json;
        serializeJson(statusDoc, json);
        webSocket.broadcastTXT(json);
      }
    } else {
      StaticJsonDocument<256> statusDoc;
      statusDoc["otaStatus"] = "OTA kontrol hatası: JSON parse";
      String json;
      serializeJson(statusDoc, json);
      webSocket.broadcastTXT(json);
    }
  } else {
    StaticJsonDocument<256> statusDoc;
    statusDoc["otaStatus"] = "OTA kontrol hatası: HTTP " + String(httpCode);
    String json;
    serializeJson(statusDoc, json);
    webSocket.broadcastTXT(json);
  }
  http.end();
}

// WiFi bağlantısını dene
bool tryConnectWiFi(String ssid, String pass) {
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi bağlandı: " + ssid + ", IP: " + WiFi.localIP().toString());
    return true;
  } else {
    Serial.println("\nWiFi bağlantısı başarısız: " + ssid);
    return false;
  }
}

// Web sunucusunu başlat
void startWebServer() {
  server.on("/", []() {
    server.send(200, "text/html", htmlPage());
  });

  server.on("/set", []() {
    if (server.hasArg("tpd")) {
      int newTpd = server.arg("tpd").toInt();
      if (newTpd >= 600 && newTpd <= 1200) {
        turnsPerDay = newTpd;
        hourlyTurns = turnsPerDay / 24;
      }
    }
    if (server.hasArg("duration")) {
      float newDuration = server.arg("duration").toFloat();
      if (newDuration >= 10.0 && newDuration <= 15.0) {
        turnDuration = newDuration;
      }
    }
    if (server.hasArg("dir")) {
      int newDir = server.arg("dir").toInt();
      if (newDir >= 1 && newDir <= 3) {
        direction = newDir;
      }
    }
    if (server.hasArg("action")) {
      String action = server.arg("action");
      if (action == "start") {
        running = true;
        lastHourTime = millis();
        completedTurns = 0;
      } else if (action == "stop") {
        running = false;
        stopMotor();
      }
    }
    writeMotorSettings();
    updateWebSocket();
    server.send(200, "text/html", htmlPage());
  });

  server.on("/wifi", []() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid != "") {
      if (tryConnectWiFi(ssid, pass)) {
        writeWiFiCredentials(ssid, pass);
        MDNS.end();
        if (!MDNS.begin(mDNS_hostname.c_str())) {
          Serial.println("mDNS yeniden başlatılamadı!");
        } else {
          MDNS.addService("http", "tcp", 80);
          MDNS.addService("ws", "tcp", 81);
        }
      } else {
        WiFi.softAPdisconnect(true);
        WiFi.softAPConfig(apIP, gateway, subnet);
        WiFi.softAP(default_ssid.c_str(), default_password, 11);
      }
    }
    updateWebSocket();
    server.send(200, "text/html", htmlPage());
  });

  server.on("/reset_wifi", []() {
    resetWiFiCredentials();
    WiFi.disconnect();
    WiFi.softAPdisconnect(true);
    WiFi.softAPConfig(apIP, gateway, subnet);
    WiFi.softAP(default_ssid.c_str(), default_password, 11);
    Serial.println("WiFi ayarları sıfırlandı, hotspot başlatıldı: " + default_ssid);
    updateWebSocket();
    server.send(200, "text/html", htmlPage());
  });

  server.on("/reset_motor", []() {
    resetMotorSettings();
    updateWebSocket();
    server.send(200, "text/html", htmlPage());
  });

  server.on("/set_name", []() {
    String newName = server.arg("custom_name");
    newName.trim();
    if (newName.length() > 0 && newName.length() <= 20) {
      bool isValid = true;
      for (int i = 0; i < newName.length(); i++) {
        char c = newName[i];
        if (!isalnum(c) && c != ' ' && c != '-' && c != '_') {
          isValid = false;
          break;
        }
      }
      if (isValid) {
        custom_name = newName;
        writeCustomName(custom_name);
        mDNS_hostname = custom_name + "-" + macSuffix;
        Serial.println("Yeni cihaz ismi: " + custom_name);
        Serial.println("Yeni mDNS Hostname: " + mDNS_hostname + ".local");
        MDNS.end();
        if (!MDNS.begin(mDNS_hostname.c_str())) {
          Serial.println("mDNS yeniden başlatılamadı!");
        } else {
          Serial.println("mDNS yeniden başlatıldı: http://" + mDNS_hostname + ".local");
          MDNS.addService("http", "tcp", 80);
          MDNS.addService("ws", "tcp", 81);
        }
        WiFi.softAPdisconnect(true);
        WiFi.softAPConfig(apIP, gateway, subnet);
        WiFi.softAP(default_ssid.c_str(), default_password, 11);
      }
    }
    updateWebSocket();
    server.send(200, "text/html", htmlPage());
  });

  server.on("/reset_name", []() {
    resetCustomName();
    updateWebSocket();
    server.send(200, "text/html", htmlPage());
  });

  server.on("/scan_wifi", []() {
    Serial.println("WiFi tarama isteği alındı.");
    if (!isScanning) {
      isScanning = true;
      String options = scanWiFiNetworks();
      isScanning = false;
      Serial.println("WiFi tarama tamamlandı, yanıt: OK");
      server.send(200, "text/plain", "OK");
    } else {
      Serial.println("WiFi tarama zaten devam ediyor!");
      server.send(200, "text/plain", "Tarama zaten devam ediyor");
    }
  });

  server.on("/status", []() {
    StaticJsonDocument<256> doc;
    String currentSSID = WiFi.SSID() != "" ? WiFi.SSID() : default_ssid;
    doc["status"] = running ? "Çalışıyor" : "Durduruldu";
    doc["completedTurns"] = completedTurns;
    doc["hourlyTurns"] = hourlyTurns;
    doc["currentSSID"] = escapeJsonString(currentSSID);
    doc["connectionStatus"] = (WiFi.status() == WL_CONNECTED) ? "Bağlandı" : "Hotspot modunda";
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["turnsPerDay"] = turnsPerDay;
    doc["turnDuration"] = turnDuration;
    doc["direction"] = direction;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/check_ota", []() {
    checkOTAUpdate();
    server.send(200, "text/html", htmlPage());
  });

  ElegantOTA.begin(&server);
  server.begin();
  webSocket.begin();
}

// Setup fonksiyonu
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  // Motor pinlerini ayarla
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  stopMotor();

  // MAC adresini al (alternatif yöntem)
  WiFi.mode(WIFI_STA);
  delay(100); // WiFi modülünün hazır olması için kısa bekleme
  uint8_t mac[6];
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    String macAddress = String(macStr);
    Serial.println("esp_wifi_get_mac ile alınan MAC adresi: " + macAddress);
    if (macAddress == "00:00:00:00:00:00" || macAddress.length() < 12) {
      Serial.println("HATA: Geçersiz MAC adresi alındı (esp_wifi_get_mac)!");
      macAddress = "FF:FF:FF:FF:FF:FF"; // Geçici varsayılan MAC
    }
    macAddress.replace(":", "");
    macSuffix = macAddress.substring(macAddress.length() - 4);
  } else {
    Serial.println("HATA: esp_wifi_get_mac başarısız!");
    String macAddress = WiFi.macAddress();
    Serial.println("WiFi.macAddress ile alınan MAC adresi: " + macAddress);
    if (macAddress == "00:00:00:00:00:00" || macAddress.length() < 12) {
      Serial.println("HATA: Geçersiz MAC adresi alındı (WiFi.macAddress)!");
      macAddress = "FF:FF:FF:FF:FF:FF"; // Geçici varsayılan MAC
    }
    macAddress.replace(":", "");
    macSuffix = macAddress.substring(macAddress.length() - 4);
  }

  default_ssid = "horus-" + macSuffix;
  mDNS_hostname = "horus-" + macSuffix;
  Serial.println("MAC son 4: " + macSuffix);
  Serial.println("Varsayılan SSID: " + default_ssid);
  Serial.println("Varsayılan mDNS: " + mDNS_hostname + ".local");

  // Kullanıcı ismini oku
  readCustomName(custom_name);
  if (custom_name.length() > 0) {
    mDNS_hostname = custom_name + "-" + macSuffix;
    Serial.println("Özel cihaz ismi kullanıldı: " + mDNS_hostname);
  }

  // Motor ayarlarını oku
  readMotorSettings();

  // WiFi ayarlarını oku ve bağlan
  String ssid, pass;
  readWiFiCredentials(ssid, pass);
  if (ssid != "" && tryConnectWiFi(ssid, pass)) {
    Serial.println("WiFi’ye bağlandı: " + ssid);
  } else {
    WiFi.softAPConfig(apIP, gateway, subnet);
    WiFi.softAP(default_ssid.c_str(), default_password, 11);
    Serial.println("Hotspot başlatıldı: " + default_ssid);
  }

  // mDNS başlat
  if (!MDNS.begin(mDNS_hostname.c_str())) {
    Serial.println("mDNS başlatılamadı!");
  } else {
    Serial.println("mDNS başlatıldı: http://" + mDNS_hostname + ".local");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  }

  // Web sunucusunu başlat
  startWebServer();
}

// Loop fonksiyonu
void loop() {
  server.handleClient();
  webSocket.loop();
  ElegantOTA.loop();

  // WiFi bağlantısını kontrol et
  if (WiFi.status() != WL_CONNECTED && millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
    String ssid, pass;
    readWiFiCredentials(ssid, pass);
    if (ssid != "") {
      Serial.println("WiFi’ye yeniden bağlanılıyor: " + ssid);
      if (tryConnectWiFi(ssid, pass)) {
        MDNS.end();
        if (!MDNS.begin(mDNS_hostname.c_str())) {
          Serial.println("mDNS yeniden başlatılamadı!");
        } else {
          MDNS.addService("http", "tcp", 80);
          MDNS.addService("ws", "tcp", 81);
        }
      }
    }
    lastReconnectAttempt = millis();
  }

  // OTA kontrolü
  if (millis() - lastOTACheck > OTA_CHECK_INTERVAL) {
    checkOTAUpdate();
    lastOTACheck = millis();
  }

  // Motor kontrolü
  if (running) {
    unsigned long currentTime = millis();
    if (currentTime - lastHourTime >= 3600000) {
      completedTurns = 0;
      lastHourTime = currentTime;
      writeMotorSettings();
    }

    if (!isTurning && completedTurns < hourlyTurns) {
      isTurning = true;
      stepsRemaining = stepsPerTurn;
      currentStep = 0;
      if (direction == 3) {
        stepDir = (completedTurns % 2 == 0) ? 1 : -1;
      } else {
        stepDir = (direction == 1) ? 1 : -1;
      }
    }

    if (isTurning && stepsRemaining > 0) {
      if (currentTime - lastStepTime >= calculateStepDelay(currentStep, minStepDelay)) {
        applyStep(currentStep);
        currentStep += stepDir;
        stepsRemaining--;
        lastStepTime = currentTime;
        if (stepsRemaining == 0) {
          isTurning = false;
          completedTurns++;
          writeMotorSettings();
          stopMotor();
          delay(turnDuration * 1000);
        }
      }
    }
  }

  // WebSocket güncellemesi
  if (millis() - lastUpdateTime > 1000) {
    updateWebSocket();
    lastUpdateTime = millis();
  }
}
