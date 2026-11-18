#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>
#include "Config.h"
#include "StorageManager.h"
#include "PortalManager.h"
#include "SensorManager.h"
#include "EspNowManager.h"
#include "WiFiManager.h"

class StateMachine {
public:
    StateMachine();

    // Inicialização da máquina de estados no setup()
    void begin();

    // Execução contínua no loop() (totalmente não-bloqueante)
    void update();

    // Transição de estado com teardown do estado anterior e setup do novo
    void transitionTo(SystemMode newMode);

    // Retorna o modo atual
    SystemMode getCurrentMode() const { return _currentMode; }

    // Enfileira pacote recebido via ESP-NOW na Matriz
    void onEspNowPayloadReceived(const EspNowPayload &payload);

private:
    SystemMode     _currentMode;
    DeviceConfig   _config;
    StorageManager _storage;
    PortalManager  _portal;
    SensorManager  _sensors;
    EspNowManager  _espNow;
    WiFiManager    _wifi;

    // Controle de verificação do botão de reset (não-bloqueante)
    unsigned long  _btnPressStart;
    bool           _btnWasPressed;

    // Controle de pacotes e tempos
    uint32_t       _packetSeq;
    unsigned long  _lastSensorSampleMs;
    unsigned long  _lastHeartbeatMs;

    // Fila estática para pacotes de satélites recebidos na Matriz (Zero Heap Fragmentation)
    static constexpr size_t RX_QUEUE_SIZE = 16;
    EspNowPayload  _rxQueue[RX_QUEUE_SIZE];
    uint8_t        _queueHead;
    uint8_t        _queueTail;
    uint8_t        _queueCount;

    bool enqueuePayload(const EspNowPayload &payload);
    bool dequeuePayload(EspNowPayload &payload);

    // Handlers dedicados de cada estado
    void handleUnconfigured();
    void handleSatellite();
    void handleMatrix();

    // Setup e teardown específicos de cada estado
    void enterUnconfigured();
    void enterSatellite();
    void enterMatrix();

    void checkResetButton();
};

#endif // STATE_MACHINE_H
