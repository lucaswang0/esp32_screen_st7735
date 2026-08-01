#include "ChartPage.h"
#include "Log.h"
#include "Display.h"
#include "DHT11Sensor.h"
#include "SharedState.h"
#include <time.h>
#include "SensorHistory.h"

extern DHT11Sensor dht1;
extern SensorHistory sensorHistory1;

// ========== 颜色定义 ==========
#define TEMP_COLOR     tft.color565(200, 55, 45)     // 朱红 - 温度
#define HUMIDITY_COLOR tft.color565(35, 95, 165)     // 深青蓝 - 湿度
#define DIM_TEXT       tft.color565(140, 120, 100)   // 中暖灰
#define WHITE          tft.color565(58, 42, 33)      // 深暖棕
#define BG_DARK        tft.color565(245, 238, 225)   // 浅暖背景
#define GRID_COLOR     tft.color565(215, 200, 180)   // 浅暖灰网格
#define GRID_LABEL     tft.color565(140, 120, 100)   // 中暖灰标签

// ========== 布局常量 ==========
const int LABEL_Y        = 0;
const int LABEL_H        = 20;      // 标签行高度

const int CHART_Y        = LABEL_Y + LABEL_H + 2;
const int CHART_H        = 72;      // 图表高度

const int TIME_LABEL_Y   = CHART_Y + CHART_H + 2;
const int TIME_LABEL_H   = 12;      // 时间标尺高度

const int BOTTOM_Y       = TIME_LABEL_Y + TIME_LABEL_H + 2;
const int BOTTOM_H       = 128 - BOTTOM_Y;

const int CHART_W        = 128;
const int CHART_X        = 0;

const int LABEL_ROW_Y    = LABEL_Y + 3;

const int GRID_COLS      = 6;
const int GRID_ROWS      = 4;

// 时间坐标轴从 0~127 占满图表框宽度；范围标签贴在框内左右边缘
const int PLOT_W         = CHART_W;  // 实际绘图宽度 = 框宽（占满 0~127）

// ============================================================================
// 双缓冲 sprite
// ============================================================================
static lgfx::LGFX_Sprite* chart_sprite = nullptr;
static bool chart_sprites_ok = false;

// ============================================================================
// 缓存变量
// ============================================================================
static bool chartInitialized = false;

// ========== 计算温度范围 ==========
void getTempRange(SensorHistory& history, float& minVal, float& maxVal) {
    int count = history.getCount();

    minVal = 999;
    maxVal = -999;

    for (int i = 0; i < count; i++) {
        const SensorSample& s = history.getSample(i);
        float val = s.temp;
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }

    if (count == 0) {
        minVal = 0;
        maxVal = 50;
    } else if (maxVal - minVal < 0.5f) {
        float center = (maxVal + minVal) / 2.0f;
        minVal = center - 1.0f;
        maxVal = center + 1.0f;
    }

    float range = maxVal - minVal;
    if (range < 1.0f) range = 1.0f;
    float padding = range * 0.1f;
    minVal -= padding;
    maxVal += padding;
    
    if (minVal < -10) minVal = -10;
    if (maxVal > 60) maxVal = 60;
    minVal = floor(minVal);
    maxVal = ceil(maxVal);
}

// ========== 计算湿度范围 ==========
void getHumidityRange(SensorHistory& history, float& minVal, float& maxVal) {
    int count = history.getCount();

    minVal = 999;
    maxVal = -999;

    for (int i = 0; i < count; i++) {
        const SensorSample& s = history.getSample(i);
        float val = s.humidity;
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }

    if (count == 0) {
        minVal = 0;
        maxVal = 100;
    } else if (maxVal - minVal < 0.5f) {
        float center = (maxVal + minVal) / 2.0f;
        minVal = center - 2.0f;
        maxVal = center + 2.0f;
    }

    float range = maxVal - minVal;
    if (range < 1.0f) range = 1.0f;
    float padding = range * 0.1f;
    minVal -= padding;
    maxVal += padding;
    
    if (minVal < 0) minVal = 0;
    if (maxVal > 100) maxVal = 100;
}

