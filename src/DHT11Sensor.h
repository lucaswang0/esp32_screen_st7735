#ifndef DHT11_SENSOR_H
#define DHT11_SENSOR_H

#include <Arduino.h>

class DHT11Sensor {
public:
    DHT11Sensor(int pin);
    void begin();
    void update();
    float getTemperature() const;
    float getHumidity() const;
    bool isValid() const;

private:
    int _pin;
    float _temperature;
    float _humidity;
    bool _valid;
    unsigned long _lastUpdate;
    const unsigned long _updateInterval;

    bool readSensor();
};

#endif
