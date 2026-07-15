#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

class WiFiManager {
public:
    WiFiManager();
    ~WiFiManager();
    
    // 连接管理
    bool connect();
    void disconnect();
    void maintainConnection();
    bool isConnected();
    
    // 状态查询
    int getRSSI();
    const char* getLocalIP();
    const char* getSSID();
    String getMacAddress();
    
    // AP 模式
    void startAPMode();
    void stopAPMode();
    bool isAPMode();
    bool isAPStarted();      // 添加这个方法
    void handleClient();
    void checkAPTimeout();
    
    // 配置管理
    void saveConfig(const String& ssid, const String& password);
    bool loadConfig(String& ssid, String& password);
    void resetConfig();
    
private:
    // Web 服务器和 DNS
    WebServer* _server;
    DNSServer* _dnsServer;
    Preferences _preferences;
    
    // 状态变量
    bool _apMode;
    unsigned long _apStartTime;
    unsigned long _lastReconnectAttempt;
    int _reconnectCount;
    
    // 常量
    static const unsigned long AP_TIMEOUT_MS = 10 * 60 * 1000;  // 10分钟
    static const unsigned long RECONNECT_INTERVAL = 30000;      // 30秒重试间隔
    static const int MAX_RECONNECT_ATTEMPTS = 5;                // 最大重试次数
    
    // 私有方法
    void setupWebServer();
    void handleRoot();
    void handleSave();
    void handleScan();
    void handleNotFound();
    String getHTML();
    bool connectToWiFi(const String& ssid, const String& password);
    bool loadSavedConfig(String& ssid, String& password);
};

#endif