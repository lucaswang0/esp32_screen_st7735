#include "ChartPage.h"
#include "Display.h"
#include "DHT11Sensor.h"
#include <time.h>
#include "SensorHistory.h"

extern DHT11Sensor dht1;
extern DHT11Sensor dht2;
extern SensorHistory sensorHistory1;
extern SensorHistory sensorHistory2;
extern bool forcePageRedraw;

// ========== 颜色定义 ==========
#define ACCENT_COLOR   tft.color565(100, 200, 255)
#define DIM_TEXT       tft.color565(150, 150, 180)
#define WHITE          tft.color565(255, 255, 255)
#define GREEN          tft.color565(80, 255, 80)
#define BG_DARK        tft.color565(4, 4, 14)
#define TFT_ORANGE     tft.color565(255, 165, 0)
#define TFT_CYAN       tft.color565(0, 255, 255)
#define GRID_COLOR     tft.color565(30, 30, 50)
#define GRID_LABEL     tft.color565(80, 80, 120)

// ========== 布局常量 ==========
const int LABEL_Y        = 0;
const int LABEL_H        = 24;

const int CHART1_Y       = LABEL_Y + LABEL_H + 2;
const int CHART1_H       = 46;

const int CHART2_Y       = CHART1_Y + CHART1_H + 2;
const int CHART2_H       = 40;

const int BOTTOM_Y       = CHART2_Y + CHART2_H + 2;
const int BOTTOM_H       = 128 - BOTTOM_Y;

const int CHART_W        = 128;
const int CHART_X        = 0;

const int LABEL_ROW1_Y   = LABEL_Y + 2;
const int LABEL_ROW2_Y   = LABEL_Y + 14;
const int LABEL_COL1     = 2;
const int LABEL_COL2     = 66;

const int GRID_COLS      = 6;
const int GRID_ROWS      = 4;

// ============================================================================
// 缓存变量
// ============================================================================
static bool chartInitialized = false;

// ========== 计算数据范围 ==========
void getValueRange(SensorHistory& history1, SensorHistory& history2, 
                   bool useTemp, float& minVal, float& maxVal) {
    int count1 = history1.getCount();
    int count2 = history2.getCount();
    
    minVal = 999;
    maxVal = -999;
    
    for (int i = 0; i < count1; i++) {
        const SensorSample& s = history1.getSample(i);
        float val = useTemp ? s.temp : s.humidity;
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }
    
    for (int i = 0; i < count2; i++) {
        const SensorSample& s = history2.getSample(i);
        float val = useTemp ? s.temp : s.humidity;
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }
    
    if (count1 == 0 && count2 == 0) {
        if (useTemp) {
            minVal = 0;
            maxVal = 50;
        } else {
            minVal = 0;
            maxVal = 100;
        }
    }
    
    float range = maxVal - minVal;
    if (range < 1.0f) range = 1.0f;
    float padding = range * 0.1f;
    minVal -= padding;
    maxVal += padding;
    if (!useTemp) {
        if (minVal < 0) minVal = 0;
        if (maxVal > 100) maxVal = 100;
    } else {
        if (minVal < -10) minVal = -10;
        if (maxVal > 60) maxVal = 60;
        minVal = floor(minVal);
        maxVal = ceil(maxVal);
    }
}

