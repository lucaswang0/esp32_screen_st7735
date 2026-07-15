#include "Display.h"

LGFX tft;

void clearRect(int x, int y, int w, int h) {
    for (int row = y; row < y + h; row++) {
        uint16_t c = tft.color565(8 + row / 20, 8 + row / 20, 20 + row / 10);
        tft.drawFastHLine(x, row, w, c);
    }
}

void drawBg() {
    for (int y = 0; y < 128; y++) {
        uint16_t c = tft.color565(8 + y / 20, 8 + y / 20, 20 + y / 10);
        tft.drawFastHLine(0, y, 128, c);
    }
}