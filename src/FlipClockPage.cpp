#include "FlipClockPage.h"
#include "digitals.h"
#include <time.h>
#include <math.h>

FlipEntry flip_table[FLIP_ANIM_HALF];

struct Digit {
    uint8_t cur;
    uint8_t max_val;
    uint8_t old;
    int8_t anim_frame;
};

static Digit digits[FLIP_N_DIGITS];
static int16_t digit_x[FLIP_N_DIGITS];
static int16_t colon_x[FLIP_N_COLONS];

static lgfx::LGFX_Sprite* _sprite = nullptr;
static bool _sprite_ok = false;
static int16_t _sprite_x = 0;
static int16_t _sprite_y = 0;

static const int CW = FLIP_DIGIT_W;
static const int CH = FLIP_DIGIT_H;
static const int HALF = FLIP_HALF_H;
static const int GAP = FLIP_GAP;
static const int COLON_W = FLIP_COLON_W;
static const int N_STRIPS = FLIP_N_STRIPS;
static const int FPS = FLIP_FPS;
static const int TOTAL_FRAMES = FLIP_TOTAL_FRAMES;
static const int ANIM_HALF = FLIP_ANIM_HALF;

static const uint16_t BG_COLOR = FLIP_BG_COLOR;
static const uint16_t COLON_CLR = FLIP_COLON_CLR;

static void build_flip_table() {
    for (int f = 0; f < ANIM_HALF; f++) {
        float angle = (float)f / ANIM_HALF * (float)M_PI / 2;
        int vis_val = (int)roundf((float)HALF * cosf(angle));
        int vis = (vis_val > 1) ? vis_val : 1;
        float widen = 0.25f * sinf(angle);
        flip_table[f].vis = (int8_t)vis;
        
        for (int i = 0; i < N_STRIPS; i++) {
            float t = (float)i / N_STRIPS;
            float sh = (float)vis / N_STRIPS;
            
            flip_table[f].upper[i] = {
                (int8_t)roundf(-vis + i * sh),
                (int8_t)roundf(-vis + (i + 1) * sh),
                (int8_t)roundf(widen * CW * (1 - t) / 2)
            };
            
            flip_table[f].lower[i] = {
                (int8_t)roundf(i * sh),
                (int8_t)roundf((i + 1) * sh),
                (int8_t)roundf(widen * CW * t / 2)
            };
        }
    }
}

static void calc_layout() {
    int group_w = CW * 2 + GAP;
    int colon_gap = GAP + COLON_W + GAP;
    int total_w = group_w * 3 + colon_gap * 2;
    
    int x;
    if (total_w <= 128) {
        x = (128 - total_w) / 2;
    } else {
        x = 0;
    }
    
    digit_x[0] = x;
    digit_x[1] = x + CW + GAP;
    x += group_w;
    
    colon_x[0] = x + GAP + COLON_W / 2;
    x += colon_gap;
    
    digit_x[2] = x;
    digit_x[3] = x + CW + GAP;
    x += group_w;
    
    colon_x[1] = x + GAP + COLON_W / 2;
    x += colon_gap;
    
    digit_x[4] = x;
    digit_x[5] = x + CW + GAP;
}

static void init_time() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        for (int i = 0; i < FLIP_N_DIGITS; i++) {
            digits[i].cur = 0;
            if (i == 0) digits[i].max_val = 2;
            else if (i == 2 || i == 4) digits[i].max_val = 5;
            else digits[i].max_val = 9;
            digits[i].old = 0;
            digits[i].anim_frame = -1;
        }
        return;
    }
    
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;
    
    digits[0] = {h / 10, 2, h / 10, -1};
    digits[1] = {h % 10, 9, h % 10, -1};
    digits[2] = {m / 10, 5, m / 10, -1};
    digits[3] = {m % 10, 9, m % 10, -1};
    digits[4] = {s / 10, 5, s / 10, -1};
    digits[5] = {s % 10, 9, s % 10, -1};
}

