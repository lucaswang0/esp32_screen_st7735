#include "SensorHistory.h"
#include "Log.h"
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
    LOG_LN("[SensorHistory] 数据已重置");
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
                LOG_T("[SensorHistory] 删除 %d 天前的旧文件: %s", i, oldFilename);
            } else {
                LOG_T("[SensorHistory] 删除失败: %s", oldFilename);
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
        LOG_T("[SensorHistory] 日期变化: %d -> %d", _lastDayOfYear, currentDay);

        char oldFilename[32];
        getFilenameForDay(oldFilename, sizeof(oldFilename), -1);
        LOG_T("[SensorHistory] 保存前一天数据到 %s", oldFilename);
        
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
        LOG_T("[SensorHistory] 保存失败: %s", filename);
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
    LOG_T("[SensorHistory] 已保存 %d/%d 条记录到 %s", savedCount, countToSave, filename);
}

bool SensorHistory::loadFromFile() {
    char filename[32];
    getCurrentFilename(filename, sizeof(filename));
    return loadFromFile(filename);
}

bool SensorHistory::loadFromFile(const char* filename) {
    fs::File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        LOG_T("[SensorHistory] 文件不存在: %s", filename);
        return false;
    }
    
    // 读取记录数量
    int fileCount = 0;
    if (file.available() >= (int)sizeof(fileCount)) {
        file.read((uint8_t*)&fileCount, sizeof(fileCount));
    }
    
    if (fileCount <= 0 || fileCount > MAX_SAMPLES) {
        LOG_T("[SensorHistory] 文件记录数异常: %d", fileCount);
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
    
    LOG_T("[SensorHistory] 从 %s 加载 %d 条记录", filename, loadedCount);
    return loadedCount > 0;
}

int SensorHistory::readByDate(const char* date, SensorSample* outBuffer, int maxSamples) {
    char filename[32];

    if (date && date[0]) {
        // 用 date 字符串拼出完整文件名（不校验日期合法性）
        snprintf(filename, sizeof(filename), "/%s_%s.dat", _filenamePrefix, date);
    } else {
        // 当天：用当前时间
        getCurrentFilename(filename, sizeof(filename));
    }

    if (!SPIFFS.exists(filename)) {
        return 0;
    }

    fs::File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        return 0;
    }

    int fileCount = 0;
    if (file.available() >= (int)sizeof(fileCount)) {
        file.read((uint8_t*)&fileCount, sizeof(fileCount));
    }
    if (fileCount <= 0 || fileCount > MAX_SAMPLES) {
        file.close();
        return 0;
    }

    int loaded = 0;
    for (int i = 0; i < fileCount && loaded < maxSamples; i++) {
        SensorSample s;
        if (file.read((uint8_t*)&s, sizeof(s)) != sizeof(s)) break;
        outBuffer[loaded++] = s;
    }
    file.close();
    return loaded;
}