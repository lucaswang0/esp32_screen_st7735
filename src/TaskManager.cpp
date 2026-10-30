#include "TaskManager.h"
#include "Log.h"
#include "SharedState.h"
#include "Display.h"
#include "DHT11Sensor.h"
#include "WiFiManager.h"
#include "MqttManager.h"
#include "SensorHistory.h"
#include "SensorWebServer.h"

#include <Arduino.h>
#include <time.h>

// ============================================================================
// 外部全局对象（在 main.cpp 中定义）
// ============================================================================
extern DHT11Sensor dht1;
extern WiFiManager wifiManager;
extern SensorHistory sensorHistory1;
extern MqttManager mqtt;
extern SensorWebServer webServer;
extern bool timeSynced;  // UI 状态显示用（main.cpp 全局）

// 配置常量（从 main.cpp 移过来）
#define TIMEZONE_OFFSET 8
#define NTP_SERVER1   "ntp1.aliyun.com"
#define NTP_SERVER2   "ntp2.aliyun.com"
#define BACKLIGHT_PERCENT 80
#define MQTT_FORCE_PUBLISH_INTERVAL 60000
// 与 SENSOR_PERIOD_MS 同步：两次发布至少间隔 5 秒，避免噪声抖动刷屏
#define MQTT_MIN_PUBLISH_INTERVAL   5000
// 噪声抑制阈值：变化量超过该值才推送，避免 DHT11 抖动刷屏
#define MQTT_TEMP_DELTA 0.5f
#define MQTT_HUM_DELTA  1.0f

// LED 引脚（用于状态指示）
#define LED_PIN 8

// ============================================================================
// 内部辅助函数
// ============================================================================
static void setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint8_t level = (percent * 255 + 50) / 100;
    ledcWrite(0, level);
}

static void autoAdjustBacklight() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return;
    int h = t.tm_hour;

    uint8_t target;
    if      (h >= 6  && h < 8)  target = 60;
    else if (h >= 8  && h < 18) target = 80;
    else if (h >= 18 && h < 20) target = 60;
    else if (h >= 20 && h < 22) target = 40;
    else                        target = 20;

    static uint8_t current = BACKLIGHT_PERCENT;
    if (target != current) {
        current = target;
        setBacklight(current);
    }
}

