#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "WiFiManager.h"
#include "TimeManager.h"
#include "WeatherManager.h"
#include "DisplayManager.h"

#include "AHT20BMP230Sensor.h"
#include "LEDController.h"
#include "TTP223Sensor.h"
#include "PageBase.h"
#include "PageManager.h"
#include "TempPage.h"
#include "CalendarPage.h"
#include "ForecastPage.h"
#include "PressurePage.h"
#include "HistoryPage.h"

// ==================== 全局对象 ====================

WiFiManager wifiManager;
TimeManager timeManager(wifiManager);
WeatherManager weatherManager(wifiManager);
DisplayManager displayManager;
AHT20BMP230Sensor aht20Bmp230Sensor(PIN_I2C_SDA, PIN_I2C_SCL);
LEDController ledController(PIN_LED_D4);
TTP223Sensor touchSensor(PIN_TOUCH);

// ==================== 页面对象 ====================

TempPage     tempPage(displayManager, weatherManager, aht20Bmp230Sensor, wifiManager);
CalendarPage calendarPage(displayManager, timeManager);
ForecastPage forecastPage(displayManager, weatherManager);
PressurePage pressurePage(displayManager, aht20Bmp230Sensor);
HistoryPage  historyPage(displayManager, aht20Bmp230Sensor);
PageManager  pageManager(tempPage, calendarPage, forecastPage, pressurePage, historyPage, displayManager);

// ==================== 背光 ====================

const int BACKLIGHT_CHANNEL = 0;
const int BACKLIGHT_LEVELS[] = {10, 80, 160, 255};
int currentBacklightLevel = 2;

// ==================== 时间戳 ====================

volatile unsigned long lastWiFiCheck = 0;
volatile unsigned long lastTimeSync = 0;
volatile unsigned long lastWeatherUpdate = 0;
volatile unsigned long lastTempRead = 0;
volatile unsigned long lastLEDConditionCheck = 0;
volatile unsigned long lastAutoBrightnessCheck = 0;
volatile unsigned long lastHistorySave = 0;

volatile int weatherStep = 0;
int lastAutoBrightnessDay = -1;
SemaphoreHandle_t displayMutex = NULL;

// ==================== FreeRTOS 时间任务声明 ====================
void TaskTimeDisplay(void *pvParameters);

// ==================== 背光与触摸辅助函数 ====================

static void setBacklightLevel(int level) {
    // 先分离，再重新附加，避免 LEDC 通道卡住
    ledcDetachPin(PIN_TFT_BL);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
    ledcWrite(BACKLIGHT_CHANNEL, level);
}

static int parseTimeToMinutes(const String& timeStr) {
    if (timeStr.length() < 5) return -1;
    int colon = timeStr.indexOf(':');
    if (colon <= 0) return -1;
    int hour = timeStr.substring(0, colon).toInt();
    int minute = timeStr.substring(colon + 1).toInt();
    return hour * 60 + minute;
}

static int calculateAutoBrightness() {
    const DailyForecast& today = weatherManager.getForecast(0);
    int sunrise = parseTimeToMinutes(today.sunrise);
    int sunset = parseTimeToMinutes(today.sunset);
    
    if (sunrise < 0 || sunset < 0) {
        Serial.println("[Brightness] 日出日落数据不可用");
        return -1;
    }
    
    int nowMinutes = timeManager.getHour() * 60 + timeManager.getMinute();
    int sunriseMinus30 = sunrise - 30;
    int sunsetPlus30 = sunset + 30;
    
    Serial.printf("[Brightness] 日出:%d 日落:%d 当前:%d\n", sunrise, sunset, nowMinutes);
    
    if (nowMinutes >= sunriseMinus30 && nowMinutes < sunset) {
        return 2;
    } else if (nowMinutes >= sunset && nowMinutes < sunsetPlus30) {
        return 1;
    } else {
        return 0;
    }
}

