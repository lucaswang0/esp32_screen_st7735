#include "ChartPage.h"
#include "Display.h"
#include <DHT.h>
#include <time.h>
#include "SensorHistory.h"

extern DHT dht1;
extern DHT dht2;
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
#define GRID_COLOR     tft.color565(30, 30, 50)     // 网格线颜色
#define GRID_LABEL     tft.color565(80, 80, 120)    // 网格标签颜色

// ========== 布局常量 ==========
const int LABEL_Y        = 0;
const int LABEL_H        = 24;       // y: 0-23  两行标签

const int CHART1_Y       = LABEL_Y + LABEL_H + 2;   // y: 26
const int CHART1_H       = 46;       // y: 26-71  温度曲线

const int CHART2_Y       = CHART1_Y + CHART1_H + 2; // y: 74
const int CHART2_H       = 40;       // y: 74-113 湿度曲线

const int BOTTOM_Y       = CHART2_Y + CHART2_H + 2; // y: 116
const int BOTTOM_H       = 128 - BOTTOM_Y;          // y: 116-127 (12px)

const int CHART_W        = 128;
const int CHART_X        = 0;

// 标签位置
const int LABEL_ROW1_Y   = LABEL_Y + 2;
const int LABEL_ROW2_Y   = LABEL_Y + 14;
const int LABEL_COL1     = 2;
const int LABEL_COL2     = 66;

// 网格参数
const int GRID_COLS      = 6;        // 横向分成6格
const int GRID_ROWS      = 4;        // 纵向分成4格

