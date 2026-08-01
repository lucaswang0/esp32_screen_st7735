#include "Theme.h"

volatile ThemeMode g_currentTheme = THEME_DAY;

// 不依赖 tft 实例的 RGB565 计算
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ========== 白天主题：浅暖米白底 + 深暖棕字 ==========
static const ThemeColors dayColors = {
    rgb565(245, 238, 225),  // bg 浅暖米白
    rgb565(58, 42, 33),     // mainText 深暖棕
    rgb565(140, 120, 100),  // dimText 中暖灰
    rgb565(210, 110, 40),   // accent 暖橙
    rgb565(200, 55, 45),    // temp 朱红
    rgb565(35, 95, 165),    // humidity 深青蓝
    rgb565(40, 135, 60),    // green 深绿
    rgb565(185, 45, 45),    // red 深红
    rgb565(210, 100, 30),   // orange 深橙
    rgb565(215, 200, 180),  // grid 浅暖灰
    rgb565(140, 120, 100),  // gridLabel 中暖灰
    rgb565(255, 255, 0)     // debugBorder 黄
};

// ========== 夜间主题：深棕褐底 + 暖米黄字 ==========
static const ThemeColors nightColors = {
    rgb565(30, 22, 18),     // bg 深棕褐
    rgb565(230, 200, 140),  // mainText 暖米黄
    rgb565(150, 125, 90),   // dimText 中暖棕
    rgb565(230, 150, 60),   // accent 暖橙
    rgb565(240, 110, 70),   // temp 暖朱红
    rgb565(120, 170, 220),  // humidity 亮青蓝
    rgb565(120, 200, 100),  // green 亮绿
    rgb565(240, 90, 80),    // red 亮红
    rgb565(240, 150, 50),   // orange 亮橙
    rgb565(55, 42, 32),     // grid 深暖灰
    rgb565(130, 108, 80),   // gridLabel 中暖棕
    rgb565(255, 255, 0)     // debugBorder 黄
};

ThemeMode themeModeFromHour(int hour) {
    return (hour >= 6 && hour < 18) ? THEME_DAY : THEME_NIGHT;
}

const ThemeColors& themeColors(ThemeMode mode) {
    return (mode == THEME_DAY) ? dayColors : nightColors;
}

const ThemeColors& currentThemeColors() {
    return themeColors(g_currentTheme);
}
