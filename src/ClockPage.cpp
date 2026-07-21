#include "ClockPage.h"
#include "Display.h"
#include "DHT11Sensor.h"
#include <time.h>
#include "WiFiManager.h"

extern DHT11Sensor dht1;
extern DHT11Sensor dht2;
extern WiFiManager wifiManager;
extern bool timeSynced;

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

const int DATE_Y        = TIME_Y + TIME_H + 12;       // y: 52
const int DATE_H        = 18;       // y: 52-69

const int SENSOR1_Y     = DATE_Y + DATE_H + 3;       // y: 73
const int SENSOR1_H     = 12;       // y: 73-84

const int SENSOR2_Y     = SENSOR1_Y + SENSOR1_H + 3; // y: 88
const int SENSOR2_H     = 12;       // y: 88-99

const int BOTTOM_Y      = SENSOR2_Y + SENSOR2_H + 4; // y: 106
const int BOTTOM_H      = 128 - BOTTOM_Y;            // y: 106-127

const int LABEL_X       = 0;

const char* WEEK_DAYS[] = {"日", "一", "二", "三", "四", "五", "六"};

// ============================================================================
// 静态缓存变量（保存上次的值，用于判断是否需要更新）
// ============================================================================
static bool initialized = false;

// 状态栏缓存
static bool lastWifi = false;
static int lastRssi = 999;
static bool lastNtp = false;

// 时间缓存
static int lastH = -1, lastM = -1, lastS = -1;

// 日期缓存
static int lastDay = -1, lastWday = -1;

// 传感器缓存
static float lastValues[4] = {-999, -999, -999, -999};

// 底部状态缓存
static int lastSeconds = -1;

// ============================================================================
// 调试辅助函数
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
void drawTime(int h, int m, int s, bool force) {
    if (!force && h == lastH && m == lastM && s == lastS) return;
    
    clearRectDebug(0, TIME_Y, 128, TIME_H + 12, "时间");
    
    int displayH = h % 12;
    if (displayH == 0) displayH = 12;
    const char* period = (h >= 12) ? "PM" : "AM";
    
    // ===== 时间（居中偏左） =====
    tft.setFont(&lgfx::fonts::Font7);
    tft.setTextColor(WHITE);
    tft.setTextDatum(middle_right);
    char buf[16];
    if (displayH <= 9) {
        snprintf(buf, sizeof(buf), "%d:%02d", displayH, m);
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", displayH, m);
    }
    tft.drawString(buf, 124, TIME_Y + TIME_H / 2);
    
    // ===== AM/PM（右上） =====
    tft.setFont(&lgfx::fonts::Font2);
    tft.setTextColor(DIM_TEXT);
    tft.setTextDatum(top_right);
    tft.drawString(period, 128, TIME_Y - 10);
    
    // ===== 秒数（右下） =====
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextDatum(bottom_right);
    char secBuf[8];
    snprintf(secBuf, sizeof(secBuf), "%02d", s);
    tft.drawString(secBuf, 128, TIME_Y + TIME_H + 10);
    
    if (ENABLE_DEBUG) {
        debugPrint("时间", TIME_Y, TIME_H, &lgfx::fonts::Font7, buf);
    }
    
    lastH = h;
    lastM = m;
    lastS = s;
}

// ============================================================================
// 辅助函数：绘制日期
// ============================================================================
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
// 公共接口函数实现
// ============================================================================

// 初始化时钟页面
void initClockPage() {
    // 重置所有缓存变量
    lastWifi = false;
    lastRssi = 999;
    lastNtp = false;
    lastH = -1;
    lastM = -1;
    lastS = -1;
    lastDay = -1;
    lastWday = -1;
    for (int i = 0; i < 4; i++) {
        lastValues[i] = -999;
    }
    lastSeconds = -1;
    initialized = true;
    
    Serial.println("[ClockPage] 初始化完成");
}

// 完整绘制时钟页面（页面切换时调用）
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
    
    // 更新传感器数据
    dht1.update();
    dht2.update();
    
    float temp1 = dht1.getTemperature();
    float hum1  = dht1.getHumidity();
    float temp2 = dht2.getTemperature();
    float hum2  = dht2.getHumidity();
    
    // 如果传感器无效，使用上次有效值
    static float prevT1 = -999, prevH1 = -999, prevT2 = -999, prevH2 = -999;
    if (!dht1.isValid()) {
        temp1 = prevT1;
        hum1  = prevH1;
    }
    if (!dht2.isValid()) {
        temp2 = prevT2;
        hum2  = prevH2;
    }
    
    bool wifiConnected = wifiManager.isConnected();
    int rssi = wifiManager.getRSSI();
    
    static bool layoutPrinted = false;
    if (!layoutPrinted) {
        printLayoutInfo();
        layoutPrinted = true;
    }
    
    // ---- 完整重绘所有内容（force = true） ----
    drawStatusBar(wifiConnected, rssi, timeSynced, true);
    drawTime(h, m, s, true);
    drawDate(year, month, day, wday, true);
    drawSensorRow(SENSOR1_Y, "T1", temp1, ACCENT_COLOR, "H1", hum1, TFT_CYAN, true);
    drawSensorRow(SENSOR2_Y, "T2", temp2, TFT_ORANGE, "H2", hum2, TFT_GREEN, true);
    drawBottomStatus(millis(), true);
    
    // 保存当前值供后续增量更新使用
    prevT1 = temp1;
    prevH1 = hum1;
    prevT2 = temp2;
    prevH2 = hum2;
    
    Serial.println("[ClockPage] 完整绘制完成");
}

// 更新时钟页面动态内容（每秒调用）
void updateClockPage() {
    // 获取当前时间
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return;  // 获取时间失败，跳过更新
    }
    
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;
    int wday = timeinfo.tm_wday;
    
    // 更新传感器数据
    dht1.update();
    dht2.update();
    
    float temp1 = dht1.getTemperature();
    float hum1  = dht1.getHumidity();
    float temp2 = dht2.getTemperature();
    float hum2  = dht2.getHumidity();
    
    // 如果传感器无效，使用上次有效值
    static float prevT1 = -999, prevH1 = -999, prevT2 = -999, prevH2 = -999;
    if (!dht1.isValid()) {
        temp1 = prevT1;
        hum1  = prevH1;
    }
    if (!dht2.isValid()) {
        temp2 = prevT2;
        hum2  = prevH2;
    }
    
    // 更新前保存旧值（供下次使用）
    prevT1 = temp1;
    prevH1 = hum1;
    prevT2 = temp2;
    prevH2 = hum2;
    
    // 获取 WiFi 状态
    bool wifiConnected = wifiManager.isConnected();
    int rssi = wifiManager.getRSSI();
    
    // ---- 只更新变化的部分（force = false，让子函数判断是否需要更新） ----
    drawStatusBar(wifiConnected, rssi, timeSynced, false);
    drawTime(h, m, s, false);
    drawDate(year, month, day, wday, false);
    drawSensorRow(SENSOR1_Y, "T1", temp1, ACCENT_COLOR, "H1", hum1, TFT_CYAN, false);
    drawSensorRow(SENSOR2_Y, "T2", temp2, TFT_ORANGE, "H2", hum2, TFT_GREEN, false);
    drawBottomStatus(millis(), false);
}