// ========== 绘制背景网格（根据实际数据范围，时间标签动态显示） ==========
void drawGrid(int chartY, int chartH, bool showLabels, 
              SensorHistory& history1, SensorHistory& history2, 
              bool useTemp) {
    // 绘制网格线（浅灰色）
    tft.setTextDatum(top_left);
    tft.setFont(&lgfx::fonts::Font0);
    tft.setTextColor(GRID_LABEL);
    
    // ===== 计算实际数据范围 =====
    int count1 = history1.getCount();
    int count2 = history2.getCount();
    
    float minVal = 999, maxVal = -999;
    
    // 从 history1 获取数据
    for (int i = 0; i < count1; i++) {
        const SensorSample& s = history1.getSample(i);
        float val = useTemp ? s.temp : s.humidity;
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }
    
    // 从 history2 获取数据
    for (int i = 0; i < count2; i++) {
        const SensorSample& s = history2.getSample(i);
        float val = useTemp ? s.temp : s.humidity;
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }
    
    // 如果没有数据，使用默认范围
    if (count1 == 0 && count2 == 0) {
        if (useTemp) {
            minVal = 0;
            maxVal = 50;
        } else {
            minVal = 0;
            maxVal = 100;
        }
    }
    
    // 扩展范围，让曲线不贴边（上下各扩展10%）
    float range = maxVal - minVal;
    if (range < 1.0f) range = 1.0f;
    float padding = range * 0.1f;
    minVal -= padding;
    maxVal += padding;
    if (minVal < 0 && !useTemp) minVal = 0;  // 湿度不低于0
    if (useTemp && minVal < -10) minVal = -10;
    
    range = maxVal - minVal;
    if (range < 1.0f) range = 1.0f;
    
    // ===== 获取当前时间 =====
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    
    // ===== 纵向网格线 (时间轴) =====
    for (int i = 0; i <= GRID_COLS; i++) {
        int x = (i * CHART_W) / GRID_COLS;
        tft.drawLine(x, chartY, x, chartY + chartH - 1, GRID_COLOR);
        
        // 显示时间标签（底部）
        if (showLabels && i < GRID_COLS) {
            char label[8];
            // 计算当前时间往前推的时间点
            // 每格代表 (当前时间 - 24小时) / 6 小时
            float hourStep = 24.0f / GRID_COLS;  // 4小时/格
            float hourOffset = (GRID_COLS - i) * hourStep;
            int displayHour = (int)(currentHour - hourOffset);
            // 处理跨天情况
            if (displayHour < 0) {
                displayHour += 24;
            }
            // 显示格式 "HH"
            snprintf(label, sizeof(label), "%02d", displayHour);
            tft.drawString(label, x + 2, chartY + chartH - 8);
        }
    }
    
    // ===== 绘制当前时间标记线（竖直虚线） =====
    if (showLabels) {
        // 计算当前时间在图表中的X位置
        // 假设最右边是当前时间，最左边是24小时前
        float hourStep = 24.0f / GRID_COLS;
        // 当前时间对应的X位置（在图表右侧）
        int currentX = CHART_W - 2;
        // 绘制一条高亮竖线标记当前时间
        tft.drawLine(currentX, chartY, currentX, chartY + chartH - 1, tft.color565(100, 100, 80));
        
        // 在底部显示 "现在" 标签
        tft.setTextColor(tft.color565(100, 200, 100));
        tft.drawString("现在", currentX - 12, chartY + chartH - 8);
    }
    
    // ===== 横向网格线 (数值轴) =====
    for (int i = 0; i <= GRID_ROWS; i++) {
        int y = chartY + (i * (chartH - 1)) / GRID_ROWS;
        tft.drawLine(0, y, CHART_W - 1, y, GRID_COLOR);
        
        // 显示数值标签（左侧）
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
    
    // 边框
    tft.drawRect(0, chartY, CHART_W, chartH, DIM_TEXT);
}

// ========== 绘制曲线通用函数（支持两条曲线） ==========
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
    
    // ===== 计算实际数据范围（与网格保持一致） =====
    int maxCount = max(count1, count2);
    
    float minVal = 999, maxVal = -999;
    
    for (int i = 0; i < maxCount; i++) {
        if (i < count1) {
            const SensorSample& s = history1.getSample(i);
            float val = useTemp ? s.temp : s.humidity;
            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
        }
        if (i < count2) {
            const SensorSample& s = history2.getSample(i);
            float val = useTemp ? s.temp : s.humidity;
            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
        }
    }
    
    float range = maxVal - minVal;
    if (range < 1.0f) range = 1.0f;
    float padding = range * 0.1f;
    minVal -= padding;
    maxVal += padding;
    if (minVal < 0 && !useTemp) minVal = 0;
    if (useTemp && minVal < -10) minVal = -10;
    range = maxVal - minVal;
    if (range < 1.0f) range = 1.0f;
    
    // ===== 绘制传感器1曲线 =====
    for (int i = 0; i < count1 - 1; i++) {
        const SensorSample& s1 = history1.getSample(i);
        const SensorSample& s2 = history1.getSample(i + 1);
        
        int x1 = (i * CHART_W) / (count1 - 1);
        int x2 = ((i + 1) * CHART_W) / (count1 - 1);
        
        float v1 = useTemp ? s1.temp : s1.humidity;
        float v2 = useTemp ? s2.temp : s2.humidity;
        
        int y1 = chartY + chartH - 1 - (int)(((v1 - minVal) / range) * (chartH - 2));
        int y2 = chartY + chartH - 1 - (int)(((v2 - minVal) / range) * (chartH - 2));
        
        y1 = constrain(y1, chartY, chartY + chartH - 1);
        y2 = constrain(y2, chartY, chartY + chartH - 1);
        
        tft.drawLine(x1, y1, x2, y2, color1);
    }
    
    // ===== 绘制传感器2曲线 =====
    for (int i = 0; i < count2 - 1; i++) {
        const SensorSample& s1 = history2.getSample(i);
        const SensorSample& s2 = history2.getSample(i + 1);
        
        int x1 = (i * CHART_W) / (count2 - 1);
        int x2 = ((i + 1) * CHART_W) / (count2 - 1);
        
        float v1 = useTemp ? s1.temp : s1.humidity;
        float v2 = useTemp ? s2.temp : s2.humidity;
        
        int y1 = chartY + chartH - 1 - (int)(((v1 - minVal) / range) * (chartH - 2));
        int y2 = chartY + chartH - 1 - (int)(((v2 - minVal) / range) * (chartH - 2));
        
        y1 = constrain(y1, chartY, chartY + chartH - 1);
        y2 = constrain(y2, chartY, chartY + chartH - 1);
        
        tft.drawLine(x1, y1, x2, y2, color2);
    }
}

