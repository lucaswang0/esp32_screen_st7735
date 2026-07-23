#include "DHT11Sensor.h"
#include <freertos/FreeRTOS.h>

DHT11Sensor::DHT11Sensor(int pin) 
    : _pin(pin), _temperature(0), _humidity(0), _valid(false), 
      _lastUpdate(0), _updateInterval(4000) {  // 内部间隔4秒，外部调用5秒，确保稳定触发
}

void DHT11Sensor::begin() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, HIGH);
    delay(100);  // 上电稳定（仅执行一次，后续 readSensor() 中不再等待）
    Serial.printf("[DHT11] 初始化完成 | 引脚: GPIO%d | 更新间隔: %dms\n", _pin, _updateInterval);
}

void DHT11Sensor::update() {
    unsigned long currentMillis = millis();
    if (currentMillis - _lastUpdate >= _updateInterval) {
        _lastUpdate = currentMillis;
        _valid = readSensor();
    }
}

float DHT11Sensor::getTemperature() const {
    return _temperature;
}

float DHT11Sensor::getHumidity() const {
    return _humidity;
}

bool DHT11Sensor::isValid() const {
    return _valid;
}

bool DHT11Sensor::readSensor() {
    const int MAX_RETRY = 3;  // 最多重试 3 次
    uint8_t data[5];
    bool readOk = false;

    for (int attempt = 1; attempt <= MAX_RETRY; attempt++) {
        memset(data, 0, sizeof(data));
        unsigned long startTime;
        bool thisAttemptOk = false;

        // 进入临界区：禁止中断打断 DHT11 时序采样
        portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&mux);

        // 拉高短等待 - 100ms 上电稳定已在 begin() 中完成
        pinMode(_pin, OUTPUT);
        digitalWrite(_pin, HIGH);
        startTime = micros();
        while (micros() - startTime < 1000) {}  // 1ms

        // 发送启动信号
        pinMode(_pin, OUTPUT);
        digitalWrite(_pin, LOW);
        startTime = micros();
        while (micros() - startTime < 18000) {}    // 18ms
        digitalWrite(_pin, HIGH);
        startTime = micros();
        while (micros() - startTime < 40) {}       // 40μs
        pinMode(_pin, INPUT_PULLUP);

        startTime = micros();
        while (digitalRead(_pin) == HIGH) {
            if (micros() - startTime > 300) {  // 放宽到 300μs，容忍 SPI/WiFi 中断
                Serial.printf("[DHT11] 等待低电平超时 | 引脚: GPIO%d\n", _pin);
                goto read_done;
            }
        }

        startTime = micros();
        while (digitalRead(_pin) == LOW) {
            if (micros() - startTime > 300) {
                Serial.printf("[DHT11] 等待高电平超时 | 引脚: GPIO%d\n", _pin);
                goto read_done;
            }
        }

        startTime = micros();
        while (digitalRead(_pin) == HIGH) {
            if (micros() - startTime > 300) {
                Serial.printf("[DHT11] 等待数据位超时 | 引脚: GPIO%d\n", _pin);
                goto read_done;
            }
        }

        for (int i = 0; i < 5; i++) {
            for (int j = 7; j >= 0; j--) {
                startTime = micros();
                while (digitalRead(_pin) == LOW) {
                    if (micros() - startTime > 300) {
                        Serial.printf("[DHT11] 数据位低电平超时 | 引脚: GPIO%d | 字节:%d 位:%d\n", _pin, i, j);
                        goto read_done;
                    }
                }

                unsigned long highStart = micros();

                startTime = micros();
                while (digitalRead(_pin) == HIGH) {
                    if (micros() - startTime > 300) {  // 放宽到 300μs
                        Serial.printf("[DHT11] 数据位高电平超时 | 引脚: GPIO%d | 字节:%d 位:%d\n", _pin, i, j);
                        goto read_done;
                    }
                }

                unsigned long highDuration = micros() - highStart;

                if (highDuration > 40) {
                    data[i] |= (1 << j);
                }
            }
        }

        thisAttemptOk = true;

        read_done:
        portEXIT_CRITICAL(&mux);
        pinMode(_pin, OUTPUT);
        digitalWrite(_pin, HIGH);

        if (!thisAttemptOk) {
            if (attempt < MAX_RETRY) {
                Serial.printf("[DHT11] 第 %d 次失败，重试 | 引脚: GPIO%d\n", attempt, _pin);
            }
            continue;
        }

        if ((data[0] + data[1] + data[2] + data[3]) == data[4]) {
            // 拒绝全零数据：5 字节全 0 时校验和碰巧通过（0+0+0+0=0），实际是通信异常
            if (data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 0) {
                Serial.printf("[DHT11] 全零数据 | 引脚: GPIO%d\n", _pin);
                if (attempt < MAX_RETRY) {
                    Serial.printf("[DHT11] 第 %d 次失败，重试 | 引脚: GPIO%d\n", attempt, _pin);
                }
                continue;
            }
            _humidity = data[0] + data[1] * 0.1;
            _temperature = data[2] + data[3] * 0.1;
            if (attempt > 1) {
                Serial.printf("[DHT11] 第 %d 次重试成功 | 引脚: GPIO%d | 温度: %.1f°C | 湿度: %.1f%% | 原始数据: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
                              attempt, _pin, _temperature, _humidity, data[0], data[1], data[2], data[3], data[4]);
            } else {
                Serial.printf("[DHT11] 读取成功 | 引脚: GPIO%d | 温度: %.1f°C | 湿度: %.1f%% | 原始数据: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
                              _pin, _temperature, _humidity, data[0], data[1], data[2], data[3], data[4]);
            }
            readOk = true;
            break;
        }

        Serial.printf("[DHT11] 校验失败 | 引脚: GPIO%d | 原始数据: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X | 校验和: 0x%02X (期望: 0x%02X)\n",
                      _pin, data[0], data[1], data[2], data[3], data[4], (data[0] + data[1] + data[2] + data[3]) & 0xFF);
        if (attempt < MAX_RETRY) {
            Serial.printf("[DHT11] 第 %d 次失败，重试 | 引脚: GPIO%d\n", attempt, _pin);
        }
    }

    if (!readOk) {
        Serial.printf("[DHT11] 连续 %d 次失败 | 引脚: GPIO%d\n", MAX_RETRY, _pin);
    }

    return readOk;
}
