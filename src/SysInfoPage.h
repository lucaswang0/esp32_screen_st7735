#ifndef SYSINFO_PAGE_H
#define SYSINFO_PAGE_H

#include <Arduino.h>
#include "Display.h"

// 初始化系统信息页面
void initSysInfoPage();

// 完整绘制系统信息页面（页面切换/主题切换时调用）
void drawSysInfoPage();

// 更新系统信息（每秒调用）
void updateSysInfoPage();

#endif