#ifndef DHT22_DRIVER_H
#define DHT22_DRIVER_H

#include <Arduino.h>
#include <DHT.h>

class DHT22Driver {
public:
    explicit DHT22Driver(uint8_t pin);

    void begin();
    void update();

    bool isValid() const { return _valid; }
    float getTemperature() const { return _temperature; }
    float getHumidity() const { return _humidity; }

private:
    uint8_t       _pin;
    DHT           _dht;
    float         _temperature;
    float         _humidity;
    bool          _valid;
    unsigned long _lastReadMs;

    static constexpr unsigned long MIN_INTERVAL_MS = 2500; // DHT22 requer >= 2s
};

#endif // DHT22_DRIVER_H
