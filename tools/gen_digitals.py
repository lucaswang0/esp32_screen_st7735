#!/usr/bin/env python3
import re

ORIG_W = 32
ORIG_H = 24
NEW_W = 17
NEW_H = 14

def parse_digitals_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    upper_match = re.search(r'const uint16_t DIGIT_UPPER\[10\]\[\d+\]\s*=\s*\{(.*?)\};', content, re.DOTALL)
    lower_match = re.search(r'const uint16_t DIGIT_LOWER\[10\]\[\d+\]\s*=\s*\{(.*?)\};', content, re.DOTALL)
    
    def parse_digit_array(match):
        digits = []
        digit_blocks = re.findall(r'/\*\s*(\d+)\s*\*/\s*((?:0x[0-9A-Fa-f]+,\s*)*)', match.group(1))
        
        for idx, block in digit_blocks:
            values = re.findall(r'0x[0-9A-Fa-f]+', block)
            pixels = [int(v, 16) for v in values]
            expected_size = ORIG_W * ORIG_H
            if len(pixels) == expected_size:
                digits.append(pixels)
        return digits
    
    return parse_digit_array(upper_match), parse_digit_array(lower_match)

def downscale_image(src, src_w, src_h, dst_w, dst_h):
    dst = []
    for dy in range(dst_h):
        src_y = (dy * src_h) // dst_h
        for dx in range(dst_w):
            src_x = (dx * src_w) // dst_w
            dst.append(src[src_y * src_w + src_x])
    return dst

def format_digit_data(digits, name, w, h):
    result = f'const uint16_t {name}[10][{w * h}] = {{\n'
    for i, digit in enumerate(digits):
        result += f'    /* {i} */\n'
        for y in range(h):
            line_start = y * w
            line_end = line_start + w
            line_values = digit[line_start:line_end]
            hex_values = [f'0x{v:04X}' for v in line_values]
            result += '    ' + ', '.join(hex_values) + ',\n'
        result += '\n'
    result += '};\n'
    return result

def main():
    input_file = r'C:\Users\user\Documents\PlatformIO\Projects\screen_st7735\tools\digitals_original.h'
    output_file = r'C:\Users\user\Documents\PlatformIO\Projects\screen_st7735\src\digitals.h'
    
    upper, lower = parse_digitals_file(input_file)
    
    print(f'Parsed {len(upper)} upper digits, {len(lower)} lower digits')
    
    new_upper = [downscale_image(d, ORIG_W, ORIG_H, NEW_W, NEW_H) for d in upper]
    new_lower = [downscale_image(d, ORIG_W, ORIG_H, NEW_W, NEW_H) for d in lower]
    
    with open(output_file, 'w') as f:
        f.write(f'// {NEW_W}x{NEW_H*2} digits, upper/lower halves, RGB565, pre-composited with BG #333\n')
        f.write('#ifndef __DIGITALS_H__\n')
        f.write('#define __DIGITALS_H__\n')
        f.write('\n')
        f.write('#include <stdint.h>\n')
        f.write('\n')
        f.write(format_digit_data(new_upper, 'DIGIT_UPPER', NEW_W, NEW_H))
        f.write('\n')
        f.write(format_digit_data(new_lower, 'DIGIT_LOWER', NEW_W, NEW_H))
        f.write('\n')
        f.write('#endif\n')
    
    print(f'Generated {output_file}')
    print(f'Original: {ORIG_W}x{ORIG_H} per half')
    print(f'New: {NEW_W}x{NEW_H} per half')

if __name__ == '__main__':
    main()