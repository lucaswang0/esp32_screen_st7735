#include "SysInfoPage.h"
#include "Log.h"
#include "Theme.h"
#include "SharedState.h"
#include "WiFiManager.h"

extern WiFiManager wifiManager;
extern volatile uint32_t g_touchCount;   // 触摸次数（main.cpp 维护）

#define WHITE    currentThemeColors().mainText
#define DIM      currentThemeColors().dimText
#define ACCENT   currentThemeColors().accent
#define TEMP_C   currentThemeColors().temp
#define HUM_C    currentThemeColors().humidity
#define GREEN    currentThemeColors().green
#define RED      currentThemeColors().red
#define GRID     currentThemeColors().grid

// ============================================================================
// 布局（128×128）
// 8 行（标题 + 7 内容）：行距 15px，正好容纳且不拥挤
// ============================================================================
static const int HEADER_Y = 1;    // 标题行
static const int SEP1_Y   = 14;   // 分隔线 1
static const int SEP2_Y   = 89;   // 分隔线 2
static const int ROW_Y[]  = { 16, 31, 46, 61, 76, 91, 106 };  // 7 行内容

enum {
    R_HEADER = 0, R_UPTIME, R_HEAP, R_TEMP, R_IP, R_FLASH, R_DHT, R_TOUCH, R_COUNT
};

// 每行缓存上一次绘制的内容，内容不变则跳过重绘（避免整页闪烁）
static char rowCache[R_COUNT][48];

static bool rowDirty(int idx, const char* key) {
    if (strcmp(rowCache[idx], key) == 0) return false;
    strncpy(rowCache[idx], key, sizeof(rowCache[idx]) - 1);
    rowCache[idx][sizeof(rowCache[idx]) - 1] = '\0';
    return true;
}

// 清空一行（行间距 16px，清除 13px 不会碰到相邻行）
static void clearRow(int y) {
    clearRect(0, y - 1, 128, 13);
}

static void formatUptime(char* buf, size_t len) {
    unsigned long sec = millis() / 1000;
    unsigned long d = sec / 86400; sec %= 86400;
    unsigned long h = sec / 3600;  sec %= 3600;
    unsigned long m = sec / 60;
    snprintf(buf, len, "%02lud %02lu:%02lu:%02lu", d, h, m, sec % 60);
}

