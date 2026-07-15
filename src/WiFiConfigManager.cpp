#include "WiFiConfigManager.h"
#include <esp_wifi.h>

WiFiConfigManager::WiFiConfigManager() :
    webServer(nullptr),
    configMode(false),
    credentialCount(0),
    apStarted(false),
    apStartTime(0),
    lastClientActivity(0),
    _txPower(27) {}

WiFiConfigManager::~WiFiConfigManager() {
    stopWebServer();
}

void WiFiConfigManager::begin() {
    Serial.println("[WiFiConfig] Initializing NVS...");
    
    preferences.begin("wifi-config", false);
    loadCredentials();
    Serial.println("[WiFiConfig] Initialized");
}

bool WiFiConfigManager::autoConnect() {
    // 1. 尝试保存的凭据
    if (hasSavedCredentials()) {
        Serial.println("[WiFiConfig] Trying saved WiFi credentials...");
        
        for (int i = 0; i < credentialCount; i++) {
            Serial.printf("   Trying SSID %d: %s\n", i + 1, savedCredentials[i].ssid.c_str());
            
            WiFi.mode(WIFI_STA);
            applyTxPower();
            WiFi.begin(savedCredentials[i].ssid.c_str(), savedCredentials[i].password.c_str());
            
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 40) {
                delay(500);
                Serial.print(".");
                attempts++;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\n[WiFiConfig] WiFi connected!");
                Serial.print("   IP: ");
                Serial.println(WiFi.localIP());
                configMode = false;
                return true;
            }
            
            Serial.println("\n[WiFiConfig] SSID failed, trying next...");
            WiFi.disconnect(false);
            delay(100);
        }
    }
    
    // 2. 尝试默认 WiFi
    Serial.printf("[WiFiConfig] Trying default WiFi: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    applyTxPower();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFiConfig] Default WiFi connected!");
        Serial.print("   IP: ");
        Serial.println(WiFi.localIP());
        configMode = false;
        return true;
    }
    
    Serial.println("\n[WiFiConfig] All connection attempts failed!");
    WiFi.disconnect(false);
    delay(100);
    return false;
}

bool WiFiConfigManager::connectToWiFi(const char* ssid, const char* password, int timeoutMs) {
    WiFi.mode(WIFI_STA);
    applyTxPower();
    WiFi.begin(ssid, password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
        delay(500);
        Serial.print(".");
    }
    
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
        configMode = false;
    }
    return connected;
}

void WiFiConfigManager::startConfigPortal() {
    Serial.println("[WiFiConfig] Starting Config Portal...");
    startAPMode();
}

