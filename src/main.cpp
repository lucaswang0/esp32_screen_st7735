#include <Arduino.h>
#include "DHT11Sensor.h"
#include <time.h>
#include <SPIFFS.h>
#include "Display.h"
#include "WiFiManager.h"
#include "SensorHistory.h"
#include "ClockPage.h"
#include "ChartPage.h"

#include "MqttManager.h"

// MQTT 配置
#define MQTT_SERVER "10.45.1.3"  // 替换为你的服务器 IP
#define MQTT_PORT 1883

// ============================================================================
// 引脚定义
// ============================================================================
#define DHTPIN1         6
#define DHTPIN2         7
#define PIN_TFT_BL      5
#define BACKLIGHT_CHANNEL 0
#define LED_PIN 8
// ============================================================================
// 时间配置
// ============================================================================
#define TIMEZONE_OFFSET 8           // 北京时间 UTC+8
#define NTP_SERVER1     "ntp1.aliyun.com"
#define NTP_SERVER2     "ntp2.aliyun.com"

// ============================================================================
// 间隔配置
// ============================================================================
#define PAGE_SWITCH_INTERVAL    10000   // 10秒切换页面
#define SAMPLE_INTERVAL         600000  // 10分钟采样
#define WIFI_CHECK_INTERVAL     10000    // 10秒检查WiFi
#define NTP_SYNC_INTERVAL       3600000 // 1小时同步NTP
#define BACKLIGHT_PERCENT       80
#define MQTT_FORCE_PUBLISH_INTERVAL 60000 // 60秒强制发布一次，即使数据未变化

// ============================================================================
// 全局对象
// ============================================================================
DHT11Sensor dht1(DHTPIN1);
DHT11Sensor dht2(DHTPIN2);

WiFiManager wifiManager;
SensorHistory sensorHistory1("sensor1");
SensorHistory sensorHistory2("sensor2");
MqttManager mqtt;
bool discoverySent = false;

struct tm timeinfo;
bool timeSynced = false;
bool forcePageRedraw = false;

// ============================================================================
// 函数声明
// ============================================================================
void setBacklight(uint8_t percent);
bool syncNTP();
void readSensors(float& t1, float& h1, float& t2, float& h2);
void saveSensorData();

// ============================================================================
// 背光控制
// ============================================================================
// 全局变量记录当前亮度
static uint8_t currentBrightness = 80;  // 默认80%
void setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint8_t level = (percent * 255 + 50) / 100;
    ledcWrite(BACKLIGHT_CHANNEL, level);
    currentBrightness = percent;  // 更新当前亮度
    Serial.printf("[Backlight] %u%% (PWM %u/255)\n", percent, level);
}

// 自动调节背光（多档亮度）
void autoAdjustBacklight() {
    getLocalTime(&timeinfo);
    int h = timeinfo.tm_hour;
    
    uint8_t targetBrightness;
    
    if (h >= 6 && h < 8) {
        targetBrightness = 60;   // 早晨 6:00-8:00 逐渐变亮
    } else if (h >= 8 && h < 18) {
        targetBrightness = 80;  // 白天 8:00-18:00 最亮
    } else if (h >= 18 && h < 20) {
        targetBrightness = 60;   // 傍晚 18:00-20:00 逐渐变暗
    } else if (h >= 20 && h < 22) {
        targetBrightness = 40;   // 晚上 20:00-22:00 较暗
    } else {
        targetBrightness = 20;   // 深夜 22:00-6:00 最暗
    }
    
    if (targetBrightness != currentBrightness) {
        setBacklight(targetBrightness);
    }
}

