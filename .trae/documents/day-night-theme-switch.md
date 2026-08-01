# 白天/夜间主题自动切换

## Context

当前两个页面(ClockPage / ChartPage)刚改造为浅色暖调"白天"主题,所有颜色是编译期 `#define tft.color565(...)` 宏,翻页时钟数字位图(`digitals.h`)的背景色也是烘焙(baked)进去的,均无法运行时切换。需求:新增一份"夜间"主题(深暖背景+暖亮字),并按时间(6:00-18:00 白天,其余夜间)自动切换。

核心挑战:把编译期颜色宏改成运行时取值,并为翻页时钟准备第二套位图。

## 配色方案

### 白天(THEME_DAY)—— 已实现,复用现有值
| 角色 | RGB | 说明 |
|------|-----|------|
| bg | (245,238,225) | 浅暖米白 |
| mainText | (58,42,33) | 深暖棕 |
| dimText | (140,120,100) | 中暖灰 |
| accent | (210,110,40) | 暖橙 |
| temp | (200,55,45) | 朱红 |
| humidity | (35,95,165) | 深青蓝 |
| green | (40,135,60) | 深绿 |
| red | (185,45,45) | 深红 |
| orange | (210,100,30) | 深橙 |
| grid | (215,200,180) | 浅暖灰 |
| gridLabel | (140,120,100) | 中暖灰 |

### 夜间(THEME_NIGHT)—— 新增
| 角色 | RGB | 说明 |
|------|-----|------|
| bg | (30,22,18) | 深棕褐 |
| mainText | (230,200,140) | 暖米黄 |
| dimText | (150,125,90) | 中暖棕 |
| accent | (230,150,60) | 暖橙 |
| temp | (240,110,70) | 暖朱红(深底需亮) |
| humidity | (120,170,220) | 亮青蓝(深底需亮) |
| green | (120,200,100) | 亮绿 |
| red | (240,90,80) | 亮红 |
| orange | (240,150,50) | 亮橙 |
| grid | (55,42,32) | 深暖灰 |
| gridLabel | (130,108,80) | 中暖棕 |

## 实施步骤

### 1. 新建 `src/Theme.h` / `src/Theme.cpp`
- `enum ThemeMode { THEME_DAY, THEME_NIGHT }`
- `struct ThemeColors { uint16_t bg, mainText, dimText, accent, temp, humidity, green, red, orange, grid, gridLabel, debugBorder; }`
- `ThemeMode themeModeFromHour(int hour)` —— `(hour>=6 && hour<18) ? THEME_DAY : THEME_NIGHT`
- `const ThemeColors& themeColors(ThemeMode)` —— 返回 dayColors / nightColors 常量引用
- `extern volatile ThemeMode g_currentTheme;` —— 全局当前主题(UI loop 维护)
- `const ThemeColors& currentThemeColors();` —— 返回 `themeColors(g_currentTheme)`
- RGB565 用内置静态函数 `rgb565(r,g,b)` 计算(`((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)`),不依赖 tft 实例,保持模块独立
- debugBorder 两套都用 (255,255,0) 黄

### 2. 改造页面颜色宏(运行时取值)
- `src/ClockPage.cpp`、`src/ChartPage.cpp`:把 `#define XXX tft.color565(r,g,b)` 改为 `#define XXX currentThemeColors().成员`
  - 例:`#define BG_COLOR currentThemeColors().bg`、`#define WHITE currentThemeColors().mainText`、`#define TEMP_RED currentThemeColors().temp` 等
  - 绘制代码本身**不用改**,宏展开自动用当前主题色
  - 删除 TFT_CYAN/TFT_GREEN 等未使用宏(可选)
- 两文件 `#include "Theme.h"`