static void handleTouchEvent(TouchType type) {
    switch (type) {
        case TOUCH_SHORT: {
            Serial.println("[Touch] Short touch - next page");
            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                pageManager.next();
                xSemaphoreGive(displayMutex);
            }
            pageManager.dispatchTouch(TOUCH_SHORT_BASE);
            break;
        }
        case TOUCH_LONG:
            Serial.println("[Touch] Long touch - WiFi reconnect");
            WiFi.disconnect(true);
            delay(100);
            wifiManager.connect();
            break;
        case TOUCH_DOUBLE: {
            currentBacklightLevel = (currentBacklightLevel + 1) % 4;
            int level = BACKLIGHT_LEVELS[currentBacklightLevel];
            setBacklightLevel(level);
            Serial.printf("[Touch] Double click - brightness: %d\n", level);
            break;
        }
        default:
            break;
    }
}

// ==================== setup ====================

void setup() {
    Serial.begin(115200);
    delay(3000);

    // 1. 先挂载 SPIFFS（关键：放在屏幕初始化之前）
    if (!SPIFFS.begin(true)) {
        Serial.println("[SPIFFS] Mount Failed - Formatting...");
        if (!SPIFFS.begin(true)) {
            Serial.println("[SPIFFS] Format Failed");
        } else {
            Serial.println("[SPIFFS] Format Success");
        }
    } else {
        Serial.println("[SPIFFS] Mount Success");
    }

    // 屏幕背光（使用 LEDC PWM 控制，不要用 digitalWrite）
    ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
    ledcWrite(BACKLIGHT_CHANNEL, BACKLIGHT_LEVELS[currentBacklightLevel]);

    // 2. 再初始化屏幕，此时 SPI 总线会被屏幕重新配置
    displayManager.init();

    // 屏幕 init 后重新附加背光 PWM（TFT_eSPI 会重置 GPIO 状态）
    ledcDetachPin(PIN_TFT_BL);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
    ledcWrite(BACKLIGHT_CHANNEL, BACKLIGHT_LEVELS[currentBacklightLevel]);
    Serial.printf("[Backlight] 重新附加背光 PWM, 等级: %d\n", currentBacklightLevel);

    // AHT20+BMP230
    if (!aht20Bmp230Sensor.begin()) {
        Serial.println("[Main] AHT20+BMP230 传感器初始化失败");
    } else {
        Serial.println("[Main] AHT20+BMP230 传感器初始化成功");
        delay(100);
        aht20Bmp230Sensor.update();
    }


    // LED
    ledController.begin();

    // 触摸
    touchSensor.begin();
    Serial.println("[Main] TTP223 touch sensor initialized");

    // 互斥锁
    displayMutex = xSemaphoreCreateMutex();

    Serial.println("\n================================================");
    Serial.println("   ESP32-C3 Weather Clock (Page-based)");
    Serial.println("================================================\n");

    wifiManager.connect();
    timeManager.update();

    if (wifiManager.isConnected()) {
        timeManager.sync();
        lastWeatherUpdate = millis();
    } else {
        Serial.println("[Main] WiFi连接失败，AP模式保持活跃");
    }

    // 启动页面管理器
    pageManager.begin();

    // 时间显示任务
    xTaskCreatePinnedToCore(
        TaskTimeDisplay,
        "TimeDisplay",
        8192,
        NULL,
        2,
        NULL,
        0
    );
}

// ==================== 时间显示任务（仅温度页面需要） ====================

