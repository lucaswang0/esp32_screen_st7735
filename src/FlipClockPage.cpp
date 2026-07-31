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
    bool changed;  // 标记该位是否有变化
};

static const int N_STRIPS = FLIP_N_STRIPS;
static const int FPS = FLIP_FPS;
static const int TOTAL_FRAMES = FLIP_TOTAL_FRAMES;
static const int ANIM_HALF = FLIP_ANIM_HALF;

static const uint16_t BG_COLOR = FLIP_BG_COLOR;
static const uint16_t COLON_CLR = FLIP_COLON_CLR;

static void build_flip_table() {
    const int HALF = FLIP_HALF_H;
    const int CW = FLIP_DIGIT_W;

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

// 缓存当前显示的完整帧（用于增量更新）
static uint16_t* _frame_buffer = nullptr;
static int _frame_width = 0;
static int _frame_height = 0;

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

// ========== 优化：只渲染单个数字 ==========
static void widget_render_single_digit(int idx, bool force_full) {
    Digit &d = widget_digits[idx];
    int cx = widget_digit_x[idx] - _widget_sprite_x;
    int scr_cx = widget_digit_x[idx];
    int scr_cy = _widget_sprite_y;

    // 如果没有变化且不是强制刷新，跳过
    if (!d.changed && !force_full && d.anim_frame < 0) {
        return;
    }

    const uint16_t* upper_buf = DIGIT_UPPER[d.cur];
    const uint16_t* lower_buf = DIGIT_LOWER[d.cur];
    const uint16_t* old_upper_buf = DIGIT_UPPER[d.old];
    const uint16_t* old_lower_buf = DIGIT_LOWER[d.old];

    if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES || force_full) {
        // 直接绘制完整数字（无动画）
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
        d.changed = false;
        return;
    }

    // 动画渲染（只渲染变化的部分）
    if (_widget_sprite_ok) {
        // 先用背景色清除该数字区域（优化：只清除变化的区域）
        _widget_sprite->fillRect(cx, 0, WCW, WCH, BG_COLOR);
        
        // 绘制当前数字的上半部分
        _widget_sprite->pushImage(cx, 0, WCW, WHALF, upper_buf);
        // 绘制旧数字的下半部分（作为动画底图）
        _widget_sprite->pushImage(cx, WHALF, WCW, WHALF, old_lower_buf);

        if (d.anim_frame < ANIM_HALF) {
            // 翻页动画前半段：旧数字向上翻
            for (int i = 0; i < N_STRIPS; i++) {
                int dy_top = WHALF + flip_table[d.anim_frame].upper[i].dy_top;
                int dy_bot = WHALF + flip_table[d.anim_frame].upper[i].dy_bot;
                int dst_h = dy_bot - dy_top;
                if (dst_h < 1) continue;

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
            // 翻页动画后半段：新数字从下翻入
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

// ========== 优化：只渲染变化的数字 ==========
static void widget_render_frame(bool force_full = false) {
    if (!_widget_sprite_ok) return;

    if (force_full) {
        // 强制全屏重绘
        _widget_sprite->fillSprite(BG_COLOR);
        for (int i = 0; i < WIDGET_N_DIGITS; i++) {
            widget_digits[i].changed = true;
            widget_render_single_digit(i, true);
        }
        for (int i = 0; i < WIDGET_N_COLONS; i++) {
            widget_render_colon(i);
        }
    } else {
        // 增量更新：只渲染有变化的数字
        bool any_changed = false;
        for (int i = 0; i < WIDGET_N_DIGITS; i++) {
            if (widget_digits[i].changed || widget_digits[i].anim_frame >= 0) {
                widget_render_single_digit(i, false);
                any_changed = true;
            }
        }
        
        // 如果没有任何变化，不需要推送到屏幕
        if (!any_changed) {
            return;
        }
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
        _frame_width = sprite_w;
        _frame_height = sprite_h;
    }

    build_flip_table();

    for (int i = 0; i < WIDGET_N_DIGITS; i++) {
        widget_digits[i].cur = 0;
        widget_digits[i].old = 0;
        widget_digits[i].anim_frame = -1;
        widget_digits[i].changed = true;  // 初始标记为需要绘制
    }
}

void drawFlipClockWidget(int h, int m, int s) {
    // 设置所有数字为强制刷新
    widget_digits[0] = {h / 10, 2, h / 10, -1, true};
    widget_digits[1] = {h % 10, 9, h % 10, -1, true};
    widget_digits[2] = {m / 10, 5, m / 10, -1, true};
    widget_digits[3] = {m % 10, 9, m % 10, -1, true};
    widget_digits[4] = {s / 10, 5, s / 10, -1, true};
    widget_digits[5] = {s % 10, 9, s % 10, -1, true};

    if (_widget_sprite_ok) {
        widget_render_frame(true);
        _widget_sprite->pushSprite(_widget_sprite_x, _widget_sprite_y);
    }
}

void updateFlipClockWidget(int h, int m, int s) {
    int new_digits[WIDGET_N_DIGITS] = {h / 10, h % 10, m / 10, m % 10, s / 10, s % 10};
    bool any_change = false;

    for (int i = 0; i < WIDGET_N_DIGITS; i++) {
        if (new_digits[i] != widget_digits[i].cur) {
            widget_digits[i].old = widget_digits[i].cur;
            widget_digits[i].cur = new_digits[i];
            widget_digits[i].anim_frame = 0;
            widget_digits[i].changed = true;
            any_change = true;
        }
    }

    // 如果有变化，立即渲染一帧（不等待定时器）
    if (any_change && _widget_sprite_ok) {
        widget_render_frame(false);
        _widget_sprite->pushSprite(_widget_sprite_x, _widget_sprite_y);
    }
}

void renderFlipClockWidgetAnimation() {
    static uint32_t _next_render = 0;
    uint32_t now = millis();

    if ((int32_t)(now - _next_render) >= 0) {
        _next_render = now + 1000 / FPS;

        bool has_animation = false;
        
        // 检查是否有数字在动画中
        for (int i = 0; i < WIDGET_N_DIGITS; i++) {
            if (widget_digits[i].anim_frame >= 0 && widget_digits[i].anim_frame < TOTAL_FRAMES) {
                has_animation = true;
                widget_digits[i].anim_frame++;
                widget_digits[i].changed = true;
            } else if (widget_digits[i].anim_frame >= TOTAL_FRAMES) {
                widget_digits[i].anim_frame = -1;
                widget_digits[i].changed = false;
            }
        }

        // 只有在有动画时才渲染
        if (has_animation && _widget_sprite_ok) {
            widget_render_frame(false);
            _widget_sprite->pushSprite(_widget_sprite_x, _widget_sprite_y);
        }
    }
}

// ========== 新增：只更新秒数（优化秒闪烁） ==========
void updateFlipClockSeconds(int s) {
    int sec_digits[2] = {s / 10, s % 10};
    bool any_change = false;
    
    // 只检查秒的两位（索引4和5）
    for (int i = 4; i < WIDGET_N_DIGITS; i++) {
        int idx = i - 4;
        if (sec_digits[idx] != widget_digits[i].cur) {
            widget_digits[i].old = widget_digits[i].cur;
            widget_digits[i].cur = sec_digits[idx];
            widget_digits[i].anim_frame = 0;
            widget_digits[i].changed = true;
            any_change = true;
        }
    }
    
    // 如果有变化，立即渲染
    if (any_change && _widget_sprite_ok) {
        widget_render_frame(false);
        _widget_sprite->pushSprite(_widget_sprite_x, _widget_sprite_y);
    }
}