// ========== 绘制背景网格 ==========
void drawGrid(lgfx::LovyanGFX& dst, int chartY, int chartH, SensorHistory& history) {
    float tempMin, tempMax;
    getTempRange(history, tempMin, tempMax);
    float tempRange = tempMax - tempMin;
    if (tempRange < 1.0f) tempRange = 1.0f;

    // 获取湿度范围
    float humMin, humMax;
    getHumidityRange(history, humMin, humMax);
    float humRange = humMax - humMin;
    if (humRange < 1.0f) humRange = 1.0f;

    // 绘制水平网格线和范围标签（在框内，文字贴框边）
    for (int i = 0; i <= GRID_ROWS; i++) {
        int y = chartY + (i * (chartH - 1)) / GRID_ROWS;
        
        // 网格线（占满 0~127）
        dst.drawLine(0, y, CHART_W - 1, y, GRID_COLOR);

        // 左侧贴框边显示温度值（红色）- 框内
        char label[8];
        float tempVal = tempMax - (i * tempRange) / GRID_ROWS;
        snprintf(label, sizeof(label), "%.0f", tempVal);
        dst.setTextDatum(middle_left);
        dst.setFont(&lgfx::fonts::Font0);
        dst.setTextColor(TEMP_COLOR);
        dst.drawString(label, 2, y);

        // 右侧贴框边显示湿度值（蓝色）- 框内
        float humVal = humMax - (i * humRange) / GRID_ROWS;
        snprintf(label, sizeof(label), "%.0f", humVal);
        dst.setTextDatum(middle_right);
        dst.setTextColor(HUMIDITY_COLOR);
        dst.drawString(label, CHART_W - 3, y);
    }

    // 绘制图表边框（完整框）
    dst.drawRect(0, chartY, CHART_W, chartH, DIM_TEXT);
    
    // 在框内底部添加单位标识（贴框边）
    dst.setTextDatum(bottom_left);
    dst.setFont(&lgfx::fonts::efontCN_12);
    dst.setTextColor(TEMP_COLOR);
    dst.drawString("°C", 2, chartY + chartH - 2);
    
    dst.setTextDatum(bottom_right);
    dst.setTextColor(HUMIDITY_COLOR);
    dst.drawString("%", CHART_W - 3, chartY + chartH - 2);
}

// ========== 绘制时间标尺（在图表外部） ==========
void drawTimeLabels(lgfx::LovyanGFX& dst, int y, int h, bool showNow) {
    dst.setTextDatum(top_left);
    dst.setFont(&lgfx::fonts::Font0);
    dst.setTextColor(GRID_LABEL);

    struct tm timeinfo;
    getLocalTime(&timeinfo);
    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;

    const int TOTAL_HOURS = 24;
    int totalMinutes = TOTAL_HOURS * 60;

    // 绘制垂直刻度线和时间标签（从 0 ~ 127 占满宽度）
    for (int i = 0; i <= GRID_COLS; i++) {
        int x = (i * PLOT_W) / GRID_COLS;
        if (x >= PLOT_W) x = PLOT_W - 1;
        dst.drawLine(x, y, x, y + h - 1, GRID_COLOR);

        char label[8];
        int labelHour = (i * TOTAL_HOURS) / GRID_COLS;
        snprintf(label, sizeof(label), "%02d", labelHour);
        // 标签靠边：靠左边的标签靠左绘制，靠右边的标签靠右绘制
        if (x < 14) {
            dst.setTextDatum(top_left);
            dst.drawString(label, x, y + 1);
        } else if (x > CHART_W - 18) {
            dst.setTextDatum(top_right);
            dst.drawString(label, x, y + 1);
        } else {
            dst.setTextDatum(top_left);
            dst.drawString(label, x + 1, y + 1);
        }
    }

    // 绘制当前时间线
    // if (showNow) {
    //     float currentXf = ((float)(currentHour * 60 + currentMinute) / totalMinutes) * PLOT_W;
    //     int currentX = constrain((int)currentXf, 0, PLOT_W - 2);
    //     dst.drawLine(currentX, y - 1, currentX, y + h - 1, tft.color565(100, 200, 100));

    //     dst.setTextColor(tft.color565(100, 200, 100));
    //     if (currentX < 20) {
    //         dst.drawString("现在", currentX + 2, y + 1);
    //     } else if (currentX > CHART_W - 24) {
    //         dst.drawString("现在", currentX - 22, y + 1);
    //     } else {
    //         dst.drawString("现在", currentX - 12, y + 1);
    //     }
    // }
}

// ========== 绘制温度曲线（红色） ==========
void drawTemperatureCurve(
    lgfx::LovyanGFX& dst,
    int chartY,
    int chartH,
    SensorHistory& history,
    uint16_t color
) {
    int count = history.getCount();
    if (count < 1) return;

    const int TOTAL_HOURS = 24;
    int totalMinutes = TOTAL_HOURS * 60;
    int firstMinutes = 0;

    float minVal, maxVal;
    getTempRange(history, minVal, maxVal);
    float range = maxVal - minVal;
    if (range < 1.0f) range = 1.0f;

    int chartBottom = chartY + chartH - 1;

    for (int i = 0; i < count - 1; i++) {
        const SensorSample& s1 = history.getSample(i);
        const SensorSample& s2 = history.getSample(i + 1);

        int sample1Minutes = s1.hour * 60 + s1.minute;
        int sample2Minutes = s2.hour * 60 + s2.minute;

        if (sample2Minutes < firstMinutes || sample1Minutes > totalMinutes) {
            continue;
        }

        float x1f = ((float)(sample1Minutes - firstMinutes) / totalMinutes) * PLOT_W;
        float x2f = ((float)(sample2Minutes - firstMinutes) / totalMinutes) * PLOT_W;

        x1f = constrain(x1f, 0.0f, (float)(PLOT_W - 1));
        x2f = constrain(x2f, 0.0f, (float)(PLOT_W - 1));

        int x1 = (int)x1f;
        int x2 = (int)x2f;

        float v1 = s1.temp;
        float v2 = s2.temp;

        int y1 = chartBottom - (int)(((v1 - minVal) / range) * (chartH - 2));
        int y2 = chartBottom - (int)(((v2 - minVal) / range) * (chartH - 2));

        y1 = constrain(y1, chartY, chartBottom);
        y2 = constrain(y2, chartY, chartBottom);

        dst.drawLine(x1, y1, x2, y2, color);
    }
}

