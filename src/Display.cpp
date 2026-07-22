#include "Display.h"

LGFX tft;

void clearRect(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, 0x18C3);
}

void drawBg() {
    tft.fillScreen(0x18C3);
}