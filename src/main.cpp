#include <Arduino.h>
#include "Log.h"
#include "SharedState.h"
#include "DHT11Sensor.h"
#include <LittleFS.h>
#include "Display.h"
#include "WiFiManager.h"
#include "SensorHistory.h"
#include "ClockPage.h"
#include "FlipClockPage.h"
#include "ChartPage.h"
#include "SysInfoPage.h"
#include "HelpPage.h"
#include "SensorWebServer.h"
#include "MqttManager.h"
#include "TaskManager.h"
#include "Theme.h"
#include <time.h>

// ============================================================================
// 引脚定义
// ============================================================================
#define DHTPIN1         7
#define PIN_TFT_BL      5
#define BACKLIGHT_CHANNEL 0
#define LED_PIN 8
#define PIN_TOUCH       10      // TP223 电容触摸按键

// ============================================================================
// 页面切换 / 触摸手势
// ============================================================================
#define TOUCH_DEBOUNCE_MS       50      // 触摸去抖（最小按下时长）
#define LONG_PRESS_MS           800     // 长按判定阈值
#define DOUBLE_CLICK_MS         300     // 双击判定窗口（也是单击延迟确认时长）
#define WAKE_MS                 10000   // 夜间息屏后单击临时点亮时长
#define BACKLIGHT_PERCENT       80

// ============================================================================
// MQTT 配置
// 服务器通过 mDNS 解析（不再使用固定 IP）：
//   - 主机名以 .local 结尾时，MqttManager 会用 MDNS.queryHost 动态解析其 IP
//   - MQTT broker 需在局域网内通过 mDNS/avahi 通告该主机名
//     例如：mosquitto + avahi 通告为 "mqtt" → 此处填 "mqtt.local"
//          Home Assistant → "homeassistant.local"
//   - 设备本机也会通告为 esp32-<MAC后缀>.local（mDNS 初始化在 MqttManager 内完成）
// ============================================================================
#define MQTT_SERVER "emini.local"
#define MQTT_PORT   1883
#define MQTT_USER   "admin"
#define MQTT_PASS   "lwei2HRTINQz"

// ============================================================================
// 全局对象
// ============================================================================
DHT11Sensor dht1(DHTPIN1);
WiFiManager wifiManager;
SensorHistory sensorHistory1("sensor1");
MqttManager mqtt;
SensorWebServer webServer;

// ============================================================================
// 跨文件全局（ClockPage 仍引用）
// ============================================================================
bool timeSynced = false;         // 同步到 g_timeInfo.synced（保留向后兼容）
volatile uint32_t g_touchCount = 0;   // 触摸次数（SysInfoPage 显示）

// ============================================================================
// 页面表
// 新增页面只需：1) 实现 init/draw/update（可选 animate）；2) 在 kPages 末尾追加一行。
// 数组顺序即触摸切换顺序，页面数量由 PAGE_COUNT 自动推导。
// ============================================================================
struct PageDef {
    void (*init)();     // 初始化（setup 阶段调用一次）
    void (*draw)();     // 整页重绘
    void (*update)();   // 每秒刷新
    void (*animate)();  // 高频动画（20FPS），无则填 nullptr
};

static const PageDef kPages[] = {
    { initClockPage, drawClockPage, updateClockPage, renderFlipClockWidgetAnimation },
    { initChartPage, drawChartPage, updateChartPage, nullptr },
    { initSysInfoPage, drawSysInfoPage, updateSysInfoPage, nullptr },
    { initHelpPage, drawHelpPage, updateHelpPage, nullptr },
};
static const int PAGE_COUNT = sizeof(kPages) / sizeof(kPages[0]);

// ============================================================================
// 触摸手势与屏幕控制状态
// ============================================================================
static int  currentPage = 0;

static bool autoScreenOff = true;       // 21:00~6:00 自动息屏，默认启用
static bool manualOff     = false;      // 用户长按手动息屏
static bool screenOff     = false;      // 背光当前是否关闭
static unsigned long wakeUntil = 0;     // 夜间临时点亮截止时刻

// 触摸手势状态机
static int  lastTouchRaw    = LOW;
static bool pressHeld       = false;
static bool longFired       = false;
static unsigned long pressStart    = 0;
static bool pendingClick    = false;
static unsigned long pendingClickMs = 0;
static unsigned long ignoreUntil   = 0;   // 唤醒后短暂忽略点击（防止双击误触切页）

static unsigned long overlayUntil = 0;    // 屏幕中部状态提示截止时刻
static unsigned long lastSecond   = 0;
static unsigned long lastRender   = 0;

