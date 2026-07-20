#include "WiFiManager.h"

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 WiFi 配置</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a2e; color: #eee; }
        .container { max-width: 400px; margin: 0 auto; background: #16213e; padding: 20px; border-radius: 10px; }
        h1 { text-align: center; color: #00d2ff; }
        label { display: block; margin: 10px 0 5px; }
        input[type="text"], input[type="password"] {
            width: 100%; padding: 10px; border: none; border-radius: 5px;
            background: #0f3460; color: #fff; font-size: 16px;
        }
        button {
            width: 100%; padding: 12px; margin-top: 15px;
            background: #00d2ff; color: #000; border: none; border-radius: 5px;
            font-size: 18px; font-weight: bold; cursor: pointer;
        }
        button:hover { background: #00b8d4; }
        .status { text-align: center; margin-top: 10px; padding: 10px; background: #0f3460; border-radius: 5px; }
        .error { color: #ff6b6b; }
        .success { color: #51cf66; }
        .scan-btn { background: #ff6b6b; margin-top: 10px; }
        .scan-btn:hover { background: #e55a5a; }
        #network-list { margin-top: 10px; }
        .network-item {
            padding: 8px 10px; margin: 5px 0; background: #0f3460;
            border-radius: 5px; cursor: pointer; display: flex; justify-content: space-between;
        }
        .network-item:hover { background: #1a4a7a; }
        .signal { color: #ffd93d; }
        .smartconfig-tip { margin-top: 15px; padding: 10px; background: #0f3460; border-radius: 5px; font-size: 14px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📶 WiFi 配置</h1>
        <div id="status" class="status">扫描网络中...</div>
        <form id="wifiForm">
            <label>WiFi 名称</label>
            <input type="text" id="ssid" placeholder="选择或输入 WiFi 名称">
            <label>WiFi 密码</label>
            <input type="password" id="password" placeholder="请输入密码">
            <button type="submit">连接 WiFi</button>
        </form>
        <button class="scan-btn" onclick="scanNetworks()">🔄 刷新网络列表</button>
        <div id="network-list"></div>
        <div class="smartconfig-tip">
            💡 <strong>ESP-Touch 配网:</strong> 打开手机 ESP-Touch APP，输入 WiFi 密码后广播，设备将自动接收配置。
        </div>
    </div>
    <script>
        function scanNetworks() {
            document.getElementById('network-list').innerHTML = '扫描中...';
            fetch('/scan').then(r => r.json()).then(data => {
                let html = '';
                data.forEach(net => {
                    const bars = net.rssi > -50 ? '▃▃▃▃' : net.rssi > -70 ? '▃▃▃_' : net.rssi > -85 ? '▃▃__' : '▃___';
                    html += `<div class="network-item" onclick="selectNetwork('${net.ssid}')">
                        <span>${net.ssid}</span>
                        <span class="signal">${bars}</span>
                    </div>`;
                });
                document.getElementById('network-list').innerHTML = html || '未找到网络';
                document.getElementById('status').innerHTML = '✅ 扫描完成，点击网络选择';
            }).catch(() => {
                document.getElementById('network-list').innerHTML = '⚠️ 扫描失败';
            });
        }
        function selectNetwork(ssid) {
            document.getElementById('ssid').value = ssid;
            document.getElementById('password').focus();
        }
        document.getElementById('wifiForm').onsubmit = function(e) {
            e.preventDefault();
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            if (!ssid) { alert('请输入 WiFi 名称'); return; }
            document.getElementById('status').innerHTML = '⏳ 连接中...';
            fetch('/save', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`
            }).then(r => r.text()).then(msg => {
                document.getElementById('status').innerHTML = msg;
                if (msg.includes('成功')) {
                    document.getElementById('status').className = 'status success';
                    setTimeout(() => { window.location.href = '/'; }, 3000);
                }
            });
        };
        scanNetworks();
    </script>
</body>
</html>
)rawliteral";

WiFiManager::WiFiManager()
    : _server(nullptr), _dnsServer(nullptr),
      _credentialCount(0),
      _apMode(false),
      _smartConfigStarted(false),
      _smartConfigDone(false),
      _apStartTime(0),
      _smartConfigStartTime(0),
      _lastReconnectAttempt(0), _reconnectCount(0),
      _txPower(27) {}

WiFiManager::~WiFiManager() {
    stopSmartConfig();
    if (_server) {
        _server->stop();
        delete _server;
        _server = nullptr;
    }
    if (_dnsServer) {
        _dnsServer->stop();
        delete _dnsServer;
        _dnsServer = nullptr;
    }
}

void WiFiManager::begin() {
    _preferences.begin("wifi", false);
    loadCredentials();
    Serial.printf("[WiFi] 初始化完成，已保存 %d 个凭据\n", _credentialCount);
}

void WiFiManager::saveCredentials(const String& ssid, const String& password) {
    for (int i = 0; i < _credentialCount; i++) {
        if (_credentials[i].ssid == ssid) {
            for (int j = i; j > 0; j--) {
                _credentials[j] = _credentials[j - 1];
            }
            _credentials[0].ssid = ssid;
            _credentials[0].password = password;
            goto save;
        }
    }

    if (_credentialCount < MAX_WIFI_CREDENTIALS) {
        for (int i = _credentialCount; i > 0; i--) {
            _credentials[i] = _credentials[i - 1];
        }
        _credentials[0].ssid = ssid;
        _credentials[0].password = password;
        _credentialCount++;
    } else {
        for (int i = MAX_WIFI_CREDENTIALS - 1; i > 0; i--) {
            _credentials[i] = _credentials[i - 1];
        }
        _credentials[0].ssid = ssid;
        _credentials[0].password = password;
    }

save:
    _preferences.putInt("count", _credentialCount);
    for (int i = 0; i < _credentialCount; i++) {
        String ssidKey = "ssid_" + String(i);
        String passKey = "pass_" + String(i);
        _preferences.putBytes(ssidKey.c_str(), _credentials[i].ssid.c_str(), _credentials[i].ssid.length() + 1);
        _preferences.putBytes(passKey.c_str(), _credentials[i].password.c_str(), _credentials[i].password.length() + 1);
    }
    Serial.printf("[WiFi] 已保存 %d 个凭据\n", _credentialCount);
}

bool WiFiManager::loadCredentials() {
    _credentialCount = _preferences.getInt("count", 0);

    if (_credentialCount > MAX_WIFI_CREDENTIALS) {
        _credentialCount = MAX_WIFI_CREDENTIALS;
    }

    for (int i = 0; i < _credentialCount; i++) {
        String ssidKey = "ssid_" + String(i);
        String passKey = "pass_" + String(i);

        size_t ssidLen = _preferences.getBytesLength(ssidKey.c_str());
        if (ssidLen > 0) {
            char* buf = new char[ssidLen];
            _preferences.getBytes(ssidKey.c_str(), buf, ssidLen);
            _credentials[i].ssid = buf;
            delete[] buf;
        } else {
            _credentials[i].ssid = "";
        }

        size_t passLen = _preferences.getBytesLength(passKey.c_str());
        if (passLen > 0) {
            char* buf = new char[passLen];
            _preferences.getBytes(passKey.c_str(), buf, passLen);
            _credentials[i].password = buf;
            delete[] buf;
        } else {
            _credentials[i].password = "";
        }
    }

    Serial.printf("[WiFi] 从 NVS 加载 %d 个凭据\n", _credentialCount);
    return _credentialCount > 0;
}

bool WiFiManager::hasSavedCredentials() {
    return _credentialCount > 0;
}

int WiFiManager::getCredentialCount() {
    return _credentialCount;
}

void WiFiManager::resetConfig() {
    _preferences.clear();
    _credentialCount = 0;
    Serial.println("[WiFi] 配置已清除");
}

void WiFiManager::setTxPower(int percentage) {
    _txPower = constrain(percentage, 0, 100);
    Serial.printf("[WiFi] TX 功率设置为 %d%%\n", _txPower);
}

void WiFiManager::applyTxPower() {
    int power = 8 + (int)((106 - 8) * (_txPower / 100.0) + 0.5);
    esp_wifi_set_max_tx_power(power);
}

bool WiFiManager::connectToWiFi(const String& ssid, const String& password) {
    if (ssid.length() == 0) return false;

    WiFi.mode(WIFI_STA);
    applyTxPower();
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] 已连接: %s, IP: %s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
        _reconnectCount = 0;
        return true;
    }

    Serial.printf("[WiFi] 连接失败: %s\n", ssid.c_str());
    return false;
}

bool WiFiManager::connect() {
    Serial.println("[WiFi] 开始连接...");

    if (hasSavedCredentials()) {
        for (int i = 0; i < _credentialCount; i++) {
            Serial.printf("[WiFi] 尝试凭据 %d: %s\n", i + 1, _credentials[i].ssid.c_str());
            if (connectToWiFi(_credentials[i].ssid, _credentials[i].password)) {
                stopSmartConfig();
                return true;
            }
            WiFi.disconnect(false);
            delay(100);
        }
    }

    Serial.printf("[WiFi] 尝试默认 WiFi: %s\n", WIFI_SSID);
    if (connectToWiFi(WIFI_SSID, WIFI_PASS)) {
        saveCredentials(WIFI_SSID, WIFI_PASS);
        stopSmartConfig();
        return true;
    }

    Serial.println("[WiFi] 所有连接尝试失败，进入 AP 模式和 ESP-Touch SmartConfig");
    startAPMode();
    startSmartConfig();
    return false;
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WiFi] 已断开连接");
}

void WiFiManager::maintainConnection() {
    if (_apMode) {
        handleClient();
        handleSmartConfig();
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        _reconnectCount = 0;
        return;
    }

    unsigned long now = millis();
    if (now - _lastReconnectAttempt < RECONNECT_INTERVAL) {
        return;
    }
    _lastReconnectAttempt = now;

    _reconnectCount++;
    Serial.printf("[WiFi] 断连，尝试重连 (尝试 %d)\n", _reconnectCount);

    if (_reconnectCount > MAX_RECONNECT_ATTEMPTS) {
        Serial.println("[WiFi] 重连次数过多，进入 AP 模式和 ESP-Touch SmartConfig");
        startAPMode();
        startSmartConfig();
        return;
    }

    if (hasSavedCredentials()) {
        for (int i = 0; i < _credentialCount; i++) {
            if (connectToWiFi(_credentials[i].ssid, _credentials[i].password)) {
                stopSmartConfig();
                return;
            }
            WiFi.disconnect(false);
            delay(100);
        }
    }

    if (connectToWiFi(WIFI_SSID, WIFI_PASS)) {
        stopSmartConfig();
        return;
    }

    if (_reconnectCount >= MAX_RECONNECT_ATTEMPTS) {
        startAPMode();
        startSmartConfig();
    }
}

void WiFiManager::startAPMode() {
    if (_apMode) return;

    WiFi.disconnect();
    delay(100);

    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String apSSID = "ESP32-" + mac.substring(6, 12);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID.c_str(), "12345678");

    _apMode = true;
    _apStartTime = millis();
    _reconnectCount = 0;

    if (!_dnsServer) {
        _dnsServer = new DNSServer();
        _dnsServer->start(53, "*", WiFi.softAPIP());
    }

    if (!_server) {
        _server = new WebServer(80);
        setupWebServer();
        _server->begin();
    }

    Serial.printf("[WiFi] AP 模式已启动: %s, IP: %s\n",
                  apSSID.c_str(), WiFi.softAPIP().toString().c_str());
    Serial.println("[WiFi] 密码: 12345678");
}

void WiFiManager::stopAPMode() {
    if (!_apMode) return;

    _apMode = false;
    _apStartTime = 0;

    if (_server) {
        _server->stop();
        delete _server;
        _server = nullptr;
    }
    if (_dnsServer) {
        _dnsServer->stop();
        delete _dnsServer;
        _dnsServer = nullptr;
    }

    WiFi.softAPdisconnect(true);
    Serial.println("[WiFi] AP 模式已停止");
}

bool WiFiManager::isAPMode() {
    return _apMode;
}

bool WiFiManager::isAPStarted() {
    return _apMode;
}

void WiFiManager::handleClient() {
    if (_apMode && _server) {
        _dnsServer->processNextRequest();
        _server->handleClient();
    }
}

void WiFiManager::checkAPTimeout() {
    if (!_apMode || _apStartTime == 0) return;

    if (millis() - _apStartTime >= AP_TIMEOUT_MS) {
        Serial.println("[WiFi] AP 超时 (10分钟)，自动停止");
        stopAPMode();
    }
}

void WiFiManager::setupWebServer() {
    if (!_server) return;

    _server->on("/", [this]() { this->handleRoot(); });
    _server->on("/save", [this]() { this->handleSave(); });
    _server->on("/scan", [this]() { this->handleScan(); });
    _server->onNotFound([this]() { this->handleNotFound(); });
}

void WiFiManager::handleRoot() {
    _server->send(200, "text/html", getHTML());
}

void WiFiManager::handleSave() {
    String ssid = _server->arg("ssid");
    String password = _server->arg("password");

    if (ssid.length() == 0) {
        _server->send(400, "text/plain", "❌ SSID 不能为空");
        return;
    }

    saveCredentials(ssid, password);

    String msg;
    if (connectToWiFi(ssid, password)) {
        msg = "✅ 连接成功！设备正在重启...";
        _server->send(200, "text/plain", msg);
        delay(500);
        ESP.restart();
    } else {
        msg = "❌ 连接失败，请检查密码";
        _server->send(200, "text/plain", msg);
    }
}

void WiFiManager::handleScan() {
    String json = "[";
    int n = WiFi.scanComplete();
    if (n == -2) {
        WiFi.scanNetworks(true);
        json = "[]";
    } else if (n >= 0) {
        for (int i = 0; i < n; ++i) {
            if (i) json += ",";
            json += "{";
            String ssid = WiFi.SSID(i);
            ssid.replace("\"", "\\\"");
            json += "\"ssid\":\"" + ssid + "\",";
            json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
            json += "\"encryption\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            json += "}";
        }
        WiFi.scanDelete();
    }
    json += "]";
    _server->send(200, "application/json", json);
}

void WiFiManager::handleNotFound() {
    _server->send(404, "text/plain", "404: Not Found");
}

String WiFiManager::getHTML() {
    return String(FPSTR(INDEX_HTML));
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

int WiFiManager::getRSSI() {
    return WiFi.RSSI();
}

const char* WiFiManager::getLocalIP() {
    static String ipStr;
    if (isConnected()) {
        ipStr = WiFi.localIP().toString();
    } else if (_apMode) {
        ipStr = WiFi.softAPIP().toString();
    } else {
        ipStr = "0.0.0.0";
    }
    return ipStr.c_str();
}

const char* WiFiManager::getSSID() {
    static String ssidStr;
    if (isConnected()) {
        ssidStr = WiFi.SSID();
    } else if (_apMode) {
        ssidStr = "ESP32-AP";
    } else {
        ssidStr = "未连接";
    }
    return ssidStr.c_str();
}

String WiFiManager::getMacAddress() {
    return WiFi.macAddress();
}

void WiFiManager::startSmartConfig() {
    if (_smartConfigStarted) return;

    Serial.println("[SmartConfig] 启动 ESP-Touch SmartConfig...");

    WiFi.beginSmartConfig();
    _smartConfigStarted = true;
    _smartConfigDone = false;
    _smartConfigStartTime = millis();

    Serial.println("[SmartConfig] 等待 ESP-Touch 广播...");
}

void WiFiManager::stopSmartConfig() {
    if (!_smartConfigStarted) return;

    Serial.println("[SmartConfig] 停止 SmartConfig...");

    WiFi.stopSmartConfig();
    _smartConfigStarted = false;
    _smartConfigDone = false;
    _smartConfigStartTime = 0;

    Serial.println("[SmartConfig] 已停止");
}

bool WiFiManager::isSmartConfigStarted() {
    return _smartConfigStarted;
}

void WiFiManager::handleSmartConfig() {
    if (!_smartConfigStarted) return;

    if (WiFi.smartConfigDone()) {
        Serial.println("[SmartConfig] 收到 ESP-Touch 配置！");
        _smartConfigDone = true;

        String ssid = WiFi.SSID();
        String password = WiFi.psk();

        Serial.print("[SmartConfig] SSID: ");
        Serial.println(ssid);

        saveCredentials(ssid, password);

        stopAPMode();
        stopSmartConfig();

        Serial.println("[SmartConfig] 配置已保存，正在连接...");
        if (connectToWiFi(ssid, password)) {
            Serial.println("[SmartConfig] 连接成功！");
        } else {
            Serial.println("[SmartConfig] 连接失败，重新启动 AP 和 SmartConfig");
            startAPMode();
            startSmartConfig();
        }
    }

    if (_smartConfigStartTime > 0 && (millis() - _smartConfigStartTime) >= SMART_CONFIG_TIMEOUT_MS) {
        Serial.println("[SmartConfig] 超时 (2分钟)，停止 SmartConfig");
        stopSmartConfig();
    }
}
