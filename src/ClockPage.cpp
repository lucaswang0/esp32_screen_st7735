#include "ClockPage.h"
#include "Display.h"
#include <DHT.h>
#include <time.h>
#include "WiFiManager.h"

extern DHT dht1;
extern DHT dht2;
extern WiFiManager wifiManager;
extern bool timeSynced;
extern bool forcePageRedraw;

// ============================================================================
// 调试开关
// ============================================================================
#define ENABLE_DEBUG false

// ========== 颜色定义 ==========
#define BG_COLOR       tft.color565(8, 8, 20)
#define CARD_BG        tft.color565(12, 12, 32)
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

// ============================================================================
// 布局常量（所有偏移都在定义时处理，绘制时直接用坐标）
// ============================================================================
const int STATUS_Y      = 0;
const int STATUS_H      = 12;       // y: 0-11

const int TIME_Y        = STATUS_Y + STATUS_H + 8;   // y: 18
const int TIME_H        = 28;       // y: 18-45

const int DATE_Y        = TIME_Y + TIME_H + 7;       // y: 52
const int DATE_H        = 18;       // y: 52-69

const int SENSOR1_Y     = DATE_Y + DATE_H + 3;       // y: 73
const int SENSOR1_H     = 12;       // y: 73-84

const int SENSOR2_Y     = SENSOR1_Y + SENSOR1_H + 3; // y: 88
const int SENSOR2_H     = 12;       // y: 88-99

const int BOTTOM_Y      = SENSOR2_Y + SENSOR2_H + 6; // y: 106
const int BOTTOM_H      = 128 - BOTTOM_Y;            // y: 106-127

const int LABEL_X       = 0;

const char* WEEK_DAYS[] = {"日", "一", "二", "三", "四", "五", "六"};

// ============================================================================
// 调试辅助函数（使用外部 clearRect）
// ============================================================================

// 声明外部 clearRect 函数（在 Display.cpp 中定义）
extern void clearRect(int x, int y, int w, int h);

// 获取字体实际高度
int getFontHeight(const lgfx::IFont* font) {
    if (font == nullptr) return 0;
    if (font == &lgfx::fonts::Font7) return 28;
    if (font == &lgfx::fonts::efontCN_12) return 12;
    if (font == &lgfx::fonts::efontCN_16) return 16;
    if (font == &lgfx::fonts::Font0) return 8;
    return 12;
}

// 打印调试信息
void debugPrint(const char* region, int y, int presetH, const lgfx::IFont* font, const char* content) {
    if (!ENABLE_DEBUG) return;
    
    int actualH = getFontHeight(font);
    int endY = y + presetH - 1;
    
    Serial.printf("[%s] y=%d~%d | 预设高度=%d | 字体高度=%d | ", 
                  region, y, endY, presetH, actualH);
    
    if (actualH > presetH) {
        Serial.printf("⚠️ 字体超出区域 %dpx!\n", actualH - presetH);
    } else if (actualH < presetH - 4) {
        Serial.printf("📏 字体偏小 (剩余%dpx)\n", presetH - actualH);
    } else {
        Serial.printf("✅ 高度匹配\n");
    }
    Serial.printf("    └─ 内容: %s\n", content);
}

// 清除区域（调试模式下带边框）
void clearRectDebug(int x, int y, int w, int h, const char* region) {
    clearRect(x, y, w, h);
    if (ENABLE_DEBUG) {
        tft.drawRect(x, y, w, h, DEBUG_BORDER);
    }
}

// ============================================================================
// 辅助函数：绘制状态栏
// ============================================================================
void drawStatusBar(bool wifiConnected, int rssi, bool ntpOk, bool force) {
    static bool lastWifi = false;
    static int lastRssi = 999;
    static bool lastNtp = false;
    
    bool wifiChanged = (wifiConnected != lastWifi || rssi != lastRssi);
    bool ntpChanged = (ntpOk != lastNtp);
    
    if (!force && !wifiChanged && !ntpChanged) return;
    
    clearRectDebug(0, STATUS_Y, 128, STATUS_H, "状态栏");
    tft.setFont(&lgfx::fonts::efontCN_12);
    tft.setTextDatum(top_left);
    
    tft.setTextColor(DIM_TEXT);
    tft.drawString("WiFi", 2, STATUS_Y);
    
    if (wifiConnected) {
        tft.setTextColor(GREEN);
        tft.drawString("●", 28, STATUS_Y);
        char rssiBuf[6];
        snprintf(rssiBuf, sizeof(rssiBuf), "%d", rssi);
        tft.setTextColor(DIM_TEXT);
        tft.drawString(rssiBuf, 38, STATUS_Y);
    } else {
        tft.setTextColor(RED);
        tft.drawString("○", 28, STATUS_Y);
    }
    
    tft.setTextDatum(top_right);
    tft.setTextColor(ntpOk ? GREEN : RED);
    tft.drawString(ntpOk ? "NTP●" : "NTP○", 124, STATUS_Y);
    tft.setTextDatum(top_left);
    
    if (ENABLE_DEBUG) {
        char debugBuf[60];
        snprintf(debugBuf, sizeof(debugBuf), "WiFi:%s RSSI:%d NTP:%s", 
                 wifiConnected ? "OK" : "NO", rssi, ntpOk ? "OK" : "NO");
        debugPrint("状态栏", STATUS_Y, STATUS_H, &lgfx::fonts::efontCN_12, debugBuf);
    }
    
    lastWifi = wifiConnected;
    lastRssi = rssi;
    lastNtp = ntpOk;
}