static void setBacklight(bool on) {
    ledcWrite(BACKLIGHT_CHANNEL, on ? (BACKLIGHT_PERCENT * 255 + 50) / 100 : 0);
}

static void redrawPage() {
    drawBg();
    kPages[currentPage].draw();
}

static bool overlayActive() {
    return overlayUntil != 0 && millis() < overlayUntil;
}

// 屏幕中部显示临时状态（双击反馈）
static void showOverlay(const char* msg) {
    tft.setFont(&lgfx::fonts::efontCN_12);
    int w = tft.textWidth(msg) + 12;
    int h = 20;
    int x = (128 - w) / 2;
    int y = 64 - h / 2;
    tft.fillRoundRect(x, y, w, h, 4, currentThemeColors().accent);
    tft.setTextDatum(middle_center);
    tft.setTextColor(currentThemeColors().bg);
    tft.drawString(msg, 64, 64);
    tft.setTextDatum(top_left);
    overlayUntil = millis() + 1500;
}

// 依据时间与手动状态决定背光开/关
static void updateScreenState(unsigned long now) {
    bool isNight = false;
    struct tm t;
    if (getLocalTime(&t, 0)) isNight = (t.tm_hour >= 21 || t.tm_hour < 6);

    bool autoWantsOff = autoScreenOff && isNight;
    bool desiredOn;
    if (autoWantsOff && now >= wakeUntil) desiredOn = false;   // 夜间强制息屏
    else                                  desiredOn = !manualOff;

    if (overlayActive()) desiredOn = true;   // 状态提示期间保持点亮

    if (!desiredOn && !screenOff) {
        screenOff = true;
        pendingClick = false;
        setBacklight(false);
        LOG_LN("[UI] 息屏");
    } else if (desiredOn && screenOff) {
        screenOff = false;
        pendingClick = false;
        setBacklight(true);
        redrawPage();
        LOG_LN("[UI] 亮屏");
    }
}

// 长按：手动息屏
static void handleLongPress() {
    manualOff = true;
    wakeUntil = 0;
    pendingClick = false;
    if (!screenOff) {
        screenOff = true;
        setBacklight(false);
    }
    LOG_LN("[UI] 长按 -> 息屏");
}

// 双击：切换自动息屏功能
static void handleDoubleClick() {
    autoScreenOff = !autoScreenOff;
    showOverlay(autoScreenOff ? "自动息屏:开" : "自动息屏:关");
    LOG_T("[UI] 双击 -> 自动息屏 %s", autoScreenOff ? "开" : "关");
}

