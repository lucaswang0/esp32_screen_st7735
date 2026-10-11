#include "MqttManager.h"
#include "Log.h"           // 全局日志宏（自动加 [HH:MM:SS.mmm] 前缀）
#include <WiFiManager.h>

extern WiFiManager wifiManager;

// 用于保护 _justConnected 标志（与 AsyncMqttClient 版本一致：check-and-clear 非原子）
static portMUX_TYPE s_justConnectedMux = portMUX_INITIALIZER_UNLOCKED;

// PubSubClient 的 state() 数字转字符串（用于 MQTT_DEBUG 诊断）
#if MQTT_DEBUG
static const char* mqttStateName(int s) {
    switch (s) {
        case -4: return "CONNECTION_TIMEOUT";
        case -3: return "CONNECTION_LOST";
        case -2: return "CONNECT_FAILED";
        case -1: return "DISCONNECTED";
        case  0: return "CONNECTED";
        case  1: return "BAD_PROTOCOL";
        case  2: return "BAD_CLIENT_ID";
        case  3: return "UNAVAILABLE";
        case  4: return "BAD_CREDENTIALS";
        case  5: return "UNAUTHORIZED";
        default: return "UNKNOWN";
    }
}
#endif

MqttManager::MqttManager()
    : _client(_wifiClient) {
    // PubSubClient 没有回调构造，所有事件由 loop() 轮询
}

MqttManager::~MqttManager() {
    if (_client.connected()) {
        _client.disconnect();
    }
}

void MqttManager::begin(const char* server, int port,
                        const char* user, const char* pass) {
    _serverHost = server;
    _port = port;
    _user = user;
    _pass = pass;

    // 检测 .local 主机名 → 走 mDNS 解析，避免硬编码 IP；
    // 其余（域名/IP）直接交给 PubSubClient 的内置解析器
    String lower = _serverHost;
    lower.toLowerCase();
    _useMDNS = lower.endsWith(".local");

    if (_useMDNS) {
        LOG_T("[MQTT] 启用 mDNS 解析: %s", _serverHost.c_str());
        // setServer 推迟到 tryConnect()，解析出 IP 后再设置
    } else {
        _client.setServer(_serverHost.c_str(), _port);
    }
    _client.setKeepAlive(60);
    _client.setBufferSize(512);     // 传感器 payload 较小，512 足够
    // socket 超时由 connect() 的 TCP 部分承担（默认 1000ms 内 connect 返回）

    if (user != nullptr && pass != nullptr) {
        LOG_T("[MQTT] 启用认证: user=%s", user);
    } else {
        LOG_LN("[MQTT] 匿名连接（无认证）");
    }

    _justConnected = false;
    transitionTo(MQTT_STATE_IDLE);

    LOG_T("[MQTT] 初始化服务器: %s:%d%s", server, port, _useMDNS ? " (mDNS)" : "");

    if (wifiManager.isConnected()) {
        transitionTo(MQTT_STATE_CONNECTING);
        tryConnect();
    }
}

bool MqttManager::resolveServerMDNS() {
    // MDNS.queryHost 接受短主机名（自动查询 <name>.local），需去掉 .local 后缀
    String host = _serverHost;
    host.toLowerCase();
    if (host.endsWith(".local")) {
        host = host.substring(0, host.length() - 6);
    }

    LOG_T("[MQTT] mDNS 查询: %s.local ...", host.c_str());
    IPAddress ip = MDNS.queryHost(host.c_str(), MDNS_QUERY_TIMEOUT_MS);

    // queryHost 失败时返回 0.0.0.0
    if (ip.toString() == "0.0.0.0") {
        LOG_LN("[MQTT] mDNS 查询失败（broker 未通告或超时）");
        return false;
    }

    _resolvedIP = ip;
    _hasResolvedIP = true;
    LOG_T("[MQTT] mDNS 解析成功: %s -> %s", _serverHost.c_str(), ip.toString().c_str());
    return true;
}

