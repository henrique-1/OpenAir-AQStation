#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

class StorageManager {
public:
    StorageManager();
    
    // Inicializa a partição NVS e garante que está pronta para uso
    bool begin();

    // Carrega as configurações persistidas
    bool loadConfig(DeviceConfig &config);

    // Salva as configurações na NVS de forma atômica
    bool saveConfig(const DeviceConfig &config);

    // Apaga todas as configurações (Factory Reset)
    bool clearConfig();

    // Verifica se há uma configuração válida salva
    bool hasValidConfig();

    // Utilitários para conversão de MAC Address
    static void macToString(const uint8_t *mac, char *outStr, size_t maxLen);
    static bool stringToMac(const char *macStr, uint8_t *outMac);

private:
    static constexpr const char* PREFS_NAMESPACE = "openair_cfg";
};

#endif // STORAGE_MANAGER_H

