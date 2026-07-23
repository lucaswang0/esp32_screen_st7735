#include "MqttManager.h"
#include <WiFiManager.h>

extern WiFiManager wifiManager;

// 用于保护 _justConnected 标志的互斥锁（check-and-clear 非原子）
static portMUX_TYPE s_justConnectedMux = portMUX_INITIALIZER_UNLOCKED;

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

void MqttManager::onConnect(bool sessionPresent) {
    // 临界区保护 _justConnected（与 isJustConnected 的 check-and-clear 互斥）
    portENTER_CRITICAL(&s_justConnectedMux);
    _justConnected = true;
    portEXIT_CRITICAL(&s_justConnectedMux);
    _retryCount = 0; // 连接成功，重置重试计数
    transitionTo(MQTT_STATE_CONNECTED);
    Serial.println("[MQTT] ✅ 连接成功");
}

void MqttManager::onDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.printf("[MQTT] 断开连接, 原因: %d\n", (int)reason);
    // 临界区保护 _justConnected（与 isJustConnected 的 check-and-clear 互斥）
    portENTER_CRITICAL(&s_justConnectedMux);
    _justConnected = false;
    portEXIT_CRITICAL(&s_justConnectedMux);

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
    // 临界区保护 check-and-clear（防止与 onConnect/onDisconnect 竞争）
    portENTER_CRITICAL(&s_justConnectedMux);
    bool was = _justConnected;
    _justConnected = false;
    portEXIT_CRITICAL(&s_justConnectedMux);
    return was;
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
