#include "SharedState.h"
#include "Log.h"

// ============================================================================
// 全局互斥锁
// ============================================================================
SemaphoreHandle_t xSensorMutex    = NULL;
SemaphoreHandle_t xTimeMutex      = NULL;
SemaphoreHandle_t xHistoryMutex   = NULL;

// ============================================================================
// 全局共享数据
// ============================================================================
SharedSensorData g_sensorData = {0, 0, false, 0};
SharedTimeInfo   g_timeInfo   = {{0}, false, false};

// ============================================================================
// WiFi 状态
// ============================================================================
volatile bool g_wifiConnected = false;
volatile bool g_wifiApMode    = false;

// ============================================================================
// 初始化（setup() 中首先调用）
// ============================================================================
void initSharedState() {
    xSensorMutex    = xSemaphoreCreateMutex();
    xTimeMutex      = xSemaphoreCreateMutex();
    xHistoryMutex   = xSemaphoreCreateMutex();

    if (!xSensorMutex || !xTimeMutex || !xHistoryMutex) {
        LOG_LN("[SharedState] 互斥锁创建失败！");
        while (1) { delay(1000); }
    }
}

// ============================================================================
// 线程安全访问函数
// ============================================================================
SensorSnapshot getSensorSnapshot() {
    SensorSnapshot snap = {0, 0, false, false, false, false, 0};
    if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snap.t1 = g_sensorData.t1;
        snap.h1 = g_sensorData.h1;
        snap.anyData = g_sensorData.valid;
        snap.lastReadMs = g_sensorData.lastReadMs;
        // 范围校验
        // 温度：DHT11 实际量程 0-50°C（0 是边界值，极少出现，>1 更可信）
        // 湿度：DHT11 实际量程 20-90%RH，0 几乎一定是通信异常
        // 全零组合（T=0 且 H=0）一律拒绝——DHT11 内部已过滤，snapshot 兜底
        snap.t1Ok = (snap.t1 > 1.0f && snap.t1 <= 50.0f) && !(snap.t1 == 0 && snap.h1 == 0);
        snap.h1Ok = (snap.h1 >= 20.0f && snap.h1 <= 90.0f) && !(snap.t1 == 0 && snap.h1 == 0);
        snap.allValid = snap.t1Ok && snap.h1Ok;
        xSemaphoreGive(xSensorMutex);
    }
    return snap;
}

void setSensorSnapshot(float t1, float h1,
                       bool t1Ok, bool h1Ok) {
    if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_sensorData.t1 = t1Ok ? t1 : g_sensorData.t1;
        g_sensorData.h1 = h1Ok ? h1 : g_sensorData.h1;
        g_sensorData.valid = true;
        g_sensorData.lastReadMs = millis();
        xSemaphoreGive(xSensorMutex);
    }
}
