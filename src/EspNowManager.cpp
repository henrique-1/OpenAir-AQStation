#include "EspNowManager.h"
#include <esp_idf_version.h>

static EspNowManager* s_espNowInstance = nullptr;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void onDataSentThunk(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (s_espNowInstance) {
        s_espNowInstance->handleSendStatus(status == ESP_NOW_SEND_SUCCESS);
    }
}

static void onDataRecvThunk(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (s_espNowInstance) {
        s_espNowInstance->handleDataReceived(info->src_addr, data, len);
    }
}
#else
static void onDataSentThunk(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (s_espNowInstance) {
        s_espNowInstance->handleSendStatus(status == ESP_NOW_SEND_SUCCESS);
    }
}

static void onDataRecvThunk(const uint8_t *mac_addr, const uint8_t *data, int len) {
    if (s_espNowInstance) {
        s_espNowInstance->handleDataReceived(mac_addr, data, len);
    }
}
#endif

EspNowManager::EspNowManager()
    : _initialized(false),
      _mode(MODE_UNCONFIGURED),
      _recvCb(nullptr),
      _lastSendSuccess(false) {
    memset(_targetMac, 0, sizeof(_targetMac));
    memset(&_peerInfo, 0, sizeof(_peerInfo));
}

bool EspNowManager::begin(SystemMode mode, const uint8_t *targetMac, uint8_t channel) {
    s_espNowInstance = this;
    _mode = mode;

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] ERRO: Falha ao inicializar o protocolo ESP-NOW.");
        _initialized = false;
        return false;
    }

    esp_now_register_send_cb(onDataSentThunk);
    esp_now_register_recv_cb(onDataRecvThunk);

    // Se estiver em modo Satélite ou com MAC de destino definido, registra o peer
    if (targetMac != nullptr) {
        memcpy(_targetMac, targetMac, 6);
        memcpy(_peerInfo.peer_addr, targetMac, 6);
        _peerInfo.channel = channel;
        _peerInfo.encrypt = false;

        // Se o peer já existir, remove antes de re-adicionar
        if (esp_now_is_peer_exist(_targetMac)) {
            esp_now_del_peer(_targetMac);
        }

        esp_err_t ret = esp_now_add_peer(&_peerInfo);
        if (ret != ESP_OK) {
            Serial.printf("[ESP-NOW] ERRO: Falha ao registrar peer de destino (codigo: %d)\n", ret);
            _initialized = false;
            return false;
        }

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 _targetMac[0], _targetMac[1], _targetMac[2],
                 _targetMac[3], _targetMac[4], _targetMac[5]);
        Serial.printf("[ESP-NOW] Peer registrado com sucesso (Canal: %d, MAC: %s)\n", channel, macStr);
    }

    _initialized = true;
    Serial.println("[ESP-NOW] Protocolo inicializado e callbacks registrados.");
    return true;
}

bool EspNowManager::send(const EspNowPayload &payload) {
    if (!_initialized) {
        Serial.println("[ESP-NOW] Falha no envio: protocolo nao inicializado.");
        return false;
    }

    esp_err_t result = esp_now_send(_targetMac, (const uint8_t *)&payload, sizeof(payload));
    if (result == ESP_OK) {
        return true;
    } else {
        Serial.printf("[ESP-NOW] Falha na chamada esp_now_send (codigo: %d)\n", result);
        return false;
    }
}

void EspNowManager::setReceiveCallback(OnPayloadReceivedCallback cb) {
    _recvCb = cb;
}

void EspNowManager::handleSendStatus(bool success) {
    _lastSendSuccess = success;
    if (success) {
        Serial.println("[ESP-NOW Status] Pacote entregue com sucesso (ACK recebido).");
    } else {
        Serial.println("[ESP-NOW Status] FALHA na entrega do pacote para o peer.");
    }
}

void EspNowManager::handleDataReceived(const uint8_t *srcMac, const uint8_t *incomingData, int len) {
    if (len == sizeof(EspNowPayload)) {
        EspNowPayload payload;
        memcpy(&payload, incomingData, sizeof(EspNowPayload));

        char senderMacStr[18];
        snprintf(senderMacStr, sizeof(senderMacStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);

        Serial.printf("[ESP-NOW RX] Pacote recebido de %s | Estacao: %s | CO2: %d | Poeira: %.1f\n",
                      senderMacStr, payload.stationMac, payload.co2, payload.poeira);

        if (_recvCb) {
            _recvCb(payload);
        }
    } else {
        Serial.printf("[ESP-NOW RX] ERRO: Pacote descartado. Tamanho recebido (%d) != esperado (%d)\n",
                      len, (int)sizeof(EspNowPayload));
    }
}
