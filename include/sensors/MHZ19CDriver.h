#ifndef MHZ19C_DRIVER_H
#define MHZ19C_DRIVER_H

#include <Arduino.h>
#include <HardwareSerial.h>

// Classificação de Qualidade do Ar Interior (QAI) conforme norma ANVISA RE nº 09/2003
enum IndoorVentilationStatus {
    VENTILATION_EXCELLENT = 0,   // <= 700 ppm: Ar Excelente / Alta Renovação
    VENTILATION_ACCEPTABLE,      // 701 - 1000 ppm: Ar Aceitável (Limite ANVISA)
    VENTILATION_INADEQUATE,      // 1001 - 1500 ppm: Ventilação Inadequada (Atenção / Risco de sonolência)
    VENTILATION_CRITICAL         // > 1500 ppm: Crítico / Alerta de Ventilação (Abertura de portas/janelas)
};

class MHZ19CDriver {
public:
    MHZ19CDriver(uint8_t rxPin, uint8_t txPin);

    void begin();
    void update();

    bool isValid() const { return _valid; }
    int getCO2() const { return _co2; }

    // Razão de Saturação de Ventilação CO2 (%): (C_CO2 / 1000 ppm) * 100 (ANVISA RE nº 09/2003)
    float getSaturacaoVentilacao() const;

    // Enquadramento de ventilação indoor adaptado segundo ANVISA RE nº 09/2003
    IndoorVentilationStatus getVentilationStatus() const;
    const char* getVentilationStatusDescription() const;

    // Valor Máximo Recomendável (VMR) de CO2 para ambientes internos pela ANVISA
    static constexpr float ANVISA_CO2_VMR_PPM = 1000.0f;

private:
    uint8_t         _rxPin;
    uint8_t         _txPin;
    HardwareSerial  _serial;
    int             _co2;
    bool            _valid;
    unsigned long   _lastPollMs;
    unsigned long   _reqSentMs;
    bool            _awaitingReply;

    static constexpr unsigned long POLL_INTERVAL_MS = 5000;
    static constexpr unsigned long REPLY_TIMEOUT_MS = 500;

    void sendReadCommand();
    bool parseResponse();
    uint8_t calculateChecksum(const uint8_t *packet);
};

#endif // MHZ19C_DRIVER_H
