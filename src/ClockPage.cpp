#include "ClockPage.h"
#include "Log.h"
#include "Display.h"
#include "DHT11Sensor.h"
#include "SharedState.h"
#include <time.h>
#include "WiFiManager.h"
#include "FlipClockPage.h"

extern DHT11Sensor dht1;
extern DHT11Sensor dht2;
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

const int SENSOR1_Y     = DATE_Y + DATE_H + 3;
const int SENSOR1_H     = 12;

const int SENSOR2_Y     = SENSOR1_Y + SENSOR1_H + 3;
const int SENSOR2_H     = 12;

const int BOTTOM_Y      = SENSOR2_Y + SENSOR2_H + 4;
const int BOTTOM_H      = 128 - BOTTOM_Y;

const int LABEL_X       = 0;

const char* WEEK_DAYS[] = {"日", "一", "二", "三", "四", "五", "六"};

static bool lastWifi = false;
static int lastRssi = 999;
static bool lastNtp = false;
static bool showIpMode = false;
static unsigned long lastStatusSwitch = 0;

static int lastH = -1, lastM = -1, lastS = -1;

static int lastDay = -1, lastWday = -1;

static float lastValues[4] = {-999, -999, -999, -999};

static int lastSeconds = -1;

extern void clearRect(int x, int y, int w, int h);