// ========== 绘制背景网格 ==========
void drawGrid(int chartY, int chartH, bool showLabels, 
              SensorHistory& history1, SensorHistory& history2, 
              bool useTemp) {
    tft.setTextDatum(top_left);
    tft.setFont(&lgfx::fonts::Font0);
    tft.setTextColor(GRID_LABEL);
    
    float minVal, maxVal;
    getValueRange(history1, history2, useTemp, minVal, maxVal);
    float range = maxVal - minVal;
    if (range < 1.0f) range = 1.0f;
    
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    
    const int TOTAL_HOURS = 24;
    int totalMinutes = TOTAL_HOURS * 60;
    
    for (int i = 0; i <= GRID_COLS; i++) {
        int x = (i * CHART_W) / GRID_COLS;
        tft.drawLine(x, chartY, x, chartY + chartH - 1, GRID_COLOR);
        
        if (showLabels && i < GRID_COLS) {
            char label[8];
            int labelHour = (i * TOTAL_HOURS) / GRID_COLS;
            snprintf(label, sizeof(label), "%02d", labelHour);
            tft.drawString(label, x + 2, chartY + chartH - 8);
        }
    }
    
    if (showLabels) {
        float currentXf = ((float)(currentHour * 60 + currentMinute) / totalMinutes) * CHART_W;
        int currentX = constrain((int)currentXf, 0, CHART_W - 2);
        tft.drawLine(currentX, chartY, currentX, chartY + chartH - 1, tft.color565(100, 100, 80));
        
        tft.setTextColor(tft.color565(100, 200, 100));
        tft.drawString("现在", currentX - 12, chartY + chartH - 8);
    }
    
    for (int i = 0; i <= GRID_ROWS; i++) {
        int y = chartY + (i * (chartH - 1)) / GRID_ROWS;
        tft.drawLine(0, y, CHART_W - 1, y, GRID_COLOR);
        
        if (showLabels) {
            char label[8];
            float val = maxVal - (i * range) / GRID_ROWS;
            if (useTemp) {
                snprintf(label, sizeof(label), "%.0f", val);
            } else {
                snprintf(label, sizeof(label), "%.0f", val);
            }
            tft.drawString(label, 2, y - 4);
        }
    }
    
    tft.drawRect(0, chartY, CHART_W, chartH, DIM_TEXT);
}

void drawDoubleCurve(
    int chartY, 
    int chartH, 
    SensorHistory& history1,
    SensorHistory& history2,
    uint16_t color1,
    uint16_t color2,
    bool useTemp
) {
    int count1 = history1.getCount();
    int count2 = history2.getCount();
    
    if (count1 < 2 || count2 < 2) return;
    
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    
    const int TOTAL_HOURS = 24;
    int totalMinutes = TOTAL_HOURS * 60;
    int firstMinutes = 0;
    
    float minVal, maxVal;
    getValueRange(history1, history2, useTemp, minVal, maxVal);
    float range = maxVal - minVal;
    if (range < 1.0f) range = 1.0f;
    
    for (int i = 0; i < count1 - 1; i++) {
        const SensorSample& s1 = history1.getSample(i);
        const SensorSample& s2 = history1.getSample(i + 1);
        
        int sample1Minutes = s1.hour * 60 + s1.minute;
        int sample2Minutes = s2.hour * 60 + s2.minute;
        
        if (sample2Minutes < firstMinutes || sample1Minutes > totalMinutes) {
            continue;
        }
        
        float x1f = ((float)(sample1Minutes - firstMinutes) / totalMinutes) * CHART_W;
        float x2f = ((float)(sample2Minutes - firstMinutes) / totalMinutes) * CHART_W;
        
        x1f = constrain(x1f, 0.0f, (float)CHART_W);
        x2f = constrain(x2f, 0.0f, (float)CHART_W);
        
        int x1 = (int)x1f;
        int x2 = (int)x2f;
        
        float v1 = useTemp ? s1.temp : s1.humidity;
        float v2 = useTemp ? s2.temp : s2.humidity;
        
        int y1 = chartY + chartH - 1 - (int)(((v1 - minVal) / range) * (chartH - 2));
        int y2 = chartY + chartH - 1 - (int)(((v2 - minVal) / range) * (chartH - 2));
        
        y1 = constrain(y1, chartY, chartY + chartH - 1);
        y2 = constrain(y2, chartY, chartY + chartH - 1);
        
        tft.drawLine(x1, y1, x2, y2, color1);
    }
    
    for (int i = 0; i < count2 - 1; i++) {
        const SensorSample& s1 = history2.getSample(i);
        const SensorSample& s2 = history2.getSample(i + 1);
        
        int sample1Minutes = s1.hour * 60 + s1.minute;
        int sample2Minutes = s2.hour * 60 + s2.minute;
        
        if (sample2Minutes < firstMinutes || sample1Minutes > totalMinutes) {
            continue;
        }
        
        float x1f = ((float)(sample1Minutes - firstMinutes) / totalMinutes) * CHART_W;
        float x2f = ((float)(sample2Minutes - firstMinutes) / totalMinutes) * CHART_W;
        
        x1f = constrain(x1f, 0.0f, (float)CHART_W);
        x2f = constrain(x2f, 0.0f, (float)CHART_W);
        
        int x1 = (int)x1f;
        int x2 = (int)x2f;
        
        float v1 = useTemp ? s1.temp : s1.humidity;
        float v2 = useTemp ? s2.temp : s2.humidity;
        
        int y1 = chartY + chartH - 1 - (int)(((v1 - minVal) / range) * (chartH - 2));
        int y2 = chartY + chartH - 1 - (int)(((v2 - minVal) / range) * (chartH - 2));
        
        y1 = constrain(y1, chartY, chartY + chartH - 1);
        y2 = constrain(y2, chartY, chartY + chartH - 1);
        
        tft.drawLine(x1, y1, x2, y2, color2);
    }
}