void MqttManager::loop() {
    unsigned long now = millis();

    // 1. 基础检查：Wi-Fi 必须连接
    if (!wifiManager.isConnected()) {
        if (_state != MQTT_STATE_IDLE) {
            LOG_LN("[MQTT] Wi-Fi 断开，挂起连接状态机");
            transitionTo(MQTT_STATE_IDLE);
        }
        return;
    }

    // 2. PubSubClient 必须定期调用 loop() 处理 keepalive PINGREQ
    _client.loop();

    // 3. 状态机处理
    switch (_state) {
        case MQTT_STATE_IDLE:
            // IDLE 状态下，如果未连接，立即发起连接
            if (!_client.connected()) {
                transitionTo(MQTT_STATE_CONNECTING);
                tryConnect();   // 同步阻塞，详见 tryConnect()
            }
            break;

        case MQTT_STATE_WAITING:
            // 等待退避时间到期后再次尝试
            if (now >= _nextRetryAt) {
                LOG_T("[MQTT] 重试间隔结束，尝试第 %d 次连接...", _retryCount + 1);
                transitionTo(MQTT_STATE_CONNECTING);
                tryConnect();
            }
            break;

        case MQTT_STATE_CONNECTING:
            // tryConnect() 内部已完成 connect() 并根据返回值转移到 CONNECTED 或 WAITING
            // 若因其他原因（异常）仍停留在此状态，强制超时重试
            if (now - _stateEnterTime > CONNECT_TIMEOUT_MS) {
                LOG_LN("[MQTT] 状态机异常卡在 CONNECTING，强制重试");
                scheduleNextRetry();
            }
            break;

        case MQTT_STATE_CONNECTED:
            // 已连接：轮询检测实际状态（PubSubClient 没有 onDisconnect 回调）
            if (!_client.connected()) {
                onInternalDisconnect();
            }
            break;

        case MQTT_STATE_SLEEPING:
            // 休眠期满自动唤醒
            if (now - _stateEnterTime > SLEEP_WAKEUP_MS) {
                LOG_LN("[MQTT] 休眠期满，定时唤醒重试...");
                resetRetry();
                transitionTo(MQTT_STATE_CONNECTING);
                tryConnect();
            }
            break;
    }
}

void MqttManager::transitionTo(MqttConnState newState) {
    if (_state == newState) return;

    MqttConnState oldState = _state;
    _state = newState;
    _stateEnterTime = millis();

    const char* stateNames[] = {"IDLE", "WAITING", "CONNECTING", "CONNECTED", "SLEEPING"};
    LOG_T("[MQTT] 状态切换: %s -> %s", stateNames[oldState], stateNames[newState]);
}

void MqttManager::tryConnect() {
    // ============ mDNS 解析（仅 .local 主机名）============
    if (_useMDNS) {
        // 无缓存时才解析；连接失败后缓存会被失效，触发下次重试重新解析
        if (!_hasResolvedIP) {
            if (!resolveServerMDNS()) {
                LOG_LN("[MQTT] mDNS 解析失败，稍后重试");
                scheduleNextRetry();
                return;
            }
        }
        _client.setServer(_resolvedIP, _port);
    }

    // 连接目标（用于日志 / TCP 探针）：mDNS 用解析后的 IP，否则用原始主机名
    String targetStr = _useMDNS ? _resolvedIP.toString() : _serverHost;
    const char* connectTarget = targetStr.c_str();

    // ============ 调试：TCP 探针（仅 MQTT_DEBUG 开启时执行，且仅首次）============
    #if MQTT_DEBUG
    static bool probed = false;
    if (!probed) {
        probed = true;
        LOG_T("[MQTT] 本地: %s, 网关: %s, DNS: %s",
            WiFi.localIP().toString().c_str(),
            WiFi.gatewayIP().toString().c_str(),
            WiFi.dnsIP().toString().c_str());
        LOG_T("[MQTT] 目标: %s:%d (%s)", connectTarget, _port, _serverHost.c_str());
        Serial.printf(LOG_TIME_FMT "[MQTT] TCP 探针: ", LOG_TIME_VAL);
        WiFiClient test;
        if (test.connect(connectTarget, _port, 5000)) {
            Serial.println("✅ 通");
            test.stop();
        } else {
            Serial.printf("❌ 失败 (errno=%d)\n", test.getWriteError());
        }
    }
    #endif
    // ============ 调试结束 ============

    // 生成 clientId（保留原 AsyncMqttClient 版本命名风格）
    String clientId = "ESP32-" + WiFi.macAddress();

    LOG_T("[MQTT] 尝试连接 %s @ %s:%d ...", clientId.c_str(), connectTarget, _port);

    // 同步 connect：阻塞到 TCP + MQTT CONNECT/CONNACK 完成（或超时）
    bool ok;
    if (_user != nullptr && _pass != nullptr) {
        ok = _client.connect(clientId.c_str(), _user, _pass);
    } else {
        ok = _client.connect(clientId.c_str());
    }

    if (ok) {
        portENTER_CRITICAL(&s_justConnectedMux);
        _justConnected = true;
        portEXIT_CRITICAL(&s_justConnectedMux);
        _retryCount = 0;
        transitionTo(MQTT_STATE_CONNECTED);
        LOG_LN("[MQTT] ✅ 连接成功");
    } else {
        #if MQTT_DEBUG
        LOG_T("[MQTT] ❌ connect() 失败, state=%d (%s)",
            _client.state(), mqttStateName(_client.state()));
        #endif
        // 连接失败：失效 mDNS 缓存，下次重试重新解析（broker IP 可能已变）
        if (_useMDNS) _hasResolvedIP = false;
        scheduleNextRetry();
    }
}

