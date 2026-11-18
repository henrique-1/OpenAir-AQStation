#include "SensorManager.h"

SensorManager::SensorManager()
    : _dht(PIN_DHT22),
      _mhz(PIN_MHZ19C_RX, PIN_MHZ19C_TX),
      _dsm(PIN_DSM501A_SIGNAL) {}

void SensorManager::begin() {
    Serial.println("[SENSORES] Inicializando barramentos e sensores...");
    _dht.begin();
    _mhz.begin();
    _dsm.begin();
    Serial.println("[SENSORES] DHT22, MH-Z19C e DSM501A inicializados.");
}

void SensorManager::update() {
    _dht.update();
    _mhz.update();
    _dsm.update();
}

bool SensorManager::hasValidReadings() const {
    return (_dht.isValid() && _mhz.isValid() && _dsm.isValid());
}

bool SensorManager::getSensorData(SensorData &data) {
    data.dhtValido = _dht.isValid();
    data.mhzValido = _mhz.isValid();
    data.dsmValido = _dsm.isValid();

    data.temperatura = _dht.getTemperature();
    data.umidade     = _dht.getHumidity();
    data.co2         = _mhz.getCO2();
    data.poeira      = _dsm.getPoeira();
    data.timestamp   = millis();

    // Retorna true se houver dados prontos para envio
    return (data.dhtValido || data.mhzValido || data.dsmValido);
}