### 3. Display.cpp 背景运行时化
- `src/Display.cpp`:`drawBg()` / `clearRect()` 的 `0xF77C` 改为 `currentThemeColors().bg`,`#include "Theme.h"`
- `src/main.cpp` setup 的 `tft.fillScreen(0xF77C)` 改为 `tft.fillScreen(currentThemeColors().bg)`

### 4. 翻页时钟双位图
- **扩展 `scripts/recolor_digitals.py`**:从原始 `digitals.h.bak`(深灰版)分别用 day 参数(NEW_BG=245,238,225 / NEW_FG=58,42,33)和 night 参数(NEW_BG=30,22,18 / NEW_FG=230,200,140)生成两套数组,合并写入 `src/digitals.h`:
  - `DIGIT_UPPER_DAY[10][238]` / `DIGIT_LOWER_DAY[10][238]`
  - `DIGIT_UPPER_NIGHT[10][238]` / `DIGIT_LOWER_NIGHT[10][238]`
  - Flash 占用 +~9.5KB(4MB flash 充足)
- **`src/FlipClockPage.cpp`**:`widget_render_single_digit` 里取数字缓冲区时按主题选择:
  ```cpp
  const uint16_t* upper_buf = (g_currentTheme==THEME_DAY) ? DIGIT_UPPER_DAY[d.cur] : DIGIT_UPPER_NIGHT[d.cur];
  ```
  - `BG_COLOR` 改为运行时 `currentThemeColors().bg`(原 `static const uint16_t BG_COLOR = FLIP_BG_COLOR;` 删除,fillSprite/fillRect 处直接用 `currentThemeColors().bg`)
  - `COLON_CLR` 改为 `currentThemeColors().mainText`
- **`src/FlipClockPage.h`**:删除 `FLIP_BG_COLOR` / `FLIP_COLON_CLR` 定义(不再需要)

### 5. UI loop 主题切换检测(`src/main.cpp` `loop()`)
- setup 末尾初始化:`struct tm t; getLocalTime(&t,0); g_currentTheme = themeModeFromHour(t.tm_hour);`(NTP 未同步前默认 DAY)
- `loop()` 每秒分支内加主题检查:
  ```cpp
  ThemeMode newTheme = themeModeFromHour(t.tm_hour);
  if (newTheme != g_currentTheme) {
      g_currentTheme = newTheme;
      drawBg();
      if (currentPage == 0) drawClockPage(); else drawChartPage();
  }
  ```
  - 切换时 drawBg + 全屏重绘(翻页时钟的 drawClockPage 会 force 重画 sprite)
- 主题读写都在 UI 线程(loop),无竞争;`g_currentTheme` 用 `volatile` 保证可见性

## 关键文件
- 新增:`src/Theme.h`、`src/Theme.cpp`
- 改:`src/ClockPage.cpp`、`src/ChartPage.cpp`(颜色宏)、`src/Display.cpp`、`src/FlipClockPage.cpp`、`src/FlipClockPage.h`、`src/main.cpp`、`src/digitals.h`、`scripts/recolor_digitals.py`

## 复用的现有机制
- `autoAdjustBacklight()`(TaskManager.cpp L49-66)已用 6/18 点分时段,主题时段与之对齐
- `loop()`(main.cpp L118)每秒刷新 + 每 10 秒切页 + drawBg/drawXxxPage 全屏重绘机制,主题切换复用此路径
- `currentThemeColors()` 返回引用,高频调用(clearRect / 20FPS 动画)开销可忽略

## 验证
1. 编译通过(PlatformIO IDE)
2. 临时改 `themeModeFromHour` 强制返回 `THEME_NIGHT`,烧录确认夜间主题:深棕褐底+暖米黄字、翻页时钟数字清晰、图表曲线在深底上可见
3. 改回,临时强制 `THEME_DAY`,确认白天主题不回归
4. 把设备时间调到 5:59 / 17:59 观察整点自动切换(切换瞬间全屏重绘无残留)
5. 翻页时钟在切换后数字颜色正确(验证双位图选择生效)
