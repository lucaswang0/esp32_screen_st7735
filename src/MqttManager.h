#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <AsyncMqttClient.h>
#include <WiFi.h>
#include <freertos/semphr.h>

// MQTT 连接状态（与 AsyncMqttClient 内部状态分离，仅用于本类的重试策略）
enum MqttConnState {
    MQTT_STATE_IDLE,          // 空闲，未启动连接
    MQTT_STATE_WAITING,       // 等待下次重试（指数退避中）
    MQTT_STATE_CONNECTING,    // 已发起连接，等待结果
    MQTT_STATE_CONNECTED,     // 已连接
    MQTT_STATE_SLEEPING       // 休眠（达到最大重试次数，等待定时唤醒或用户干预）
};

class MqttManager {
public:
    MqttManager();
    ~MqttManager();
    void begin(const char* server, int port);
    void loop();
    bool isConnected() const;
    bool isJustConnected();

    void publish(const char* topic, const char* payload);
    void publish(const char* topic, float value, int decimals = 1);

private:
    AsyncMqttClient _client;
    const char* _server = nullptr;
    int _port = 1883;
    bool _justConnected = false;

    // ============ 重试状态机 ============
    MqttConnState _state = MQTT_STATE_IDLE;
    unsigned long _stateEnterTime = 0;   // 进入当前状态的时间（ms）
    unsigned long _nextRetryAt = 0;      // 下一次重试的时间点（ms）
    int _retryCount = 0;                 // 连续失败次数

    // ============ 可调参数 ============
    static const unsigned long INITIAL_BACKOFF_MS = 5000;   // 首次重试延迟 5s
    static const unsigned long MAX_BACKOFF_MS     = 600000; // 最长重试延迟 10min
    static const int MAX_RETRY_COUNT             = 8;       // 最多重试 8 次后休眠
    static const unsigned long SLEEP_WAKEUP_MS   = 300000;  // 休眠 5 分钟后自动唤醒
    static const unsigned long CONNECT_TIMEOUT_MS = 10000;  // 一次连接尝试的最长等待

    void onConnect(bool sessionPresent);
    void onDisconnect(AsyncMqttClientDisconnectReason reason);
    void onPublish(uint16_t packetId);
    void tryConnect();
    void scheduleNextRetry();
    void enterSleep();
    void resetRetry();
    void transitionTo(MqttConnState newState);
    unsigned long getCurrentBackoff() const;
};

#endif