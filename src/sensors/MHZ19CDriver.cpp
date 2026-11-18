#include "sensors/MHZ19CDriver.h"

MHZ19CDriver::MHZ19CDriver(uint8_t rxPin, uint8_t txPin)
    : _rxPin(rxPin),
      _txPin(txPin),
      _serial(2),
      _co2(0),
      _valid(false),
      _lastPollMs(0),
      _reqSentMs(0),
      _awaitingReply(false) {}

void MHZ19CDriver::begin() {
    _serial.begin(9600, SERIAL_8N1, _rxPin, _txPin);
    _lastPollMs = millis();
}

void MHZ19CDriver::update() {
    unsigned long now = millis();

    if (_awaitingReply) {
        if (_serial.available() >= 9) {
            parseResponse();
            _awaitingReply = false;
        } else if (now - _reqSentMs > REPLY_TIMEOUT_MS) {
            // Timeout de resposta
            _awaitingReply = false;
            size_t bytesDisponiveis = _serial.available();
            static unsigned long lastWarnMs = 0;
            if (now - lastWarnMs >= 15000) {
                lastWarnMs = now;
                Serial.printf("[MH-Z19C] AVISO: Sensor nao respondeu (bytes no buffer: %u). Verifique pinos 16/17 (TX/RX) e alimentacao 5V.\n", (unsigned int)bytesDisponiveis);
            }
            while (_serial.available()) {
                _serial.read();
            }
        }
    } else {
        if (now - _lastPollMs >= POLL_INTERVAL_MS) {
            _lastPollMs = now;
            sendReadCommand();
        }
    }
}

void MHZ19CDriver::sendReadCommand() {
    // Limpa eventuais bytes residuais na fila serial
    while (_serial.available()) {
        _serial.read();
    }

    // Comando padrão de leitura de CO2: 0xFF 0x01 0x86 0x00 0x00 0x00 0x00 0x00 0x79
    const uint8_t cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
    _serial.write(cmd, 9);
    _reqSentMs = millis();
    _awaitingReply = true;
}

bool MHZ19CDriver::parseResponse() {
    uint8_t buf[9];
    _serial.readBytes(buf, 9);

    if (buf[0] != 0xFF || buf[1] != 0x86) {
        Serial.printf("[MH-Z19C] Cabecalho de resposta invalido: 0x%02X 0x%02X\n", buf[0], buf[1]);
        return false;
    }

    uint8_t expectedChecksum = calculateChecksum(buf);
    if (buf[8] != expectedChecksum) {
        Serial.printf("[MH-Z19C] Checksum incorreto (recebido: 0x%02X, esperado: 0x%02X)\n", buf[8], expectedChecksum);
        return false;
    }

    int ppm = (static_cast<int>(buf[2]) << 8) | buf[3];

    // Validação da faixa aceita pela API (0 a 50.000 ppm)
    if (ppm >= 0 && ppm <= 50000) {
        _co2 = ppm;
        _valid = true;
        Serial.printf("[MH-Z19C] Leitura de CO2: %d ppm | Saturacao: %.1f%% (%s)\n",
                      _co2, getSaturacaoVentilacao(), getVentilationStatusDescription());
        return true;
    }

    return false;
}

float MHZ19CDriver::getSaturacaoVentilacao() const {
    if (!_valid) {
        return 0.0f;
    }
    // Razão de Saturação CO2 (%) = (C_CO2 / 1000 ppm) * 100 (ANVISA RE nº 09/2003)
    return (_co2 / ANVISA_CO2_VMR_PPM) * 100.0f;
}

IndoorVentilationStatus MHZ19CDriver::getVentilationStatus() const {
    if (_co2 <= 700) {
        return VENTILATION_EXCELLENT;
    } else if (_co2 <= 1000) {
        return VENTILATION_ACCEPTABLE;
    } else if (_co2 <= 1500) {
        return VENTILATION_INADEQUATE;
    } else {
        return VENTILATION_CRITICAL;
    }
}

const char* MHZ19CDriver::getVentilationStatusDescription() const {
    switch (getVentilationStatus()) {
        case VENTILATION_EXCELLENT:
            return "Excelente (Alta Renovacao)";
        case VENTILATION_ACCEPTABLE:
            return "Aceitavel (Limite ANVISA)";
        case VENTILATION_INADEQUATE:
            return "Ventilacao Inadequada (Atencao)";
        case VENTILATION_CRITICAL:
        default:
            return "Critico (Alerta de Ventilacao)";
    }
}

uint8_t MHZ19CDriver::calculateChecksum(const uint8_t *packet) {
    uint8_t sum = 0;
    for (int i = 1; i < 8; i++) {
        sum += packet[i];
    }
    return static_cast<uint8_t>(0xFF - sum + 1);
}