// ============================================================================
// NTP 同步（非阻塞）
// ============================================================================
bool syncNTP() {
    configTime(TIMEZONE_OFFSET * 3600, 0, NTP_SERVER1, NTP_SERVER2);
    
    // 非阻塞等待，最多等5秒
    int retry = 0;
    while (retry < 25) {  // 25 * 200ms = 5秒
        if (getLocalTime(&timeinfo, 0)) {
            Serial.printf("[NTP] 同步成功: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            return true;
        }
        delay(200);
        retry++;
    }
    
    Serial.println("[NTP] 同步失败");
    return false;
}

// ============================================================================
// 读取传感器数据（统一读取，避免重复）
// ============================================================================
void readSensors(float& t1, float& h1, float& t2, float& h2) {
    dht1.update();
    dht2.update();
    
    t1 = dht1.getTemperature();
    h1 = dht1.getHumidity();
    t2 = dht2.getTemperature();
    h2 = dht2.getHumidity();
    
    static float lastT1 = -999, lastH1 = -999, lastT2 = -999, lastH2 = -999;
    
    if (!dht1.isValid() || t1 < -10 || t1 > 60 || h1 < 0 || h1 > 100) {
        if (lastT1 != -999) t1 = lastT1;
        if (lastH1 != -999) h1 = lastH1;
    }
    if (!dht2.isValid() || t2 < -10 || t2 > 60 || h2 < 0 || h2 > 100) {
        if (lastT2 != -999) t2 = lastT2;
        if (lastH2 != -999) h2 = lastH2;
    }
    
    lastT1 = t1;
    lastH1 = h1;
    lastT2 = t2;
    lastH2 = h2;
}

// ============================================================================
// 保存传感器数据到SD卡
// ============================================================================
void saveSensorData() {
    float t1, h1, t2, h2;
    readSensors(t1, h1, t2, h2);
    
    getLocalTime(&timeinfo);
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    
    // 检查是否跨天（在添加数据之前）
    sensorHistory1.checkMidnightReset(h, m);
    sensorHistory2.checkMidnightReset(h, m);
    
    sensorHistory1.addSample(h, m, t1, h1);
    sensorHistory2.addSample(h, m, t2, h2);
    
    sensorHistory1.saveToFile();
    sensorHistory2.saveToFile();
    
    Serial.printf("[Sample] T1=%.1f H1=%.1f T2=%.1f H2=%.1f\n", t1, h1, t2, h2);
}

// ============================================================================
// 页面切换
// ============================================================================
void switchPage(int page) {
    forcePageRedraw = true;
    drawBg();
    Serial.printf("[Page] 切换到页面 %d\n", page);
    
    if (page == 0) {
        drawClockPage();
    } else {
        drawChartPage();
    }
    forcePageRedraw = false;
}

// ============================================================================
// 处理WiFi状态
// ============================================================================
void handleWiFi() {
    if (wifiManager.isAPStarted()) {
        wifiManager.handleClient();
        wifiManager.maintainConnection();
        wifiManager.checkAPTimeout();
    } else if (wifiManager.isConnected()) {
        wifiManager.maintainConnection();
    } else {
        Serial.println("[WiFi] 断连，尝试重连...");
        if (wifiManager.connect()) {
            Serial.println("[WiFi] 重连成功！");
            timeSynced = syncNTP();
        }
    }
}

void sendDiscoveryMessages() {
    // 温度传感器发现
    mqtt.publish(
        "homeassistant/sensor/esp32_temp1/config",
        "{\"name\":\"ESP32 温度1\",\"state_topic\":\"esp32/sensor/temp1\",\"unit_of_measurement\":\"°C\",\"unique_id\":\"esp32_temp1\"}"
    );
    // 湿度传感器发现
    mqtt.publish(
        "homeassistant/sensor/esp32_hum1/config",
        "{\"name\":\"ESP32 湿度1\",\"state_topic\":\"esp32/sensor/hum1\",\"unit_of_measurement\":\"%\",\"unique_id\":\"esp32_hum1\"}"
    );
    // 温度2
    mqtt.publish(
        "homeassistant/sensor/esp32_temp2/config",
        "{\"name\":\"ESP32 温度2\",\"state_topic\":\"esp32/sensor/temp2\",\"unit_of_measurement\":\"°C\",\"unique_id\":\"esp32_temp2\"}"
    );
    // 湿度2
    mqtt.publish(
        "homeassistant/sensor/esp32_hum2/config",
        "{\"name\":\"ESP32 湿度2\",\"state_topic\":\"esp32/sensor/hum2\",\"unit_of_measurement\":\"%\",\"unique_id\":\"esp32_hum2\"}"
    );
}

void publishSensorData() {
    static float lastT1 = -999, lastH1 = -999;
    static float lastT2 = -999, lastH2 = -999;
    static unsigned long lastForcePublish = 0;
    
    dht1.update();
    dht2.update();
    
    float t1 = dht1.getTemperature();
    float h1 = dht1.getHumidity();
    float t2 = dht2.getTemperature();
    float h2 = dht2.getHumidity();
    
    unsigned long now = millis();
    bool forcePublish = (now - lastForcePublish >= MQTT_FORCE_PUBLISH_INTERVAL);
    
    if (forcePublish) {
        lastForcePublish = now;
    }
    
    // 数据变化时发布，或强制发布间隔到达时发布
    if (forcePublish || t1 != lastT1) {
        mqtt.publish("esp32/sensor/temp1", t1);
        lastT1 = t1;
    }
    if (forcePublish || h1 != lastH1) {
        mqtt.publish("esp32/sensor/hum1", h1);
        lastH1 = h1;
    }
    if (forcePublish || t2 != lastT2) {
        mqtt.publish("esp32/sensor/temp2", t2);
        lastT2 = t2;
    }
    if (forcePublish || h2 != lastH2) {
        mqtt.publish("esp32/sensor/hum2", h2);
        lastH2 = h2;
    }
    
    if (forcePublish) {
        Serial.println("[MQTT] 定时强制发布传感器数据");
    }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║         ✨ 现代时钟 v2.0 ✨          ║");
    Serial.println("╚══════════════════════════════════════╝");
    
    // ---- 初始化硬件 ----
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);
    delay(100);
    digitalWrite(2, HIGH);
    delay(150);
    
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(0x0000);
    
    dht1.begin();
    dht2.begin();
    
    // ---- 背光 ----
    ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
    setBacklight(BACKLIGHT_PERCENT);
    
    // ---- LED指示灯 ----
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // ---- WiFi ----
    wifiManager.begin();
    if (wifiManager.connect()) {
        timeSynced = syncNTP();

        // 初始化 MQTT（非阻塞，只设置服务器）
        mqtt.begin(MQTT_SERVER, MQTT_PORT);
        discoverySent = false;
            
} else {
        Serial.println("[WiFi] 进入 AP 配网模式");
        Serial.println("  热点: ESP32-Weather");
        Serial.println("  访问: 192.168.4.1");
        wifiManager.handleClient();
    }
    
    // ---- SPIFFS ----
    if (!SPIFFS.begin(true)) {
        Serial.println("[SPIFFS] 初始化失败");
    }
    
    // ---- 加载历史数据 ----
    sensorHistory1.loadFromFile();
    sensorHistory2.loadFromFile();
    
    // ---- 初始化时钟页面 ----
    initClockPage();
    
    // ---- 初始化图表页面 ----
    initChartPage();
    
    
    // ---- 首次绘制 ----
    Serial.println("[OK] 初始化完成");
    drawBg();
    delay(500);
    switchPage(0);
}


