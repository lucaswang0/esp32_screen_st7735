#include "WiFiManager.h"

// ============================================================================
// HTML 页面模板（配网页面）
// ============================================================================
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

// ============================================================================
// WiFiManager 实现
// ============================================================================
WiFiManager::WiFiManager() 
    : _server(nullptr), _dnsServer(nullptr), _apMode(false), 
      _apStartTime(0), _lastReconnectAttempt(0), _reconnectCount(0) {}

WiFiManager::~WiFiManager() {
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

// ============================================================================
// 配置存储
// ============================================================================
void WiFiManager::saveConfig(const String& ssid, const String& password) {
    _preferences.begin("wifi", false);
    _preferences.putString("ssid", ssid);
    _preferences.putString("password", password);
    _preferences.end();
    Serial.printf("[WiFi] 配置已保存: %s\n", ssid.c_str());
}

bool WiFiManager::loadConfig(String& ssid, String& password) {
    _preferences.begin("wifi", true);
    ssid = _preferences.getString("ssid", "");
    password = _preferences.getString("password", "");
    _preferences.end();
    return ssid.length() > 0;
}

void WiFiManager::resetConfig() {
    _preferences.begin("wifi", false);
    _preferences.clear();
    _preferences.end();
    Serial.println("[WiFi] 配置已清除");
}

bool WiFiManager::loadSavedConfig(String& ssid, String& password) {
    return loadConfig(ssid, password);
}

// ============================================================================
// WiFi 连接
// ============================================================================
bool WiFiManager::connectToWiFi(const String& ssid, const String& password) {
    if (ssid.length() == 0) return false;
    
    WiFi.mode(WIFI_STA);
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
    
    // 1. 尝试加载保存的配置
    String ssid, password;
    if (loadSavedConfig(ssid, password) && ssid.length() > 0) {
        Serial.printf("[WiFi] 加载配置: %s\n", ssid.c_str());
        if (connectToWiFi(ssid, password)) {
            return true;
        }
    }
    
    // 2. 连接失败，进入 AP 模式
    Serial.println("[WiFi] 无法连接，进入 AP 模式");
    startAPMode();
    return false;
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WiFi] 已断开连接");
}

void WiFiManager::maintainConnection() {
    // AP 模式下不维护
    if (_apMode) {
        handleClient();
        return;
    }
    
    // 已连接，检查状态
    if (WiFi.status() == WL_CONNECTED) {
        _reconnectCount = 0;
        return;
    }
    
    // 断连重连（限制频率）
    unsigned long now = millis();
    if (now - _lastReconnectAttempt < RECONNECT_INTERVAL) {
        return;
    }
    _lastReconnectAttempt = now;
    
    _reconnectCount++;
    Serial.printf("[WiFi] 断连，尝试重连 (尝试 %d)\n", _reconnectCount);
    
    if (_reconnectCount > MAX_RECONNECT_ATTEMPTS) {
        Serial.println("[WiFi] 重连次数过多，进入 AP 模式");
        startAPMode();
        return;
    }
    
    // 尝试重连
    String ssid, password;
    if (loadSavedConfig(ssid, password) && ssid.length() > 0) {
        connectToWiFi(ssid, password);
    } else {
        startAPMode();
    }
}

// ============================================================================
// AP 模式
// ============================================================================
void WiFiManager::startAPMode() {
    if (_apMode) return;
    
    // 断开原有连接
    WiFi.disconnect();
    delay(100);
    
    // 获取 MAC 地址作为 AP 名称后缀
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String apSSID = "ESP32-" + mac.substring(6, 12);
    
    // 启动 AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID.c_str(), "12345678");
    
    _apMode = true;
    _apStartTime = millis();
    _reconnectCount = 0;
    
    // 设置 DNS 和 Web 服务器
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

// ============================================================================
// Web 服务器
// ============================================================================
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
    
    // 保存配置
    saveConfig(ssid, password);
    
    // 尝试连接
    String msg;
    if (connectToWiFi(ssid, password)) {
        msg = "✅ 连接成功！设备正在重启...";
        _server->send(200, "text/plain", msg);
        stopAPMode();
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
            json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
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

// ============================================================================
// 状态查询
// ============================================================================
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

bool WiFiManager::isAPStarted() {
    return _apMode;
}