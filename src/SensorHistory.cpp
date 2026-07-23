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

void SensorHistory::getFilenameForDay(char* buffer, size_t size, int dayOffset) {
    time_t now = time(NULL);
    time_t targetTime = now + dayOffset * 24 * 3600;
    struct tm *tm_info = localtime(&targetTime);
    snprintf(buffer, size, "/%s_%04d-%02d-%02d.dat", 
             _filenamePrefix, 
             tm_info->tm_year + 1900, 
             tm_info->tm_mon + 1, 
             tm_info->tm_mday);
}

void SensorHistory::cleanOldFiles() {
    // 从 MAX_HISTORY_DAYS 天前向后逐个检查，超过保留天数的文件全部删除
    // 不使用 break：文件可能因为断电等原因被部分删除
    for (int i = MAX_HISTORY_DAYS; i <= 365; i++) {
        char oldFilename[32];
        getFilenameForDay(oldFilename, sizeof(oldFilename), -i);
        
        if (SPIFFS.exists(oldFilename)) {
            if (SPIFFS.remove(oldFilename)) {
                Serial.printf("[SensorHistory] 删除 %d 天前的旧文件: %s\n", i, oldFilename);
            } else {
                Serial.printf("[SensorHistory] 删除失败: %s\n", oldFilename);
            }
        }
    }
}

void SensorHistory::checkDayChange(uint8_t currentHour, uint8_t currentMinute) {
    int currentDay = getDayOfYear(currentHour, currentMinute);
    
    if (_lastDayOfYear == -1) {
        _lastDayOfYear = currentDay;
        return;
    }
    
    if (currentDay != _lastDayOfYear) {
        Serial.printf("[SensorHistory] 日期变化: %d -> %d\n", _lastDayOfYear, currentDay);
        
        char oldFilename[32];
        getFilenameForDay(oldFilename, sizeof(oldFilename), -1);
        Serial.printf("[SensorHistory] 保存前一天数据到 %s\n", oldFilename);
        
        if (_count > 0) {
            saveToFile(oldFilename);
        }
        
        cleanOldFiles();
        
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
    
    // 先清空再加载，避免与已有数据叠加
    _head = 0;
    _count = 0;
    
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