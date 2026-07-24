#ifndef LOG_H
#define LOG_H

#include <Arduino.h>

// ============================================================================
// 全局日志宏 —— 自动加 [HH:MM:SS.mmm] 时间戳前缀
// ============================================================================
//
// 用法：
//   LOG_T("[MQTT] 状态切换: %s -> %s", from, to);  // printf 风格
//   LOG_LN("[MQTT] Wi-Fi 断开");                    // println 风格
//
// 时间格式：上电后运行时间（uptime），不受 NTP/系统时间影响
//   限制：millis() 在 ~49.7 天后回卷，% 24 后小时数会归零但不影响时序
//
// 选择 uptime 而非 RTC 时间的原因：
//   - 设备启动早期 NTP 未同步，RTC 是 1970 年
//   - 调试时更容易计算"启动后多久发生"的事件间隔
// ============================================================================

// 上电运行时间（HH:MM:SS.mmm）
#define LOG_TIME_FMT  "[%02lu:%02lu:%02lu.%03lu] "
#define LOG_TIME_VAL \
    (unsigned long)((millis() / 3600000UL) % 24UL), \
    (unsigned long)((millis() / 60000UL) % 60UL), \
    (unsigned long)((millis() / 1000UL) % 60UL), \
    (unsigned long)(millis() % 1000UL)

// printf 风格
#define LOG_T(fmt, ...)  Serial.printf(LOG_TIME_FMT fmt "\n", LOG_TIME_VAL, ##__VA_ARGS__)

// println 风格（简单字符串）
#define LOG_LN(msg)      Serial.printf(LOG_TIME_FMT "%s\n", LOG_TIME_VAL, msg)

#endif
