#ifndef SENSOR_HISTORY_H
#define SENSOR_HISTORY_H

#include <Arduino.h>
#include <SPIFFS.h>

#define MAX_SAMPLES 288

struct SensorSample {
    uint8_t hour;
    uint8_t minute;
    float temp;
    float humidity;
};

class SensorHistory {
public:
    SensorHistory(const char* filenamePrefix);
    
    void addSample(uint8_t hour, uint8_t minute, float temp, float humidity);
    const SensorSample& getSample(int index) const;
    int getCount() const;
    void reset();
    void checkMidnightReset(uint8_t currentHour, uint8_t currentMinute);
    void checkDayChange(uint8_t currentHour, uint8_t currentMinute);
    
    float getMinTemp() const;
    float getMaxTemp() const;
    float getMinHumidity() const;
    float getMaxHumidity() const;
    
    void saveToFile();
    bool loadFromFile();
    void saveToFile(const char* filename);
    bool loadFromFile(const char* filename);
    
private:
    SensorSample _samples[MAX_SAMPLES];
    int _head;
    int _count;
    int _lastDayOfYear;
    const char* _filenamePrefix;
    static const int MAX_HISTORY_DAYS = 7;
    
    void getCurrentFilename(char* buffer, size_t size);
    void getFilenameForDay(char* buffer, size_t size, int dayOffset);
    int getDayOfYear(uint8_t hour, uint8_t minute);
    void cleanOldFiles();
};

#endif