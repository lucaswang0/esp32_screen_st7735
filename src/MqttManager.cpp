#include "MqttManager.h"
#include <WiFiManager.h>

extern WiFiManager wifiManager;

MqttManager::MqttManager() {
    // 设置回调
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
    
    Serial.printf("[MQTT] 服务器: %s:%d\n", server, port);
    
    // WiFi 已连接时立即尝试连接
    if (wifiManager.isConnected()) {
        connect();
    }
}

void MqttManager::loop() {
    // AsyncMqttClient 内部事件驱动，不需要轮询
    // 保留空函数以兼容 main.cpp 的调用
    if (wifiManager.isConnected() && !_client.connected()) {
        connect();
    }
}

bool MqttManager::isConnected() const {
    return _client.connected();
}

bool MqttManager::isJustConnected() {
    if (_justConnected) {
        _justConnected = false;
        return true;
    }
    return false;
}

void MqttManager::publish(const char* topic, const char* payload) {
    if (_client.connected()) {
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
    if (!_client.connected()) {
        Serial.println("[MQTT] 未连接，无法发送发现消息");
        return;
    }
    
    // 构建发现主题
    String configTopic = _baseTopic + "/sensor/";
    configTopic += stateTopic;
    configTopic += "/config";
    
    // 构建 JSON 消息
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

void MqttManager::connect() {
    if (!_client.connected() && wifiManager.isConnected()) {
        Serial.println("[MQTT] 尝试连接...");
        _client.connect();
    }
}

void MqttManager::onConnect(bool sessionPresent) {
    _justConnected = true;
    Serial.println("[MQTT] ✅ 连接成功");
}

void MqttManager::onDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.printf("[MQTT] 断开连接, 原因: %d\n", (int)reason);
    _justConnected = false;
    // AsyncMqttClient 会自动重连，但我们在 loop() 中也做一次保险
}

void MqttManager::onPublish(uint16_t packetId) {
    // 可选：发布确认回调
    // Serial.printf("[MQTT] 发布确认 packetId=%u\n", packetId);
}