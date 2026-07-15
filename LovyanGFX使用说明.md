# LovyanGFX 驱动使用说明

## 概述

本文档说明如何在 ESP32-C3 上使用 LovyanGFX 驱动 ST7735S 屏幕。

## 硬件配置

| 功能 | GPIO |
|------|------|
| SCLK | GPIO4 |
| MOSI | GPIO3 |
| RST  | GPIO2 |
| DC   | GPIO1 |
| CS   | GPIO0 |
| BL   | GPIO5 |

## 屏幕参数

- 驱动芯片: ST7735S
- 分辨率: 128×128
- 接口: SPI
- X 偏移: 0
- Y 偏移: 0

## PlatformIO 配置

在 `platformio.ini` 中添加：

```ini
lib_deps = lovyan03/LovyanGFX

build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
```

## 驱动代码

```cpp
#include <Arduino.h>
#include <LovyanGFX.hpp>

// LovyanGFX 屏幕配置
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Panel_ST7735S _panel_instance;
  lgfx::Bus_SPI _bus_instance;

  LGFX(void) {
    // 配置 SPI 总线
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;  // ESP32-C3 使用 SPI2_HOST
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.pin_sclk = 4;  // GPIO4
      cfg.pin_mosi = 3;  // GPIO3
      cfg.pin_miso = -1;
      cfg.pin_dc   = 1;  // GPIO1
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    // 配置面板参数
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 0;     // GPIO0
      cfg.pin_rst = 2;    // GPIO2
      cfg.pin_busy = -1;
      cfg.memory_width = 128;
      cfg.memory_height = 160;
      cfg.panel_width = 128;
      cfg.panel_height = 128;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.rgb_order = false;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};

LGFX tft;
```

## 初始化

```cpp
void setup() {
  Serial.begin(115200);
  delay(1500);

  // 初始化 TFT
  tft.init();

  // 打开背光
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH);

  // 清屏
  tft.fillScreen(TFT_BLACK);
}
```

## 常用绘图函数

```cpp
// 清屏
tft.fillScreen(TFT_BLACK);

// 画点
tft.drawPixel(x, y, TFT_WHITE);

// 画线
tft.drawLine(x1, y1, x2, y2, TFT_RED);

// 画矩形
tft.drawRect(x, y, w, h, TFT_YELLOW);
tft.fillRect(x, y, w, h, TFT_BLUE);

// 画圆
tft.drawCircle(x, y, r, TFT_GREEN);
tft.fillCircle(x, y, r, TFT_GREEN);

// 显示文字
tft.setTextColor(TFT_WHITE);
tft.setTextSize(2);
tft.setCursor(10, 10);
tft.println("Hello");
```

## 预定义颜色

```cpp
TFT_BLACK
TFT_WHITE
TFT_RED
TFT_GREEN
TFT_BLUE
TFT_YELLOW
TFT_CYAN
TFT_MAGENTA
```

## 注意事项

1. ESP32-C3 使用 `SPI2_HOST` 而非 `HSPI_HOST`
2. 背光引脚需要手动控制（GPIO5）
3. 如果上传失败，可降低上传速度：`board_upload.speed = 115200`