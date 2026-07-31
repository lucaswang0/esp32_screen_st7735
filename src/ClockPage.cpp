#include "ClockPage.h"
#include "Log.h"
#include "Display.h"
#include "DHT11Sensor.h"
#include "SharedState.h"
#include <time.h>
#include "WiFiManager.h"
#include "FlipClockPage.h"

extern DHT11Sensor dht1;
extern WiFiManager wifiManager;
extern bool timeSynced;

#define ENABLE_DEBUG false

#define BG_COLOR       tft.color565(8, 8, 20)
#define ACCENT_COLOR   tft.color565(100, 200, 255)
#define DIM_TEXT       tft.color565(180, 180, 200)
#define WHITE          tft.color565(255, 255, 255)
#define GREEN          tft.color565(80, 255, 80)
#define RED            tft.color565(255, 80, 80)
#define TFT_CYAN       tft.color565(0, 255, 255)
#define TEMP_RED       tft.color565(255, 50, 50)     // 温度红
#define HUM_BLUE       tft.color565(50, 100, 255)    // 湿度蓝
#define TFT_ORANGE     tft.color565(255, 165, 0)
#define TFT_GREEN      tft.color565(0, 255, 0)
#define CLEAR_COLOR    tft.color565(0, 0, 0)
#define DEBUG_BORDER   tft.color565(255, 255, 0)

const int STATUS_Y      = 0;
const int STATUS_H      = 12;

const int TIME_Y        = STATUS_Y + STATUS_H + 4;
const int TIME_H        = 32;

const int DATE_Y        = TIME_Y + TIME_H + 4;
const int DATE_H        = 18;

const int SENSOR_Y       = DATE_Y + DATE_H + 3;
const int SENSOR_H       = 18;

const int BOTTOM_Y       = SENSOR_Y + SENSOR_H + 4;
const int BOTTOM_H       = 128 - BOTTOM_Y;

const int LABEL_X       = 0;

const char* WEEK_DAYS[] = {"日", "一", "二", "三", "四", "五", "六"};

static bool lastWifi = false;
static int lastRssi = 999;
static bool lastNtp = false;
static bool showIpMode = false;
static unsigned long lastStatusSwitch = 0;

static int lastH = -1, lastM = -1, lastS = -1;
static int lastDisplayMinute = -1;  // 用于检测分钟是否变化

static int lastDay = -1, lastWday = -1;
static int lastDisplayDay = -1;

static float lastValues[2] = {-999, -999};
// 传感器行：上一次绘制的文本（用于用背景色擦除残留）
static char lastBuf1[20] = "";
static char lastBuf2[20] = "";

static int lastSeconds = -1;

extern void clearRect(int x, int y, int w, int h);

int getFontHeight(const lgfx::IFont* font) {
    if (font == nullptr) return 0;
    if (font == &lgfx::fonts::Font7) return 28;
    if (font == &lgfx::fonts::efontCN_12) return 12;
    if (font == &lgfx::fonts::efontCN_14) return 14;
    if (font == &lgfx::fonts::efontCN_16) return 16;
    if (font == &lgfx::fonts::Font0) return 8;
    return 12;
}

void debugPrint(const char* region, int y, int presetH, const lgfx::IFont* font, const char* content) {
    if (!ENABLE_DEBUG) return;
    int actualH = getFontHeight(font);
    int endY = y + presetH - 1;
    Serial.printf(LOG_TIME_FMT "[%s] y=%d~%d | 预设高度=%d | 字体高度=%d | ", LOG_TIME_VAL,
                  region, y, endY, presetH, actualH);
    if (actualH > presetH) {
        Serial.printf("⚠️ 字体超出区域 %dpx!\n", actualH - presetH);
    } else if (actualH < presetH - 4) {
        Serial.printf("📏 字体偏小 (剩余%dpx)\n", presetH - actualH);
    } else {
        Serial.printf("✅ 高度匹配\n");
    }
    Serial.printf(LOG_TIME_FMT "    └─ 内容: %s\n", LOG_TIME_VAL, content);
}

