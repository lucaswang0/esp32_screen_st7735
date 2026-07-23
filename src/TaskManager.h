#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>

// ============================================================================
// FreeRTOS 任务参数
// ============================================================================
#define TASK_STACK_UI          16384  // 16KB - 渲染 + Sprite
#define TASK_STACK_SENSOR      4096   // 4KB - DHT11 临界区
#define TASK_STACK_WIFI        8192   // 8KB - WiFi scan / saveCredentials
#define TASK_STACK_NTP         4096   // 4KB
#define TASK_STACK_SAMPLE      6144   // 6KB - SPIFFS I/O
#define TASK_STACK_MQTT        6144   // 6KB
#define TASK_STACK_WEB         8192   // 8KB - HTTP 处理
#define TASK_STACK_BACKLIGHT   2048   // 2KB

// 任务优先级（数字越大优先级越高）
#define PRIO_UI          3
#define PRIO_SENSOR      2
#define PRIO_WIFI        1
#define PRIO_NTP         1
#define PRIO_SAMPLE      1
#define PRIO_MQTT        1
#define PRIO_WEB         1
#define PRIO_BACKLIGHT   0

// 任务周期
#define SENSOR_PERIOD_MS      5000
#define WIFI_PERIOD_MS        100
#define NTP_RETRY_INTERVAL_MS 300000   // 失败后 5 分钟重试
#define SAMPLE_PERIOD_MS      600000   // 10 分钟
#define MQTT_PERIOD_MS        50
#define WEB_PERIOD_MS         10
#define BACKLIGHT_PERIOD_MS   30000

// ============================================================================
// 公共 API
// ============================================================================

// 创建所有 FreeRTOS 任务（在 setup() 末尾调用）
// 注意：调用前必须先 initSharedState() 并完成所有硬件初始化
void startTasks();

#endif
