#include "StateMachine.h"
#include <esp_wifi.h>

static StateMachine* s_fsmInstance = nullptr;

static void onGlobalEspNowRecv(const EspNowPayload &payload) {
    if (s_fsmInstance) {
        s_fsmInstance->onEspNowPayloadReceived(payload);
    }
}

StateMachine::StateMachine()
    : _currentMode(MODE_UNCONFIGURED),
      _portal(_storage),
      _btnPressStart(0),
      _btnWasPressed(false),
      _packetSeq(0),
      _lastSensorSampleMs(0),
      _lastHeartbeatMs(0),
      _queueHead(0),
      _queueTail(0),
      _queueCount(0) {
    memset(&_config, 0, sizeof(_config));
    s_fsmInstance = this;
}

void StateMachine::begin() {
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    pinMode(PIN_RESET_BUTTON, INPUT_PULLUP);

    _storage.begin();

    // Verificação de segurança no boot: Se o botão BOOT estiver pressionado no arranque, limpa NVS
    if (digitalRead(PIN_RESET_BUTTON) == LOW) {
        Serial.println("[BOOT] Botao BOOT pressionado no arranque. Formatando NVS...");
        _storage.clearConfig();
        for (int i = 0; i < 6; i++) {
            digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
            delay(100);
        }
    }

    uint8_t baseMac[6];
    char macStr[18];
    esp_wifi_get_mac(WIFI_IF_STA, baseMac);
    StorageManager::macToString(baseMac, macStr, sizeof(macStr));
    Serial.printf("[BOOT] Identificador fisico desta estacao (MAC): %s\n", macStr);

    if (_storage.hasValidConfig() && _storage.loadConfig(_config)) {
        Serial.println("[BOOT] Configuracao valida encontrada na NVS.");
        if (_config.mode == MODE_SATELLITE) {
            char targetMacStr[18];
            StorageManager::macToString(_config.targetMac, targetMacStr, sizeof(targetMacStr));
            Serial.printf("[BOOT] Modo carregado: SATELITE (Destino MAC: %s)\n", targetMacStr);
            transitionTo(MODE_SATELLITE);
        } else if (_config.mode == MODE_MATRIX) {
            Serial.printf("[BOOT] Modo carregado: MATRIZ (Rede: %s)\n", _config.wifiSsid);
            transitionTo(MODE_MATRIX);
        } else {
            transitionTo(MODE_UNCONFIGURED);
        }
    } else {
        Serial.println("[BOOT] Nenhuma configuracao valida encontrada. Ativando Ponto de Acesso de configuracao.");
        transitionTo(MODE_UNCONFIGURED);
    }
}

void StateMachine::transitionTo(SystemMode newMode) {
    if (_currentMode == MODE_UNCONFIGURED && newMode != MODE_UNCONFIGURED) {
        _portal.stop();
    }

    _currentMode = newMode;
    _lastSensorSampleMs = millis();
    _lastHeartbeatMs = millis();

    switch (_currentMode) {
        case MODE_UNCONFIGURED:
            enterUnconfigured();
            break;
        case MODE_SATELLITE:
            enterSatellite();
            break;
        case MODE_MATRIX:
            enterMatrix();
            break;
    }
}

void StateMachine::enterUnconfigured() {
    Serial.println("\n==========================================");
    Serial.println("[ESTADO] Ativando MODE_UNCONFIGURED");
    Serial.println("==========================================");
    digitalWrite(PIN_STATUS_LED, HIGH);
    _portal.begin();
}

void StateMachine::enterSatellite() {
    Serial.println("\n==========================================");
    Serial.println("[ESTADO] Ativando MODE_SATELLITE");
    Serial.println("==========================================");

    _sensors.begin();

    // WiFi em modo Station é pré-requisito para o ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    _espNow.begin(MODE_SATELLITE, _config.targetMac);
}

void StateMachine::enterMatrix() {
    Serial.println("\n==========================================");
    Serial.println("[ESTADO] Ativando MODE_MATRIX (Gateway)");
    Serial.println("==========================================");

    _sensors.begin();

    // Inicia Wi-Fi Station para comunicação com a nuvem Laravel
    _wifi.begin(_config.wifiSsid, _config.wifiPassword, _config.serverUrl, _config.apiKey);

    // Inicializa ESP-NOW para receber dados dos satélites
    _espNow.begin(MODE_MATRIX, nullptr);
    _espNow.setReceiveCallback(onGlobalEspNowRecv);
}

void StateMachine::checkResetButton() {
    int btnState = digitalRead(PIN_RESET_BUTTON);

    if (btnState == LOW) {
        if (!_btnWasPressed) {
            _btnWasPressed = true;
            _btnPressStart = millis();
        } else {
            if (millis() - _btnPressStart >= RESET_BUTTON_HOLD_MS) {
                Serial.println("\n[RESET] Botao mantido por 3s! Resetando NVS...");
                _storage.clearConfig();
                _btnWasPressed = false;
                transitionTo(MODE_UNCONFIGURED);
            }
        }
    } else {
        _btnWasPressed = false;
    }
}

void StateMachine::update() {
    checkResetButton();

    switch (_currentMode) {
        case MODE_UNCONFIGURED:
            handleUnconfigured();
            break;
        case MODE_SATELLITE:
            handleSatellite();
            break;
        case MODE_MATRIX:
            handleMatrix();
            break;
    }

    yield(); // Prevenção de estouro do Watchdog Timer (WDT) conforme AGENTS.md
}