void clearRectDebug(int x, int y, int w, int h, const char* region) {
    clearRect(x, y, w, h);
    if (ENABLE_DEBUG) {
        tft.drawRect(x, y, w, h, DEBUG_BORDER);
    }
}

// ========== 优化后的 drawStatusBar ==========
void drawStatusBar(bool wifiConnected, int rssi, bool ntpOk, bool force) {
    static unsigned long lastBlink = 0;
    static bool blinkOn = true;
    static bool lastApMode = false;

    if (g_wifiApMode) {
        unsigned long now = millis();
        if (now - lastBlink >= 500) {
            lastBlink = now;
            blinkOn = !blinkOn;
            force = true;
        }
        if (force) {
            clearRectDebug(0, STATUS_Y, 128, STATUS_H, "状态栏");
            tft.setFont(&lgfx::fonts::efontCN_12);
            tft.setTextDatum(middle_center);
            if (blinkOn) {
                tft.setTextColor(TFT_ORANGE);
                tft.drawString("请配置WIFI进行联网", 64, STATUS_Y + STATUS_H / 2);
            }
            tft.setTextDatum(top_left);
        }
        lastApMode = true;
        lastWifi = wifiConnected;
        lastRssi = rssi;
        lastNtp = ntpOk;
        return;
    }

    if (lastApMode) {
        force = true;
        lastApMode = false;
        showIpMode = false;
        lastStatusSwitch = millis();
    }

    bool wifiChanged = (wifiConnected != lastWifi || rssi != lastRssi);
    bool ntpChanged = (ntpOk != lastNtp);
    
    {
        unsigned long now = millis();
        if (wifiConnected && (now - lastStatusSwitch >= 5000)) {
            showIpMode = !showIpMode;
            lastStatusSwitch = now;
            force = true;
        }
    }
    
    if (!force && !wifiChanged && !ntpChanged) return;
    
    clearRectDebug(0, STATUS_Y, 128, STATUS_H, "状态栏");
    tft.setFont(&lgfx::fonts::efontCN_12);
    tft.setTextDatum(top_left);
    
    tft.setTextColor(DIM_TEXT);
    tft.drawString("WiFi", 0, STATUS_Y);

    if (wifiConnected) {
        if (showIpMode) {
            String ip = wifiManager.getLocalIP();
            tft.setTextColor(ACCENT_COLOR);
            tft.drawString(ip, 28, STATUS_Y);
        } else {
            tft.setTextColor(GREEN);
            tft.drawString("●", 28, STATUS_Y);
            char rssiBuf[6];
            snprintf(rssiBuf, sizeof(rssiBuf), "%d", rssi);
            tft.setTextColor(DIM_TEXT);
            tft.drawString(rssiBuf, 38, STATUS_Y);
        }
    } else {
        tft.setTextColor(RED);
        tft.drawString("○", 28, STATUS_Y);
    }

    tft.setTextDatum(top_right);
    tft.setTextColor(ntpOk ? GREEN : RED);
    tft.drawString(ntpOk ? "NTP●" : "NTP○", 127, STATUS_Y);
    tft.setTextDatum(top_left);
    
    lastWifi = wifiConnected;
    lastRssi = rssi;
    lastNtp = ntpOk;
}

// ========== 优化后的 drawTime ==========
void drawTime(int h, int m, int s, bool force) {
    int currentMinute = h * 60 + m;
    
    // 如果没有任何变化，直接返回
    if (!force && currentMinute == lastDisplayMinute && s == lastS) {
        return;
    }
    
    // 如果只是秒变化，且翻页时钟支持单独更新秒数
    if (!force && currentMinute == lastDisplayMinute && s != lastS) {
        // 只更新秒数（如果翻页时钟支持）
        // 如果不支持单独更新秒数，则注释掉这个分支，让下面的代码执行
        #ifdef FLIP_CLOCK_SUPPORTS_SECOND_UPDATE
        updateFlipClockSeconds(s);
        lastS = s;
        return;
        #endif
    }
    
    // 分钟变化或强制刷新时才清空重绘
    clearRectDebug(0, TIME_Y, 128, TIME_H, "时间");

    if (force) {
        drawFlipClockWidget(h, m, s);
    } else {
        updateFlipClockWidget(h, m, s);
    }
    
    lastH = h;
    lastM = m;
    lastS = s;
    lastDisplayMinute = currentMinute;
}

