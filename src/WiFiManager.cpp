#include "WiFiManager.h"
#include <cstdio>
#include <cstring>

WiFiManager::WiFiManager()
    : _isConnected(false),
      _lastReconnectAttempt(0),
      _reconnectIntervalMs(INITIAL_RECONNECT_MS),
      _lastHttpSendMs(0) {
    memset(_ssid, 0, sizeof(_ssid));
    memset(_password, 0, sizeof(_password));
    memset(_serverUrl, 0, sizeof(_serverUrl));
    memset(_apiKey, 0, sizeof(_apiKey));
}

void WiFiManager::begin(const char *ssid, const char *password, const char *serverUrl, const char *apiKey) {
    if (ssid) {
        strncpy(_ssid, ssid, sizeof(_ssid) - 1);
        _ssid[sizeof(_ssid) - 1] = '\0';
    }
    if (password) {
        strncpy(_password, password, sizeof(_password) - 1);
        _password[sizeof(_password) - 1] = '\0';
    }
    if (serverUrl && strlen(serverUrl) > 0) {
        strncpy(_serverUrl, serverUrl, sizeof(_serverUrl) - 1);
        _serverUrl[sizeof(_serverUrl) - 1] = '\0';
    } else {
        strncpy(_serverUrl, DEFAULT_SERVER_URL, sizeof(_serverUrl) - 1);
        _serverUrl[sizeof(_serverUrl) - 1] = '\0';
    }
    if (apiKey) {
        strncpy(_apiKey, apiKey, sizeof(_apiKey) - 1);
        _apiKey[sizeof(_apiKey) - 1] = '\0';
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.printf("[WIFI] Conectando a rede: %s ...\n", _ssid);
    attemptReconnect();
}

void WiFiManager::attemptReconnect() {
    _lastReconnectAttempt = millis();
    WiFi.begin(_ssid, _password);
}

void WiFiManager::update() {
    bool currentStatus = (WiFi.status() == WL_CONNECTED);

    if (currentStatus && !_isConnected) {
        // Acabou de conectar
        _isConnected = true;
        _reconnectIntervalMs = INITIAL_RECONNECT_MS; // Reseta backoff
        Serial.printf("[WIFI] Conectado com sucesso! IP: %s | Canal Wi-Fi: %d\n",
                      WiFi.localIP().toString().c_str(), WiFi.channel());
    } else if (!currentStatus && _isConnected) {
        // Conexão caiu
        _isConnected = false;
        Serial.println("[WIFI] Conexao perdida. Iniciando rotina de reconexao com backoff.");
    }

    // Rotina de reconexão não-bloqueante se desconectado
    if (!_isConnected) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= _reconnectIntervalMs) {
            Serial.printf("[WIFI] Tentando reconectar (intervalo: %lu ms)...\n", _reconnectIntervalMs);
            attemptReconnect();

            // Exponential Backoff
            _reconnectIntervalMs *= 2;
            if (_reconnectIntervalMs > MAX_RECONNECT_MS) {
                _reconnectIntervalMs = MAX_RECONNECT_MS;
            }
        }
    }
}

bool WiFiManager::sendTelemetry(const char *macAddress, float temp, float umid, int co2, float poeira) {
    if (!_isConnected) {
        Serial.println("[HTTP Telemetria] Wi-Fi desconectado. Envio abortado.");
        return false;
    }

    unsigned long now = millis();
    if (now - _lastHttpSendMs < MIN_HTTP_INTERVAL_MS) {
        Serial.println("[HTTP Telemetria] Aguardando controle de vazao (rate limit: max 60 req/min).");
        return false;
    }

    HTTPClient http;
    http.setTimeout(DEFAULT_HTTP_TIMEOUT_MS);

    if (!http.begin(_serverUrl)) {
        Serial.println("[HTTP Telemetria] ERRO: Falha ao iniciar conexao com o endpoint.");
        return false;
    }

    // Cabeçalhos HTTP obrigatórios definidos pela API
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("X-Sensor-Key", _apiKey);

    // Monta o payload JSON em buffer estático para prevenir fragmentação de memória heap (AGENTS.md Diretriz 2)
    char jsonPayload[256];
    snprintf(jsonPayload, sizeof(jsonPayload),
             "{\"mac_address\":\"%s\",\"temperatura\":%.2f,\"umidade\":%.2f,\"co2\":%d,\"poeira\":%.2f}",
             macAddress, temp, umid, co2, poeira);

    Serial.printf("[HTTP POST] Enviando telemetria da estacao (MAC: %s) para %s ...\n", macAddress, _serverUrl);
    Serial.printf("[HTTP POST] Payload JSON: %s\n", jsonPayload);

    int httpCode = http.POST(reinterpret_cast<uint8_t*>(jsonPayload), strlen(jsonPayload));
    _lastHttpSendMs = millis();

    bool success = false;
    if (httpCode == HTTP_CODE_CREATED) { // 201 Created
        Serial.println("[HTTP Resposta 201]: Medicao registrada com sucesso no Laravel.");
        success = true;
    } else {
        Serial.printf("[HTTP Erro %d]: ", httpCode);
        if (httpCode > 0) {
            String respBody = http.getString();
            Serial.printf("Resposta do Servidor: %s\n", respBody.c_str());
        } else {
            Serial.println(http.errorToString(httpCode).c_str());
        }
    }

    http.end();
    return success;
}
