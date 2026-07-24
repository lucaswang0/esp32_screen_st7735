#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <PubSubClient.h>          // 同步 MQTT 客户端（基于 WiFiClient）
#include <WiFi.h>
#include <freertos/semphr.h>

// ============================================================================
// 调试开关
// ============================================================================
// MQTT_DEBUG = 0   完全静默（发布/连接/重试的常规日志仍输出）
// MQTT_DEBUG = 1   额外输出 TCP 探针、connect() 详细错误码等诊断信息
//                  （修改后需重新编译才能生效）
#ifndef MQTT_DEBUG
#define MQTT_DEBUG  1
#endif

// MQTT 连接状态机（与 PubSubClient 内部状态分离，仅用于本类的重试策略）
enum MqttConnState {
    MQTT_STATE_IDLE,          // 空闲，未启动连接
    MQTT_STATE_WAITING,       // 等待下次重试（指数退避中）
    MQTT_STATE_CONNECTING,    // 已发起连接，等待结果
    MQTT_STATE_CONNECTED,     // 已连接
    MQTT_STATE_SLEEPING       // 休眠（达到最大重试次数，等待定时唤醒）
};

class MqttManager {
public:
    MqttManager();
    ~MqttManager();
    void begin(const char* server, int port,
               const char* user = nullptr, const char* pass = nullptr);
    void loop();
    bool isConnected();    // 非 const：PubSubClient::connected() 不带 const
    bool isJustConnected();

    void publish(const char* topic, const char* payload);
    void publish(const char* topic, float value, int decimals = 1);

private:
    WiFiClient _wifiClient;
    PubSubClient _client;             // 用 _wifiClient 构造
    const char* _server = nullptr;
    int _port = 1883;
    const char* _user = nullptr;
    const char* _pass = nullptr;
    bool _justConnected = false;

    // ============ 重试状态机 ============
    MqttConnState _state = MQTT_STATE_IDLE;
    unsigned long _stateEnterTime = 0;
    unsigned long _nextRetryAt = 0;
    int _retryCount = 0;

    // ============ 可调参数 ============
    static const unsigned long INITIAL_BACKOFF_MS   = 5000;    // 首次重试延迟 5s
    static const unsigned long MAX_BACKOFF_MS       = 600000;  // 最长重试延迟 10min
    static const int           MAX_RETRY_COUNT      = 8;       // 最多重试 8 次后休眠
    static const unsigned long SLEEP_WAKEUP_MS      = 300000;  // 休眠 5 分钟后自动唤醒
    static const unsigned long CONNECT_TIMEOUT_MS   = 10000;   // 一次 connect() 的最长等待

    void tryConnect();             // 同步调用，阻塞至 connect() 返回
    void scheduleNextRetry();
    void enterSleep();
    void resetRetry();
    void onInternalDisconnect();   // Polling 检测到断开
    void transitionTo(MqttConnState newState);
    unsigned long getCurrentBackoff() const;
};

#endif
