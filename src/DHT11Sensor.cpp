#include "DHT11Sensor.h"
#include <freertos/FreeRTOS.h>

DHT11Sensor::DHT11Sensor(int pin) 
    : _pin(pin), _temperature(0), _humidity(0), _valid(false), 
      _lastUpdate(0), _updateInterval(4000) {  // 内部间隔4秒，外部调用5秒，确保稳定触发
}

void DHT11Sensor::begin() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, HIGH);
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
    uint8_t data[5] = {0};
    unsigned long startTime;

    // 进入临界区：禁止中断打断 DHT11 时序采样（约 5ms 阻塞，可接受）
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    // 先拉高等待稳定
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, HIGH);
    // 临界区内不能用 delay()，改用忙等
    startTime = micros();
    while (micros() - startTime < 100000) {}  // 100ms

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
            portEXIT_CRITICAL(&mux);
            Serial.printf("[DHT11] 等待低电平超时 | 引脚: GPIO%d\n", _pin);
            pinMode(_pin, OUTPUT);
            digitalWrite(_pin, HIGH);
            return false;  // 读取失败，但保留之前的数据
        }
    }

    startTime = micros();
    while (digitalRead(_pin) == LOW) {
        if (micros() - startTime > 300) {
            portEXIT_CRITICAL(&mux);
            Serial.printf("[DHT11] 等待高电平超时 | 引脚: GPIO%d\n", _pin);
            pinMode(_pin, OUTPUT);
            digitalWrite(_pin, HIGH);
            return false;
        }
    }

    startTime = micros();
    while (digitalRead(_pin) == HIGH) {
        if (micros() - startTime > 300) {
            portEXIT_CRITICAL(&mux);
            Serial.printf("[DHT11] 等待数据位超时 | 引脚: GPIO%d\n", _pin);
            pinMode(_pin, OUTPUT);
            digitalWrite(_pin, HIGH);
            return false;
        }
    }

    for (int i = 0; i < 5; i++) {
        for (int j = 7; j >= 0; j--) {
            startTime = micros();
            while (digitalRead(_pin) == LOW) {
                if (micros() - startTime > 300) {
                    portEXIT_CRITICAL(&mux);
                    Serial.printf("[DHT11] 数据位低电平超时 | 引脚: GPIO%d | 字节:%d 位:%d\n", _pin, i, j);
                    pinMode(_pin, OUTPUT);
                    digitalWrite(_pin, HIGH);
                    return false;
                }
            }

            unsigned long highStart = micros();

            startTime = micros();
            while (digitalRead(_pin) == HIGH) {
                if (micros() - startTime > 300) {  // 放宽到 300μs
                    portEXIT_CRITICAL(&mux);
                    Serial.printf("[DHT11] 数据位高电平超时 | 引脚: GPIO%d | 字节:%d 位:%d\n", _pin, i, j);
                    pinMode(_pin, OUTPUT);
                    digitalWrite(_pin, HIGH);
                    return false;
                }
            }

            unsigned long highDuration = micros() - highStart;

            if (highDuration > 40) {
                data[i] |= (1 << j);
            }
        }
    }

    portEXIT_CRITICAL(&mux);
    
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, HIGH);
    
    if ((data[0] + data[1] + data[2] + data[3]) == data[4]) {
        _humidity = data[0] + data[1] * 0.1;
        _temperature = data[2] + data[3] * 0.1;
        Serial.printf("[DHT11] 读取成功 | 引脚: GPIO%d | 温度: %.1f°C | 湿度: %.1f%% | 原始数据: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", 
                      _pin, _temperature, _humidity, data[0], data[1], data[2], data[3], data[4]);
        return true;
    } else {
        Serial.printf("[DHT11] 校验失败 | 引脚: GPIO%d | 原始数据: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X | 校验和: 0x%02X (期望: 0x%02X)\n", 
                      _pin, data[0], data[1], data[2], data[3], data[4], (data[0] + data[1] + data[2] + data[3]) & 0xFF);
        return false;  // 校验失败，保留之前的数据
    }
}