int getFontHeight(const lgfx::IFont* font) {
    if (font == nullptr) return 0;
    if (font == &lgfx::fonts::Font7) return 28;
    if (font == &lgfx::fonts::efontCN_12) return 12;
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

void drawStatusBar(bool wifiConnected, int rssi, bool ntpOk, bool force) {
    // AP 模式：所有 WiFi 凭据尝试失败，闪烁提示用户配网
    // 闪烁节拍独立于 force/缓存机制，保证 UI loop 每秒调用也能看到闪烁
    static unsigned long lastBlink = 0;
    static bool blinkOn = true;
    static bool lastApMode = false;

    if (g_wifiApMode) {
        unsigned long now = millis();
        if (now - lastBlink >= 500) {
            lastBlink = now;
            blinkOn = !blinkOn;
            force = true;  // 触发重绘
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
        // 同步缓存，避免 AP 模式退出后误判状态变化
        lastWifi = wifiConnected;
        lastRssi = rssi;
        lastNtp = ntpOk;
        return;
    }

    // 退出 AP 模式：强制重绘一次恢复正常状态栏
    if (lastApMode) {
        force = true;
        lastApMode = false;
        // 重置切换缓存，避免进入 AP 时残留的 showIpMode 状态
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
    tft.drawString("WiFi", 2, STATUS_Y);
    
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
    tft.drawString(ntpOk ? "NTP●" : "NTP○", 124, STATUS_Y);
    tft.setTextDatum(top_left);
    
    lastWifi = wifiConnected;
    lastRssi = rssi;
    lastNtp = ntpOk;
}

void drawTime(int h, int m, int s, bool force) {
    if (!force && h == lastH && m == lastM && s == lastS) return;
    
    clearRectDebug(0, TIME_Y, 128, TIME_H, "时间");
    
    if (force) {
        drawFlipClockWidget(h, m, s);
    } else {
        updateFlipClockWidget(h, m, s);
    }
    
    lastH = h;
    lastM = m;
    lastS = s;
}

void drawDate(int year, int month, int day, int wday, bool force) {
    if (!force && day == lastDay && wday == lastWday) return;
    
    clearRectDebug(0, DATE_Y, 128, DATE_H, "日期");
    
    tft.setFont(&lgfx::fonts::efontCN_16);
    tft.setTextColor(WHITE);
    tft.setTextDatum(top_left);
    
    char dateBuf[20];
    snprintf(dateBuf, sizeof(dateBuf), "%d/%02d/%02d", year, month, day);
    tft.drawString(dateBuf, 6, DATE_Y);
    
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextDatum(top_right);
    
    char weekBuf[8];
    snprintf(weekBuf, sizeof(weekBuf), "周%s", WEEK_DAYS[wday]);
    tft.drawString(weekBuf, 124, DATE_Y);
    
    lastDay = day;
    lastWday = wday;
}

void drawSensorRow(int y, const char* label1, float value1, uint16_t color1,
                   const char* label2, float value2, uint16_t color2, bool force) {
    int idx = 0;
    if (y == SENSOR1_Y) idx = 0;
    else if (y == SENSOR2_Y) idx = 2;
    else return;
    
    bool changed = (force || value1 != lastValues[idx] || value2 != lastValues[idx + 1]);
    if (!changed) return;
    
    char regionName[20];
    snprintf(regionName, sizeof(regionName), "传感器%d", idx/2 + 1);
    
    clearRectDebug(0, y, 128, SENSOR1_H, regionName);
    
    tft.setFont(&lgfx::fonts::efontCN_12);
    tft.setTextDatum(top_left);
    
    tft.setTextColor(color1);
    char buf1[20];
    snprintf(buf1, sizeof(buf1), "%s:%.1f°C", label1, value1);
    tft.drawString(buf1, LABEL_X, y);
    
    tft.setTextColor(color2);
    tft.setTextDatum(top_right);
    char buf2[20];
    snprintf(buf2, sizeof(buf2), "%.1f%% %s", value2, label2);
    tft.drawString(buf2, 126, y);
    
    int lineX = 64;
    tft.drawLine(lineX, y + 2, lineX, y + SENSOR1_H - 2, DIM_TEXT);
    
    lastValues[idx] = value1;
    lastValues[idx + 1] = value2;
}

void drawBottomStatus(unsigned long uptime, bool force) {
    int seconds = (int)(uptime / 1000);
    if (!force && seconds == lastSeconds) return;
    
    clearRectDebug(0, BOTTOM_Y, 128, BOTTOM_H, "底部状态");
    tft.setFont(&lgfx::fonts::efontCN_12);
    tft.setTextDatum(top_left);
    
    char buf[20];
    if (seconds < 60) {
        snprintf(buf, sizeof(buf), "运行:%ds", seconds);
    } else if (seconds < 3600) {
        snprintf(buf, sizeof(buf), "运行:%dm%ds", seconds/60, seconds%60);
    } else {
        snprintf(buf, sizeof(buf), "运行:%dh%dm", seconds/3600, (seconds%3600)/60);
    }
    tft.setTextColor(DIM_TEXT);
    tft.drawString(buf, 2, BOTTOM_Y);
    
    tft.setTextDatum(top_right);
    tft.setTextColor(GREEN);
    tft.drawString("传感器●", 126, BOTTOM_Y);
    tft.setTextDatum(top_left);
    
    lastSeconds = seconds;
}

void initClockPage() {
    lastWifi = false;
    lastRssi = 999;
    lastNtp = false;
    showIpMode = false;
    lastStatusSwitch = millis();
    lastH = -1;
    lastM = -1;
    lastS = -1;
    lastDay = -1;
    lastWday = -1;
    for (int i = 0; i < 4; i++) {
        lastValues[i] = -999;
    }
    lastSeconds = -1;
    
    initFlipClockWidget(0, TIME_Y, 128, TIME_H);
    
    LOG_LN("[ClockPage] 初始化完成");
}

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
    float temp1 = snap.t1, hum1 = snap.h1, temp2 = snap.t2, hum2 = snap.h2;

    static float prevT1 = -999, prevH1 = -999, prevT2 = -999, prevH2 = -999;
    if (!snap.t1Ok && prevT1 != -999) temp1 = prevT1;
    if (!snap.h1Ok && prevH1 != -999) hum1  = prevH1;
    if (!snap.t2Ok && prevT2 != -999) temp2 = prevT2;
    if (!snap.h2Ok && prevH2 != -999) hum2  = prevH2;
    if (snap.t1Ok) prevT1 = temp1;
    if (snap.h1Ok) prevH1 = hum1;
    if (snap.t2Ok) prevT2 = temp2;
    if (snap.h2Ok) prevH2 = hum2;

    bool wifiConnected = wifiManager.isConnected();
    int rssi = wifiManager.getRSSI();

    drawStatusBar(wifiConnected, rssi, timeSynced, true);
    drawTime(h, m, s, true);
    drawDate(year, month, day, wday, true);
    drawSensorRow(SENSOR1_Y, "T1", temp1, ACCENT_COLOR, "H1", hum1, TFT_CYAN, true);
    drawSensorRow(SENSOR2_Y, "T2", temp2, TFT_ORANGE, "H2", hum2, TFT_GREEN, true);
    drawBottomStatus(millis(), true);

    LOG_LN("[ClockPage] 完整绘制完成");
}

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
    float temp1 = snap.t1, hum1 = snap.h1, temp2 = snap.t2, hum2 = snap.h2;

    static float prevT1 = -999, prevH1 = -999, prevT2 = -999, prevH2 = -999;
    if (!snap.t1Ok && prevT1 != -999) temp1 = prevT1;
    if (!snap.h1Ok && prevH1 != -999) hum1  = prevH1;
    if (!snap.t2Ok && prevT2 != -999) temp2 = prevT2;
    if (!snap.h2Ok && prevH2 != -999) hum2  = prevH2;
    if (snap.t1Ok) prevT1 = temp1;
    if (snap.h1Ok) prevH1 = hum1;
    if (snap.t2Ok) prevT2 = temp2;
    if (snap.h2Ok) prevH2 = hum2;

    bool wifiConnected = wifiManager.isConnected();
    int rssi = wifiManager.getRSSI();

    drawStatusBar(wifiConnected, rssi, timeSynced, false);
    drawTime(h, m, s, false);
    drawDate(year, month, day, wday, false);
    drawSensorRow(SENSOR1_Y, "T1", temp1, ACCENT_COLOR, "H1", hum1, TFT_CYAN, false);
    drawSensorRow(SENSOR2_Y, "T2", temp2, TFT_ORANGE, "H2", hum2, TFT_GREEN, false);
    drawBottomStatus(millis(), false);
}