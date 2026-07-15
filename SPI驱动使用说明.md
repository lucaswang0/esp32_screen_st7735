# ST7735 屏幕驱动使用说明

## 概述

本驱动用于 ST7735 1.44寸 TFT LCD 屏幕（型号 MD144_SPI_V07，分辨率 128×128），支持 ESP32-C3 等芯片，通过 SPI 接口通信。

## 文件结构

```
lib/st7735_driver/
├── st7735_driver.h    # 头文件（引脚定义、颜色定义、函数声明）
├── st7735_driver.cpp  # 驱动实现（含 8x16 ASCII 字体数据）
└── 128img.c           # 图片数据（可选，RGB565 格式）
```

## 移植到其他工程

### 1. 复制文件

将 `lib/st7735_driver/` 目录复制到新工程的 `lib/` 目录下。

### 2. 配置引脚

在 `st7735_driver.h` 中修改引脚定义，或在 `platformio.ini` 中通过编译选项定义：

```c
// 默认引脚定义（ESP32-C3）
#define PIN_SCLK  4   // SPI 时钟
#define PIN_MOSI  3   // SPI 数据输出
#define PIN_MISO  -1  // SPI 数据输入（不使用）
#define PIN_RST   2   // 复位引脚
#define PIN_DC    1   // 数据/命令选择
#define PIN_CS    0   // 片选
#define PIN_BL    5   // 背光控制
```

或在 `platformio.ini` 中：
```ini
build_flags = 
    -D PIN_SCLK=4
    -D PIN_MOSI=3
    -D PIN_RST=2
    -D PIN_DC=1
    -D PIN_CS=0
    -D PIN_BL=5
```

### 3. 配置屏幕偏移

不同版本的 ST7735 屏幕有不同的偏移值：

| 屏幕版本 | TFT_X_OFFSET | TFT_Y_OFFSET |
|---------|-------------|-------------|
| 红色Tab | 0 | 0 |
| 绿色Tab | 2 | 3 |
| 绿色Tab | 0 | 24 |
| 黑色Tab | 0 | 24 |

在 `st7735_driver.h` 中修改：
```c
#define TFT_X_OFFSET  0
#define TFT_Y_OFFSET  0
```

## API 函数

### 初始化

```c
void tftInit();          // 初始化屏幕
void fillScreen(uint16_t color);  // 清屏（填充指定颜色）
```

### 基础绘图

```c
void drawPixel(int16_t x, int16_t y, uint16_t color);  // 绘制像素点
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);  // 填充矩形
void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);  // 绘制矩形边框
void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);  // 绘制直线
void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color);  // 绘制圆形
```

### 文字显示

```c
void drawChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);  // 绘制单个字符
void drawText(int16_t x, int16_t y, const char* text, uint16_t color, uint16_t bg, uint8_t size);  // 绘制字符串
```

**参数说明：**
- `x, y`: 起始坐标
- `c`: 字符（ASCII 32-126）
- `text`: 字符串
- `color`: 字符颜色
- `bg`: 背景颜色
- `size`: 字体大小（1=8x16, 2=16x32, ...）

### 图片显示

```c
void drawImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* image);  // 显示 RGB565 图片（uint16_t 格式）
void drawImageRGB565(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* image);  // 显示 RGB565 图片（uint8_t 格式，LVGL 生成）
void drawImageMonochrome(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* image, uint16_t color, uint16_t bg);  // 显示单色图片
```

### 工具函数

```c
uint16_t color565(uint8_t r, uint8_t g, uint8_t b);  // 将 RGB 转换为 RGB565 格式
```

## 预定义颜色

```c
#define C_BLACK   0x0000  // 黑色
#define C_WHITE   0xFFFF  // 白色
#define C_RED     0xF800  // 红色
#define C_GREEN   0x07E0  // 绿色
#define C_BLUE    0x001F  // 蓝色
#define C_YELLOW  0xFFE0  // 黄色
#define C_CYAN    0x07FF  // 青色
#define C_MAGENTA 0xF81F  // 紫色
#define C_ORANGE  0xFD20  // 橙色
#define C_PINK    0xF81F  // 粉色
#define C_GRAY    0x8410  // 灰色
```

## 使用示例

### 基础示例

```c
#include "st7735_driver.h"

void setup() {
  tftInit();
  fillScreen(C_BLACK);
  
  // 显示文字
  drawText(10, 10, "Hello World", C_WHITE, C_BLACK, 1);
  
  // 绘制图形
  drawRect(10, 50, 30, 20, C_RED);
  drawCircle(50, 60, 15, C_BLUE);
  fillRect(80, 50, 20, 30, C_GREEN);
}

void loop() {}
```

### 显示图片

```c
#include "st7735_driver.h"

// 外部图片数据
extern const uint8_t img_128[];

void setup() {
  tftInit();
  fillScreen(C_BLACK);
  
  // 显示 128x128 图片
  drawImageRGB565(0, 0, 128, 128, img_128);
}

void loop() {}
```

## 图片数据生成

### 使用 LVGL Image Converter

1. 访问 https://lvgl.io/tools/imageconverter
2. 选择 LVGL v9
3. 设置：
   - Color format: RGB565
   - Output format: C array
4. 上传图片（建议尺寸 128x160 或更小）
5. 生成并下载 `.c` 文件

### 数据格式说明

LVGL 生成的数据为 `uint8_t` 格式，每像素 2 字节（低字节在前）：

```c
const uint8_t img_128[] = {
  0x64, 0x10,  // 第一个像素
  0x64, 0x08,  // 第二个像素
  ...
};
```

使用 `drawImageRGB565()` 函数显示。

## 屏幕尺寸

默认配置为 128x160，可在头文件中修改：

```c
#define TFT_WIDTH  128
#define TFT_HEIGHT 160
```

## SPI 频率

默认 SPI 频率为 16MHz，在 `tftInit()` 中设置：

```c
spi.setFrequency(16000000);
```

可根据需要调整（建议范围 4MHz - 40MHz）。

## 注意事项

1. **屏幕偏移**：如果显示位置不正确，请根据屏幕版本调整偏移值
2. **字节顺序**：LVGL 生成的图片数据是低字节在前，使用 `drawImageRGB565()` 函数
3. **内存占用**：128x160 RGB565 图片占用 40KB 内存，注意 ESP32-C3 的内存限制
4. **字体范围**：当前字体支持 ASCII 32-126（空格到 ~）

## 版本信息

- 驱动版本：1.0
- 适用芯片：ESP32-C3 / ESP32 / ESP8266 等
- 屏幕型号：MD144_SPI_V07（ST7735 1.44寸，128×128）
- 接口：SPI