static void sync_time() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;
    
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;
    
    int new_digits[FLIP_N_DIGITS] = {
        h / 10,
        h % 10,
        m / 10,
        m % 10,
        s / 10,
        s % 10
    };
    
    for (int i = 0; i < FLIP_N_DIGITS; i++) {
        if (new_digits[i] != digits[i].cur) {
            digits[i].old = digits[i].cur;
            digits[i].cur = new_digits[i];
            digits[i].anim_frame = 0;
        }
    }
}

static void render_trapezoid(int cx, const uint16_t *src, const StripEntry *strips) {
    for (int i = 0; i < N_STRIPS; i++) {
        int dy_top = HALF + strips[i].dy_top;
        int dy_bot = HALF + strips[i].dy_bot;
        int dst_h = dy_bot - dy_top;
        
        if (dst_h < 1) continue;
        
        int ext = strips[i].ext;
        
        int src_row = i;
        if (src_row < 0) src_row = 0;
        if (src_row >= HALF) src_row = HALF - 1;
        
        uint16_t line[CW];
        for (int x = 0; x < CW; x++) {
            line[x] = src[src_row * CW + x];
        }
        
        for (int y = 0; y < dst_h; y++) {
            if (dy_top + y < 0 || dy_top + y >= CH) continue;
            
            _sprite->pushImage(cx, dy_top + y, CW, 1, line);
            
            for (int e = 0; e < ext; e++) {
                _sprite->drawPixel(cx + CW + e, dy_top + y, BG_COLOR);
            }
        }
    }
}

static void render_card(int idx) {
    Digit &d = digits[idx];
    int cx = digit_x[idx] - _sprite_x;
    int scr_cx = digit_x[idx];
    int scr_cy = _sprite_y;
    
    const uint16_t* upper_buf = DIGIT_UPPER[d.cur];
    const uint16_t* lower_buf = DIGIT_LOWER[d.cur];
    const uint16_t* old_upper_buf = DIGIT_UPPER[d.old];
    const uint16_t* old_lower_buf = DIGIT_LOWER[d.old];
    
    if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
        if (_sprite_ok) {
            _sprite->pushImage(cx, 0, CW, HALF, upper_buf);
            _sprite->pushImage(cx, HALF, CW, HALF, lower_buf);
        } else {
            tft.pushImage(scr_cx, scr_cy, CW, HALF, upper_buf);
            tft.pushImage(scr_cx, scr_cy + HALF, CW, HALF, lower_buf);
        }
        
        if (d.anim_frame >= TOTAL_FRAMES) {
            d.anim_frame = -1;
        }
        return;
    }
    
    if (_sprite_ok) {
        _sprite->pushImage(cx, 0, CW, HALF, upper_buf);
        _sprite->pushImage(cx, HALF, CW, HALF, old_lower_buf);
        
        if (d.anim_frame < ANIM_HALF) {
            render_trapezoid(cx, old_upper_buf, flip_table[d.anim_frame].upper);
        } else {
            render_trapezoid(cx, lower_buf, flip_table[TOTAL_FRAMES - 1 - d.anim_frame].lower);
        }
    } else {
        tft.pushImage(scr_cx, scr_cy, CW, HALF, upper_buf);
        tft.pushImage(scr_cx, scr_cy + HALF, CW, HALF, old_lower_buf);
    }
}

static void render_colon(int idx) {
    int cx = colon_x[idx] - _sprite_x;
    int scr_cx = colon_x[idx];
    int scr_cy1 = _sprite_y + CH * 3 / 10;
    int scr_cy2 = _sprite_y + CH * 7 / 10;
    int y1 = CH * 3 / 10;
    int y2 = CH * 7 / 10;
    
    if (_sprite_ok) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx * dx + dy * dy > 5) continue;
                
                if (y1 + dy >= 0 && y1 + dy < CH) {
                    _sprite->drawPixel(cx + dx, y1 + dy, COLON_CLR);
                }
                if (y2 + dy >= 0 && y2 + dy < CH) {
                    _sprite->drawPixel(cx + dx, y2 + dy, COLON_CLR);
                }
            }
        }
    } else {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx * dx + dy * dy > 5) continue;
                
                if (scr_cy1 + dy >= 0 && scr_cy1 + dy < 128) {
                    tft.drawPixel(scr_cx + dx, scr_cy1 + dy, COLON_CLR);
                }
                if (scr_cy2 + dy >= 0 && scr_cy2 + dy < 128) {
                    tft.drawPixel(scr_cx + dx, scr_cy2 + dy, COLON_CLR);
                }
            }
        }
    }
}