void MqttManager::scheduleNextRetry() {
    _retryCount++;
    if (_retryCount >= MAX_RETRY_COUNT) {
        enterSleep();
    } else {
        unsigned long backoff = getCurrentBackoff();
        _nextRetryAt = millis() + backoff;
        LOG_T("[MQTT] 连接失败，%d s 后重试 (次数: %d/%d)",
            backoff / 1000, _retryCount, MAX_RETRY_COUNT);
        transitionTo(MQTT_STATE_WAITING);
    }
}

unsigned long MqttManager::getCurrentBackoff() const {
    // 指数退避: INITIAL * 2^(retry-1)，限制最大移位数避免 UB
    int shift = _retryCount - 1;
    if (shift < 0) shift = 0;
    if (shift > 20) shift = 20;  // 1UL << 31 在 32 位 unsigned long 上是 UB
    unsigned long backoff = INITIAL_BACKOFF_MS * (1UL << shift);
    return (backoff > MAX_BACKOFF_MS) ? MAX_BACKOFF_MS : backoff;
}

void MqttManager::enterSleep() {
    LOG_LN("[MQTT] 达到最大重试次数，进入休眠状态 (5min 后自动唤醒)");
    transitionTo(MQTT_STATE_SLEEPING);
}

void MqttManager::resetRetry() {
    _retryCount = 0;
}

void MqttManager::onInternalDisconnect() {
    // 在 CONNECTED 状态被 loop() 轮询到掉线时调用
    #if MQTT_DEBUG
    LOG_T("[MQTT] 断开连接, state=%d (%s)",
        _client.state(), mqttStateName(_client.state()));
    #else
    LOG_LN("[MQTT] 断开连接");
    #endif

    portENTER_CRITICAL(&s_justConnectedMux);
    _justConnected = false;
    portEXIT_CRITICAL(&s_justConnectedMux);

    scheduleNextRetry();
}

bool MqttManager::isConnected() {
    return _state == MQTT_STATE_CONNECTED && _client.connected();
}

bool MqttManager::isJustConnected() {
    portENTER_CRITICAL(&s_justConnectedMux);
    bool was = _justConnected;
    _justConnected = false;
    portEXIT_CRITICAL(&s_justConnectedMux);
    return was;
}

void MqttManager::publish(const char* topic, const char* payload) {
    if (isConnected()) {
        bool ok = _client.publish(topic, payload, true);   // retained=true
        if (!ok) {
            LOG_T("[MQTT] publish 失败: %s", topic);
        }
    } else {
        LOG_T("[MQTT] 未连接，无法发布: %s", topic);
    }
}

void MqttManager::publish(const char* topic, float value, int decimals) {
    char buf[16];
    dtostrf(value, 1, decimals, buf);
    publish(topic, buf);
}
