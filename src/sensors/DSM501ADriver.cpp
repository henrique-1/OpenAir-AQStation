#include "sensors/DSM501ADriver.h"
#include <cmath>

static DSM501ADriver* s_dsmInstance = nullptr;

static void IRAM_ATTR isrDsmSignal() {
    if (s_dsmInstance) {
        s_dsmInstance->handleInterrupt();
    }
}

DSM501ADriver::DSM501ADriver(uint8_t signalPin)
    : _signalPin(signalPin),
      _poeira(0.0f),
      _lastRatio(0.0f),
      _valid(false),
      _bootTimeMs(0),
      _windowStartMs(0),
      _pulseStartUs(0),
      _accumulatedLowUs(0) {}

void DSM501ADriver::begin() {
    s_dsmInstance = this;
    pinMode(_signalPin, INPUT);

    _bootTimeMs = millis();
    _windowStartMs = millis();
    _pulseStartUs = 0;
    _accumulatedLowUs = 0;

    attachInterrupt(digitalPinToInterrupt(_signalPin), isrDsmSignal, CHANGE);
    Serial.println("[DSM501A] Driver iniciado com interrupcao por hardware (sem delay).");
}

void DSM501ADriver::handleInterrupt() {
    int level = digitalRead(_signalPin);
    uint32_t nowUs = micros();

    if (level == LOW) {
        _pulseStartUs = nowUs;
    } else {
        if (_pulseStartUs > 0) {
            uint32_t diff = nowUs - _pulseStartUs;
            _accumulatedLowUs += diff;
            _pulseStartUs = 0;
        }
    }
}

void DSM501ADriver::update() {
    unsigned long now = millis();

    // Período obrigatório de estabilização do resistor térmico interno (1 minuto)
    if (now - _bootTimeMs < WARMUP_TIME_MS) {
        return;
    }

    if (now - _windowStartMs >= SAMPLE_INTERVAL_MS) {
        // Seção crítica para captura e reset dos acumuladores atômicos
        noInterrupts();
        uint32_t lowUs = _accumulatedLowUs;
        _accumulatedLowUs = 0;
        _pulseStartUs = 0;
        interrupts();

        unsigned long actualWindowMs = now - _windowStartMs;
        _windowStartMs = now;

        // Calcula a porcentagem Low Pulse Occupancy (LPO)
        float lowMs = lowUs / 1000.0f;
        float ratio = (lowMs / (float)actualWindowMs) * 100.0f;
        if (ratio > 100.0f) ratio = 100.0f;
        if (ratio < 0.0f) ratio = 0.0f;
        _lastRatio = ratio;

        // Equação polinomial de concentração de partículas (pcs / 0.01 cf):
        // Concentração = 1.1 * (%Low)^3 - 3.8 * (%Low)^2 + 520 * (%Low) + 0.62
        float particleCount = 0.0f;
        if (ratio > 0.0f) {
            particleCount = 1.1f * pow(ratio, 3) - 3.8f * pow(ratio, 2) + 520.0f * ratio + 0.62f;
            if (particleCount < 0.0f) particleCount = 0.0f;
        }

        // Conversão de concentração para massa (µg/m³):
        // PM2.5 (µg/m³) = Concentração * 3531.5 * Massa da Partícula
        float pm25 = particleCount * VOLUMETRIC_FACTOR * PARTICLE_MASS_UG;
        if (pm25 < 0.0f) pm25 = 0.0f;

        // Validação da faixa aceita pela API (0.0 a 5000.0 µg/m³)
        if (pm25 <= 5000.0f) {
            _poeira = pm25;
            _valid = true;
            Serial.printf("[DSM501A] Janela de 30s concluida -> LPO: %.2f%% | PM2.5: %.2f µg/m³\n", _lastRatio, _poeira);
        }
    }
}