void WiFiConfigManager::startAPMode() {
    if (apStarted) return;
    
    Serial.println("[WiFiConfig] Starting AP mode...");
    
    // 断开 WiFi 连接
    WiFi.disconnect(true);
    delay(100);
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("ESP32-Weather", NULL, 1, false, 4);
    
    delay(500);
    
    Serial.printf("[WiFiConfig] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("[WiFiConfig] AP SSID: %s\n", WiFi.softAPSSID());
    
    apStarted = true;
    apStartTime = millis();
    lastClientActivity = millis();
    configMode = true;
    
    startWebServer();
}

void WiFiConfigManager::stopAPMode() {
    if (!apStarted) return;
    
    Serial.println("[WiFiConfig] Stopping AP mode...");
    
    stopWebServer();
    WiFi.softAPdisconnect(true);
    delay(100);
    
    apStarted = false;
    apStartTime = 0;
    configMode = false;
    
    Serial.println("[WiFiConfig] AP mode stopped");
}

bool WiFiConfigManager::isAPStarted() {
    return apStarted;
}

unsigned long WiFiConfigManager::getAPStartTime() {
    return apStartTime;
}

void WiFiConfigManager::checkAutoStop() {
    if (!apStarted) return;
    
    // 检查超时（10分钟无活动）
    if (millis() - apStartTime >= AP_TIMEOUT_MS) {
        Serial.println("[WiFiConfig] AP timeout, auto stopping...");
        stopAPMode();
    }
}

void WiFiConfigManager::setTxPower(int percentage) {
    _txPower = constrain(percentage, 0, 100);
    Serial.printf("[WiFiConfig] TX power set to %d%%\n", _txPower);
}

void WiFiConfigManager::applyTxPower() {
    int power = 8 + (106 - 8) * (_txPower / 100.0);
    esp_wifi_set_max_tx_power(power);
}

void WiFiConfigManager::startWebServer() {
    if (webServer) {
        stopWebServer();
    }
    
    webServer = new WebServer(80);
    
    webServer->on("/", HTTP_GET, [this]() { handleRoot(); });
    webServer->on("/scan", HTTP_GET, [this]() { handleScan(); });
    webServer->on("/save", HTTP_POST, [this]() { handleSave(); });
    webServer->onNotFound([this]() { handleNotFound(); });
    
    webServer->begin();
    Serial.println("[WiFiConfig] Web server started");
}

void WiFiConfigManager::stopWebServer() {
    if (webServer) {
        webServer->stop();
        delete webServer;
        webServer = nullptr;
        Serial.println("[WiFiConfig] Web server stopped");
    }
}

void WiFiConfigManager::handleClient() {
    if (webServer) {
        webServer->handleClient();
    }
}

void WiFiConfigManager::handleRoot() {
    lastClientActivity = millis();
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name="viewport" content="width=device-width,initial-scale=1.0">
    <title>ESP32 WiFi配置</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: Arial, sans-serif; background: #1a1a2e; padding: 20px; }
        .container { max-width: 400px; margin: 0 auto; background: #16213e; padding: 20px; border-radius: 10px; }
        h2 { text-align: center; color: #00d2ff; margin-bottom: 20px; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; color: #aaa; }
        select, input { width: 100%; padding: 10px; border: none; border-radius: 5px; background: #0f3460; color: #fff; font-size: 16px; }
        select option { background: #0f3460; }
        button { width: 100%; padding: 12px; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; margin-bottom: 10px; }
        button.scan { background: #2196F3; color: white; }
        button.scan:hover { background: #1976D2; }
        button.save { background: #4CAF50; color: white; }
        button.save:hover { background: #45a049; }
        .status { text-align: center; margin-top: 10px; color: #aaa; }
        .loading { text-align: center; padding: 20px; color: #aaa; }
    </style>
</head>
<body>
    <div class="container">
        <h2>📶 WiFi 配置</h2>
        <div class="form-group">
            <label>选择WiFi网络:</label>
            <select id="wifiSelect">
                <option value="">-- 点击扫描 --</option>
            </select>
        </div>
        <button class="scan" onclick="scanNetworks()">🔄 扫描WiFi</button>
        <div class="form-group">
            <label>WiFi密码:</label>
            <input type="password" id="password" placeholder="请输入密码">
        </div>
        <button class="save" onclick="saveConfig()">💾 保存并连接</button>
        <div class="status" id="status"></div>
    </div>
    <script>
        function scanNetworks() {
            var select = document.getElementById('wifiSelect');
            select.innerHTML = '<option value="">-- 扫描中... --</option>';
            document.getElementById('status').textContent = '扫描中...';
            
            fetch('/scan')
                .then(r => r.json())
                .then(data => {
                    select.innerHTML = '';
                    if (data.length === 0) {
                        select.innerHTML = '<option value="">未找到WiFi</option>';
                        document.getElementById('status').textContent = '未找到WiFi网络';
                    } else {
                        data.forEach(net => {
                            var opt = document.createElement('option');
                            opt.value = net.ssid;
                            opt.text = net.ssid + ' (' + net.rssi + ' dBm)';
                            select.appendChild(opt);
                        });
                        document.getElementById('status').textContent = '✅ 扫描完成，请选择网络';
                    }
                })
                .catch(err => {
                    select.innerHTML = '<option value="">扫描失败</option>';
                    document.getElementById('status').textContent = '❌ 扫描失败，请重试';
                });
        }

        function saveConfig() {
            var ssid = document.getElementById('wifiSelect').value;
            var password = document.getElementById('password').value;

            if (!ssid) {
                document.getElementById('status').textContent = '⚠️ 请先扫描并选择WiFi网络';
                return;
            }

            document.getElementById('status').textContent = '⏳ 正在保存...';

            var formData = new FormData();
            formData.append('ssid', ssid);
            formData.append('password', password);

            fetch('/save', { method: 'POST', body: formData })
                .then(r => r.text())
                .then(data => {
                    document.getElementById('status').innerHTML = '✅ 配置已保存，设备将重启<br><small>请重新连接WiFi</small>';
                    setTimeout(() => { window.location.href = '/'; }, 5000);
                })
                .catch(err => {
                    document.getElementById('status').textContent = '❌ 保存失败';
                });
        }
        
        // 自动扫描
        setTimeout(scanNetworks, 500);
    </script>
</body>
</html>
)rawliteral";
    
    webServer->send(200, "text/html", html);
}

void WiFiConfigManager::handleScan() {
    lastClientActivity = millis();
    Serial.println("[WiFiConfig] Scanning networks...");
    
    WiFi.scanNetworks(true);
    
    // 等待扫描完成
    int scanResult = WiFi.scanComplete();
    while (scanResult == -2) {
        delay(100);
        scanResult = WiFi.scanComplete();
    }
    
    String json = "[";
    int n = scanResult;
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        String ssid = WiFi.SSID(i);
        ssid.replace("\"", "\\\"");
        json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    
    webServer->send(200, "application/json", json);
    WiFi.scanDelete();
}

void WiFiConfigManager::handleSave() {
    lastClientActivity = millis();
    
    String ssid = webServer->arg("ssid");
    String password = webServer->arg("password");
    
    if (ssid.length() == 0) {
        webServer->send(400, "text/plain", "SSID required");
        return;
    }
    
    Serial.printf("[WiFiConfig] Saving: %s\n", ssid.c_str());
    saveCredentials(ssid, password);
    
    webServer->send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
}

void WiFiConfigManager::handleNotFound() {
    webServer->send(404, "text/plain", "Not Found");
}

void WiFiConfigManager::saveCredentials(const String& ssid, const String& password) {
    // 检查是否已存在
    for (int i = 0; i < credentialCount; i++) {
        if (savedCredentials[i].ssid == ssid) {
            // 移动到第一位
            for (int j = i; j > 0; j--) {
                savedCredentials[j] = savedCredentials[j - 1];
            }
            savedCredentials[0].ssid = ssid;
            savedCredentials[0].password = password;
            goto save;
        }
    }
    
    // 新增
    if (credentialCount < MAX_WIFI_CREDENTIALS) {
        for (int i = credentialCount; i > 0; i--) {
            savedCredentials[i] = savedCredentials[i - 1];
        }
        savedCredentials[0].ssid = ssid;
        savedCredentials[0].password = password;
        credentialCount++;
    } else {
        // 超过最大数量，覆盖最后一个
        for (int i = MAX_WIFI_CREDENTIALS - 1; i > 0; i--) {
            savedCredentials[i] = savedCredentials[i - 1];
        }
        savedCredentials[0].ssid = ssid;
        savedCredentials[0].password = password;
    }

save:
    preferences.putInt("count", credentialCount);
    for (int i = 0; i < credentialCount; i++) {
        String ssidKey = "ssid_" + String(i);
        String passKey = "pass_" + String(i);
        preferences.putString(ssidKey.c_str(), savedCredentials[i].ssid);
        preferences.putString(passKey.c_str(), savedCredentials[i].password);
    }
    preferences.end();
    Serial.printf("[WiFiConfig] Saved %d credentials\n", credentialCount);
}

bool WiFiConfigManager::loadCredentials() {
    credentialCount = preferences.getInt("count", 0);
    
    if (credentialCount > MAX_WIFI_CREDENTIALS) {
        credentialCount = MAX_WIFI_CREDENTIALS;
    }
    
    for (int i = 0; i < credentialCount; i++) {
        String ssidKey = "ssid_" + String(i);
        String passKey = "pass_" + String(i);
        savedCredentials[i].ssid = preferences.getString(ssidKey.c_str(), "");
        savedCredentials[i].password = preferences.getString(passKey.c_str(), "");
    }
    
    preferences.end();
    Serial.printf("[WiFiConfig] Loaded %d credentials\n", credentialCount);
    return credentialCount > 0;
}

bool WiFiConfigManager::hasSavedCredentials() {
    return credentialCount > 0;
}

IPAddress WiFiConfigManager::getIP() {
    if (configMode || apStarted) {
        return WiFi.softAPIP();
    }
    return WiFi.localIP();
}

bool WiFiConfigManager::isConfigMode() {
    return configMode || apStarted;
}

const char* WiFiConfigManager::getConfigSSID() {
    return "ESP32-Weather";
}

const char* WiFiConfigManager::getConfigPassword() {
    return "";
}