// ========== 优化后的 drawDate ==========
void drawDate(int year, int month, int day, int wday, bool force) {
    int currentDay = year * 10000 + month * 100 + day;
    
    if (!force && currentDay == lastDisplayDay) {
        return;
    }
    
    clearRectDebug(0, DATE_Y, 128, DATE_H, "日期");
    
    tft.setFont(&lgfx::fonts::efontCN_16);
    tft.setTextColor(WHITE);
    tft.setTextDatum(top_left);
    
    char dateBuf[20];
    snprintf(dateBuf, sizeof(dateBuf), "%d/%02d/%02d", year, month, day);
    tft.drawString(dateBuf, 0, DATE_Y);

    tft.setTextColor(ACCENT_COLOR);
    tft.setTextDatum(top_right);

    char weekBuf[8];
    snprintf(weekBuf, sizeof(weekBuf), "周%s", WEEK_DAYS[wday]);
    tft.drawString(weekBuf, 127, DATE_Y);
    
    lastDay = day;
    lastWday = wday;
    lastDisplayDay = currentDay;
}

// ========== 优化后的 drawSensorRow ==========
void drawSensorRow(int y, const char* label1, float value1, uint16_t color1,
                   const char* label2, float value2, uint16_t color2, bool force) {
    if (y != SENSOR_Y) return;

    // 值变化超过阈值才更新
    bool changed = (force || 
                    abs(value1 - lastValues[0]) > 0.05f || 
                    abs(value2 - lastValues[1]) > 0.05f);
    if (!changed) return;

    tft.setFont(&lgfx::fonts::efontCN_14);
    
    // 准备新文本
    char buf1[20];
    snprintf(buf1, sizeof(buf1), "%s:%.1f°C", label1, value1);
    char buf2[20];
    snprintf(buf2, sizeof(buf2), "%.1f%% %s", value2, label2);

    // 先用背景色擦除上一帧文本（避免新文本比旧文本短时残留字尾）
    if (lastBuf1[0] != '\0') {
        tft.setTextDatum(top_left);
        tft.setTextColor(BG_COLOR);
        tft.drawString(lastBuf1, LABEL_X, y);
    }
    if (lastBuf2[0] != '\0') {
        tft.setTextDatum(top_right);
        tft.setTextColor(BG_COLOR);
        tft.drawString(lastBuf2, 127, y);
    }

    // 绘制新文本
    tft.setTextDatum(top_left);
    tft.setTextColor(color1);
    tft.drawString(buf1, LABEL_X, y);
    tft.setTextDatum(top_right);
    tft.setTextColor(color2);
    tft.drawString(buf2, 127, y);

    // 缓存新文本作为下一次的"旧文本"
    strncpy(lastBuf1, buf1, sizeof(lastBuf1));
    lastBuf1[sizeof(lastBuf1) - 1] = '\0';
    strncpy(lastBuf2, buf2, sizeof(lastBuf2));
    lastBuf2[sizeof(lastBuf2) - 1] = '\0';

    // 分隔线只需要绘制一次（首次或强制刷新时）
    static bool lineDrawn = false;
    if (force || !lineDrawn) {
        tft.drawLine(64, y + 2, 64, y + SENSOR_H - 2, DIM_TEXT);
        lineDrawn = true;
    }

    lastValues[0] = value1;
    lastValues[1] = value2;
}

