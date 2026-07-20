#ifndef CLOCK_PAGE_H
#define CLOCK_PAGE_H

#include <Arduino.h>
#include "Display.h"

// 初始化时钟页面（在 setup 中调用）
void initClockPage();

// 完整绘制时钟页面（页面切换时调用）
void drawClockPage();

// 更新时钟页面动态内容（每秒调用）
void updateClockPage();



#endif