// ========== 绘制标签栏（两行） ==========
void drawLabels(float temp1, float temp2, float hum1, float hum2, bool force) {
    static float lastT1 = -999, lastT2 = -999, lastH1 = -999, lastH2 = -999;
    
    if (!force && temp1 == lastT1 && temp2 == lastT2 && hum1 == lastH1 && hum2 == lastH2) {
        return;
    }
    
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
    
    lastT1 = temp1;
    lastT2 = temp2;
    lastH1 = hum1;
    lastH2 = hum2;
}

// ========== 绘制底部数据（时间） ==========
void drawBottomData(int h, int m, bool force) {
    static int lastH = -1, lastM = -1;
    
    if (!force && h == lastH && m == lastM) return;
    
    clearRect(0, BOTTOM_Y, CHART_W, BOTTOM_H);
    tft.setFont(&lgfx::fonts::Font0);
    tft.setTextDatum(middle_center);
    tft.setTextColor(DIM_TEXT);
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    tft.drawString(buf, 64, BOTTOM_Y + BOTTOM_H / 2);
    
    lastH = h;
    lastM = m;
}

// ========== 主绘制函数 ==========
void drawChartPage() {
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    // ===== 1. 静态缓存 =====
    static float prevT1 = -999, prevH1 = -999;
    static float prevT2 = -999, prevH2 = -999;
    
    // ===== 2. 读取传感器数据 =====
    float temp1 = dht1.readTemperature();
    float hum1  = dht1.readHumidity();
    float temp2 = dht2.readTemperature();
    float hum2  = dht2.readHumidity();
    
    if (isnan(temp1)) temp1 = prevT1;
    if (isnan(hum1))  hum1  = prevH1;
    if (isnan(temp2)) temp2 = prevT2;
    if (isnan(hum2))  hum2  = prevH2;
    
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;
    
    // ===== 3. 强制刷新 =====
    static bool lastForce = false;
    if (forcePageRedraw && !lastForce) {
        sensorHistory1.loadFromFile();
        sensorHistory2.loadFromFile();
        drawLabels(temp1, temp2, hum1, hum2, true);
        clearRect(CHART_X, CHART1_Y, CHART_W, CHART1_H);
        clearRect(CHART_X, CHART2_Y, CHART_W, CHART2_H);
        clearRect(0, BOTTOM_Y, CHART_W, BOTTOM_H);
    }
    lastForce = forcePageRedraw;
    
    // ===== 4. 绘制标签栏 =====
    drawLabels(temp1, temp2, hum1, hum2, forcePageRedraw);
    
    // ===== 5. 绘制温度曲线（T1 + T2） =====
    static int lastS = -1;
    if (forcePageRedraw || s != lastS) {
        clearRect(CHART_X, CHART1_Y, CHART_W, CHART1_H);
        // 先画网格（根据实际温度数据范围）
        drawGrid(CHART1_Y, CHART1_H, true, 
                 sensorHistory1, sensorHistory2, true);
        // 再画曲线（在网格上面）
        drawDoubleCurve(CHART1_Y, CHART1_H, 
                        sensorHistory1, sensorHistory2, 
                        ACCENT_COLOR, TFT_ORANGE, 
                        true);
    }
    
    // ===== 6. 绘制湿度曲线（H1 + H2） =====
    if (forcePageRedraw || s != lastS) {
        clearRect(CHART_X, CHART2_Y, CHART_W, CHART2_H);
        // 先画网格（根据实际湿度数据范围）
        drawGrid(CHART2_Y, CHART2_H, true, 
                 sensorHistory1, sensorHistory2, false);
        // 再画曲线（在网格上面）
        drawDoubleCurve(CHART2_Y, CHART2_H, 
                        sensorHistory1, sensorHistory2, 
                        TFT_CYAN, GREEN, 
                        false);
    }
    
    // ===== 7. 绘制底部时间 =====
    drawBottomData(h, m, forcePageRedraw);
    
    // ===== 8. 更新状态 =====
    lastS = s;
    prevT1 = temp1;
    prevH1 = hum1;
    prevT2 = temp2;
    prevH2 = hum2;
}