static void render_frame() {
    if (!_sprite_ok) return;
    
    _sprite->fillSprite(BG_COLOR);
    
    for (int i = 0; i < FLIP_N_DIGITS; i++) {
        render_card(i);
    }
    
    for (int i = 0; i < FLIP_N_COLONS; i++) {
        render_colon(i);
    }
}

void initFlipClockPage() {
    calc_layout();
    build_flip_table();
    init_time();
    
    int sprite_w = digit_x[FLIP_N_DIGITS - 1] + CW - digit_x[0];
    int sprite_h = CH;
    _sprite_x = digit_x[0];
    _sprite_y = (128 - CH) / 2;
    
    if (!_sprite) {
        _sprite = new lgfx::LGFX_Sprite(&tft);
    }
    
    _sprite_ok = _sprite->createSprite(sprite_w, sprite_h);
    
    if (_sprite_ok) {
        _sprite->setSwapBytes(true);
    }
    
    tft.fillScreen(BG_COLOR);
    
    if (_sprite_ok) {
        render_frame();
        _sprite->pushSprite(_sprite_x, _sprite_y);
    } else {
        tft.setSwapBytes(true);
        for (int i = 0; i < FLIP_N_DIGITS; i++) {
            render_card(i);
        }
        for (int i = 0; i < FLIP_N_COLONS; i++) {
            render_colon(i);
        }
    }
}

void drawFlipClockPage() {
    tft.fillScreen(BG_COLOR);
    
    init_time();
    
    if (_sprite_ok) {
        render_frame();
        _sprite->pushSprite(_sprite_x, _sprite_y);
    } else {
        for (int i = 0; i < FLIP_N_DIGITS; i++) {
            render_card(i);
        }
        for (int i = 0; i < FLIP_N_COLONS; i++) {
            render_colon(i);
        }
    }
}

void updateFlipClockPage() {
    sync_time();
}

void renderFlipClockAnimation() {
    static uint32_t _next_second = 0;
    static uint32_t _next_render = 0;
    uint32_t now = millis();
    
    if (_next_second == 0) {
        _next_second = now + 1000;
    }
    if ((int32_t)(now - _next_second) >= 0) {
        _next_second += 1000;
        sync_time();
    }
    
    if ((int32_t)(now - _next_render) >= 0) {
        _next_render = now + 1000 / FPS;
        
        if (_sprite_ok) {
            render_frame();
            _sprite->pushSprite(_sprite_x, _sprite_y);
        } else {
            tft.fillScreen(BG_COLOR);
            for (int i = 0; i < FLIP_N_DIGITS; i++) {
                render_card(i);
            }
            for (int i = 0; i < FLIP_N_COLONS; i++) {
                render_colon(i);
            }
        }
        
        for (int i = 0; i < FLIP_N_DIGITS; i++) {
            if (digits[i].anim_frame >= 0 && digits[i].anim_frame < TOTAL_FRAMES) {
                digits[i].anim_frame++;
            } else if (digits[i].anim_frame >= TOTAL_FRAMES) {
                digits[i].anim_frame = -1;
            }
        }
    }
}

#define WIDGET_DIGIT_W  17
#define WIDGET_DIGIT_H  28
#define WIDGET_HALF_H   14
#define WIDGET_GAP      2
#define WIDGET_COLON_W  4
#define WIDGET_N_DIGITS 6
#define WIDGET_N_COLONS 2

static Digit widget_digits[WIDGET_N_DIGITS];
static int16_t widget_digit_x[WIDGET_N_DIGITS];
static int16_t widget_colon_x[WIDGET_N_COLONS];
static lgfx::LGFX_Sprite* _widget_sprite = nullptr;
static bool _widget_sprite_ok = false;
static int16_t _widget_sprite_x = 0;
static int16_t _widget_sprite_y = 0;

static const int WCW = WIDGET_DIGIT_W;
static const int WCH = WIDGET_DIGIT_H;
static const int WHALF = WIDGET_HALF_H;
static const int WGAP = WIDGET_GAP;
static const int WCOLON_W = WIDGET_COLON_W;