// ============================================================================
// Loop
// ============================================================================
void loop() {
    static unsigned long lastSecond = 0;
    static unsigned long lastWiFiCheck = 0;
    static unsigned long lastPageSwitch = 0;
    static unsigned long lastSample = 0;
    static unsigned long lastNtpSync = 0;
    static unsigned long lastBacklightCheck = 0;
    static unsigned long lastLedToggle = 0;
    static bool ledState = false;
    static int currentPage = 0;
    static bool initialDraw = true;
    static int lastRecordMinute = -1;

    unsigned long now = millis();

    // ---- MQTT 状态机（非阻塞） ----
    mqtt.loop();
    
    // ---- 检测刚连接成功，发送发现消息 ----
    if (mqtt.isJustConnected() && !discoverySent) {
        Serial.println("[MQTT] 连接成功，发送发现消息...");
        sendDiscoveryMessages();
        discoverySent = true;
    }
    
    // ---- LED指示灯控制 ----
    if (wifiManager.isConnected()) {
        digitalWrite(LED_PIN, LOW);
    } else {
        if (now - lastLedToggle >= 500) {
            lastLedToggle = now;
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState);
        }
    }
    
    // ---- 1. 页面切换 (每10秒) ----
    if (now - lastPageSwitch >= PAGE_SWITCH_INTERVAL) {
        lastPageSwitch = now;
        currentPage = 1 - currentPage;
        forcePageRedraw = true;
        drawBg();
        
        if (currentPage == 0) {
            drawClockPage();
        } else {
            drawChartPage();
        }
        Serial.printf("[Page] 切换到页面 %d\n", currentPage);
    }

    // ---- 2. 每秒刷新 (只更新动态部分) ----
    if (now - lastSecond >= 1000) {
        if (currentPage == 0) {
            updateClockPage();
        } else {
            updateChartPage();
        }
        if (mqtt.isConnected()) {
            publishSensorData();  // 发布传感器数据
            // Serial.println("[MQTT] 发布传感器数据,有变化时才发送");
        }
        lastSecond = now;
        forcePageRedraw = false;

    }

// // ---- 3. 定期完整刷新（防止显示异常，可选） ----
// if (now - lastFullRedraw >= 60000) {  // 每60秒完整重绘一次
//     lastFullRedraw = now;
//     drawBg();
//     if (currentPage == 0) {
//         drawClockPage();
//     } else {
//         drawChartPage();
//     }
//     Serial.println("[Page] 定期完整刷新");
// }
    
    // ---- 3. 采集数据 (每10分钟，NTP同步成功后开始，分钟为10的倍数时记录) ----
    if (timeSynced) {
        getLocalTime(&timeinfo);
        int currentMinute = timeinfo.tm_min;
        if (currentMinute % 10 == 0 && currentMinute != lastRecordMinute) {
            lastRecordMinute = currentMinute;
            saveSensorData();
            Serial.printf("[Sample] 定时记录: %02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min);
        }
    }
    
    // ---- 4. WiFi 维护 (每5秒) ----
    if (now - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
        lastWiFiCheck = now;
        handleWiFi();
    }
    
    // ---- 5. NTP 定期同步 (每小时) ----
    // 条件改为：网络已连接 且 (从未同步 或 到达同步间隔)
    if (WiFi.status() == WL_CONNECTED && 
        (!timeSynced || (now - lastNtpSync >= NTP_SYNC_INTERVAL))) {
        
        lastNtpSync = now;
        if (syncNTP()) {
            timeSynced = true;
        } else {
            timeSynced = false;
            // 失败后缩短重试间隔，比如5分钟后重试
            lastNtpSync = now - NTP_SYNC_INTERVAL + 300000; // 5分钟后重试
        }
    }
    
    // ---- 6. 背光自动调节 (每30秒) ----
    if (now - lastBacklightCheck >= 30000) {
        lastBacklightCheck = now;
        autoAdjustBacklight();
    }
    
    delay(50);
}