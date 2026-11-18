#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include "Config.h"
#include "sensors/DHT22Driver.h"
#include "sensors/MHZ19CDriver.h"
#include "sensors/DSM501ADriver.h"

class SensorManager {
public:
    SensorManager();

    // Inicializa todos os sensores
    void begin();

    // Atualiza periodicamente todos os sensores de forma não-bloqueante
    void update();

    // Preenche a estrutura com a leitura mais recente validada
    bool getSensorData(SensorData &data);

    // Retorna se pelo menos as métricas essenciais estão válidas
    bool hasValidReadings() const;

    // Métodos de conveniência para análise de ventilação indoor (ANVISA RE nº 09/2003)
    float getCO2Saturacao() const { return _mhz.getSaturacaoVentilacao(); }
    IndoorVentilationStatus getVentilationStatus() const { return _mhz.getVentilationStatus(); }
    const char* getVentilationStatusDescription() const { return _mhz.getVentilationStatusDescription(); }

private:
    DHT22Driver   _dht;
    MHZ19CDriver  _mhz;
    DSM501ADriver _dsm;
};

#endif // SENSOR_MANAGER_H