// ============================================================================
// 辅助函数：绘制时间
// ============================================================================
void drawTime(int h, int m, bool force) {
    static int lastH = -1, lastM = -1;
    
    if (!force && h == lastH && m == lastM) return;
    
    clearRectDebug(0, TIME_Y, 128, TIME_H, "时间");
    tft.setFont(&lgfx::fonts::Font7);
    tft.setTextColor(WHITE);
    tft.setTextDatum(middle_center);
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    tft.drawString(buf, 64, TIME_Y + TIME_H / 2);
    
    if (ENABLE_DEBUG) {
        debugPrint("时间", TIME_Y, TIME_H, &lgfx::fonts::Font7, buf);
    }
    
    lastH = h;
    lastM = m;
}

// ============================================================================
// 辅助函数：绘制日期
// ============================================================================
void drawDate(int year, int month, int day, int wday, bool force) {
    static int lastDay = -1, lastWday = -1;
    
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
    
    if (ENABLE_DEBUG) {
        char debugBuf[30];
        snprintf(debugBuf, sizeof(debugBuf), "%s %s", dateBuf, weekBuf);
        debugPrint("日期", DATE_Y, DATE_H, &lgfx::fonts::efontCN_16, debugBuf);
    }
    
    lastDay = day;
    lastWday = wday;
}

// ============================================================================
// 辅助函数：绘制传感器行（左右并排显示，无进度条）
// ============================================================================
void drawSensorRow(
    int y,
    const char* label1,
    float value1,
    uint16_t color1,
    const char* label2,
    float value2,
    uint16_t color2,
    bool force
) {
    static float lastValues[4] = {-999, -999, -999, -999};
    int idx = 0;
    if (y == SENSOR1_Y) idx = 0;
    else if (y == SENSOR2_Y) idx = 2;
    
    bool changed = (force || value1 != lastValues[idx] || value2 != lastValues[idx + 1]);
    if (!changed) return;
    
    char regionName[20];
    snprintf(regionName, sizeof(regionName), "传感器%d", idx/2 + 1);
    
    clearRectDebug(0, y, 128, SENSOR1_H, regionName);
    
    tft.setFont(&lgfx::fonts::efontCN_12);
    tft.setTextDatum(top_left);
    
    // ===== 左侧：温度 =====
    tft.setTextColor(color1);
    char buf1[20];
    snprintf(buf1, sizeof(buf1), "%s:%.1f°C", label1, value1);
    tft.drawString(buf1, LABEL_X, y);
    
    // ===== 右侧：湿度 =====
    tft.setTextColor(color2);
    tft.setTextDatum(top_right);
    char buf2[20];
    snprintf(buf2, sizeof(buf2), "%.1f%% %s", value2, label2);
    tft.drawString(buf2, 126, y);
    
    // ===== 中间：分隔线 =====
    int lineX = 64;
    tft.drawLine(lineX, y + 2, lineX, y + SENSOR1_H - 2, DIM_TEXT);
    
    if (ENABLE_DEBUG) {
        char debugBuf[50];
        snprintf(debugBuf, sizeof(debugBuf), "%s:%.1f°C  %s:%.1f%%", label1, value1, label2, value2);
        debugPrint(regionName, y, SENSOR1_H, &lgfx::fonts::efontCN_12, debugBuf);
    }
    
    lastValues[idx] = value1;
    lastValues[idx + 1] = value2;
}

// ============================================================================
// 辅助函数：绘制底部状态
// ============================================================================
void drawBottomStatus(unsigned long uptime, bool force) {
    static int lastSeconds = -1;
    
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
    
    if (ENABLE_DEBUG) {
        char debugBuf[40];
        snprintf(debugBuf, sizeof(debugBuf), "%s 传感器OK", buf);
        debugPrint("底部状态", BOTTOM_Y, BOTTOM_H, &lgfx::fonts::efontCN_12, debugBuf);
    }
    
    lastSeconds = seconds;
}

