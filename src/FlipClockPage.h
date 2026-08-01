#ifndef FLIP_CLOCK_PAGE_H
#define FLIP_CLOCK_PAGE_H

#include <Arduino.h>
#include "Display.h"

#define FLIP_DIGIT_W  17
#define FLIP_DIGIT_H  28
#define FLIP_HALF_H   14
#define FLIP_GAP      2
#define FLIP_COLON_W  4

#define FLIP_FPS          20
#define FLIP_TOTAL_FRAMES 20
#define FLIP_ANIM_HALF    10
#define FLIP_N_STRIPS     14

#define FLIP_BG_COLOR     0xF77C
#define FLIP_COLON_CLR    0x3944

#define FLIP_N_DIGITS     6
#define FLIP_N_COLONS     2

struct StripEntry {
    int8_t dy_top, dy_bot, ext;
};

struct FlipEntry {
    int8_t vis;
    StripEntry upper[FLIP_N_STRIPS];
    StripEntry lower[FLIP_N_STRIPS];
};

extern FlipEntry flip_table[FLIP_ANIM_HALF];

void initFlipClockWidget(int x, int y, int w, int h);
void drawFlipClockWidget(int h, int m, int s);
void updateFlipClockWidget(int h, int m, int s);
void renderFlipClockWidgetAnimation();

#endif
