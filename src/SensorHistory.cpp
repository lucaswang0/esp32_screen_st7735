#include "SensorHistory.h"
#include <time.h>

SensorHistory::SensorHistory(const char* filenamePrefix) 
    : _head(0), _count(0), _lastDayOfYear(-1), _filenamePrefix(filenamePrefix) {
    for (int i = 0; i < MAX_SAMPLES; i++) {
        _samples[i].hour = 0;
        _samples[i].minute = 0;
        _samples[i].temp = 0.0f;
        _samples[i].humidity = 0.0f;
    }
}

void SensorHistory::addSample(uint8_t hour, uint8_t minute, float temp, float humidity) {
    _samples[_head].hour = hour;
    _samples[_head].minute = minute;
    _samples[_head].temp = temp;
    _samples[_head].humidity = humidity;
    
    _head = (_head + 1) % MAX_SAMPLES;
    if (_count < MAX_SAMPLES) {
        _count++;
    }
}

const SensorSample& SensorHistory::getSample(int index) const {
    if (index < 0 || index >= _count) {
        static SensorSample empty = {0, 0, 0.0f, 0.0f};
        return empty;
    }
    // 环形缓冲区读取：从最早的数据开始
    int startIdx = (_head - _count + MAX_SAMPLES) % MAX_SAMPLES;
    int idx = (startIdx + index) % MAX_SAMPLES;
    return _samples[idx];
}

int SensorHistory::getCount() const {
    return _count;
}

void SensorHistory::reset() {
    _head = 0;
    _count = 0;
    Serial.println("[SensorHistory] 数据已重置");
}

// 获取一年中的第几天
int SensorHistory::getDayOfYear(uint8_t hour, uint8_t minute) {
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    return timeinfo.tm_yday;  // tm_yday: 0-365
}

// 检查是否跨天（新的一天）
void SensorHistory::checkDayChange(uint8_t currentHour, uint8_t currentMinute) {
    int currentDay = getDayOfYear(currentHour, currentMinute);
    
    // 如果是第一次调用，记录当前日期
    if (_lastDayOfYear == -1) {
        _lastDayOfYear = currentDay;
        return;
    }
    
    // 如果日期变了，说明是新的一天
    if (currentDay != _lastDayOfYear) {
        Serial.printf("[SensorHistory] 日期变化: %d -> %d, 重置数据\n", _lastDayOfYear, currentDay);
        reset();
        _lastDayOfYear = currentDay;
    }
}

// 保留旧接口兼容性
void SensorHistory::checkMidnightReset(uint8_t currentHour, uint8_t currentMinute) {
    checkDayChange(currentHour, currentMinute);
}

float SensorHistory::getMinTemp() const {
    if (_count == 0) return 0.0f;
    float min = 999.0f;
    for (int i = 0; i < _count; i++) {
        const SensorSample& s = getSample(i);
        if (s.temp < min) {
            min = s.temp;
        }
    }
    return min;
}

float SensorHistory::getMaxTemp() const {
    if (_count == 0) return 0.0f;
    float max = -999.0f;
    for (int i = 0; i < _count; i++) {
        const SensorSample& s = getSample(i);
        if (s.temp > max) {
            max = s.temp;
        }
    }
    return max;
}

float SensorHistory::getMinHumidity() const {
    if (_count == 0) return 0.0f;
    float min = 999.0f;
    for (int i = 0; i < _count; i++) {
        const SensorSample& s = getSample(i);
        if (s.humidity < min) {
            min = s.humidity;
        }
    }
    return min;
}

float SensorHistory::getMaxHumidity() const {
    if (_count == 0) return 0.0f;
    float max = -999.0f;
    for (int i = 0; i < _count; i++) {
        const SensorSample& s = getSample(i);
        if (s.humidity > max) {
            max = s.humidity;
        }
    }
    return max;
}

void SensorHistory::getCurrentFilename(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    snprintf(buffer, size, "/%s_%04d-%02d-%02d.dat", 
             _filenamePrefix, 
             tm_info->tm_year + 1900, 
             tm_info->tm_mon + 1, 
             tm_info->tm_mday);
}

void SensorHistory::saveToFile() {
    char filename[32];
    getCurrentFilename(filename, sizeof(filename));
    saveToFile(filename);
}

void SensorHistory::saveToFile(const char* filename) {
    fs::File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
        Serial.printf("[SensorHistory] 保存失败: %s\n", filename);
        return;
    }
    
    // 保存数据头：记录数量
    int countToSave = _count;
    file.write((const uint8_t*)&countToSave, sizeof(countToSave));
    
    // 保存每条数据
    int savedCount = 0;
    for (int i = 0; i < countToSave; i++) {
        const SensorSample& s = getSample(i);
        if (file.write((const uint8_t*)&s, sizeof(SensorSample)) == sizeof(SensorSample)) {
            savedCount++;
        }
    }
    
    file.close();
    Serial.printf("[SensorHistory] 已保存 %d/%d 条记录到 %s\n", savedCount, countToSave, filename);
}

bool SensorHistory::loadFromFile() {
    char filename[32];
    getCurrentFilename(filename, sizeof(filename));
    return loadFromFile(filename);
}

bool SensorHistory::loadFromFile(const char* filename) {
    reset();
    
    fs::File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        Serial.printf("[SensorHistory] 文件不存在: %s\n", filename);
        return false;
    }
    
    // 读取记录数量
    int fileCount = 0;
    if (file.available() >= (int)sizeof(fileCount)) {
        file.read((uint8_t*)&fileCount, sizeof(fileCount));
    }
    
    if (fileCount <= 0 || fileCount > MAX_SAMPLES) {
        Serial.printf("[SensorHistory] 文件记录数异常: %d\n", fileCount);
        file.close();
        return false;
    }
    
    // 读取每条数据
    int loadedCount = 0;
    for (int i = 0; i < fileCount; i++) {
        SensorSample s;
        if (file.read((uint8_t*)&s, sizeof(s)) != sizeof(s)) {
            break;
        }
        addSample(s.hour, s.minute, s.temp, s.humidity);
        loadedCount++;
    }
    
    file.close();
    
    // 更新 _lastDayOfYear
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    _lastDayOfYear = timeinfo.tm_yday;
    
    Serial.printf("[SensorHistory] 从 %s 加载 %d 条记录\n", filename, loadedCount);
    return loadedCount > 0;
}