// ========== 绘制标签栏 ==========
void drawLabels(float temp1, float temp2, float hum1, float hum2, bool force) {
    static float lastT1 = -999, lastT2 = -999, lastH1 = -999, lastH2 = -999;
    static bool cacheValid = false;
    
    // 🔥 force=true 时强制重置缓存并绘制
    if (force) {
        cacheValid = false;
    }
    
    // 如果缓存有效且数值没变，跳过绘制
    if (cacheValid && 
        temp1 == lastT1 && temp2 == lastT2 && 
        hum1 == lastH1 && hum2 == lastH2) {
        return;
    }
    
    // 清空并绘制标签区域
    tft.fillRect(0, LABEL_Y, CHART_W, LABEL_H, BG_DARK);
    tft.setFont(&lgfx::fonts::Font0);
    tft.setTextDatum(top_left);
    
    // ===== 第1行：温度 =====
    tft.setTextColor(ACCENT_COLOR);
    char buf[20];
    snprintf(buf, sizeof(buf), "T1:%.1fC", temp1);
    tft.drawString(buf, LABEL_COL1, LABEL_ROW1_Y);
    
    tft.setTextColor(TFT_ORANGE);
    snprintf(buf, sizeof(buf), "T2:%.1fC", temp2);
    tft.drawString(buf, LABEL_COL2, LABEL_ROW1_Y);
    
    // ===== 第2行：湿度 =====
    tft.setTextColor(TFT_CYAN);
    snprintf(buf, sizeof(buf), "H1:%.1f%%", hum1);
    tft.drawString(buf, LABEL_COL1, LABEL_ROW2_Y);
    
    tft.setTextColor(GREEN);
    snprintf(buf, sizeof(buf), "H2:%.1f%%", hum2);
    tft.drawString(buf, LABEL_COL2, LABEL_ROW2_Y);
    
    // 更新缓存
    lastT1 = temp1;
    lastT2 = temp2;
    lastH1 = hum1;
    lastH2 = hum2;
    cacheValid = true;
}

// ========== 绘制底部数据 ==========
void drawBottomData(int h, int m, int s, bool force) {
    static int lastH = -1, lastM = -1, lastS = -1;
    static bool cacheValid = false;
    
    // 🔥 force=true 时强制重置缓存并绘制
    if (force) {
        cacheValid = false;
    }
    
    // 如果缓存有效且数值没变，跳过绘制
    if (cacheValid && h == lastH && m == lastM && s == lastS) {
        return;
    }
    
    clearRect(0, BOTTOM_Y, CHART_W, BOTTOM_H);
    tft.setFont(&lgfx::fonts::Font0);
    tft.setTextDatum(middle_center);
    tft.setTextColor(DIM_TEXT);
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    tft.drawString(buf, 64, BOTTOM_Y + BOTTOM_H / 2);
    
    lastH = h;
    lastM = m;
    lastS = s;
    cacheValid = true;
}