// 触摸手势状态机（单击/双击/长按）
static void handleTouch(unsigned long now) {
    int touch = digitalRead(PIN_TOUCH);

    // 按下沿
    if (touch == HIGH && lastTouchRaw == LOW) {
        pressStart = now;
        pressHeld  = true;
        longFired  = false;
    }
    // 长按检测
    if (touch == HIGH && pressHeld && !longFired && now - pressStart >= LONG_PRESS_MS) {
        longFired = true;
        handleLongPress();
    }
    // 抬起沿
    if (touch == LOW && lastTouchRaw == HIGH) {
        pressHeld = false;
        unsigned long dur = now - pressStart;
        if (!longFired && dur >= TOUCH_DEBOUNCE_MS && now >= ignoreUntil) {
            g_touchCount++;
            if (screenOff) {
                // 息屏状态单击：临时点亮，不切页
                wakeUntil   = now + WAKE_MS;
                manualOff   = false;
                ignoreUntil = now + DOUBLE_CLICK_MS + 50;
                LOG_LN("[UI] 单击 -> 唤醒");
            } else if (pendingClick && now - pendingClickMs <= DOUBLE_CLICK_MS) {
                pendingClick = false;
                handleDoubleClick();
            } else {
                pendingClick   = true;
                pendingClickMs = now;
            }
        }
    }
    lastTouchRaw = touch;

    // 单击延迟确认：窗口内无第二次点击则切页
    if (pendingClick && !screenOff && now - pendingClickMs >= DOUBLE_CLICK_MS) {
        pendingClick = false;
        currentPage = (currentPage + 1 >= PAGE_COUNT) ? 0 : currentPage + 1;
        overlayUntil = 0;
        redrawPage();
        LOG_T("[UI] 单击切换到页面 %d", currentPage);
    }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║     ✨ 现代时钟 v2.1 (FreeRTOS) ✨   ║");
    Serial.println("╚══════════════════════════════════════╝");
    LOG_T("[MAIN] 启动 v2.1 (uptime=%lu ms)", millis());

    // ─────────── 硬件探测（一次性打印，用于确定 SysInfoPage 显示项）───────────
    LOG_LN("────────────── 硬件探测 ──────────────");
    LOG_T("Chip   : %s  rev%d  cores=%d",
          ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
    LOG_T("CPU    : %lu MHz", (unsigned long)getCpuFrequencyMhz());
    LOG_T("Flash  : %lu KB (%lu MB)  speed=%lu MHz  mode=%d",
          (unsigned long)(ESP.getFlashChipSize() / 1024),
          (unsigned long)(ESP.getFlashChipSize() / 1048576),
          (unsigned long)(ESP.getFlashChipSpeed() / 1000000),
          (int)ESP.getFlashChipMode());
    LOG_T("Heap   : total %lu KB  free %lu KB  min %lu KB  maxAlloc %lu KB",
          (unsigned long)(ESP.getHeapSize() / 1024),
          (unsigned long)(ESP.getFreeHeap() / 1024),
          (unsigned long)(ESP.getMinFreeHeap() / 1024),
          (unsigned long)(ESP.getMaxAllocHeap() / 1024));
    LOG_T("PSRAM  : %lu KB", (unsigned long)(ESP.getPsramSize() / 1024));
    LOG_T("SDK    : %s", ESP.getSdkVersion());
    LOG_T("Sketch : %lu KB used  free %lu KB",
          (unsigned long)(ESP.getSketchSize() / 1024),
          (unsigned long)(ESP.getFreeSketchSpace() / 1024));
    LOG_LN("──────────────────────────────────────");

    // 1. 互斥锁和共享状态
    initSharedState();

    // 2. 屏幕硬件初始化
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);
    delay(100);
    digitalWrite(2, HIGH);
    delay(150);
    tft.init();
    tft.setRotation(2);
    // 初始化主题（NTP 未同步前按当前小时判断，失败默认白天）
    struct tm themeT;
    if (getLocalTime(&themeT, 0)) {
        g_currentTheme = themeModeFromHour(themeT.tm_hour);
    }
    tft.fillScreen(currentThemeColors().bg);

    // 3. 背光
    ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
    ledcWrite(BACKLIGHT_CHANNEL, (BACKLIGHT_PERCENT * 255 + 50) / 100);

    // 4. LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // 4.1 触摸按键 (TP223，推挽输出，按下为高电平)
    pinMode(PIN_TOUCH, INPUT);

    // 5. 传感器
    dht1.begin();

    // 6. LittleFS
    if (!LittleFS.begin(true)) {
        LOG_LN("[LittleFS] 初始化失败");
    }
    sensorHistory1.loadFromFile();

    // 7. WiFi 管理器（实际连接由 TaskWiFi 异步进行）
    wifiManager.begin();

    // 8. MQTT / Web 服务初始化（实际运行由各自任务处理）
    mqtt.begin(MQTT_SERVER, MQTT_PORT, MQTT_USER, MQTT_PASS);
    webServer.begin();

    // 9. 页面初始化
    for (int i = 0; i < PAGE_COUNT; i++) kPages[i].init();

    // 10. 首次绘制
    LOG_LN("[OK] 初始化完成");
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
    unsigned long now = millis();

    // 1. 触摸手势：单击切页 / 双击自动息屏开关 / 长按息屏 / 息屏单击唤醒
    handleTouch(now);

    // 2. 依据时间与手动状态更新背光（夜间 21:00~6:00 自动息屏）
    updateScreenState(now);

    // 3. 状态提示超时后恢复当前页面
    if (overlayUntil != 0 && now >= overlayUntil) {
        overlayUntil = 0;
        if (!screenOff) redrawPage();
    }

    bool hidden = screenOff || overlayActive();

    // 4. 每秒刷新动态部分
    if (!hidden && now - lastSecond >= 1000) {
        lastSecond = now;
        // 主题切换检测（6:00-18:00 白天，其余夜间）
        struct tm loopT;
        if (getLocalTime(&loopT, 0)) {
            ThemeMode newTheme = themeModeFromHour(loopT.tm_hour);
            if (newTheme != g_currentTheme) {
                g_currentTheme = newTheme;
                redrawPage();
                LOG_T("[UI] 主题切换 -> %s", newTheme == THEME_DAY ? "白天" : "夜间");
            }
        }
        kPages[currentPage].update();
    }

    // 5. 当前页面的高频动画 (20FPS)
    if (!hidden && kPages[currentPage].animate && now - lastRender >= (1000 / 20)) {
        lastRender = now;
        kPages[currentPage].animate();
    }

    // 短延时让出 CPU
    delay(5);
}
