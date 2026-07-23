#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

// ============================================================================
// 共享传感器数据（DHT11 × 2）
// ============================================================================
struct SharedSensorData {
    float t1, h1, t2, h2;  // 温度/湿度，0 表示未读取
    bool valid;            // 至少成功读取过一次
    unsigned long lastReadMs;
};

// ============================================================================
// 共享时间数据
// ============================================================================
struct SharedTimeInfo {
    struct tm timeinfo;
    bool synced;           // NTP 是否同步过
    bool valid;            // timeinfo 当前是否有效
};

// ============================================================================
// 传感器快照（线程安全读取，所有消费者使用此接口而非直接读 DHT11）
// ============================================================================
struct SensorSnapshot {
    float t1, h1, t2, h2;
    bool t1Ok, h1Ok, t2Ok, h2Ok;   // 数值在合理范围内
    bool allValid;                  // 4 个值全部合法（历史采样依据此标志）
    bool anyData;                   // 至少成功读取过一次
    unsigned long lastReadMs;
};

// ============================================================================
// 全局互斥锁
// ============================================================================
extern SemaphoreHandle_t xSensorMutex;     // 保护 SharedSensorData
extern SemaphoreHandle_t xTimeMutex;       // 保护 SharedTimeInfo
extern SemaphoreHandle_t xHistoryMutex;    // 保护 SensorHistory1/2

// ============================================================================
// 全局共享数据
// ============================================================================
extern SharedSensorData g_sensorData;
extern SharedTimeInfo   g_timeInfo;

// ============================================================================
// WiFi 状态（单核 bool 读写安全，无需 mutex）
// ============================================================================
extern volatile bool g_wifiConnected;
extern volatile bool g_wifiApMode;

// ============================================================================
// 初始化（创建所有互斥锁，setup() 开头调用）
// ============================================================================
void initSharedState();

// ============================================================================
// 线程安全访问函数
// ============================================================================

// 获取当前传感器快照（内部加锁，无 SPI 阻塞）
SensorSnapshot getSensorSnapshot();

// 写入当前传感器快照（仅 Sensor 任务调用）
void setSensorSnapshot(float t1, float h1, float t2, float h2,
                       bool t1Ok, bool h1Ok, bool t2Ok, bool h2Ok);

#endif
