#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_wifi.h>

#define WIFI_SSID      "Chinanet-CMCC-01"
#define WIFI_PASS      "Yealink123"
#define MAX_WIFI_CREDENTIALS 5

typedef struct {
    String ssid;
    String password;
} WiFiCredential;

class WiFiManager {
public:
    WiFiManager();
    ~WiFiManager();

    void begin();

    bool connect();
    void disconnect();
    void maintainConnection();
    bool isConnected();

    int getRSSI();
    String getLocalIP();
    String getSSID();

    void startAPMode();
    void stopAPMode();
    bool isAPStarted();
    void handleClient();
    void checkAPTimeout();

    void startSmartConfig();
    void stopSmartConfig();
    void handleSmartConfig();

    void saveCredentials(const String& ssid, const String& password);
    bool hasSavedCredentials();
    void setTxPower(int percentage);

private:
    WebServer* _server;
    DNSServer* _dnsServer;
    Preferences _preferences;

    WiFiCredential _credentials[MAX_WIFI_CREDENTIALS];
    int _credentialCount;

    bool _apMode;
    bool _smartConfigStarted;
    unsigned long _apStartTime;
    unsigned long _smartConfigStartTime;
    unsigned long _lastReconnectAttempt;
    int _reconnectCount;
    int _txPower;

    // ============ 扫描缓存 ============
    String _scanCache;            // 上次扫描的 JSON 结果
    unsigned long _scanCacheTime; // 缓存时间
    int _lastScanStatus;          // -2=未开始, -1=扫描中, >=0=结果数

    static const unsigned long AP_TIMEOUT_MS = 10 * 60 * 1000;
    static const unsigned long SMART_CONFIG_TIMEOUT_MS = 120 * 1000;
    static const unsigned long RECONNECT_INTERVAL = 30000;
    static const int MAX_RECONNECT_ATTEMPTS = 5;
    static const unsigned long SCAN_CACHE_MS = 15000;  // 扫描结果缓存 15s

    bool connectToWiFi(const String& ssid, const String& password);
    void applyTxPower();
    bool loadCredentials();
    void setupWebServer();
    void handleRoot();
    void handleSave();
    void handleScan();
    void handleNotFound();
    String getHTML();
    void performScan();           // 实际执行扫描（带缓存）
    void buildScanJson(String& json);
};

#endif