// ============================================================================
// 渲染所有行（force = true 时忽略缓存全量重绘）
// ============================================================================
static void renderRows(bool force) {
    if (force) {
        for (int i = 0; i < R_COUNT; i++) rowCache[i][0] = '\0';
    }

    tft.setFont(&lgfx::fonts::efontCN_12);

    bool wifiOk = wifiManager.isConnected();

    // ---- 标题行：ESP32-C3   160MHz r4 ----
    {
        char key[48];
        snprintf(key, sizeof(key), "cpu|%lu|%u", (unsigned long)getCpuFrequencyMhz(),
                 (unsigned)ESP.getChipRevision());
        if (rowDirty(R_HEADER, key)) {
            char right[20];
            snprintf(right, sizeof(right), "%luMHz r%u",
                     (unsigned long)getCpuFrequencyMhz(), (unsigned)ESP.getChipRevision());
            clearRow(HEADER_Y);
            tft.setTextDatum(top_left);
            tft.setTextColor(WHITE);
            tft.drawString("ESP32-C3", 0, HEADER_Y);
            tft.setTextDatum(top_right);
            tft.setTextColor(DIM);
            tft.drawString(right, 127, HEADER_Y);
        }
    }

    // ---- Uptime: 02d 03:45:12 ----
    {
        char val[20];
        formatUptime(val, sizeof(val));
        char key[48];
        snprintf(key, sizeof(key), "up|%s", val);
        if (rowDirty(R_UPTIME, key)) {
            clearRow(ROW_Y[0]);
            tft.setTextDatum(top_left);
            tft.setTextColor(DIM);
            tft.drawString("Uptime:", 0, ROW_Y[0]);
            tft.setTextDatum(top_right);
            tft.setTextColor(WHITE);
            tft.drawString(val, 127, ROW_Y[0]);
        }
    }

    // ---- Heap: 145K / min 120K ----
    {
        char val[24];
        snprintf(val, sizeof(val), "%uK / min %uK",
                 (unsigned)(ESP.getFreeHeap() / 1024),
                 (unsigned)(ESP.getMinFreeHeap() / 1024));
        char key[48];
        snprintf(key, sizeof(key), "heap|%s", val);
        if (rowDirty(R_HEAP, key)) {
            clearRow(ROW_Y[1]);
            tft.setTextDatum(top_left);
            tft.setTextColor(DIM);
            tft.drawString("Heap:", 0, ROW_Y[1]);
            tft.setTextDatum(top_right);
            tft.setTextColor(WHITE);
            tft.drawString(val, 127, ROW_Y[1]);
        }
    }

    // ---- Flash: 4MB  80MHz ----
    {
        char val[20];
        snprintf(val, sizeof(val), "%uMB  %luMHz",
                 (unsigned)(ESP.getFlashChipSize() / 1048576),
                 (unsigned long)(ESP.getFlashChipSpeed() / 1000000));
        char key[48];
        snprintf(key, sizeof(key), "flash|%s", val);
        if (rowDirty(R_FLASH, key)) {
            clearRow(ROW_Y[2]);
            tft.setTextDatum(top_left);
            tft.setTextColor(DIM);
            tft.drawString("Flash:", 0, ROW_Y[2]);
            tft.setTextDatum(top_right);
            tft.setTextColor(WHITE);
            tft.drawString(val, 127, ROW_Y[2]);
        }
    }

    // ---- Temp: 42°C    RSSI -58dBm ----
    {
        char lv[12], rv[16];
        snprintf(lv, sizeof(lv), "%.0f°C", temperatureRead());
        if (wifiOk) snprintf(rv, sizeof(rv), "%ddBm", wifiManager.getRSSI());
        else        snprintf(rv, sizeof(rv), "--");
        char key[48];
        snprintf(key, sizeof(key), "temp|%s|%s|%d", lv, rv, wifiOk);
        if (rowDirty(R_TEMP, key)) {
            clearRow(ROW_Y[3]);
            tft.setTextDatum(top_left);
            tft.setTextColor(DIM);
            tft.drawString("Temp:", 0, ROW_Y[3]);
            tft.setTextDatum(top_right);
            tft.setTextColor(TEMP_C);
            tft.drawString(lv, 62, ROW_Y[3]);
            tft.setTextDatum(top_left);
            tft.setTextColor(DIM);
            tft.drawString("RSSI", 68, ROW_Y[3]);
            tft.setTextDatum(top_right);
            tft.setTextColor(wifiOk ? GREEN : RED);
            tft.drawString(rv, 127, ROW_Y[3]);
        }
    }

    // ---- IP: 192.168.1.100 ----
    {
        char val[20];
        if (wifiOk) {
            String ip = wifiManager.getLocalIP();
            strncpy(val, ip.c_str(), sizeof(val) - 1);
            val[sizeof(val) - 1] = '\0';
        } else {
            strcpy(val, "--");
        }
        char key[48];
        snprintf(key, sizeof(key), "ip|%s", val);
        if (rowDirty(R_IP, key)) {
            clearRow(ROW_Y[4]);
            tft.setTextDatum(top_left);
            tft.setTextColor(DIM);
            tft.drawString("IP:", 0, ROW_Y[4]);
            tft.setTextDatum(top_right);
            tft.setTextColor(ACCENT);
            tft.drawString(val, 127, ROW_Y[4]);
        }
    }

    // ---- DHT11: 25.3°C   48.0%RH ----
    {
        SensorSnapshot snap = getSensorSnapshot();
        char tv[12], hv[12];
        if (snap.anyData) {
            snprintf(tv, sizeof(tv), "%.1f°C", snap.t1);
            snprintf(hv, sizeof(hv), "%.1f%%", snap.h1);
        } else {
            strcpy(tv, "--");
            strcpy(hv, "--");
        }
        char key[48];
        snprintf(key, sizeof(key), "dht|%s|%s", tv, hv);
        if (rowDirty(R_DHT, key)) {
            clearRow(ROW_Y[5]);
            tft.setTextDatum(top_left);
            tft.setTextColor(DIM);
            tft.drawString("DHT11:", 0, ROW_Y[5]);
            tft.setTextDatum(top_right);
            tft.setTextColor(TEMP_C);
            tft.drawString(tv, 80, ROW_Y[5]);
            tft.setTextColor(HUM_C);
            tft.drawString(hv, 127, ROW_Y[5]);
        }
    }

    // ---- TTP223: 12次 ----
    {
        char val[12];
        snprintf(val, sizeof(val), "%lu次", (unsigned long)g_touchCount);
        char key[48];
        snprintf(key, sizeof(key), "touch|%s", val);
        if (rowDirty(R_TOUCH, key)) {
            clearRow(ROW_Y[6]);
            tft.setTextDatum(top_left);
            tft.setTextColor(DIM);
            tft.drawString("TTP223:", 0, ROW_Y[6]);
            tft.setTextDatum(top_right);
            tft.setTextColor(ACCENT);
            tft.drawString(val, 127, ROW_Y[6]);
        }
    }

    tft.setTextDatum(top_left);
}

// ============================================================================
// 对外接口
// ============================================================================
void initSysInfoPage() {
    for (int i = 0; i < R_COUNT; i++) rowCache[i][0] = '\0';
    LOG_LN("[SysInfoPage] 初始化完成");
}

void drawSysInfoPage() {
    tft.drawLine(0, SEP1_Y, 127, SEP1_Y, GRID);
    tft.drawLine(0, SEP2_Y, 127, SEP2_Y, GRID);
    renderRows(true);
    LOG_LN("[SysInfoPage] 完整绘制完成");
}

void updateSysInfoPage() {
    renderRows(false);
}