#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 src/digitals.h.bak（原始深灰版）生成 day/night 双主题位图，
写入 src/digitals.h：
  DIGIT_UPPER_DAY / DIGIT_LOWER_DAY     白天：浅暖底 + 深暖字
  DIGIT_UPPER_NIGHT / DIGIT_LOWER_NIGHT 夜间：深暖底 + 暖亮字
按像素灰度亮度在 [新背景, 新前景] 间插值，保留 7 段字形与抗锯齿。

用法: python scripts/recolor_digitals.py
"""
import re

BAK = 'src/digitals.h.bak'
OUT = 'src/digitals.h'

OLD_BG = 0x18C3            # 原背景 #333
OLD_FG = 0xCE79            # 原数字亮白

DAY_BG = (245, 238, 225)   # 浅暖米白（与 Theme 白天 bg 一致）
DAY_FG = (58, 42, 33)      # 深暖棕（与 Theme 白天 mainText 一致）
NIGHT_BG = (30, 22, 18)    # 深棕褐（与 Theme 夜间 bg 一致）
NIGHT_FG = (230, 200, 140) # 暖米黄（与 Theme 夜间 mainText 一致）


def rgb565_to_rgb888(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


L_BG = lum(rgb565_to_rgb888(OLD_BG))
L_FG = lum(rgb565_to_rgb888(OLD_FG))


def make_mapping(vals, new_bg, new_fg):
    m = {}
    for v in set(vals):
        L = lum(rgb565_to_rgb888(v))
        t = (L - L_BG) / (L_FG - L_BG) if L_FG != L_BG else 0.0
        t = max(0.0, min(1.0, t))   # 比背景更暗的归入背景
        m[v] = rgb888_to_rgb565(*lerp(new_bg, new_fg, t))
    return m


def remap_def(def_text, mapping, suffix):
    def_text = re.sub(r'0x[0-9A-Fa-f]{4}',
                      lambda m: "0x{:04X}".format(mapping[int(m.group(0), 16)]),
                      def_text)
    def_text = def_text.replace('DIGIT_UPPER', 'DIGIT_UPPER_' + suffix)
    def_text = def_text.replace('DIGIT_LOWER', 'DIGIT_LOWER_' + suffix)
    return def_text


def main():
    with open(BAK, 'r', encoding='utf-8') as f:
        bak = f.read()

    vals = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]{4}', bak)]
    day_map = make_mapping(vals, DAY_BG, DAY_FG)
    night_map = make_mapping(vals, NIGHT_BG, NIGHT_FG)

    upper_def = re.search(r'(const uint16_t DIGIT_UPPER\[10\]\[238\] = \{.*?\};)',
                          bak, re.S).group(1)
    lower_def = re.search(r'(const uint16_t DIGIT_LOWER\[10\]\[238\] = \{.*?\};)',
                          bak, re.S).group(1)

    day_upper = remap_def(upper_def, day_map, 'DAY')
    day_lower = remap_def(lower_def, day_map, 'DAY')
    night_upper = remap_def(upper_def, night_map, 'NIGHT')
    night_lower = remap_def(lower_def, night_map, 'NIGHT')

    out = """// 17x28 digits, upper/lower halves, RGB565, dual theme (day/night)
// Auto-generated from digitals.h.bak by scripts/recolor_digitals.py
#ifndef __DIGITALS_H__
#define __DIGITALS_H__

#include <stdint.h>

// ========== Day theme: warm-light BG (0x%04X) + dark-warm digits (0x%04X) ==========
%s
%s

// ========== Night theme: dark-warm BG (0x%04X) + warm-light digits (0x%04X) ==========
%s
%s

#endif
""" % (rgb888_to_rgb565(*DAY_BG), rgb888_to_rgb565(*DAY_FG),
       day_upper, day_lower,
       rgb888_to_rgb565(*NIGHT_BG), rgb888_to_rgb565(*NIGHT_FG),
       night_upper, night_lower)

    with open(OUT, 'w', encoding='utf-8') as f:
        f.write(out)

    print("day:   BG 0x%04X  FG 0x%04X  (%d 唯一色)" %
          (rgb888_to_rgb565(*DAY_BG), rgb888_to_rgb565(*DAY_FG), len(day_map)))
    print("night: BG 0x%04X  FG 0x%04X  (%d 唯一色)" %
          (rgb888_to_rgb565(*NIGHT_BG), rgb888_to_rgb565(*NIGHT_FG), len(night_map)))
    print("已写入 %s" % OUT)


if __name__ == '__main__':
    main()