static void widget_calc_layout(int x, int y, int w, int h) {
    int group_w = WCW * 2 + WGAP;
    int colon_gap = WGAP + WCOLON_W + WGAP;
    int total_w = group_w * 3 + colon_gap * 2;
    
    int start_x = x + (w - total_w) / 2;
    int start_y = y + (h - WCH) / 2;
    
    widget_digit_x[0] = start_x;
    widget_digit_x[1] = start_x + WCW + WGAP;
    
    int cx = start_x + group_w;
    widget_colon_x[0] = cx + WGAP + WCOLON_W / 2;
    
    cx += colon_gap;
    widget_digit_x[2] = cx;
    widget_digit_x[3] = cx + WCW + WGAP;
    
    cx += group_w;
    widget_colon_x[1] = cx + WGAP + WCOLON_W / 2;
    
    cx += colon_gap;
    widget_digit_x[4] = cx;
    widget_digit_x[5] = cx + WCW + WGAP;
    
    _widget_sprite_x = start_x;
    _widget_sprite_y = start_y;
}

static void widget_render_card(int idx) {
    Digit &d = widget_digits[idx];
    int cx = widget_digit_x[idx] - _widget_sprite_x;
    int scr_cx = widget_digit_x[idx];
    int scr_cy = _widget_sprite_y;
    
    const uint16_t* upper_buf = DIGIT_UPPER[d.cur];
    const uint16_t* lower_buf = DIGIT_LOWER[d.cur];
    const uint16_t* old_upper_buf = DIGIT_UPPER[d.old];
    const uint16_t* old_lower_buf = DIGIT_LOWER[d.old];
    
    if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
        if (_widget_sprite_ok) {
            _widget_sprite->pushImage(cx, 0, WCW, WHALF, upper_buf);
            _widget_sprite->pushImage(cx, WHALF, WCW, WHALF, lower_buf);
        } else {
            tft.pushImage(scr_cx, scr_cy, WCW, WHALF, upper_buf);
            tft.pushImage(scr_cx, scr_cy + WHALF, WCW, WHALF, lower_buf);
        }
        
        if (d.anim_frame >= TOTAL_FRAMES) {
            d.anim_frame = -1;
        }
        return;
    }
    
    if (_widget_sprite_ok) {
        _widget_sprite->pushImage(cx, 0, WCW, WHALF, upper_buf);
        _widget_sprite->pushImage(cx, WHALF, WCW, WHALF, old_lower_buf);
        
        if (d.anim_frame < ANIM_HALF) {
            for (int i = 0; i < N_STRIPS; i++) {
                int dy_top = WHALF + flip_table[d.anim_frame].upper[i].dy_top;
                int dy_bot = WHALF + flip_table[d.anim_frame].upper[i].dy_bot;
                int dst_h = dy_bot - dy_top;
                if (dst_h < 1) continue;
                
                int ext = flip_table[d.anim_frame].upper[i].ext;
                int src_row = i;
                if (src_row < 0) src_row = 0;
                if (src_row >= WHALF) src_row = WHALF - 1;
                
                uint16_t line[WCW];
                for (int x = 0; x < WCW; x++) {
                    line[x] = old_upper_buf[src_row * WCW + x];
                }
                
                for (int y = 0; y < dst_h; y++) {
                    if (dy_top + y < 0 || dy_top + y >= WCH) continue;
                    _widget_sprite->pushImage(cx, dy_top + y, WCW, 1, line);
                }
            }
        } else {
            for (int i = 0; i < N_STRIPS; i++) {
                int dy_top = WHALF + flip_table[TOTAL_FRAMES - 1 - d.anim_frame].lower[i].dy_top;
                int dy_bot = WHALF + flip_table[TOTAL_FRAMES - 1 - d.anim_frame].lower[i].dy_bot;
                int dst_h = dy_bot - dy_top;
                if (dst_h < 1) continue;
                
                int src_row = i;
                if (src_row < 0) src_row = 0;
                if (src_row >= WHALF) src_row = WHALF - 1;
                
                uint16_t line[WCW];
                for (int x = 0; x < WCW; x++) {
                    line[x] = lower_buf[src_row * WCW + x];
                }
                
                for (int y = 0; y < dst_h; y++) {
                    if (dy_top + y < 0 || dy_top + y >= WCH) continue;
                    _widget_sprite->pushImage(cx, dy_top + y, WCW, 1, line);
                }
            }
        }
    }
}

