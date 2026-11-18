#include "sensors/DHT22Driver.h"

DHT22Driver::DHT22Driver(uint8_t pin)
    : _pin(pin),
      _dht(pin, DHT22),
      _temperature(0.0f),
      _humidity(0.0f),
      _valid(false),
      _lastReadMs(0) {}

void DHT22Driver::begin() {
    _dht.begin();
    _lastReadMs = millis();
}

void DHT22Driver::update() {
    unsigned long now = millis();
    if (now - _lastReadMs < MIN_INTERVAL_MS) {
        return;
    }
    _lastReadMs = now;

    float t = _dht.readTemperature();
    float h = _dht.readHumidity();

    if (!isnan(t) && !isnan(h) && t >= -50.0f && t <= 100.0f && h >= 0.0f && h <= 100.0f) {
        _temperature = t;
        _humidity = h;
        _valid = true;
    } else {
        _valid = false;
        Serial.println("[DHT22] Aviso: Falha na leitura ou valor fora da faixa aceitavel.");
    }
}
