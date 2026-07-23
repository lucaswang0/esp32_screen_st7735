#include "MqttManager.h"
#include <WiFiManager.h>

extern WiFiManager wifiManager;

MqttManager::MqttManager() {
    _client.onConnect([this](bool sessionPresent) { onConnect(sessionPresent); });
    _client.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { onDisconnect(reason); });
    _client.onPublish([this](uint16_t packetId) { onPublish(packetId); });
}

MqttManager::~MqttManager() {
    _client.disconnect();
}

void MqttManager::begin(const char* server, int port) {
    _server = server;
    _port = port;
    
    _client.setServer(server, port);
    _client.setKeepAlive(60);
    _client.setClientId(("ESP32-" + WiFi.macAddress()).c_str());
    _client.setCleanSession(true);
    
    _justConnected = false;
    transitionTo(MQTT_STATE_IDLE);
    
    Serial.printf("[MQTT] 初始化服务器: %s:%d\n", server, port);
    
    if (wifiManager.isConnected()) {
        transitionTo(MQTT_STATE_CONNECTING);
        tryConnect();
    }
}

void MqttManager::loop() {
    unsigned long now = millis();

    // 1. 基础检查：Wi-Fi 必须连接
    if (!wifiManager.isConnected()) {
        if (_state != MQTT_STATE_IDLE) {
            Serial.println("[MQTT] Wi-Fi 断开，挂起连接状态机");
            transitionTo(MQTT_STATE_IDLE);
        }
        return;
    }

    // 2. 状态机处理
    switch (_state) {
        case MQTT_STATE_IDLE:
            // 如果处于 IDLE 且未连接，立即尝试连接
            if (!_client.connected()) {
                transitionTo(MQTT_STATE_CONNECTING);
                tryConnect();
            }
            break;

        case MQTT_STATE_WAITING:
            // 检查是否到达下一次重试时间点
            if (now >= _nextRetryAt) {
                Serial.printf("[MQTT] 重试间隔结束，尝试第 %d 次连接...\n", _retryCount + 1);
                transitionTo(MQTT_STATE_CONNECTING);
                tryConnect();
            }
            break;

        case MQTT_STATE_CONNECTING:
            // 检查连接是否超时
            if (now - _stateEnterTime >= CONNECT_TIMEOUT_MS) {
                Serial.println("[MQTT] 连接尝试超时，准备进入退避等待");
                scheduleNextRetry();
            }
            break;

        case MQTT_STATE_CONNECTED:
            // 客户端内部断开但回调未触发时，主动检测
            if (!_client.connected()) {
                Serial.println("[MQTT] 检测到客户端已断开（回调未触发）");
                scheduleNextRetry();
            }
            break;

        case MQTT_STATE_SLEEPING:
            // 检查是否到达定时唤醒时间
            if (now - _stateEnterTime >= SLEEP_WAKEUP_MS) {
                Serial.println("[MQTT] 休眠期满，定时唤醒重试...");
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
    Serial.printf("[MQTT] 状态切换: %s -> %s\n", stateNames[oldState], stateNames[newState]);
}

void MqttManager::tryConnect() {
    if (!_client.connected()) {
        _client.connect();
    }
}

void MqttManager::scheduleNextRetry() {
    _retryCount++;
    if (_retryCount >= MAX_RETRY_COUNT) {
        enterSleep();
    } else {
        unsigned long backoff = getCurrentBackoff();
        _nextRetryAt = millis() + backoff;
        Serial.printf("[MQTT] 连接失败，%d s 后重试 (次数: %d/%d)\n", backoff/1000, _retryCount, MAX_RETRY_COUNT);
        transitionTo(MQTT_STATE_WAITING);
    }
}

unsigned long MqttManager::getCurrentBackoff() const {
    // 指数退避: INITIAL * 2^(retry-1)，限制最大移位数避免 UB
    int shift = _retryCount - 1;
    if (shift < 0) shift = 0;
    if (shift > 20) shift = 20;  // 防止 UB：1UL << 31 在 32 位 unsigned long 上是 UB
    unsigned long backoff = INITIAL_BACKOFF_MS * (1UL << shift);
    return (backoff > MAX_BACKOFF_MS) ? MAX_BACKOFF_MS : backoff;
}

void MqttManager::enterSleep() {
    Serial.println("[MQTT] 达到最大重试次数，进入休眠状态 (5min 后自动唤醒)");
    transitionTo(MQTT_STATE_SLEEPING);
}

void MqttManager::resetRetry() {
    _retryCount = 0;
}

void MqttManager::wakeup() {
    if (_state == MQTT_STATE_SLEEPING) {
        Serial.println("[MQTT] 收到手动唤醒指令");
        resetRetry();
        transitionTo(MQTT_STATE_CONNECTING);
        tryConnect();
    }
}

void MqttManager::onConnect(bool sessionPresent) {
    _justConnected = true;
    _retryCount = 0; // 连接成功，重置重试计数
    transitionTo(MQTT_STATE_CONNECTED);
    Serial.println("[MQTT] ✅ 连接成功");
}

void MqttManager::onDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.printf("[MQTT] 断开连接, 原因: %d\n", (int)reason);
    _justConnected = false;
    
    // 主动断开（外部调用 disconnect）不重试
    // CONNECT_TIMEOUT_MS 内由 loop() 处理的超时也不需要这里处理
    if (_state == MQTT_STATE_CONNECTED || _state == MQTT_STATE_CONNECTING) {
        scheduleNextRetry();
    }
}

void MqttManager::onPublish(uint16_t packetId) {
    // 发布确认
}

bool MqttManager::isConnected() const {
    return _state == MQTT_STATE_CONNECTED && _client.connected();
}

bool MqttManager::isJustConnected() {
    if (_justConnected) {
        _justConnected = false;
        return true;
    }
    return false;
}

void MqttManager::publish(const char* topic, const char* payload) {
    if (isConnected()) {
        _client.publish(topic, 1, true, payload);
    } else {
        Serial.printf("[MQTT] 未连接，无法发布: %s\n", topic);
    }
}

void MqttManager::publish(const char* topic, float value, int decimals) {
    char buf[16];
    dtostrf(value, 1, decimals, buf);
    publish(topic, buf);
}

void MqttManager::setAutoDiscovery(bool enable) {
    _autoDiscovery = enable;
}

void MqttManager::sendDiscovery(const char* name, const char* deviceClass,
                                 const char* unitOfMeasurement, const char* stateTopic) {
    if (!isConnected()) {
        Serial.println("[MQTT] 未连接，无法发送发现消息");
        return;
    }
    
    String configTopic = _baseTopic + "/sensor/" + String(stateTopic) + "/config";
    String payload = "{";
    payload += "\"name\":\"" + String(name) + "\",";
    payload += "\"state_topic\":\"" + String(stateTopic) + "\",";
    payload += "\"unit_of_measurement\":\"" + String(unitOfMeasurement) + "\",";
    payload += "\"device_class\":\"" + String(deviceClass) + "\",";
    payload += "\"unique_id\":\"" + String(stateTopic) + "\",";
    payload += "\"device\":{";
    payload += "\"identifiers\":[\"" + _deviceId + "\"],";
    payload += "\"name\":\"ESP32 传感器\",";
    payload += "\"model\":\"ESP32-C3\",";
    payload += "\"manufacturer\":\"Espressif\"";
    payload += "}";
    payload += "}";
    
    uint16_t packetId = _client.publish(configTopic.c_str(), 1, true, payload.c_str());
    if (packetId) {
        Serial.printf("[MQTT] 发现配置已发送: %s\n", configTopic.c_str());
    } else {
        Serial.printf("[MQTT] 发现配置发送失败: %s\n", configTopic.c_str());
    }
}

unsigned long MqttManager::getNextRetryIn() const {
    unsigned long now = millis();
    if (_state == MQTT_STATE_WAITING) {
        return (_nextRetryAt > now) ? (_nextRetryAt - now) : 0;
    }
    if (_state == MQTT_STATE_SLEEPING) {
        unsigned long elapsed = now - _stateEnterTime;
        return (elapsed < SLEEP_WAKEUP_MS) ? (SLEEP_WAKEUP_MS - elapsed) : 0;
    }
    if (_state == MQTT_STATE_CONNECTING) {
        unsigned long elapsed = now - _stateEnterTime;
        return (elapsed < CONNECT_TIMEOUT_MS) ? (CONNECT_TIMEOUT_MS - elapsed) : 0;
    }
    return 0;
}