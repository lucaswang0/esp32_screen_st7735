#include <Arduino.h>
#include "SharedState.h"
#include "DHT11Sensor.h"
#include <SPIFFS.h>
#include "Display.h"
#include "WiFiManager.h"
#include "SensorHistory.h"
#include "ClockPage.h"
#include "FlipClockPage.h"
#include "ChartPage.h"
#include "SensorWebServer.h"
#include "MqttManager.h"
#include "TaskManager.h"

// ============================================================================
// 引脚定义
// ============================================================================
#define DHTPIN1         6
#define DHTPIN2         7
#define PIN_TFT_BL      5
#define BACKLIGHT_CHANNEL 0
#define LED_PIN 8

// ============================================================================
// 页面切换
// ============================================================================
#define PAGE_SWITCH_INTERVAL    10000   // 10秒切换页面
#define BACKLIGHT_PERCENT       80

// ============================================================================
// MQTT 配置
// ============================================================================
#define MQTT_SERVER "10.45.1.3"
#define MQTT_PORT   1883

// ============================================================================
// 全局对象
// ============================================================================
DHT11Sensor dht1(DHTPIN1);
DHT11Sensor dht2(DHTPIN2);
WiFiManager wifiManager;
SensorHistory sensorHistory1("sensor1");
SensorHistory sensorHistory2("sensor2");
MqttManager mqtt;
SensorWebServer webServer;

// ============================================================================
// 跨文件全局（ClockPage/ChartPage 仍引用）
// ============================================================================
bool timeSynced = false;         // 同步到 g_timeInfo.synced（保留向后兼容）
bool forcePageRedraw = false;    // ChartPage 引用

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║     ✨ 现代时钟 v2.1 (FreeRTOS) ✨   ║");
    Serial.println("╚══════════════════════════════════════╝");

    // 1. 互斥锁和共享状态
    initSharedState();

    // 2. 屏幕硬件初始化
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);
    delay(100);
    digitalWrite(2, HIGH);
    delay(150);
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(0x0000);

    // 3. 背光
    ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
    ledcWrite(BACKLIGHT_CHANNEL, (BACKLIGHT_PERCENT * 255 + 50) / 100);

    // 4. LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // 5. 传感器
    dht1.begin();
    dht2.begin();

    // 6. SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("[SPIFFS] 初始化失败");
    }
    sensorHistory1.loadFromFile();
    sensorHistory2.loadFromFile();

    // 7. WiFi 管理器（实际连接由 TaskWiFi 异步进行）
    wifiManager.begin();

    // 8. MQTT / Web 服务初始化（实际运行由各自任务处理）
    mqtt.begin(MQTT_SERVER, MQTT_PORT);
    webServer.begin();

    // 9. 页面初始化
    initClockPage();
    initFlipClockPage();
    initChartPage();

    // 10. 首次绘制
    Serial.println("[OK] 初始化完成");
    drawBg();
    delay(500);
    drawClockPage();

    // 11. 启动所有 FreeRTOS 任务
    startTasks();
}

// ============================================================================
// UI Loop - 页面切换、显示刷新（最高优先级任务，由 Arduino loop() 执行）
// ============================================================================
void loop() {
    static unsigned long lastSecond   = 0;
    static unsigned long lastPageSwitch = 0;
    static unsigned long lastRender   = 0;
    static int currentPage = 0;

    unsigned long now = millis();

    // 1. 页面切换 (每 10 秒)
    if (now - lastPageSwitch >= PAGE_SWITCH_INTERVAL) {
        lastPageSwitch = now;
        currentPage = (currentPage + 1) % 2;
        drawBg();
        if (currentPage == 0) drawClockPage();
        else                  drawChartPage();
        Serial.printf("[UI] 切换到页面 %d\n", currentPage);
    }

    // 2. 每秒刷新动态部分
    if (now - lastSecond >= 1000) {
        lastSecond = now;
        if (currentPage == 0) updateClockPage();
        else                  updateChartPage();
    }

    // 3. 翻页时钟动画 (20FPS)
    if (currentPage == 0) {
        if (now - lastRender >= (1000 / 20)) {
            lastRender = now;
            renderFlipClockWidgetAnimation();
        }
    }

    // 短延时让出 CPU
    delay(5);
}
