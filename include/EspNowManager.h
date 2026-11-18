#ifndef ESP_NOW_MANAGER_H
#define ESP_NOW_MANAGER_H

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "Config.h"

// Callback para repassar pacotes recebidos pela Matriz ou nós intermediários
typedef void (*OnPayloadReceivedCallback)(const EspNowPayload &payload);

class EspNowManager {
public:
    EspNowManager();

    // Inicializa o ESP-NOW em modo Satélite ou Matriz
    bool begin(SystemMode mode, const uint8_t *targetMac = nullptr, uint8_t channel = 0);

    // Envia dados para o nó configurado
    bool send(const EspNowPayload &payload);

    // Registra função para tratar pacotes recebidos
    void setReceiveCallback(OnPayloadReceivedCallback cb);

    // Chamado pelas rotinas de callback do driver da Espressif
    void handleSendStatus(bool success);
    void handleDataReceived(const uint8_t *srcMac, const uint8_t *incomingData, int len);

    bool isReady() const { return _initialized; }

private:
    bool                       _initialized;
    SystemMode                 _mode;
    uint8_t                    _targetMac[6];
    esp_now_peer_info_t        _peerInfo;
    OnPayloadReceivedCallback  _recvCb;
    volatile bool              _lastSendSuccess;
};

#endif // ESP_NOW_MANAGER_H
