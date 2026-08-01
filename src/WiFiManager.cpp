#include "WiFiManager.h"
#include "Log.h"

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
      _apStartTime(0),
      _smartConfigStartTime(0),
      _smartConfigProcessing(false),
      _smartConfigWaitStart(0),
      _lastReconnectAttempt(0), _reconnectCount(0),
      _txPower(27),
      _scanCache("[]"),
      _scanCacheTime(0),
      _lastScanStatus(-2) {}

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
    LOG_T("[WiFi] 初始化完成，已保存 %d 个凭据", _credentialCount);
}

void WiFiManager::saveCredentials(const String& ssid, const String& password) {
    if (ssid.length() == 0) {
        LOG_LN("[WiFi] 拒绝保存空 SSID 凭据");
        return;
    }
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
    LOG_T("[WiFi] 已保存 %d 个凭据", _credentialCount);
}

bool WiFiManager::loadCredentials() {
    int stored = _preferences.getInt("count", 0);

    if (stored > MAX_WIFI_CREDENTIALS) {
        stored = MAX_WIFI_CREDENTIALS;
    }

    _credentialCount = 0;
    for (int i = 0; i < stored; i++) {
        String ssidKey = "ssid_" + String(i);
        String passKey = "pass_" + String(i);

        String ssid;
        size_t ssidLen = _preferences.getBytesLength(ssidKey.c_str());
        if (ssidLen > 0) {
            char* buf = new char[ssidLen];
            _preferences.getBytes(ssidKey.c_str(), buf, ssidLen);
            ssid = buf;
            delete[] buf;
        }

        String password;
        size_t passLen = _preferences.getBytesLength(passKey.c_str());
        if (passLen > 0) {
            char* buf = new char[passLen];
            _preferences.getBytes(passKey.c_str(), buf, passLen);
            password = buf;
            delete[] buf;
        }

        // 过滤空 SSID 凭据（历史上可能因 SmartConfig 读取时机问题污染）
        if (ssid.length() == 0) {
            LOG_T("[WiFi] 跳过空 SSID 凭据 (索引 %d)", i);
            continue;
        }

        _credentials[_credentialCount].ssid = ssid;
        _credentials[_credentialCount].password = password;
        _credentialCount++;
    }

    LOG_T("[WiFi] 从 NVS 加载 %d 个凭据", _credentialCount);
    return _credentialCount > 0;
}

bool WiFiManager::hasSavedCredentials() {
    return _credentialCount > 0;
}

void WiFiManager::setTxPower(int percentage) {
    _txPower = constrain(percentage, 0, 100);
    LOG_T("[WiFi] TX 功率设置为 %d%%", _txPower);
}

void WiFiManager::applyTxPower() {
    // ESP32-C3 TX power 范围：8-78（单位 0.25dBm，8=2dBm, 78=19.5dBm）
    int power = 8 + (int)((78 - 8) * (_txPower / 100.0) + 0.5);
    if (power < 8) power = 8;
    if (power > 78) power = 78;
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
        LOG_T("[WiFi] 已连接: %s, IP: %s", ssid.c_str(), WiFi.localIP().toString().c_str());
        _reconnectCount = 0;
        return true;
    }

    LOG_T("[WiFi] 连接失败: %s", ssid.c_str());
    return false;
}