// ============================================================================
// 主绘制函数
// ============================================================================
void drawChartPage() {
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    static float prevT1 = -999, prevH1 = -999;
    static float prevT2 = -999, prevH2 = -999;
    
    dht1.update();
    dht2.update();
    
    float temp1 = dht1.getTemperature();
    float hum1  = dht1.getHumidity();
    float temp2 = dht2.getTemperature();
    float hum2  = dht2.getHumidity();
    
    if (!dht1.isValid()) {
        temp1 = prevT1;
        hum1  = prevH1;
    }
    if (!dht2.isValid()) {
        temp2 = prevT2;
        hum2  = prevH2;
    }
    
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;
    
    static bool firstDraw = true;
    
    // 🔥 关键：forcePageRedraw 或 firstDraw 都触发完整绘制
    bool needFullDraw = forcePageRedraw || firstDraw;
    
    if (needFullDraw) {
        sensorHistory1.loadFromFile();
        sensorHistory2.loadFromFile();
        
        // 清空所有区域
        clearRect(0, LABEL_Y, CHART_W, LABEL_H);
        clearRect(CHART_X, CHART1_Y, CHART_W, CHART1_H);
        clearRect(CHART_X, CHART2_Y, CHART_W, CHART2_H);
        clearRect(0, BOTTOM_Y, CHART_W, BOTTOM_H);
        
        firstDraw = false;
        chartInitialized = true;
        
        Serial.println("[ChartPage] 完整绘制");
    }
    
    // 🔥 始终调用 drawLabels 和 drawBottomData，让它们内部判断是否更新
    // 使用 needFullDraw 作为 force 参数，确保首次/切换时强制绘制
    drawLabels(temp1, temp2, hum1, hum2, needFullDraw);
    
    static int lastS = -1;
    if (needFullDraw || s != lastS) {
        clearRect(CHART_X, CHART1_Y, CHART_W, CHART1_H);
        drawGrid(CHART1_Y, CHART1_H, true, 
                 sensorHistory1, sensorHistory2, true);
        drawDoubleCurve(CHART1_Y, CHART1_H, 
                        sensorHistory1, sensorHistory2, 
                        ACCENT_COLOR, TFT_ORANGE, 
                        true);
        
        clearRect(CHART_X, CHART2_Y, CHART_W, CHART2_H);
        drawGrid(CHART2_Y, CHART2_H, true, 
                 sensorHistory1, sensorHistory2, false);
        drawDoubleCurve(CHART2_Y, CHART2_H, 
                        sensorHistory1, sensorHistory2, 
                        TFT_CYAN, GREEN, 
                        false);
        
        lastS = s;
    }
    
    drawBottomData(h, m, s, needFullDraw);
    
    prevT1 = temp1;
    prevH1 = hum1;
    prevT2 = temp2;
    prevH2 = hum2;
}

// ============================================================================
// 初始化图表页面
// ============================================================================
void initChartPage() {
    chartInitialized = false;
    
    sensorHistory1.loadFromFile();
    sensorHistory2.loadFromFile();
    
    Serial.println("[ChartPage] 初始化完成");
}

// ============================================================================
// 更新图表数据（每秒调用）
// ============================================================================
void updateChartPage() {
    if (!chartInitialized) {
        initChartPage();
        forcePageRedraw = true;
        return;
    }
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return;
    }
    
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;
    
    dht1.update();
    dht2.update();
    
    float temp1 = dht1.getTemperature();
    float hum1  = dht1.getHumidity();
    float temp2 = dht2.getTemperature();
    float hum2  = dht2.getHumidity();
    
    static float prevT1 = -999, prevH1 = -999;
    static float prevT2 = -999, prevH2 = -999;
    
    if (!dht1.isValid()) {
        temp1 = prevT1;
        hum1  = prevH1;
    }
    if (!dht2.isValid()) {
        temp2 = prevT2;
        hum2  = prevH2;
    }
    
    prevT1 = temp1;
    prevH1 = hum1;
    prevT2 = temp2;
    prevH2 = hum2;
    
    // 更新标签栏（force=false，内部判断数值是否变化）
    drawLabels(temp1, temp2, hum1, hum2, false);
    
    // 更新底部时间
    drawBottomData(h, m, s, false);
    
    static int lastS = -1;
    if (s != lastS) {
        clearRect(CHART_X, CHART1_Y, CHART_W, CHART1_H);
        drawGrid(CHART1_Y, CHART1_H, true, 
                 sensorHistory1, sensorHistory2, true);
        drawDoubleCurve(CHART1_Y, CHART1_H, 
                        sensorHistory1, sensorHistory2, 
                        ACCENT_COLOR, TFT_ORANGE, 
                        true);
        
        clearRect(CHART_X, CHART2_Y, CHART_W, CHART2_H);
        drawGrid(CHART2_Y, CHART2_H, true, 
                 sensorHistory1, sensorHistory2, false);
        drawDoubleCurve(CHART2_Y, CHART2_H, 
                        sensorHistory1, sensorHistory2, 
                        TFT_CYAN, GREEN, 
                        false);
        
        lastS = s;
    }
}