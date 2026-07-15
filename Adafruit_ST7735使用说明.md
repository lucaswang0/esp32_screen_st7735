# ST7735 屏幕驱动使用说明

## 概述

本驱动基于 **Adafruit_ST7735** 库，适用于 ST7735 1.44寸 TFT LCD 屏幕（型号 MD144_SPI_V07，分辨率 128×128），支持 ESP32-C3 等芯片，通过 SPI 接口通信。

## 硬件连接

| 屏幕引脚 | ESP32-C3 GPIO | 说明 |
|---------|---------------|------|
| SCLK | GPIO4 | SPI 时钟 |
| MOSI | GPIO3 | SPI 数据输出 |
| RST | GPIO2 | 复位引脚 |
| DC | GPIO1 | 数据/命令选择 |
| CS | GPIO0 | 片选 |
| BL | GPIO5 | 背光控制 |

## 文件结构

使用 Adafruit_ST7735 库只需在 `platformio.ini` 中添加库依赖即可，无需额外文件。

```
项目/
├── platformio.ini     # 项目配置（含库依赖）
└── src/
    └── main.cpp       # 主程序
```

## PlatformIO 配置

在 `platformio.ini` 中添加库依赖：

```ini
[env:esp32-c3-devkitm-1]
platform = espressif32@6.5.0
board = esp32-c3-devkitm-1
framework = arduino

lib_deps =
    adafruit/Adafruit ST7735 and ST7789 Library
    adafruit/Adafruit BusIO
```

## 基本使用

```cpp
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// 引脚定义
#define TFT_CS   0
#define TFT_DC   1
#define TFT_RST  2
#define TFT_BL   5
#define TFT_SCLK 4
#define TFT_MOSI 3

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// 显示偏移量（Adafruit 库默认偏移）
uint8_t y_offset = 32;

void setup() {
  Serial.begin(115200);

  // 初始化 ST7735 - 黑色标签
  tft.initR(INITR_BLACKTAB);

  // 打开背光
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // 清屏
  tft.fillScreen(ST7735_BLACK);

  // 画对角线测试
  tft.drawLine(0, y_offset, 127, y_offset + 127, ST7735_RED);
  tft.drawLine(127, y_offset, 0, y_offset + 127, ST7735_BLUE);
  tft.drawRect(0, y_offset, 128, 128, ST7735_YELLOW);

  // 显示文字
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(2);
  tft.setCursor(20, y_offset + 20);
  tft.println("Hello!");

  Serial.println("Display done!");
}

void loop() {
  delay(1000);
}
```

## API 函数

### 初始化

```cpp
tft.initR(INITR_BLACKTAB);  // 初始化黑色标签屏幕
tft.initR(INITR_GREENTAB);  // 初始化绿色标签屏幕
tft.initR(INITR_REDTAB);    // 初始化红色标签屏幕

pinMode(TFT_BL, OUTPUT);
digitalWrite(TFT_BL, HIGH);  // 打开背光
digitalWrite(TFT_BL, LOW);  // 关闭背光

tft.fillScreen(color);  // 清屏
```

### 基础绘图

```cpp
tft.drawPixel(x, y, color);                    // 绘制像素点
tft.drawLine(x0, y0, x1, y1, color);          // 绘制直线
tft.drawRect(x, y, w, h, color);              // 绘制矩形边框
tft.fillRect(x, y, w, h, color);              // 填充矩形
tft.drawCircle(x, y, r, color);               // 绘制圆形边框
tft.fillCircle(x, y, r, color);               // 填充圆形
tft.drawTriangle(x0, y0, x1, y1, x2, y2, color);  // 绘制三角形
```

### 文字显示

```cpp
tft.setTextColor(color);           // 设置文字颜色
tft.setTextSize(size);            // 设置文字大小 (1-8)
tft.setCursor(x, y);              // 设置光标位置
tft.println("text");              // 打印并换行
tft.print("text");               // 打印不换行
```

### 颜色常量

| 常量 | 颜色 |
|------|------|
| ST7735_BLACK | 黑色 |
| ST7735_WHITE | 白色 |
| ST7735_RED | 红色 |
| ST7735_GREEN | 绿色 |
| ST7735_BLUE | 蓝色 |
| ST7735_YELLOW | 黄色 |
| ST7735_CYAN | 青色 |
| ST7735_MAGENTA | 紫色 |
| ST7735_ORANGE | 橙色 |

## 显示偏移

Adafruit 库的默认 Y 偏移为 32，所有绘图函数都需要加上偏移量：

```cpp
uint8_t y_offset = 32;

// 绘制内容时使用偏移
tft.drawLine(0, y_offset, 127, y_offset + 127, ST7735_RED);
tft.setCursor(10, y_offset + 20);
```

## 屏幕旋转

```cpp
tft.setRotation(0);  // 0°, 0° (默认)
tft.setRotation(1);  // 90°
tft.setRotation(2);  // 180°
tft.setRotation(3);  // 270°
```

## 注意事项

1. **Y偏移**：Adafruit 库默认有 Y=32 的偏移，所有坐标需要加上偏移量
2. **背光控制**：需要在初始化后手动设置背光引脚为输出并打开
3. **屏幕型号**：不同标签颜色（黑/绿/红）需要选择对应的初始化函数
4. **SPI引脚**：确保硬件连接的 SPI 引脚与代码中定义的一致

## 版本信息

- 驱动版本：1.0
- 适用芯片：ESP32-C3 / ESP32 / ESP8266 等
- 屏幕型号：MD144_SPI_V07（ST7735 1.44寸，128×128）
- 库版本：Adafruit ST7735 and ST7789 Library
- 接口：SPI