static bool syncNTP() {
    // 只在首次调用时调用 configTime()，避免反复重启 SNTP 导致同步无法完成
    static bool s_ntpConfigured = false;
    if (!s_ntpConfigured) {
        configTime(TIMEZONE_OFFSET * 3600, 0, NTP_SERVER1, NTP_SERVER2);
        s_ntpConfigured = true;
        LOG_LN("[TaskNTP] SNTP 已配置 (ntp1.aliyun.com / ntp2.aliyun.com)");
    }

    struct tm t;
    int retry = 0;
    while (retry < 50) {  // 50 * 200ms = 10s
        if (getLocalTime(&t, 0)) {
            LOG_T("[TaskNTP] NTP 同步成功: %04d-%02d-%02d %02d:%02d:%02d",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);
            if (xSemaphoreTake(xTimeMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_timeInfo.timeinfo = t;
                g_timeInfo.synced   = true;
                g_timeInfo.valid    = true;
                xSemaphoreGive(xTimeMutex);
            }
            timeSynced = true;  // 同步 UI 状态全局
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        retry++;
    }
    LOG_LN("[TaskNTP] NTP 同步失败 (10秒超时)");
    timeSynced = false;
    return false;
}

static void publishDiscoveryMessages() {
    mqtt.publish("homeassistant/sensor/esp32_temp1/config",
        "{\"name\":\"ESP32 温度1\",\"state_topic\":\"esp32/sensor/temp1\",\"unit_of_measurement\":\"°C\",\"unique_id\":\"esp32_temp1\"}");
    mqtt.publish("homeassistant/sensor/esp32_hum1/config",
        "{\"name\":\"ESP32 湿度1\",\"state_topic\":\"esp32/sensor/hum1\",\"unit_of_measurement\":\"%\",\"unique_id\":\"esp32_hum1\"}");
}

static void publishSensorData() {
    static float lastT1 = -999, lastH1 = -999;
    static unsigned long lastForcePublish = 0;
    static unsigned long lastPublishMs = 0;

    SensorSnapshot s = getSensorSnapshot();
    if (!s.anyData) return;

    float t1 = s.t1, h1 = s.h1;
    if (!s.t1Ok && lastT1 != -999) t1 = lastT1;
    if (!s.h1Ok && lastH1 != -999) h1 = lastH1;

    unsigned long now = millis();
    bool forcePublish = (now - lastForcePublish >= MQTT_FORCE_PUBLISH_INTERVAL);
    // 节流：与传感器读取周期对齐，强制发布不受限
    bool intervalOk = forcePublish || (now - lastPublishMs >= MQTT_MIN_PUBLISH_INTERVAL);

    if (forcePublish) lastForcePublish = now;

    bool anyPublished = false;
    if (intervalOk && (forcePublish || fabsf(t1 - lastT1) >= MQTT_TEMP_DELTA)) { mqtt.publish("esp32/sensor/temp1", t1); anyPublished = true; if (s.t1Ok) lastT1 = t1; }
    if (intervalOk && (forcePublish || fabsf(h1 - lastH1) >= MQTT_HUM_DELTA) ) { mqtt.publish("esp32/sensor/hum1",  h1); anyPublished = true; if (s.h1Ok) lastH1 = h1; }

    if (anyPublished) lastPublishMs = now;
}

// ============================================================================
// Task 1: Sensor - 每 5s 读 DHT11 一次（独立高优先级任务，避免阻塞 UI）
// ============================================================================
static void taskSensor(void* param) {
    unsigned long okCount = 0;     // 累计成功次数
    unsigned long badCount = 0;    // 累计失败次数（任意值不合法）
    float lastT1 = -999.0f;        // 上次打印的读数
    float lastH1 = -999.0f;
    bool  lastT1Ok = false;        // 上次打印的合法性
    bool  lastH1Ok = false;
    bool  firstLog = true;         // 首次必定打印
    while (1) {
        dht1.update();
        float t1 = dht1.getTemperature();
        float h1 = dht1.getHumidity();
        bool t1Ok = dht1.isValid() && t1 > 1.0f && t1 <= 50.0f;
        bool h1Ok = dht1.isValid() && h1 >= 20.0f && h1 <= 90.0f;
        // 全零组合兜底
        if (t1 == 0 && h1 == 0) { t1Ok = false; h1Ok = false; }
        setSensorSnapshot(t1, h1, t1Ok, h1Ok);

        if (t1Ok && h1Ok) {
            okCount++;
        } else {
            badCount++;
        }
        // 仅在读数或合法性变化时打印，避免稳定时每 5s 刷屏
        bool changed = firstLog || t1 != lastT1 || h1 != lastH1 ||
                       t1Ok != lastT1Ok || h1Ok != lastH1Ok;
        if (changed) {
            LOG_T("[Sensor] T1=%.1f%s H1=%.1f%s | 累计: 成功=%lu 失败=%lu",
                          t1, t1Ok ? "" : "✗", h1, h1Ok ? "" : "✗",
                          okCount, badCount);
            lastT1 = t1; lastH1 = h1;
            lastT1Ok = t1Ok; lastH1Ok = h1Ok;
            firstLog = false;
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

// ============================================================================
// Task 2: WiFi - 维护连接，处理 AP 模式请求
// 说明：AP/配网门户的 handleClient/handleSmartConfig 已由
//       WiFiManager::maintainConnection() 内部处理，此处不再重复调用
// ============================================================================
static void taskWiFi(void* param) {
    static bool webStarted = false;
    static bool initialConnectDone = false;
    static unsigned long lastLedToggle = 0;
    static bool ledState = false;

    while (1) {
        // 首次连接（一次性，connect() 内部 delay() 会让出 CPU）
        if (!initialConnectDone && !wifiManager.isAPStarted()) {
            LOG_LN("[TaskWiFi] 首次连接尝试...");
            if (wifiManager.connect()) {
                LOG_LN("[TaskWiFi] 首次连接成功");
            }
            initialConnectDone = true;
        }

        // 维护连接：内部处理重连 / AP 配网门户
        wifiManager.maintainConnection();

        bool connected = wifiManager.isConnected();
        g_wifiConnected = connected;
        g_wifiApMode    = wifiManager.isAPStarted();

        // 网页服务跟随 WiFi 状态：连接后由 SensorWebServer 提供，
        // AP/配网模式由 WiFiManager 门户提供，二者不同时占用 80 端口
        if (connected) {
            if (!webStarted) {
                webServer.start();
                webStarted = true;
            }
        } else {
            if (webStarted) {
                webServer.stop();
                webStarted = false;
            }
        }

        // LED 指示（断连时 500ms 闪烁）
        if (connected) {
            digitalWrite(LED_PIN, LOW);
        } else {
            unsigned long now = millis();
            if (now - lastLedToggle >= 500) {
                lastLedToggle = now;
                ledState = !ledState;
                digitalWrite(LED_PIN, ledState);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(WIFI_PERIOD_MS));
    }
}

// ============================================================================
// Task 3: NTP - 等待 WiFi，按周期同步时间
// ============================================================================
static void taskNTP(void* param) {
    // 等待 WiFi 连上
    while (!wifiManager.isConnected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    syncNTP();  // 首次同步

    const unsigned long NTP_SYNC_INTERVAL = 3600000UL;       // 1 小时
    const unsigned long NTP_RETRY_SHORT   = 300000UL;        // 5 分钟（失败后）
    unsigned long lastSync = millis();

    while (1) {
        if (wifiManager.isConnected()) {
            unsigned long now = millis();
            bool needSync = false;
            if (xSemaphoreTake(xTimeMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                needSync = !g_timeInfo.synced || (now - lastSync >= NTP_SYNC_INTERVAL);
                xSemaphoreGive(xTimeMutex);
            }
            if (needSync) {
                if (syncNTP()) {
                    lastSync = now;
                } else {
                    lastSync = now - NTP_SYNC_INTERVAL + NTP_RETRY_SHORT;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// Task 4: Sample - 每 10 分钟记录一次数据
// ============================================================================
static void taskSample(void* param) {
    int lastRecordMinute = -1;
    unsigned long lastFallback = 0;  // 即使 NTP 未同步，也定期保存

    while (1) {
        bool synced = false;
        if (xSemaphoreTake(xTimeMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            synced = g_timeInfo.synced && g_timeInfo.valid;
            xSemaphoreGive(xTimeMutex);
        }

        SensorSnapshot s = getSensorSnapshot();
        if (!s.anyData) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        // 任何值不合法就跳过本次采样（避免 0/0 等异常值进入历史）
        if (!s.allValid) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (synced) {
            struct tm t;
            if (getLocalTime(&t, 0)) {
                int currentMinute = t.tm_min;
                if (currentMinute % 10 == 0 && currentMinute != lastRecordMinute) {
                    lastRecordMinute = currentMinute;
                    // 在 history mutex 下写入
                    if (xSemaphoreTake(xHistoryMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                        sensorHistory1.checkMidnightReset(t.tm_hour, t.tm_min);
                        sensorHistory1.addSample(t.tm_hour, t.tm_min, s.t1, s.h1);
                        sensorHistory1.saveToFile();
                        xSemaphoreGive(xHistoryMutex);
                    }
                    LOG_T("[TaskSample] T1=%.1f H1=%.1f",
                                  s.t1, s.h1);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ============================================================================
// Task 5: MQTT - 状态机 + 发布
// ============================================================================
static void taskMQTT(void* param) {
    static bool discoverySent = false;
    static unsigned long lastSecondTick = 0;

    while (1) {
        mqtt.loop();

        if (mqtt.isJustConnected() && !discoverySent) {
            LOG_LN("[TaskMQTT] 连接成功，发送发现消息");
            publishDiscoveryMessages();
            discoverySent = true;
        }
        if (!mqtt.isConnected()) {
            discoverySent = false;
        }

        // 每秒发布一次（如果已连接）
        if (mqtt.isConnected()) {
            unsigned long now = millis();
            if (now - lastSecondTick >= 1000) {
                lastSecondTick = now;
                publishSensorData();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MQTT_PERIOD_MS));
    }
}

// ============================================================================
// Task 6: Web - HTTP 服务循环
// ============================================================================
static void taskWeb(void* param) {
    while (1) {
        if (wifiManager.isConnected() && webServer.isRunning()) {
            webServer.handleClient();
        }
        vTaskDelay(pdMS_TO_TICKS(WEB_PERIOD_MS));
    }
}

// ============================================================================
// Task 7: Backlight - 30s 调一次背光
// ============================================================================
static void taskBacklight(void* param) {
    while (1) {
        autoAdjustBacklight();
        vTaskDelay(pdMS_TO_TICKS(BACKLIGHT_PERIOD_MS));
    }
}

// ============================================================================
// 创建所有任务（在 setup() 末尾调用）
// ============================================================================
void startTasks() {
    LOG_LN("[TaskManager] 创建 FreeRTOS 任务...");

    xTaskCreate(taskSensor,    "Sensor",    TASK_STACK_SENSOR,    NULL, PRIO_SENSOR,    NULL);
    xTaskCreate(taskWiFi,      "WiFi",      TASK_STACK_WIFI,      NULL, PRIO_WIFI,      NULL);
    xTaskCreate(taskNTP,       "NTP",       TASK_STACK_NTP,       NULL, PRIO_NTP,       NULL);
    xTaskCreate(taskSample,    "Sample",    TASK_STACK_SAMPLE,    NULL, PRIO_SAMPLE,    NULL);
    xTaskCreate(taskMQTT,      "MQTT",      TASK_STACK_MQTT,      NULL, PRIO_MQTT,      NULL);
    xTaskCreate(taskWeb,       "Web",       TASK_STACK_WEB,       NULL, PRIO_WEB,       NULL);
    xTaskCreate(taskBacklight, "Backlight", TASK_STACK_BACKLIGHT, NULL, PRIO_BACKLIGHT, NULL);

    // UI 任务 = Arduino 的 loop() 任务，使用默认 stack
    LOG_LN("[TaskManager] 任务创建完成");
}
