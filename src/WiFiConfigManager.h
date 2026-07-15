#ifndef WIFI_CONFIG_MANAGER_H
#define WIFI_CONFIG_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_wifi.h>  // 添加这个头文件

#define WIFI_SSID      "Froad-Guest"
#define WIFI_PASS      "Tr#d5@gL"
#define MAX_WIFI_CREDENTIALS 5

typedef struct {
    String ssid;
    String password;
} WiFiCredential;

class WiFiConfigManager {
public:
    WiFiConfigManager();
    ~WiFiConfigManager();  // 添加析构函数
    
    void begin();
    bool autoConnect();
    void startConfigPortal();
    void scanNetworks();
    void saveCredentials(const String& ssid, const String& password);
    bool loadCredentials();
    bool hasSavedCredentials();
    IPAddress getIP();
    bool isConfigMode();
    const char* getConfigSSID();
    const char* getConfigPassword();
    void handleClient();
    void startAPMode();
    void stopAPMode();
    bool isAPStarted();
    unsigned long getAPStartTime();
    void checkAutoStop();  // 添加自动停止检查

public:
    void setTxPower(int percentage);

private:
    void startWebServer();
    void stopWebServer();  // 添加停止Web服务器
    void handleRoot();
    void handleScan();
    void handleSave();
    void handleNotFound();
    bool connectToWiFi(const char* ssid, const char* password, int timeoutMs = 20000);
    void applyTxPower();

    Preferences preferences;
    WebServer* webServer;
    bool configMode;
    WiFiCredential savedCredentials[MAX_WIFI_CREDENTIALS];
    int credentialCount;
    bool apStarted;
    unsigned long apStartTime;
    unsigned long lastClientActivity;  // 添加客户端活动时间
    static const unsigned long AP_TIMEOUT_MS = 10 * 60 * 1000;  // 10分钟
    int _txPower;  // 0~100 功率百分比
};

#endif