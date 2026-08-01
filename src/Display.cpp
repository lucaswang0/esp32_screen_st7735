#include "Display.h"

LGFX tft;

void clearRect(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, 0xF77C);
}

void drawBg() {
    tft.fillScreen(0xF77C);
}