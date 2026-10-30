#ifndef HELP_PAGE_H
#define HELP_PAGE_H

#include <Arduino.h>
#include "Display.h"

// 初始化帮助页面
void initHelpPage();

// 完整绘制帮助页面（页面切换/主题切换时调用）
void drawHelpPage();

// 帮助页面无动态内容，空实现
void updateHelpPage();

#endif