void TaskTimeDisplay(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);

    for (;;) {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            timeManager.update();
            if (pageManager.current() == PageManager::PAGE_TEMP) {
                displayManager.displayTime(
                    timeManager.getYear(),
                    timeManager.getMonth(),
                    timeManager.getDay(),
                    timeManager.getHour(),
                    timeManager.getMinute(),
                    timeManager.getSecond(),
                    timeManager.getWeekday()
                );
            }
            xSemaphoreGive(displayMutex);
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ==================== loop ====================

void loop() {
    unsigned long now = millis();

    // -------- 触摸 --------
    touchSensor.update();
    if (touchSensor.hasNewTouch()) {
        handleTouchEvent(touchSensor.getLastTouchType());
        touchSensor.clearTouchEvent();
    }

    // -------- WiFi --------
    if (now - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
        lastWiFiCheck = now;
        if (wifiManager.isAPStarted()) {
            wifiManager.handleClient();
            wifiManager.maintainConnection();
            if (wifiManager.isConnected()) {
                Serial.println("[Main] WiFi connected via config portal!");
                timeManager.sync();
                lastTimeSync = now;
                lastWeatherUpdate = now;
            }
        } else if (wifiManager.isConnected()) {
            wifiManager.maintainConnection();
        } else {
            Serial.println("[Main] WiFi not connected, attempting connection...");
            if (wifiManager.connect()) {
                Serial.println("[Main] WiFi connection successful!");
                timeManager.sync();
                lastTimeSync = now;
                lastWeatherUpdate = now;
            }
        }
    }

    wifiManager.checkAPTimeout();

    // -------- NTP 时间同步 --------
    if (now - lastTimeSync >= TIME_SYNC_INTERVAL) {
        lastTimeSync = now;
        if (wifiManager.isConnected()) {
            timeManager.sync();
        }
    }

    // -------- 天气拉取 --------
    bool weatherDataEmpty = (weatherManager.getCity().length() == 0 ||
                            weatherManager.getTemperature().length() == 0 ||
                            weatherManager.getWeatherText().length() == 0);
    if (weatherDataEmpty || weatherStep != 0) {
        if (wifiManager.isConnected()) {
            switch (weatherStep) {
                case 0:
                    Serial.println("[Main] Step 1/3: 获取城市信息");
                    if (weatherManager.fetchCityInfo()) weatherStep = 1;
                    break;
                case 1:
                    Serial.println("[Main] Step 2/3: 获取当前天气");
                    if (weatherManager.fetchCurrentWeather()) weatherStep = 2;
                    break;
                case 2:
                    Serial.println("[Main] Step 3/3: 获取天气预报");
                    if (weatherManager.fetch3DayForecast()) {
                        weatherStep = 0;
                        lastWeatherUpdate = now;
                        Serial.println("[Main] 天气数据获取完成！");
                    }
                    break;
            }
        }
    } else if (now - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL) {
        lastWeatherUpdate = now;
        weatherStep = 1;
    }

    // -------- AHT20+BMP230 --------
    if (now - lastTempRead >= 5000) {
        lastTempRead = now;
        aht20Bmp230Sensor.update();
    }


    // -------- 定时保存传感器数据到SPIFFS（每10分钟） --------
    if (now - lastHistorySave >= 600000) {
        lastHistorySave = now;
        if (aht20Bmp230Sensor.isValid()) {
            historyPage.addRecord(
                aht20Bmp230Sensor.getTemperature(),
                aht20Bmp230Sensor.getHumidity(),
                aht20Bmp230Sensor.getPressure()
            );
            Serial.printf("[History] 保存传感器数据: 温度=%.1f°C 湿度=%.1f%% 气压=%.1fhPa\n",
                aht20Bmp230Sensor.getTemperature(),
                aht20Bmp230Sensor.getHumidity(),
                aht20Bmp230Sensor.getPressure());

            // 保存后立即检查气压预警
            if (pressurePage.checkAlert()) {
                Serial.println("[Alert] 气压警告触发，自动切换到气压页面！");
                pageManager.switchTo(PageManager::PAGE_PRESSURE);
            }
        }
    }

    // -------- 显示当前页面 --------
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        pageManager.update();
        xSemaphoreGive(displayMutex); 
    }  

    // -------- LED 状态 --------
    if (now - lastLEDConditionCheck >= 500) {
        lastLEDConditionCheck = now;
        if (!wifiManager.isConnected()) {
            ledController.setState(LED_STATE_BLINK_FAST);
        } else if (weatherManager.getTemperature().length() == 0) {
            ledController.setState(LED_STATE_BLINK_ONCE);
        } else {
            ledController.setState(LED_STATE_OFF);
        }
    }

    // -------- 智能亮度（每天只调整一次） --------
    if (now - lastAutoBrightnessCheck >= 60000) {
        lastAutoBrightnessCheck = now;
        int today = timeManager.getDay();
        if (today != lastAutoBrightnessDay && wifiManager.isConnected()) {
            int targetLevel = calculateAutoBrightness();
            if (targetLevel >= 0 && targetLevel != currentBacklightLevel) {
                currentBacklightLevel = targetLevel;
                setBacklightLevel(BACKLIGHT_LEVELS[currentBacklightLevel]);
                Serial.printf("[Brightness] Auto: %d -> %d\n", 
                    currentBacklightLevel, BACKLIGHT_LEVELS[currentBacklightLevel]);
            }
            lastAutoBrightnessDay = today;
        }
    }
    ledController.update();

    displayManager.debugPrintScreenVariables();
    delay(10);
}
