#ifndef ST7735_DRIVER_H
#define ST7735_DRIVER_H

#include <Arduino.h>
#include <SPI.h>

// ============================================================
//  引脚定义
// ============================================================
#ifndef PIN_SCLK
#define PIN_SCLK  4
#endif

#ifndef PIN_MOSI
#define PIN_MOSI  3
#endif

#ifndef PIN_MISO
#define PIN_MISO  -1
#endif

#ifndef PIN_RST
#define PIN_RST   2
#endif

#ifndef PIN_DC
#define PIN_DC    1
#endif

#ifndef PIN_CS
#define PIN_CS    0
#endif

#ifndef PIN_BL
#define PIN_BL    5
#endif

// ============================================================
//  屏幕尺寸定义（MD144: 128x128）
// ============================================================
#ifndef TFT_WIDTH
#define TFT_WIDTH  128
#endif

#ifndef TFT_HEIGHT
#define TFT_HEIGHT 128
#endif

// ============================================================
//  屏幕偏移定义（根据屏幕版本调整）
// ============================================================
#ifndef TFT_X_OFFSET
#define TFT_X_OFFSET  0
#endif

#ifndef TFT_Y_OFFSET
#define TFT_Y_OFFSET  0
#endif

// ============================================================
//  颜色定义
// ============================================================
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_YELLOW  0xFFE0
#define C_CYAN    0x07FF
#define C_MAGENTA 0xF81F
#define C_ORANGE  0xFD20
#define C_PINK    0xF81F
#define C_GRAY    0x8410

// ============================================================
//  函数声明
// ============================================================

// ---- 初始化 ----
void tftInit();

// ---- 绘图函数 ----
void fillScreen(uint16_t color);
void drawPixel(int16_t x, int16_t y, uint16_t color);
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

// ---- 英文字符 ----
void drawChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
void drawText(int16_t x, int16_t y, const char* text, uint16_t color, uint16_t bg, uint8_t size);

// ---- 图片显示（新增） ----
void drawImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* image);
void drawImageRGB565(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* image);
void drawImageMonochrome(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* image, uint16_t color, uint16_t bg);

// ---- 工具函数 ----
uint16_t color565(uint8_t r, uint8_t g, uint8_t b);

#endif // ST7735_DRIVER_H