#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Config.h"

class WiFiManager {
public:
    WiFiManager();

    // Inicializa a conexão Wi-Fi em modo Station
    void begin(const char *ssid, const char *password, const char *serverUrl, const char *apiKey);

    // Gerencia o estado da conexão e reconexões não-bloqueantes com backoff
    void update();

    // Envia medição de telemetria para a API Laravel (POST /api/medicoes)
    bool sendTelemetry(const char *macAddress, float temp, float umid, int co2, float poeira);

    bool isConnected() const { return _isConnected; }
    uint8_t getChannel() const { return WiFi.channel(); }

private:
    char          _ssid[33];
    char          _password[65];
    char          _serverUrl[128];
    char          _apiKey[65];
    bool          _isConnected;
    unsigned long _lastReconnectAttempt;
    unsigned long _reconnectIntervalMs;
    unsigned long _lastHttpSendMs;

    static constexpr unsigned long MIN_HTTP_INTERVAL_MS = 1000; // Limite de vazão: máx 60 req/min (1 req/seg)
    static constexpr unsigned long INITIAL_RECONNECT_MS = 5000; // 5 segundos
    static constexpr unsigned long MAX_RECONNECT_MS     = 60000;// 1 minuto (backoff teto)

    void attemptReconnect();
};

#endif // WIFI_MANAGER_H
