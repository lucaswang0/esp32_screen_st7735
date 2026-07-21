#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <AsyncMqttClient.h>
#include <WiFi.h>

class MqttManager {
public:
    MqttManager();
    ~MqttManager();
    void begin(const char* server, int port);
    void loop();  // 保留兼容性，AsyncMqttClient 不需要轮询
    bool isConnected() const;
    bool isJustConnected();
    void publish(const char* topic, const char* payload);
    void publish(const char* topic, float value, int decimals = 1);
    void setAutoDiscovery(bool enable);
    void sendDiscovery(const char* name, const char* deviceClass, 
                       const char* unitOfMeasurement, const char* stateTopic);

private:
    AsyncMqttClient _client;
    const char* _server = nullptr;
    int _port = 1883;
    bool _autoDiscovery = false;
    String _baseTopic = "homeassistant";
    String _deviceId = "esp32_sensor";
    bool _justConnected = false;

    void onConnect(bool sessionPresent);
    void onDisconnect(AsyncMqttClientDisconnectReason reason);
    void onPublish(uint16_t packetId);
    void connect();
};

#endif