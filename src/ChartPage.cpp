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
    } else if (maxVal - minVal < 0.5f) {
        // 数据全部相同或差异过小时，扩展范围
        float center = (maxVal + minVal) / 2.0f;
        minVal = center - 1.0f;
        maxVal = center + 1.0f;
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
        
        // "现在"文字位置：靠右显示，避免被左边框裁剪
        tft.setTextColor(tft.color565(100, 200, 100));
        if (currentX < 20) {
            tft.drawString("现在", currentX + 2, chartY + chartH - 8);
        } else if (currentX > CHART_W - 24) {
            tft.drawString("现在", currentX - 22, chartY + chartH - 8);
        } else {
            tft.drawString("现在", currentX - 12, chartY + chartH - 8);
        }
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
    
    if (count1 < 1 && count2 < 1) return;
    
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
    
    int chartBottom = chartY + chartH - 1;
    
    auto drawHistoryCurve = [&](SensorHistory& history, int count, uint16_t color) {
        for (int i = 0; i < count - 1; i++) {
            const SensorSample& s1 = history.getSample(i);
            const SensorSample& s2 = history.getSample(i + 1);
            
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
            
            int y1 = chartBottom - (int)(((v1 - minVal) / range) * (chartH - 2));
            int y2 = chartBottom - (int)(((v2 - minVal) / range) * (chartH - 2));
            
            y1 = constrain(y1, chartY, chartBottom);
            y2 = constrain(y2, chartY, chartBottom);
            
            tft.drawLine(x1, y1, x2, y2, color);
        }
    };
    
    if (count1 >= 2) drawHistoryCurve(history1, count1, color1);
    if (count2 >= 2) drawHistoryCurve(history2, count2, color2);
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
// 共享的传感器值缓存（drawChartPage/updateChartPage 共用）
// ============================================================================
static float s_temp1 = 0, s_hum1 = 0, s_temp2 = 0, s_hum2 = 0;
static float s_prevT1 = -999, s_prevH1 = -999;
static float s_prevT2 = -999, s_prevH2 = -999;

// 读取并缓存传感器数据（无效或 0 时使用上次的值）
static void readAndCacheSensors() {
    dht1.update();
    dht2.update();

    s_temp1 = dht1.getTemperature();
    s_hum1  = dht1.getHumidity();
    s_temp2 = dht2.getTemperature();
    s_hum2  = dht2.getHumidity();

    // 有效性检查：DHT11 温度范围 -20~60°C，湿度 1-100%RH
    bool t1Ok = dht1.isValid() && s_temp1 >= -20 && s_temp1 <= 60;
    bool h1Ok = dht1.isValid() && s_hum1  > 0 && s_hum1  <= 100;
    bool t2Ok = dht2.isValid() && s_temp2 >= -20 && s_temp2 <= 60;
    bool h2Ok = dht2.isValid() && s_hum2  > 0 && s_hum2  <= 100;

    if (t1Ok) s_prevT1 = s_temp1; else if (s_prevT1 != -999) s_temp1 = s_prevT1;
    if (h1Ok) s_prevH1 = s_hum1;  else if (s_prevH1 != -999) s_hum1  = s_prevH1;
    if (t2Ok) s_prevT2 = s_temp2; else if (s_prevT2 != -999) s_temp2 = s_prevT2;
    if (h2Ok) s_prevH2 = s_hum2;  else if (s_prevH2 != -999) s_hum2  = s_prevH2;
}

// 重绘单个图表（标签栏 + 网格 + 曲线）
static void redrawChart(int y, int h, bool useTemp, uint16_t c1, uint16_t c2) {
    clearRect(CHART_X, y, CHART_W, h);
    drawGrid(y, h, true, sensorHistory1, sensorHistory2, useTemp);
    drawDoubleCurve(y, h, sensorHistory1, sensorHistory2, c1, c2, useTemp);
}

// ============================================================================
// 主绘制函数（页面切换时完整重绘）
// ============================================================================
void drawChartPage() {
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    readAndCacheSensors();
    
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;
    
    // 重新加载历史数据
    sensorHistory1.reset();
    sensorHistory2.reset();
    sensorHistory1.loadFromFile();
    sensorHistory2.loadFromFile();
    
    // 清空所有区域
    clearRect(0, LABEL_Y, CHART_W, LABEL_H);
    clearRect(CHART_X, CHART1_Y, CHART_W, CHART1_H);
    clearRect(CHART_X, CHART2_Y, CHART_W, CHART2_H);
    clearRect(0, BOTTOM_Y, CHART_W, BOTTOM_H);
    
    // 强制重绘标签和底部时间
    drawLabels(s_temp1, s_temp2, s_hum1, s_hum2, true);
    drawBottomData(h, m, s, true);
    
    // 重绘两个图表
    redrawChart(CHART1_Y, CHART1_H, true, ACCENT_COLOR, TFT_ORANGE);
    redrawChart(CHART2_Y, CHART2_H, false, TFT_CYAN, GREEN);
    
    chartInitialized = true;
    Serial.println("[ChartPage] 完整绘制");
}

// ============================================================================
// 初始化图表页面
// ============================================================================
void initChartPage() {
    chartInitialized = false;
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
    
    readAndCacheSensors();
    
    // 更新标签栏（内部判断数值变化）
    drawLabels(s_temp1, s_temp2, s_hum1, s_hum2, false);
    
    // 更新底部时间
    drawBottomData(h, m, s, false);
    
    // 每秒重绘图表（曲线随当前时间游标移动）
    static int lastS = -1;
    if (s != lastS) {
        redrawChart(CHART1_Y, CHART1_H, true, ACCENT_COLOR, TFT_ORANGE);
        redrawChart(CHART2_Y, CHART2_H, false, TFT_CYAN, GREEN);
        lastS = s;
    }
}