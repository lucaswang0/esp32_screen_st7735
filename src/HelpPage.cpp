#include "HelpPage.h"
#include "Log.h"
#include "Theme.h"

#define WHITE    currentThemeColors().mainText
#define ACCENT   currentThemeColors().accent
#define GRID     currentThemeColors().grid

// 帮助内容（header=true 的条目作为小标题，用强调色）
struct HelpLine {
    const char* text;
    bool        header;
};

static const HelpLine kHelp[] = {
    { "按键:",                  true  },
    { "单击-切换页面",          false },
    { "双击-自动息屏开关",      false },
    { "长按-关闭屏幕",          false },
    { "息屏单击-唤醒",          false },
    { "功能:",                  true  },
    { "时钟/图表/系统/帮助",    false },
    { "WiFi/MQTT/网页服务",     false },
    { "夜间21-6点自动息屏",     false },
};

// ============================================================================
// 逐字符（UTF-8 感知）自动换行绘制，返回下一行 y
// ============================================================================
static int drawWrappedText(const char* text, int x, int y,
                           int maxW, int lineH, uint16_t color, int maxY) {
    tft.setTextDatum(top_left);
    tft.setTextColor(color);

    char line[64];
    int li = 0;
    const char* p = text;

    while (*p) {
        unsigned char c = (unsigned char)*p;
        int n = 1;
        if (c >= 0xF0)      n = 4;
        else if (c >= 0xE0) n = 3;
        else if (c >= 0xC0) n = 2;

        // 缓冲区不够，先输出当前行
        if (li + n >= (int)sizeof(line) - 1) {
            line[li] = '\0';
            if (y + lineH <= maxY) tft.drawString(line, x, y);
            y += lineH;
            li = 0;
        }

        memcpy(&line[li], p, n);
        li += n;
        line[li] = '\0';

        // 超宽则回退当前字符，换行重排
        if (tft.textWidth(line) > maxW && li > n) {
            li -= n;
            line[li] = '\0';
            if (y + lineH <= maxY) tft.drawString(line, x, y);
            y += lineH;
            memcpy(&line[0], p, n);
            li = n;
            line[li] = '\0';
        }
        p += n;
    }
    if (li > 0) {
        line[li] = '\0';
        if (y + lineH <= maxY) tft.drawString(line, x, y);
        y += lineH;
    }
    tft.setTextDatum(top_left);
    return y;
}

// ============================================================================
// 对外接口
// ============================================================================
void initHelpPage() {
    LOG_LN("[HelpPage] 初始化完成");
}

void drawHelpPage() {
    tft.setFont(&lgfx::fonts::efontCN_12);

    // 标题
    tft.setTextDatum(top_left);
    tft.setTextColor(WHITE);
    tft.drawString("帮助", 0, 0);
    tft.drawLine(0, 13, 127, 13, GRID);

    // 内容（自动换行）
    int y = 15;
    int count = sizeof(kHelp) / sizeof(kHelp[0]);
    for (int i = 0; i < count; i++) {
        int x = kHelp[i].header ? 0 : 6;
        y = drawWrappedText(kHelp[i].text, x, y, 127 - x, 12,
                            kHelp[i].header ? ACCENT : WHITE, 127);
    }
    LOG_LN("[HelpPage] 绘制完成");
}

void updateHelpPage() {
    // 静态页面，无需刷新
}
