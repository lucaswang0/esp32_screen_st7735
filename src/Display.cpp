#include "Display.h"
#include "Theme.h"

LGFX tft;

void clearRect(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, currentThemeColors().bg);
}

void drawBg() {
    tft.fillScreen(currentThemeColors().bg);
}