void StateMachine::handleUnconfigured() {
    _portal.update();

    static unsigned long lastBlink = 0;
    if (millis() - lastBlink >= 500) {
        lastBlink = millis();
        digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    }

    if (_portal.isConfigSaved()) {
        _config = _portal.getSavedConfig();
        Serial.println("[FSM] Nova configuracao recebida do portal. Realizando transicao...");
        transitionTo(_config.mode);
    }
}

void StateMachine::handleSatellite() {
    _sensors.update();

    unsigned long now = millis();

    // Heartbeat LED
    if (now - _lastHeartbeatMs >= 2000) {
        _lastHeartbeatMs = now;
        digitalWrite(PIN_STATUS_LED, HIGH);
    }
    if (digitalRead(PIN_STATUS_LED) == HIGH && (now - _lastHeartbeatMs >= 50)) {
        digitalWrite(PIN_STATUS_LED, LOW);
    }

    // Leitura e envio periódico via ESP-NOW
    unsigned long intervalMs = (_config.sampleIntervalSec > 0 ? _config.sampleIntervalSec : DEFAULT_SAMPLE_SEC) * 1000UL;
    if (now - _lastSensorSampleMs >= intervalMs) {
        _lastSensorSampleMs = now;

        SensorData data;
        if (_sensors.getSensorData(data)) {
            EspNowPayload payload;
            memset(&payload, 0, sizeof(payload));

            // Obtém o MAC local como identificador de origem
            uint8_t baseMac[6];
            esp_wifi_get_mac(WIFI_IF_STA, baseMac);
            StorageManager::macToString(baseMac, payload.stationMac, sizeof(payload.stationMac));

            payload.packetId    = ++_packetSeq;
            payload.temperatura = data.temperatura;
            payload.umidade     = data.umidade;
            payload.co2         = data.co2;
            payload.poeira      = data.poeira;
            payload.hops        = 1;

            Serial.printf("[SATELITE] Enviando telemetria '%s' via ESP-NOW (T:%.1f, U:%.1f, CO2:%d, Poeira:%.1f)...\n",
                          payload.stationMac, payload.temperatura, payload.umidade, payload.co2, payload.poeira);

            _espNow.send(payload);
        } else {
            Serial.println("[SATELITE] Aguardando estabilizacao dos sensores...");
        }
    }
}

void StateMachine::handleMatrix() {
    _sensors.update();
    _wifi.update();

    unsigned long now = millis();

    // Heartbeat LED
    if (now - _lastHeartbeatMs >= 1000) {
        _lastHeartbeatMs = now;
        digitalWrite(PIN_STATUS_LED, HIGH);
    }
    if (digitalRead(PIN_STATUS_LED) == HIGH && (now - _lastHeartbeatMs >= 40)) {
        digitalWrite(PIN_STATUS_LED, LOW);
    }

    if (_wifi.isConnected()) {
        // 1. Processa leituras recebidas dos satélites via ESP-NOW
        EspNowPayload satPayload;
        if (dequeuePayload(satPayload)) {
            Serial.printf("[MATRIZ Gateway] Reencaminhando leitura do Satelite '%s' para a API Laravel...\n", satPayload.stationMac);
            _wifi.sendTelemetry(satPayload.stationMac,
                                satPayload.temperatura,
                                satPayload.umidade,
                                satPayload.co2,
                                satPayload.poeira);
        }

        // 2. Coleta e despacha medição dos sensores locais da própria Matriz
        unsigned long intervalMs = (_config.sampleIntervalSec > 0 ? _config.sampleIntervalSec : DEFAULT_SAMPLE_SEC) * 1000UL;
        if (now - _lastSensorSampleMs >= intervalMs) {
            _lastSensorSampleMs = now;

            SensorData localData;
            if (_sensors.getSensorData(localData)) {
                uint8_t baseMac[6];
                char localMacStr[18];
                esp_wifi_get_mac(WIFI_IF_STA, baseMac);
                StorageManager::macToString(baseMac, localMacStr, sizeof(localMacStr));

                Serial.printf("[MATRIZ Local] Medições coletadas (MAC %s):\n"
                              "  -> Temperatura: %.1f °C\n"
                              "  -> Umidade:     %.1f %%\n"
                              "  -> CO2:         %d ppm (Saturacao: %.1f%% | %s)\n"
                              "  -> PM2.5:       %.2f µg/m³\n",
                              localMacStr,
                              localData.temperatura,
                              localData.umidade,
                              localData.co2,
                              _sensors.getCO2Saturacao(),
                              _sensors.getVentilationStatusDescription(),
                              localData.poeira);

                _wifi.sendTelemetry(localMacStr,
                                    localData.temperatura,
                                    localData.umidade,
                                    localData.co2,
                                    localData.poeira);
            }
        }
    }
}

void StateMachine::onEspNowPayloadReceived(const EspNowPayload &payload) {
    if (!enqueuePayload(payload)) {
        Serial.println("[MATRIZ Queue] AVISO: Fila cheia. Descartando pacote mais antigo.");
    }
}

bool StateMachine::enqueuePayload(const EspNowPayload &payload) {
    if (_queueCount < RX_QUEUE_SIZE) {
        _rxQueue[_queueHead] = payload;
        _queueHead = (_queueHead + 1) % RX_QUEUE_SIZE;
        _queueCount++;
        return true;
    }
    return false;
}

bool StateMachine::dequeuePayload(EspNowPayload &payload) {
    if (_queueCount > 0) {
        payload = _rxQueue[_queueTail];
        _queueTail = (_queueTail + 1) % RX_QUEUE_SIZE;
        _queueCount--;
        return true;
    }
    return false;
}