static void widget_render_colon(int idx) {
    int cx = widget_colon_x[idx] - _widget_sprite_x;
    int y1 = WCH * 3 / 10;
    int y2 = WCH * 7 / 10;
    
    if (_widget_sprite_ok) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (y1 + dy >= 0 && y1 + dy < WCH) {
                    _widget_sprite->drawPixel(cx + dx, y1 + dy, COLON_CLR);
                }
                if (y2 + dy >= 0 && y2 + dy < WCH) {
                    _widget_sprite->drawPixel(cx + dx, y2 + dy, COLON_CLR);
                }
            }
        }
    }
}

static void widget_render_frame() {
    if (!_widget_sprite_ok) return;
    
    _widget_sprite->fillSprite(BG_COLOR);
    
    for (int i = 0; i < WIDGET_N_DIGITS; i++) {
        widget_render_card(i);
    }
    
    for (int i = 0; i < WIDGET_N_COLONS; i++) {
        widget_render_colon(i);
    }
}

void initFlipClockWidget(int x, int y, int w, int h) {
    widget_calc_layout(x, y, w, h);
    
    int sprite_w = widget_digit_x[WIDGET_N_DIGITS - 1] + WCW - widget_digit_x[0];
    int sprite_h = WCH;
    
    if (!_widget_sprite) {
        _widget_sprite = new lgfx::LGFX_Sprite(&tft);
    }
    
    _widget_sprite_ok = _widget_sprite->createSprite(sprite_w, sprite_h);
    
    if (_widget_sprite_ok) {
        _widget_sprite->setSwapBytes(true);
    }
    
    for (int i = 0; i < WIDGET_N_DIGITS; i++) {
        widget_digits[i].cur = 0;
        widget_digits[i].old = 0;
        widget_digits[i].anim_frame = -1;
    }
}

void drawFlipClockWidget(int h, int m, int s) {
    widget_digits[0] = {h / 10, 2, h / 10, -1};
    widget_digits[1] = {h % 10, 9, h % 10, -1};
    widget_digits[2] = {m / 10, 5, m / 10, -1};
    widget_digits[3] = {m % 10, 9, m % 10, -1};
    widget_digits[4] = {s / 10, 5, s / 10, -1};
    widget_digits[5] = {s % 10, 9, s % 10, -1};
    
    if (_widget_sprite_ok) {
        widget_render_frame();
        _widget_sprite->pushSprite(_widget_sprite_x, _widget_sprite_y);
    }
}

void updateFlipClockWidget(int h, int m, int s) {
    int new_digits[WIDGET_N_DIGITS] = {h / 10, h % 10, m / 10, m % 10, s / 10, s % 10};
    
    for (int i = 0; i < WIDGET_N_DIGITS; i++) {
        if (new_digits[i] != widget_digits[i].cur) {
            widget_digits[i].old = widget_digits[i].cur;
            widget_digits[i].cur = new_digits[i];
            widget_digits[i].anim_frame = 0;
        }
    }
}

void renderFlipClockWidgetAnimation() {
    static uint32_t _next_render = 0;
    uint32_t now = millis();
    
    if ((int32_t)(now - _next_render) >= 0) {
        _next_render = now + 1000 / FPS;
        
        if (_widget_sprite_ok) {
            widget_render_frame();
            _widget_sprite->pushSprite(_widget_sprite_x, _widget_sprite_y);
        }
        
        for (int i = 0; i < WIDGET_N_DIGITS; i++) {
            if (widget_digits[i].anim_frame >= 0 && widget_digits[i].anim_frame < TOTAL_FRAMES) {
                widget_digits[i].anim_frame++;
            } else if (widget_digits[i].anim_frame >= TOTAL_FRAMES) {
                widget_digits[i].anim_frame = -1;
            }
        }
    }
}