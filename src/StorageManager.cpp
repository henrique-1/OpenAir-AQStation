#include "StorageManager.h"
#include <nvs_flash.h>
#include <cstdio>
#include <cstring>

StorageManager::StorageManager() {}

bool StorageManager::begin() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("[NVS] Formatando e recuperando particao NVS...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        Serial.printf("[NVS] ERRO ao inicializar NVS: %d\n", err);
        return false;
    }

    Preferences prefs;
    if (prefs.begin(PREFS_NAMESPACE, false)) {
        prefs.end();
        Serial.println("[NVS] Particao NVS pronta e validada.");
        return true;
    }

    Serial.println("[NVS] ERRO: Falha ao abrir namespace do Preferences.");
    return false;
}

bool StorageManager::hasValidConfig() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, true)) {
        return false;
    }

    uint8_t modeVal = prefs.getUChar("mode", MODE_UNCONFIGURED);
    prefs.end();

    return (modeVal == MODE_SATELLITE || modeVal == MODE_MATRIX);
}

bool StorageManager::loadConfig(DeviceConfig &config) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, true)) {
        Serial.println("[NVS] Falha ao abrir Preferences em modo leitura.");
        return false;
    }

    config.mode = static_cast<SystemMode>(prefs.getUChar("mode", MODE_UNCONFIGURED));

    String ssid = prefs.getString("wifi_ssid", "");
    String pass = prefs.getString("wifi_pass", "");
    String url  = prefs.getString("server_url", DEFAULT_SERVER_URL);
    String key  = prefs.getString("api_key", "");

    strncpy(config.wifiSsid, ssid.c_str(), sizeof(config.wifiSsid) - 1);
    config.wifiSsid[sizeof(config.wifiSsid) - 1] = '\0';

    strncpy(config.wifiPassword, pass.c_str(), sizeof(config.wifiPassword) - 1);
    config.wifiPassword[sizeof(config.wifiPassword) - 1] = '\0';

    strncpy(config.serverUrl, url.c_str(), sizeof(config.serverUrl) - 1);
    config.serverUrl[sizeof(config.serverUrl) - 1] = '\0';

    strncpy(config.apiKey, key.c_str(), sizeof(config.apiKey) - 1);
    config.apiKey[sizeof(config.apiKey) - 1] = '\0';

    size_t macLen = prefs.getBytes("target_mac", config.targetMac, 6);
    if (macLen != 6) {
        memset(config.targetMac, 0, 6);
    }

    config.sampleIntervalSec = prefs.getUShort("sample_sec", DEFAULT_SAMPLE_SEC);

    prefs.end();

    if (config.mode == MODE_SATELLITE) {
        bool allZero = true;
        for (int i = 0; i < 6; i++) {
            if (config.targetMac[i] != 0) {
                allZero = false;
                break;
            }
        }
        return !allZero;
    } else if (config.mode == MODE_MATRIX) {
        return (strlen(config.wifiSsid) > 0);
    }

    return false;
}

bool StorageManager::saveConfig(const DeviceConfig &config) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        Serial.println("[NVS] ERRO CRITICO: Nao foi possivel abrir Preferences para escrita!");
        return false;
    }

    prefs.putUChar("mode", static_cast<uint8_t>(config.mode));
    prefs.putBytes("target_mac", config.targetMac, 6);
    prefs.putString("wifi_ssid", config.wifiSsid);
    prefs.putString("wifi_pass", config.wifiPassword);
    prefs.putString("server_url", config.serverUrl);
    prefs.putString("api_key", config.apiKey);
    prefs.putUShort("sample_sec", config.sampleIntervalSec);

    prefs.end();
    Serial.println("[NVS] Configuracao salva com sucesso no flash!");
    return true;
}

bool StorageManager::clearConfig() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }
    bool success = prefs.clear();
    prefs.end();
    Serial.println("[NVS] Memoria NVS limpa com sucesso.");
    return success;
}

void StorageManager::macToString(const uint8_t *mac, char *outStr, size_t maxLen) {
    if (!mac || !outStr || maxLen < 18) return;
    snprintf(outStr, maxLen, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool StorageManager::stringToMac(const char *macStr, uint8_t *outMac) {
    if (!macStr || !outMac) return false;

    unsigned int bytes[6];
    int values = sscanf(macStr, "%x:%x:%x:%x:%x:%x",
                        &bytes[0], &bytes[1], &bytes[2],
                        &bytes[3], &bytes[4], &bytes[5]);

    if (values != 6) {
        values = sscanf(macStr, "%x-%x-%x-%x-%x-%x",
                        &bytes[0], &bytes[1], &bytes[2],
                        &bytes[3], &bytes[4], &bytes[5]);
    }

    if (values == 6) {
        for (int i = 0; i < 6; i++) {
            outMac[i] = static_cast<uint8_t>(bytes[i]);
        }
        return true;
    }

    return false;
}

