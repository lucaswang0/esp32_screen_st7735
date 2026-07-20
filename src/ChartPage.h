#ifndef CHART_PAGE_H
#define CHART_PAGE_H

#include <Arduino.h>
#include "Display.h"

// 初始化图表页面
void initChartPage();

// 完整绘制图表页面（页面切换时调用）
void drawChartPage();

// 更新图表数据（每秒调用）
void updateChartPage();

#endif