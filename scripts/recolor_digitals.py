#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
对 src/digitals.h 做颜色重映射:
  原: 暗背景(#333 / 0x18C3) + 亮白数字(0xCE79) + 抗锯齿过渡
  新: 浅暖背景 + 深暖数字 + 过渡
保留 7 段字形与抗锯齿——按像素灰度亮度在 [新背景, 新前景] 间插值。

用法:
  python scripts/recolor_digitals.py            # 预览映射表，不写文件
  python scripts/recolor_digitals.py --apply    # 写入 src/digitals.h（备份 .bak）
"""
import re
import sys
import shutil
from collections import Counter

SRC = 'src/digitals.h'

# ---- 新配色（浅暖背景 + 深暖数字）----
NEW_BG = (245, 238, 225)   # 浅暖米白（页面背景同色）
NEW_FG = (58, 42, 33)      # 深暖棕（数字）

# ---- 原位图已知端点 ----
OLD_BG = 0x18C3            # 背景 #333
OLD_FG = 0xCE79            # 数字亮白


def rgb565_to_rgb888(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def lum(rgb):
    r, g, b = rgb
    return 0.299 * r + 0.587 * g + 0.114 * b


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def main():
    apply = '--apply' in sys.argv
    with open(SRC, 'r', encoding='utf-8') as f:
        content = f.read()

    vals = re.findall(r'0x[0-9A-Fa-f]{4}', content)
    cnt = Counter(int(v, 16) for v in vals)

    L_BG = lum(rgb565_to_rgb888(OLD_BG))
    L_FG = lum(rgb565_to_rgb888(OLD_FG))

    mapping = {}
    for v in cnt:
        L = lum(rgb565_to_rgb888(v))
        t = (L - L_BG) / (L_FG - L_BG) if L_FG != L_BG else 0.0
        t = max(0.0, min(1.0, t))           # 比背景更暗的归入背景(t=0)
        mapping[v] = rgb888_to_rgb565(*lerp(NEW_BG, NEW_FG, t))

    print(f"总像素: {len(vals)}  唯一色: {len(cnt)}")
    print(f"新背景 NEW_BG={NEW_BG} -> 0x{rgb888_to_rgb565(*NEW_BG):04X}")
    print(f"新数字 NEW_FG={NEW_FG} -> 0x{rgb888_to_rgb565(*NEW_FG):04X}")
    print(f"{'原色':>8} {'原RGB888':>16} {'亮度':>6} {'t':>5}  {'新色':>8} {'新RGB888':>16}  {'数量':>6}")
    for v, c in cnt.most_common():
        L = lum(rgb565_to_rgb888(v))
        t = max(0.0, min(1.0, (L - L_BG) / (L_FG - L_BG))) if L_FG != L_BG else 0.0
        print(f"0x{v:04X} {str(rgb565_to_rgb888(v)):>16} {L:6.1f} {t:5.2f}  "
              f"0x{mapping[v]:04X} {str(rgb565_to_rgb888(mapping[v])):>16}  {c:>6}")

    if not apply:
        print("\n(预览模式，未写文件。加 --apply 写入 src/digitals.h)")
        return

    def repl(m):
        return "0x{:04X}".format(mapping[int(m.group(0), 16)])

    new_content = re.sub(r'0x[0-9A-Fa-f]{4}', repl, content)
    shutil.copy(SRC, SRC + '.bak')
    with open(SRC, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print(f"\n已写入 {SRC}（备份 {SRC}.bak）")


if __name__ == '__main__':
    main()
