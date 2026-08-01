#ifndef THEME_H
#define THEME_H

#include <stdint.h>

// 主题模式
enum ThemeMode { THEME_DAY = 0, THEME_NIGHT = 1 };

// 主题颜色集（RGB565）
struct ThemeColors {
    uint16_t bg;          // 背景
    uint16_t mainText;    // 主文字
    uint16_t dimText;     // 次要文字
    uint16_t accent;      // 强调
    uint16_t temp;        // 温度
    uint16_t humidity;    // 湿度
    uint16_t green;       // 连接OK
    uint16_t red;         // 断连
    uint16_t orange;      // AP提示
    uint16_t grid;        // 网格
    uint16_t gridLabel;   // 网格标签
    uint16_t debugBorder; // 调试边框
};

// 根据小时判断主题（6:00-18:00 白天，其余夜间）
ThemeMode themeModeFromHour(int hour);

// 取某主题的颜色集
const ThemeColors& themeColors(ThemeMode mode);

// 全局当前主题（UI loop 维护，单线程读写）
extern volatile ThemeMode g_currentTheme;

// 当前主题颜色（便捷：返回 themeColors(g_currentTheme)）
const ThemeColors& currentThemeColors();

#endif
