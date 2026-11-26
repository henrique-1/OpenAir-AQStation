#ifndef PORTAL_MANAGER_H
#define PORTAL_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "Config.h"
#include "StorageManager.h"

class PortalManager {
public:
    PortalManager(StorageManager &storage);

    // Inicia o Ponto de Acesso e os servidores DNS/HTTP
    void begin();

    // Processa requisições DNS e HTTP (não-bloqueante, chamado no loop)
    void update();

    // Encerra os servidores e desliga o rádio AP
    void stop();

    // Informa se a configuração acabou de ser salva
    bool isConfigSaved() const { return _configSaved; }

    // Retorna a configuração recém-submetida
    const DeviceConfig& getSavedConfig() const { return _pendingConfig; }

private:
    StorageManager &_storage;
    WebServer       _server;
    DNSServer       _dnsServer;
    bool            _isRunning;
    bool            _configSaved;
    DeviceConfig    _pendingConfig;

    void setupRoutes();
    void handleRoot();
    void handleSave();
    void handleScan();
    void handleImageLogo();
    void handleNotFound();

    // Gera a página HTML do portal de configuração
    String buildHtmlPage();
};

#endif // PORTAL_MANAGER_H