bool WiFiManager::connect() {
    LOG_LN("[WiFi] 开始连接...");

    if (hasSavedCredentials()) {
        for (int i = 0; i < _credentialCount; i++) {
            LOG_T("[WiFi] 尝试凭据 %d: %s", i + 1, _credentials[i].ssid.c_str());
            if (connectToWiFi(_credentials[i].ssid, _credentials[i].password)) {
                stopSmartConfig();
                return true;
            }
            WiFi.disconnect(false);
            delay(100);
        }
    }

    LOG_T("[WiFi] 尝试默认 WiFi: %s", WIFI_SSID);
    if (connectToWiFi(WIFI_SSID, WIFI_PASS)) {
        saveCredentials(WIFI_SSID, WIFI_PASS);
        stopSmartConfig();
        return true;
    }

    LOG_LN("[WiFi] 所有连接尝试失败，进入 AP 模式和 ESP-Touch SmartConfig");
    startAPMode();
    startSmartConfig();
    return false;
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    LOG_LN("[WiFi] 已断开连接");
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
    LOG_T("[WiFi] 断连，尝试重连 (尝试 %d)", _reconnectCount);

    if (_reconnectCount > MAX_RECONNECT_ATTEMPTS) {
        LOG_LN("[WiFi] 重连次数过多，进入 AP 模式和 ESP-Touch SmartConfig");
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
    if (!WiFi.softAP(apSSID.c_str(), "12345678")) {
        LOG_LN("[WiFi] AP 启动失败！");
        return;
    }

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

    LOG_T("[WiFi] AP 模式已启动: %s, IP: %s",
                  apSSID.c_str(), WiFi.softAPIP().toString().c_str());
    LOG_LN("[WiFi] 密码: 12345678");
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
    // 切回 STA 模式，便于后续重新连接
    WiFi.mode(WIFI_STA);
    LOG_LN("[WiFi] AP 模式已停止");
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
        LOG_LN("[WiFi] AP 超时 (10分钟)，自动停止");
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

    // 写入 NVS 后尝试连接
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

void WiFiManager::buildScanJson(String& json) {
    int n = WiFi.scanComplete();
    
    if (n == -2) {
        // 扫描未启动
        json = "[]";
    } else if (n == -1) {
        // 扫描进行中 - 返回当前已有的扫描结果（如果有）
        // 这里复用上次缓存，避免一直返回空
        json = _scanCache;
    } else if (n >= 0) {
        // 扫描完成，构建 JSON
        json = "[";
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
        json += "]";
        WiFi.scanDelete();
        _lastScanStatus = n;
        _scanCache = json;
        _scanCacheTime = millis();
    } else {
        // 未知错误，返回缓存
        json = _scanCache;
    }
}

void WiFiManager::performScan() {
    unsigned long now = millis();
    
    // 如果缓存未过期，直接复用
    if (_lastScanStatus >= 0 && (now - _scanCacheTime) < SCAN_CACHE_MS) {
        return;
    }
    
    // 启动新的异步扫描（如果当前没有进行中的扫描）
    if (WiFi.scanComplete() == -2 || WiFi.scanComplete() == -1) {
        WiFi.scanNetworks(true);
        _lastScanStatus = -1;
    }
}

void WiFiManager::handleScan() {
    performScan();
    String json;
    buildScanJson(json);
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

String WiFiManager::getLocalIP() {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    if (_apMode) {
        return WiFi.softAPIP().toString();
    }
    return "0.0.0.0";
}

String WiFiManager::getSSID() {
    if (isConnected()) {
        return WiFi.SSID();
    }
    if (_apMode) {
        return "ESP32-AP";
    }
    return "未连接";
}

void WiFiManager::startSmartConfig() {
    if (_smartConfigStarted) return;

    LOG_LN("[SmartConfig] 启动 ESP-Touch SmartConfig...");

    WiFi.beginSmartConfig();
    _smartConfigStarted = true;
    _smartConfigStartTime = millis();

    LOG_LN("[SmartConfig] 等待 ESP-Touch 广播...");
}

void WiFiManager::stopSmartConfig() {
    if (!_smartConfigStarted) return;

    LOG_LN("[SmartConfig] 停止 SmartConfig...");

    WiFi.stopSmartConfig();
    _smartConfigStarted = false;
    _smartConfigStartTime = 0;

    LOG_LN("[SmartConfig] 已停止");
}

void WiFiManager::handleSmartConfig() {
    if (!_smartConfigStarted) return;

    if (WiFi.smartConfigDone()) {
        // smartConfigDone() 触发瞬间 STA 通常尚未关联，此时
        // WiFi.SSID()/psk() 返回空串，直接保存会污染 NVS（写入空凭据）。
        // 因此用状态机非阻塞等待 STA 连接成功后再读取凭据。
        if (!_smartConfigProcessing) {
            _smartConfigProcessing = true;
            _smartConfigWaitStart = millis();
            LOG_LN("[SmartConfig] 收到 ESP‑Touch 配置，等待 STA 关联...");
        }

        if (WiFi.status() == WL_CONNECTED) {
            String ssid = WiFi.SSID();
            String password = WiFi.psk();
            LOG_T("[SmartConfig] SSID: %s", ssid.c_str());

            _smartConfigProcessing = false;

            if (ssid.length() == 0) {
                // 已连接却读不到 SSID，属异常，丢弃并停止，避免空写
                LOG_LN("[SmartConfig] SSID 为空，丢弃本次配置");
                stopSmartConfig();
                return;
            }

            saveCredentials(ssid, password);
            stopSmartConfig();
            stopAPMode();
            LOG_LN("[SmartConfig] 凭据已保存，设备即将重启...");
            delay(500);
            ESP.restart();
        } else if (millis() - _smartConfigWaitStart >= 15000) {
            // 等待 15s 仍未关联，放弃本次，重置标志让后续可重试
            LOG_LN("[SmartConfig] 等待 STA 关联超时，重置处理状态");
            _smartConfigProcessing = false;
            stopSmartConfig();
        }
        return;
    }

    // 超时处理：如果未收到配置，停止 SmartConfig
    if (_smartConfigStartTime > 0 && (millis() - _smartConfigStartTime) >= SMART_CONFIG_TIMEOUT_MS) {
        LOG_LN("[SmartConfig] 超时 (2分钟)，停止 SmartConfig");
        stopSmartConfig();
    }
}
