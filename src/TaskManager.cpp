#include "TaskManager.h"
#include "SharedState.h"
#include "Display.h"
#include "DHT11Sensor.h"
#include "WiFiManager.h"
#include "MqttManager.h"
#include "SensorHistory.h"
#include "SensorWebServer.h"

#include <Arduino.h>
#include <time.h>
#include <SPIFFS.h>

// ============================================================================
// 外部全局对象（在 main.cpp 中定义）
// ============================================================================
extern DHT11Sensor dht1;
extern DHT11Sensor dht2;
extern WiFiManager wifiManager;
extern SensorHistory sensorHistory1;
extern SensorHistory sensorHistory2;
extern MqttManager mqtt;
extern SensorWebServer webServer;
extern bool timeSynced;  // UI 状态显示用（main.cpp 全局）

// 配置常量（从 main.cpp 移过来）
#define TIMEZONE_OFFSET 8
#define NTP_SERVER1   "ntp1.aliyun.com"
#define NTP_SERVER2   "ntp2.aliyun.com"
#define BACKLIGHT_PERCENT 80
#define MQTT_FORCE_PUBLISH_INTERVAL 60000

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
        Serial.println("[TaskNTP] SNTP 已配置 (ntp1.aliyun.com / ntp2.aliyun.com)");
    }

    struct tm t;
    int retry = 0;
    while (retry < 50) {  // 50 * 200ms = 10s
        if (getLocalTime(&t, 0)) {
            Serial.printf("[TaskNTP] NTP 同步成功: %04d-%02d-%02d %02d:%02d:%02d\n",
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
    Serial.println("[TaskNTP] NTP 同步失败 (10秒超时)");
    timeSynced = false;
    return false;
}

static void publishDiscoveryMessages() {
    mqtt.publish("homeassistant/sensor/esp32_temp1/config",
        "{\"name\":\"ESP32 温度1\",\"state_topic\":\"esp32/sensor/temp1\",\"unit_of_measurement\":\"°C\",\"unique_id\":\"esp32_temp1\"}");
    mqtt.publish("homeassistant/sensor/esp32_hum1/config",
        "{\"name\":\"ESP32 湿度1\",\"state_topic\":\"esp32/sensor/hum1\",\"unit_of_measurement\":\"%\",\"unique_id\":\"esp32_hum1\"}");
    mqtt.publish("homeassistant/sensor/esp32_temp2/config",
        "{\"name\":\"ESP32 温度2\",\"state_topic\":\"esp32/sensor/temp2\",\"unit_of_measurement\":\"°C\",\"unique_id\":\"esp32_temp2\"}");
    mqtt.publish("homeassistant/sensor/esp32_hum2/config",
        "{\"name\":\"ESP32 湿度2\",\"state_topic\":\"esp32/sensor/hum2\",\"unit_of_measurement\":\"%\",\"unique_id\":\"esp32_hum2\"}");
}

static void publishSensorData() {
    static float lastT1 = -999, lastH1 = -999, lastT2 = -999, lastH2 = -999;
    static unsigned long lastForcePublish = 0;

    SensorSnapshot s = getSensorSnapshot();
    if (!s.anyData) return;

    float t1 = s.t1, h1 = s.h1, t2 = s.t2, h2 = s.h2;
    if (!s.t1Ok && lastT1 != -999) t1 = lastT1;
    if (!s.h1Ok && lastH1 != -999) h1 = lastH1;
    if (!s.t2Ok && lastT2 != -999) t2 = lastT2;
    if (!s.h2Ok && lastH2 != -999) h2 = lastH2;

    unsigned long now = millis();
    bool forcePublish = (now - lastForcePublish >= MQTT_FORCE_PUBLISH_INTERVAL);
    if (forcePublish) lastForcePublish = now;

    if (forcePublish || t1 != lastT1) { mqtt.publish("esp32/sensor/temp1", t1); if (s.t1Ok) lastT1 = t1; }
    if (forcePublish || h1 != lastH1) { mqtt.publish("esp32/sensor/hum1",  h1); if (s.h1Ok) lastH1 = h1; }
    if (forcePublish || t2 != lastT2) { mqtt.publish("esp32/sensor/temp2", t2); if (s.t2Ok) lastT2 = t2; }
    if (forcePublish || h2 != lastH2) { mqtt.publish("esp32/sensor/hum2",  h2); if (s.h2Ok) lastH2 = h2; }
}

// ============================================================================
// Task 1: Sensor - 每 5s 读 DHT11 一次（独立高优先级任务，避免阻塞 UI）
// ============================================================================
static void taskSensor(void* param) {
    unsigned long okCount = 0;     // 累计成功次数
    unsigned long badCount = 0;    // 累计失败次数（任意值不合法）
    while (1) {
        dht1.update();
        dht2.update();
        float t1 = dht1.getTemperature();
        float h1 = dht1.getHumidity();
        float t2 = dht2.getTemperature();
        float h2 = dht2.getHumidity();
        bool t1Ok = dht1.isValid() && t1 > 1.0f && t1 <= 50.0f;
        bool h1Ok = dht1.isValid() && h1 >= 20.0f && h1 <= 90.0f;
        bool t2Ok = dht2.isValid() && t2 > 1.0f && t2 <= 50.0f;
        bool h2Ok = dht2.isValid() && h2 >= 20.0f && h2 <= 90.0f;
        // 全零组合兜底
        if (t1 == 0 && h1 == 0) { t1Ok = false; h1Ok = false; }
        if (t2 == 0 && h2 == 0) { t2Ok = false; h2Ok = false; }
        setSensorSnapshot(t1, h1, t2, h2, t1Ok, h1Ok, t2Ok, h2Ok);

        // 周期打印当前读数 + 校验结果（让用户能跟温度计核对）
        if (t1Ok && h1Ok && t2Ok && h2Ok) {
            okCount++;
        } else {
            badCount++;
        }
        Serial.printf("[Sensor] T1=%.1f%s H1=%.1f%s T2=%.1f%s H2=%.1f%s | 累计: 成功=%lu 失败=%lu\n",
                      t1, t1Ok ? "" : "✗", h1, h1Ok ? "" : "✗",
                      t2, t2Ok ? "" : "✗", h2, h2Ok ? "" : "✗",
                      okCount, badCount);

        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

// ============================================================================
// Task 2: WiFi - 维护连接，处理 AP 模式请求
// ============================================================================
static void taskWiFi(void* param) {
    static bool webStarted = false;
    static bool initialConnectDone = false;
    static unsigned long lastLedToggle = 0;
    static bool ledState = false;

    while (1) {
        bool connected = wifiManager.isConnected();
        g_wifiConnected = connected;
        g_wifiApMode    = wifiManager.isAPStarted();

        // 首次连接（一次性，connect() 内部 delay() 会让出 CPU）
        if (!initialConnectDone && !wifiManager.isAPStarted()) {
            Serial.println("[TaskWiFi] 首次连接尝试...");
            if (wifiManager.connect()) {
                Serial.println("[TaskWiFi] 首次连接成功");
            }
            initialConnectDone = true;
        }

        if (wifiManager.isAPStarted()) {
            wifiManager.handleClient();
            wifiManager.checkAPTimeout();
        } else if (connected) {
            // 启停 Web 服务跟随 WiFi 状态
            if (!webStarted) {
                webServer.start();
                webStarted = true;
            }
            // 已连接，调用 maintainConnection 重置重连计数
            wifiManager.maintainConnection();
        } else {
            // 断连时停 Web，让 maintainConnection 处理重连（内部 30s 退避）
            if (webStarted) {
                webServer.stop();
                webStarted = false;
            }
            wifiManager.maintainConnection();
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

        // 4 个值中任何一个不合法就跳过本次采样（避免 0/0 等异常值进入历史）
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
                        sensorHistory2.checkMidnightReset(t.tm_hour, t.tm_min);
                        sensorHistory1.addSample(t.tm_hour, t.tm_min, s.t1, s.h1);
                        sensorHistory2.addSample(t.tm_hour, t.tm_min, s.t2, s.h2);
                        sensorHistory1.saveToFile();
                        sensorHistory2.saveToFile();
                        xSemaphoreGive(xHistoryMutex);
                    }
                    Serial.printf("[TaskSample] T1=%.1f H1=%.1f T2=%.1f H2=%.1f\n",
                                  s.t1, s.h1, s.t2, s.h2);
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
            Serial.println("[TaskMQTT] 连接成功，发送发现消息");
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
    Serial.println("[TaskManager] 创建 FreeRTOS 任务...");

    xTaskCreate(taskSensor,    "Sensor",    TASK_STACK_SENSOR,    NULL, PRIO_SENSOR,    NULL);
    xTaskCreate(taskWiFi,      "WiFi",      TASK_STACK_WIFI,      NULL, PRIO_WIFI,      NULL);
    xTaskCreate(taskNTP,       "NTP",       TASK_STACK_NTP,       NULL, PRIO_NTP,       NULL);
    xTaskCreate(taskSample,    "Sample",    TASK_STACK_SAMPLE,    NULL, PRIO_SAMPLE,    NULL);
    xTaskCreate(taskMQTT,      "MQTT",      TASK_STACK_MQTT,      NULL, PRIO_MQTT,      NULL);
    xTaskCreate(taskWeb,       "Web",       TASK_STACK_WEB,       NULL, PRIO_WEB,       NULL);
    xTaskCreate(taskBacklight, "Backlight", TASK_STACK_BACKLIGHT, NULL, PRIO_BACKLIGHT, NULL);

    // UI 任务 = Arduino 的 loop() 任务，使用默认 stack
    Serial.println("[TaskManager] 任务创建完成");
}