// ========== 优化后的 drawBottomStatus ==========
void drawBottomStatus(unsigned long uptime, bool force) {
    int seconds = (int)(uptime / 1000);
    if (!force && seconds == lastSeconds) return;
    
    char buf[20];
    if (seconds < 60) {
        snprintf(buf, sizeof(buf), "运行:%ds", seconds);
    } else if (seconds < 3600) {
        snprintf(buf, sizeof(buf), "运行:%dm%ds", seconds/60, seconds%60);
    } else {
        snprintf(buf, sizeof(buf), "运行:%dh%dm", seconds/3600, (seconds%3600)/60);
    }
    
    tft.setFont(&lgfx::fonts::efontCN_12);
    tft.setTextDatum(top_left);

    // 清空底部状态条左侧文本区域，避免旧文本残留
    clearRectDebug(0, BOTTOM_Y, 64, BOTTOM_H, "底部状态-文本");

    // 绘制新文本
    tft.setTextColor(DIM_TEXT);
    tft.drawString(buf, 0, BOTTOM_Y);

    // 传感器状态LED只在首次或强制刷新时绘制
    static bool ledDrawn = false;
    if (force || !ledDrawn) {
        tft.setTextDatum(top_right);
        tft.setTextColor(GREEN);
        tft.drawString("传感器●", 127, BOTTOM_Y);
        tft.setTextDatum(top_left);
        ledDrawn = true;
    }
    
    lastSeconds = seconds;
}

// ========== 初始化 ==========
void initClockPage() {
    lastWifi = false;
    lastRssi = 999;
    lastNtp = false;
    showIpMode = false;
    lastStatusSwitch = millis();
    lastH = -1;
    lastM = -1;
    lastS = -1;
    lastDisplayMinute = -1;
    lastDay = -1;
    lastWday = -1;
    lastDisplayDay = -1;
    for (int i = 0; i < 2; i++) {
        lastValues[i] = -999;
    }
    lastBuf1[0] = '\0';
    lastBuf2[0] = '\0';
    lastSeconds = -1;
    
    initFlipClockWidget(0, TIME_Y, 128, TIME_H);
    
    LOG_LN("[ClockPage] 初始化完成");
}

// ========== 绘制时钟页面 ==========
void drawClockPage() {
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;
    int wday = timeinfo.tm_wday;

    SensorSnapshot snap = getSensorSnapshot();
    float temp1 = snap.t1, hum1 = snap.h1;

    static float prevT1 = -999, prevH1 = -999;
    if (!snap.t1Ok && prevT1 != -999) temp1 = prevT1;
    if (!snap.h1Ok && prevH1 != -999) hum1  = prevH1;
    if (snap.t1Ok) prevT1 = temp1;
    if (snap.h1Ok) prevH1 = hum1;

    bool wifiConnected = wifiManager.isConnected();
    int rssi = wifiManager.getRSSI();

    // 强制绘制所有区域（首次绘制）
    drawStatusBar(wifiConnected, rssi, timeSynced, true);
    drawTime(h, m, s, true);
    drawDate(year, month, day, wday, true);
    drawSensorRow(SENSOR_Y, "T", temp1, TEMP_RED, "H", hum1, HUM_BLUE, true);
    drawBottomStatus(millis(), true);

    LOG_LN("[ClockPage] 完整绘制完成");
}

// ========== 更新时钟页面（每秒调用） ==========
void updateClockPage() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return;
    }

    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;
    int wday = timeinfo.tm_wday;

    SensorSnapshot snap = getSensorSnapshot();
    float temp1 = snap.t1, hum1 = snap.h1;

    static float prevT1 = -999, prevH1 = -999;
    if (!snap.t1Ok && prevT1 != -999) temp1 = prevT1;
    if (!snap.h1Ok && prevH1 != -999) hum1  = prevH1;
    if (snap.t1Ok) prevT1 = temp1;
    if (snap.h1Ok) prevH1 = hum1;

    bool wifiConnected = wifiManager.isConnected();
    int rssi = wifiManager.getRSSI();

    // 增量更新各区域
    drawStatusBar(wifiConnected, rssi, timeSynced, false);
    drawTime(h, m, s, false);
    drawDate(year, month, day, wday, false);
    drawSensorRow(SENSOR_Y, "T", temp1, TEMP_RED, "H", hum1, HUM_BLUE, false);
    drawBottomStatus(millis(), false);
}