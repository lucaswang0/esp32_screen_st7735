#ifndef SENSOR_WEB_SERVER_H
#define SENSOR_WEB_SERVER_H

#include <Arduino.h>
#include "SensorHistory.h"

// 前向声明：避免头文件依赖 ESP32 WebServer 库
class WebServer;

class SensorWebServer {
public:
    SensorWebServer();
    ~SensorWebServer();

    // 初始化（在 setup 中调用一次）
    void begin();

    // 启动 HTTP 服务（WiFi 连接成功后调用）
    void start();

    // 停止 HTTP 服务
    void stop();

    // 处理客户端请求（每帧调用）
    void handleClient();

    // 查询是否运行
    bool isRunning() const { return _running; }

private:
    WebServer* _server;
    bool _running;

    // 引用全局 SensorHistory（在 begin() 中注入）
    SensorHistory* _history1;

    void setupRoutes();
    void handleRoot();
    void handleApiData();
    void handleApiHistory();
    void handleApiDates();
    void handleNotFound();

    // 工具：构造 JSON
    String buildCurrentJson() const;
    String buildHistoryJson(SensorHistory& history, const char* type, const char* date) const;
};

#endif
