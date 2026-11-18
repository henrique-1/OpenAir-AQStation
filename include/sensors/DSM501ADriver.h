#ifndef DSM501A_DRIVER_H
#define DSM501A_DRIVER_H

#include <Arduino.h>

class DSM501ADriver {
public:
    explicit DSM501ADriver(uint8_t signalPin);

    void begin();
    void update();

    bool isValid() const { return _valid; }
    float getPoeira() const { return _poeira; }
    float getRatio() const { return _lastRatio; }

    // Chamado internamente pela rotina de interrupção (ISR)
    void handleInterrupt();

    // Constantes de conversão física (Roy et al., 2017)
    // Fator volumétrico de conversão de 0.01 cf (283 ml) para 1 m³ (1 / 0.000283168 m³)
    static constexpr float VOLUMETRIC_FACTOR = 3531.5f;

    // Massa estimada de uma partícula PM2.5 esférica em µg
    // r = 0.44 µm, densidade = 1.65 g/cm³ (1.65e12 µg/m³), V = (4/3)*pi*r³ = 3.5682e-19 m³
    // Massa = densidade * volume ≈ 5.8875e-7 µg
    static constexpr float PARTICLE_MASS_UG  = 5.8875e-7f;

private:
    uint8_t       _signalPin;
    float         _poeira;       // Concentração PM2.5 em µg/m³
    float         _lastRatio;    // Porcentagem LPO (0 a 100%)
    bool          _valid;

    unsigned long _bootTimeMs;
    unsigned long _windowStartMs;
    
    volatile uint32_t _pulseStartUs;
    volatile uint32_t _accumulatedLowUs;

    static constexpr unsigned long WARMUP_TIME_MS     = 60000; // 1 minuto de pré-aquecimento do heater
    static constexpr unsigned long SAMPLE_INTERVAL_MS = 30000; // Janela de 30s para cálculo do LPO
};

#endif // DSM501A_DRIVER_H