// ============================================================================
// 调试：打印完整布局信息
// ============================================================================
void printLayoutInfo() {
    if (!ENABLE_DEBUG) return;
    
    Serial.println("\n╔══════════════════════════════════════════════════════════╗");
    Serial.println("║              ClockPage 布局调试信息                      ║");
    Serial.println("╚══════════════════════════════════════════════════════════╝");
    
    struct RegionInfo {
        const char* name;
        int y;
        int h;
        const lgfx::IFont* font;
    };
    
    RegionInfo regions[] = {
        {"状态栏", STATUS_Y, STATUS_H, &lgfx::fonts::efontCN_12},
        {"时间", TIME_Y, TIME_H, &lgfx::fonts::Font7},
        {"日期", DATE_Y, DATE_H, &lgfx::fonts::efontCN_16},
        {"传感器1", SENSOR1_Y, SENSOR1_H, &lgfx::fonts::efontCN_12},
        {"传感器2", SENSOR2_Y, SENSOR2_H, &lgfx::fonts::efontCN_12},
        {"底部状态", BOTTOM_Y, BOTTOM_H, &lgfx::fonts::efontCN_12}
    };
    
    Serial.println("\n┌────────┬──────────┬──────────┬──────────┬─────────────┐");
    Serial.println("│ 区域   │ Y起始    │ Y结束    │ 预设高度 │ 字体高度    │");
    Serial.println("├────────┼──────────┼──────────┼──────────┼─────────────┤");
    
    for (int i = 0; i < 6; i++) {
        int endY = regions[i].y + regions[i].h - 1;
        int fontH = getFontHeight(regions[i].font);
        const char* status;
        if (fontH > regions[i].h) {
            status = "⚠️ 超出!";
        } else if (fontH < regions[i].h - 4) {
            status = "📏 偏小";
        } else {
            status = "✅ 匹配";
        }
        Serial.printf("│ %-6s │ %3d-%-3d  │ %3d-%-3d  │ %4d     │ %2d (%s)  │\n",
                      regions[i].name, regions[i].y, regions[i].y + regions[i].h - 1,
                      regions[i].y, endY, regions[i].h, fontH, status);
    }
    
    Serial.println("└────────┴──────────┴──────────┴──────────┴─────────────┘");
    
    Serial.println("\n🔍 重叠检查:");
    int regionsY[][2] = {
        {STATUS_Y, STATUS_Y + STATUS_H - 1},
        {TIME_Y, TIME_Y + TIME_H - 1},
        {DATE_Y, DATE_Y + DATE_H - 1},
        {SENSOR1_Y, SENSOR1_Y + SENSOR1_H - 1},
        {SENSOR2_Y, SENSOR2_Y + SENSOR2_H - 1},
        {BOTTOM_Y, BOTTOM_Y + BOTTOM_H - 1}
    };
    const char* names[] = {"状态栏", "时间", "日期", "传感器1", "传感器2", "底部状态"};
    
    bool hasOverlap = false;
    for (int i = 0; i < 6; i++) {
        for (int j = i + 1; j < 6; j++) {
            if (regionsY[i][0] <= regionsY[j][1] && regionsY[j][0] <= regionsY[i][1]) {
                Serial.printf("  ❌ 重叠: %s 与 %s 重叠!\n", names[i], names[j]);
                hasOverlap = true;
            }
        }
    }
    
    if (!hasOverlap) {
        Serial.println("  ✅ 所有区域无重叠");
    }
    
    Serial.println("╚══════════════════════════════════════════════════════════╝\n");
}

// ============================================================================
// 主绘制函数
// ============================================================================
void drawClockPage() {
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;
    int wday = timeinfo.tm_wday;
    
    static float prevT1 = -999, prevH1 = -999, prevT2 = -999, prevH2 = -999;
    
    float temp1 = dht1.readTemperature();
    float hum1  = dht1.readHumidity();
    float temp2 = dht2.readTemperature();
    float hum2  = dht2.readHumidity();
    
    if (isnan(temp1)) temp1 = prevT1;
    if (isnan(hum1))  hum1  = prevH1;
    if (isnan(temp2)) temp2 = prevT2;
    if (isnan(hum2))  hum2  = prevH2;
    
    bool wifiConnected = wifiManager.isConnected();
    int rssi = wifiManager.getRSSI();
    
    static bool layoutPrinted = false;
    if (!layoutPrinted) {
        printLayoutInfo();
        layoutPrinted = true;
    }
    
    static bool lastForce = false;
    if (forcePageRedraw && !lastForce) {
        drawStatusBar(wifiConnected, rssi, timeSynced, true);
        drawTime(h, m, true);
        drawDate(year, month, day, wday, true);
        drawSensorRow(SENSOR1_Y, "T1", temp1, ACCENT_COLOR, "H1", hum1, TFT_CYAN, true);
        drawSensorRow(SENSOR2_Y, "T2", temp2, TFT_ORANGE, "H2", hum2, TFT_GREEN, true);
        drawBottomStatus(millis(), true);
        
        prevT1 = temp1; prevH1 = hum1; prevT2 = temp2; prevH2 = hum2;
    }
    lastForce = forcePageRedraw;
    
    drawStatusBar(wifiConnected, rssi, timeSynced, forcePageRedraw);
    drawTime(h, m, forcePageRedraw);
    drawDate(year, month, day, wday, forcePageRedraw);
    drawSensorRow(SENSOR1_Y, "T1", temp1, ACCENT_COLOR, "H1", hum1, TFT_CYAN, forcePageRedraw);
    drawSensorRow(SENSOR2_Y, "T2", temp2, TFT_ORANGE, "H2", hum2, TFT_GREEN, forcePageRedraw);
    drawBottomStatus(millis(), forcePageRedraw);
    
    prevT1 = temp1;
    prevH1 = hum1;
    prevT2 = temp2;
    prevH2 = hum2;
}