// ========== 绘制湿度曲线（蓝色） ==========
void drawHumidityCurve(
    lgfx::LovyanGFX& dst,
    int chartY,
    int chartH,
    SensorHistory& history,
    uint16_t color
) {
    int count = history.getCount();
    if (count < 1) return;

    const int TOTAL_HOURS = 24;
    int totalMinutes = TOTAL_HOURS * 60;
    int firstMinutes = 0;

    // 使用温度范围来映射湿度（保持Y轴一致）
    float tempMin, tempMax;
    getTempRange(history, tempMin, tempMax);
    float range = tempMax - tempMin;
    if (range < 1.0f) range = 1.0f;

    int chartBottom = chartY + chartH - 1;

    for (int i = 0; i < count - 1; i++) {
        const SensorSample& s1 = history.getSample(i);
        const SensorSample& s2 = history.getSample(i + 1);

        int sample1Minutes = s1.hour * 60 + s1.minute;
        int sample2Minutes = s2.hour * 60 + s2.minute;

        if (sample2Minutes < firstMinutes || sample1Minutes > totalMinutes) {
            continue;
        }

        float x1f = ((float)(sample1Minutes - firstMinutes) / totalMinutes) * PLOT_W;
        float x2f = ((float)(sample2Minutes - firstMinutes) / totalMinutes) * PLOT_W;

        x1f = constrain(x1f, 0.0f, (float)(PLOT_W - 1));
        x2f = constrain(x2f, 0.0f, (float)(PLOT_W - 1));

        int x1 = (int)x1f;
        int x2 = (int)x2f;

        // 将湿度映射到温度范围（0-100% -> 温度范围）
        float v1 = (s1.humidity / 100.0f) * range + tempMin;
        float v2 = (s2.humidity / 100.0f) * range + tempMin;

        int y1 = chartBottom - (int)(((v1 - tempMin) / range) * (chartH - 2));
        int y2 = chartBottom - (int)(((v2 - tempMin) / range) * (chartH - 2));

        y1 = constrain(y1, chartY, chartBottom);
        y2 = constrain(y2, chartY, chartBottom);

        dst.drawLine(x1, y1, x2, y2, color);
    }
}

// ========== 绘制标签栏 ==========
void drawLabels(float temp1, float hum1, bool force) {
    static float lastT1 = -999, lastH1 = -999;
    static bool cacheValid = false;

    if (force) {
        cacheValid = false;
    }

    if (cacheValid && temp1 == lastT1 && hum1 == lastH1) {
        return;
    }

    tft.fillRect(0, LABEL_Y, CHART_W, LABEL_H, BG_DARK);
    tft.setFont(&lgfx::fonts::Font0);
    tft.setTextDatum(middle_left);

    // 温度显示（红色）
    tft.setTextColor(TEMP_COLOR);
    char buf[20];
    snprintf(buf, sizeof(buf), "T:%.1fC", temp1);
    tft.drawString(buf, 2, LABEL_Y + LABEL_H/2);
    
    // 湿度显示（蓝色）
    tft.setTextColor(HUMIDITY_COLOR);
    snprintf(buf, sizeof(buf), "  H:%.1f%%", hum1);
    tft.drawString(buf, 60, LABEL_Y + LABEL_H/2);

    // 图例（右侧）
    tft.setTextDatum(middle_right);
    tft.setTextColor(TEMP_COLOR);
    tft.drawString("温度", CHART_W - 2, LABEL_Y + LABEL_H/2 - 6);
    tft.setTextColor(HUMIDITY_COLOR);
    tft.drawString("湿度", CHART_W - 2, LABEL_Y + LABEL_H/2 + 6);

    lastT1 = temp1;
    lastH1 = hum1;
    cacheValid = true;
}

// ========== 绘制底部时间 ==========
void drawBottomData(int h, int m, int s, bool force) {
    static int lastH = -1, lastM = -1, lastS = -1;
    static bool cacheValid = false;
    
    if (force) {
        cacheValid = false;
    }
    
    if (cacheValid && h == lastH && m == lastM && s == lastS) {
        return;
    }
    
    clearRect(0, BOTTOM_Y, CHART_W, BOTTOM_H);
    tft.setFont(&lgfx::fonts::efontCN_14);
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
// 共享的传感器值缓存
// ============================================================================
static float s_temp1 = 0, s_hum1 = 0;
static float s_prevT1 = -999, s_prevH1 = -999;

static void readAndCacheSensors() {
    SensorSnapshot snap = getSensorSnapshot();
    s_temp1 = snap.t1;
    s_hum1  = snap.h1;

    if (snap.t1Ok) s_prevT1 = s_temp1; else if (s_prevT1 != -999) s_temp1 = s_prevT1;
    if (snap.h1Ok) s_prevH1 = s_hum1;  else if (s_prevH1 != -999) s_hum1  = s_prevH1;
}

// 重绘图表
static void redrawChart() {
    lgfx::LGFX_Sprite* sp = chart_sprite;

    if (chart_sprites_ok && sp) {
        sp->fillSprite(BG_DARK);
        
        // 绘制网格（包含温度范围和湿度范围）
        drawGrid(*sp, 0, CHART_H, sensorHistory1);
        
        // 绘制湿度曲线（蓝色，先绘制在底层）
        drawHumidityCurve(*sp, 0, CHART_H, sensorHistory1, HUMIDITY_COLOR);
        
        // 绘制温度曲线（红色，后绘制在顶层）
        drawTemperatureCurve(*sp, 0, CHART_H, sensorHistory1, TEMP_COLOR);
        
        // 推送到屏幕
        sp->pushSprite(CHART_X, CHART_Y);
    } else {
        clearRect(CHART_X, CHART_Y, CHART_W, CHART_H);
        
        drawGrid(tft, CHART_Y, CHART_H, sensorHistory1);
        drawHumidityCurve(tft, CHART_Y, CHART_H, sensorHistory1, HUMIDITY_COLOR);
        drawTemperatureCurve(tft, CHART_Y, CHART_H, sensorHistory1, TEMP_COLOR);
    }
}

// ============================================================================
// 主绘制函数
// ============================================================================
void drawChartPage() {
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    readAndCacheSensors();

    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;

    sensorHistory1.reset();
    sensorHistory1.loadFromFile();

    // 清空所有区域
    clearRect(0, LABEL_Y, CHART_W, LABEL_H);
    clearRect(CHART_X, CHART_Y, CHART_W, CHART_H);
    clearRect(0, TIME_LABEL_Y, CHART_W, TIME_LABEL_H);
    clearRect(0, BOTTOM_Y, CHART_W, BOTTOM_H);

    // 绘制标签
    drawLabels(s_temp1, s_hum1, true);
    
    // 绘制图表
    redrawChart();
    
    // 绘制时间标尺（在图表外部）
    tft.setFont(&lgfx::fonts::Font0);
    drawTimeLabels(tft, TIME_LABEL_Y, TIME_LABEL_H, true);
    
    // 绘制底部时间
    drawBottomData(h, m, s, true);

    chartInitialized = true;
    LOG_LN("[ChartPage] 完整绘制");
}

// ============================================================================
// 初始化图表页面
// ============================================================================
void initChartPage() {
    if (!chart_sprite) chart_sprite = new lgfx::LGFX_Sprite(&tft);
    if (!chart_sprites_ok) {
        bool ok = chart_sprite->createSprite(CHART_W, CHART_H);
        chart_sprites_ok = ok;
        if (chart_sprites_ok) {
            chart_sprite->setSwapBytes(true);
            LOG_T("[ChartPage] sprite 双缓冲已分配 (%d 字节)", CHART_W * CHART_H * 2);
        } else {
            LOG_LN("[ChartPage] ⚠️ sprite 创建失败，回退到直接绘图（可能闪烁）");
        }
    }
    chartInitialized = false;
    LOG_LN("[ChartPage] 初始化完成");
}

// ============================================================================
// 更新图表数据
// ============================================================================
void updateChartPage() {
    if (!chartInitialized) {
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

    // 更新标签
    drawLabels(s_temp1, s_hum1, false);
    
    // 更新时间标尺（每秒更新）
    static int lastS = -1;
    if (s != lastS) {
        // 重绘时间标尺（显示"现在"标记移动）
        clearRect(0, TIME_LABEL_Y, CHART_W, TIME_LABEL_H);
        drawTimeLabels(tft, TIME_LABEL_Y, TIME_LABEL_H, true);
        
        // 重绘图表（曲线更新）
        redrawChart();
        
        lastS = s;
    }
    
    // 更新底部时间
    drawBottomData(h, m, s, false);
}