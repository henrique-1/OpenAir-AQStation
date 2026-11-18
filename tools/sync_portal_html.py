#!/usr/bin/env python3
"""
Sincroniza o HTML de tools/portal_preview.html para dentro de src/PortalManager.cpp,
incorporando a nova lógica de scan de redes Wi-Fi e rotas de assets WebP.
"""
import os
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
PREVIEW_HTML_PATH = os.path.join(SCRIPT_DIR, "portal_preview.html")
PORTAL_CPP_PATH = os.path.join(PROJECT_ROOT, "src", "PortalManager.cpp")

with open(PREVIEW_HTML_PATH, "r", encoding="utf-8") as f:
    html = f.read()

# Substitui os valores estáticos por tags dinâmicas do C++
html = html.replace("24:6F:28:B2:1A:4C", "%MAC_ADDR%")
html = html.replace('value="https://openair.bissoli.tech/api/medicoes"', 'value="%SERVER_URL%"')
# Remove o badge de preview
html = html.replace('<span class="badge-preview">Preview Local</span>', '')

cpp_code = f"""#include "PortalManager.h"
#include "PortalImages.h"
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <map>
#include <vector>
#include <algorithm>

static const byte DNS_PORT = 53;

// =============================================================================
// Conversor de Autenticação Wi-Fi para Texto Amigável
// =============================================================================
static const char* authModeToString(wifi_auth_mode_t auth, bool &secure) {{
    switch (auth) {{
        case WIFI_AUTH_OPEN:
            secure = false;
            return "Aberta";
        case WIFI_AUTH_WEP:
            secure = true;
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            secure = true;
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
            secure = true;
            return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:
            secure = true;
            return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            secure = true;
            return "WPA2-Enterprise";
        case WIFI_AUTH_WPA3_PSK:
            secure = true;
            return "WPA3-SAE";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            secure = true;
            return "WPA2/WPA3";
        default:
            secure = true;
            return "Protegida";
    }}
}}

// =============================================================================
// Templates HTML em Flash (PROGMEM) via C++ Raw String Literal
// =============================================================================
static const char SUCCESS_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>Sucesso</title>
    <style>
        body{{font-family:sans-serif;padding:20px;text-align:center;background:#f0f9ff;color:#0369a1}}
        .card{{background:white;padding:30px;border-radius:12px;box-shadow:0 4px 6px rgba(0,0,0,0.1);max-width:400px;margin:auto}}
    </style>
</head>
<body>
    <div class="card">
        <h2>Configuração Salva com Sucesso!</h2>
        <p>A estação foi configurada e iniciará as medições.</p>
        <p>O Ponto de Acesso será desligado e a estação entrará em operação.</p>
    </div>
</body>
</html>)rawliteral";

static const char PORTAL_HTML[] PROGMEM = R"rawliteral({html})rawliteral";

// =============================================================================
// Implementação da Classe PortalManager
// =============================================================================
PortalManager::PortalManager(StorageManager &storage)
    : _storage(storage), _server(80), _isRunning(false), _configSaved(false) {{
    memset(&_pendingConfig, 0, sizeof(_pendingConfig));
}}

void PortalManager::begin() {{
    _configSaved = false;

    uint8_t mac[6];
    char apSsid[32];
    
    // Modo duplo (AP + STA) para permitir o escaneamento de redes ao redor enquanto provê o AP
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect();

    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(apSsid, sizeof(apSsid), "%s%02X%02X", DEFAULT_AP_SSID_PREFIX, mac[4], mac[5]);

    WiFi.softAP(apSsid, DEFAULT_AP_PASSWORD);

    IPAddress apIP = WiFi.softAPIP();

    // Redireciona todas as requisições DNS para o IP local (Captive Portal)
    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsServer.start(DNS_PORT, "*", apIP);

    setupRoutes();
    _server.begin();
    _isRunning = true;

    Serial.printf("[PORTAL] Ponto de Acesso criado: %s\\n", apSsid);
    Serial.printf("[PORTAL] Servidor de configuracao rodando em: http://%s/\\n", apIP.toString().c_str());
}}

void PortalManager::update() {{
    if (!_isRunning) return;

    _dnsServer.processNextRequest();
    _server.handleClient();
}}

void PortalManager::stop() {{
    if (!_isRunning) return;

    _server.stop();
    _dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    _isRunning = false;

    Serial.println("[PORTAL] Ponto de Acesso e Servidor Web desligados.");
}}

void PortalManager::setupRoutes() {{
    _server.on("/", HTTP_GET, [this]() {{ handleRoot(); }});
    _server.on("/save", [this]() {{ handleSave(); }});
    _server.on("/scan", HTTP_GET, [this]() {{ handleScan(); }});

    // Assets de Imagem estáticos em PROGMEM
    _server.on("/images/1.webp", HTTP_GET, [this]() {{ handleImageIcon(); }});
    _server.on("/images/3.webp", HTTP_GET, [this]() {{ handleImageLogo(); }});
    _server.on("/images/1.png", HTTP_GET, [this]() {{ handleImageIcon(); }});
    _server.on("/images/3.png", HTTP_GET, [this]() {{ handleImageLogo(); }});

    // Endpoints de detecção de Captive Portal
    _server.on("/generate_204", HTTP_GET, [this]() {{ handleRoot(); }});
    _server.on("/hotspot-detect.html", HTTP_GET, [this]() {{ handleRoot(); }});
    _server.on("/ncsi.txt", HTTP_GET, [this]() {{ handleRoot(); }});
    _server.on("/connecttest.txt", HTTP_GET, [this]() {{ handleRoot(); }});
    _server.on("/redirect", HTTP_GET, [this]() {{ handleRoot(); }});
    _server.on("/success.txt", HTTP_GET, [this]() {{ handleRoot(); }});

    _server.onNotFound([this]() {{ handleNotFound(); }});
}}

void PortalManager::handleRoot() {{
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "-1");
    _server.send(200, "text/html", buildHtmlPage());
}}

void PortalManager::handleImageIcon() {{
    _server.sendHeader("Cache-Control", "public, max-age=86400");
    _server.send_P(200, "image/webp", (const char*)IMAGE_ICON_WEBP, IMAGE_ICON_WEBP_LEN);
}}

void PortalManager::handleImageLogo() {{
    _server.sendHeader("Cache-Control", "public, max-age=86400");
    _server.send_P(200, "image/webp", (const char*)IMAGE_LOGO_WEBP, IMAGE_LOGO_WEBP_LEN);
}}

void PortalManager::handleScan() {{
    Serial.println("[PORTAL] Escaneando redes Wi-Fi...");
    int16_t n = WiFi.scanNetworks(false, true);

    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();

    if (n > 0) {{
        struct NetInfo {{
            int32_t rssi;
            String security;
            bool secure;
        }};
        std::map<String, NetInfo> uniqueNets;

        for (int16_t i = 0; i < n; ++i) {{
            String ssid = WiFi.SSID(i);
            ssid.trim();
            if (ssid.length() == 0) continue;

            int32_t rssi = WiFi.RSSI(i);
            bool secure = false;
            const char* secStr = authModeToString(WiFi.encryptionType(i), secure);

            auto it = uniqueNets.find(ssid);
            if (it == uniqueNets.end() || rssi > it->second.rssi) {{
                uniqueNets[ssid] = {{ rssi, String(secStr), secure }};
            }}
        }}

        struct NetEntry {{
            String ssid;
            int32_t rssi;
            String security;
            bool secure;
        }};
        std::vector<NetEntry> sortedNets;
        sortedNets.reserve(uniqueNets.size());
        for (const auto &pair : uniqueNets) {{
            sortedNets.push_back({{ pair.first, pair.second.rssi, pair.second.security, pair.second.secure }});
        }}

        std::sort(sortedNets.begin(), sortedNets.end(), [](const NetEntry &a, const NetEntry &b) {{
            return a.rssi > b.rssi;
        }});

        for (const auto &net : sortedNets) {{
            JsonObject item = array.add<JsonObject>();
            item["ssid"] = net.ssid;
            item["rssi"] = net.rssi;
            item["security"] = net.security;
            item["secure"] = net.secure;
        }}

        Serial.printf("[PORTAL] Scan concluido: %d redes unicas encontradas.\\n", (int)sortedNets.size());
    }} else {{
        Serial.println("[PORTAL] Nenhuma rede encontrada.");
    }}

    WiFi.scanDelete();

    String responseJson;
    serializeJson(doc, responseJson);

    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.send(200, "application/json", responseJson);
}}

void PortalManager::handleSave() {{
    Serial.println("\\n[PORTAL] Processando submissao em /save...");

    if (!_server.hasArg("mode")) {{
        Serial.println("[PORTAL] Erro: parametro 'mode' ausente.");
        _server.send(400, "text/html", "<h3>Erro: Modo de operacao nao selecionado.</h3>");
        return;
    }}

    int modeVal = _server.arg("mode").toInt();
    DeviceConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    if (modeVal == MODE_SATELLITE) {{
        cfg.mode = MODE_SATELLITE;
        String macStr = _server.hasArg("target_mac") ? _server.arg("target_mac") : "";
        macStr.trim();

        if (!StorageManager::stringToMac(macStr.c_str(), cfg.targetMac)) {{
            Serial.printf("[PORTAL] Erro: MAC invalido: '%s'\\n", macStr.c_str());
            _server.send(400, "text/html", "<h3>Erro: Formato de Endereco MAC invalido. Use AA:BB:CC:DD:EE:FF</h3>");
            return;
        }}
        Serial.printf("[PORTAL] Modo: SATELITE | Destino MAC: %s\\n", macStr.c_str());
    }} else if (modeVal == MODE_MATRIX) {{
        cfg.mode = MODE_MATRIX;
        String ssid = _server.hasArg("ssid") ? _server.arg("ssid") : "";
        String pass = _server.hasArg("password") ? _server.arg("password") : "";
        String url  = _server.hasArg("url") ? _server.arg("url") : "";
        String key  = _server.hasArg("api_key") ? _server.arg("api_key") : "";

        ssid.trim();
        pass.trim();
        url.trim();
        key.trim();

        if (ssid.length() == 0) {{
            Serial.println("[PORTAL] Erro: SSID vazio.");
            _server.send(400, "text/html", "<h3>Erro: Nome da Rede Wi-Fi (SSID) e obrigatorio.</h3>");
            return;
        }}

        if (url.length() == 0) {{
            url = DEFAULT_SERVER_URL;
        }}

        strncpy(cfg.wifiSsid, ssid.c_str(), sizeof(cfg.wifiSsid) - 1);
        cfg.wifiSsid[sizeof(cfg.wifiSsid) - 1] = '\\0';

        strncpy(cfg.wifiPassword, pass.c_str(), sizeof(cfg.wifiPassword) - 1);
        cfg.wifiPassword[sizeof(cfg.wifiPassword) - 1] = '\\0';

        strncpy(cfg.serverUrl, url.c_str(), sizeof(cfg.serverUrl) - 1);
        cfg.serverUrl[sizeof(cfg.serverUrl) - 1] = '\\0';

        strncpy(cfg.apiKey, key.c_str(), sizeof(cfg.apiKey) - 1);
        cfg.apiKey[sizeof(cfg.apiKey) - 1] = '\\0';

        Serial.printf("[PORTAL] Modo: MATRIZ | Rede: %s | Endpoint: %s\\n", cfg.wifiSsid, cfg.serverUrl);
    }} else {{
        Serial.printf("[PORTAL] Erro: Modo numerico invalido: %d\\n", modeVal);
        _server.send(400, "text/html", "<h3>Erro: Modo invalido.</h3>");
        return;
    }}

    cfg.sampleIntervalSec = DEFAULT_SAMPLE_SEC;

    // Grava na memória não-volátil (NVS) de forma atômica
    if (_storage.saveConfig(cfg)) {{
        _pendingConfig = cfg;
        _configSaved = true;

        _server.send(200, "text/html", SUCCESS_HTML);
        Serial.println("[PORTAL] Sucesso: Gravacao na NVS concluida.");
    }} else {{
        Serial.println("[PORTAL] ERRO FATAL: Falha ao persistir na NVS.");
        _server.send(500, "text/html", "<h3>Erro ao salvar na memoria NVS. Verifique o console serial.</h3>");
    }}
}}

void PortalManager::handleNotFound() {{
    _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    _server.send(302, "text/plain", "");
}}

String PortalManager::buildHtmlPage() {{
    uint8_t mac[6];
    char macStr[18];
    // Obtém o MAC físico da interface Station (identificador único da estação)
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    StorageManager::macToString(mac, macStr, sizeof(macStr));

    String html = PORTAL_HTML;
    html.replace("%MAC_ADDR%", macStr);
    html.replace("%SERVER_URL%", DEFAULT_SERVER_URL);
    return html;
}}
"""

with open(PORTAL_CPP_PATH, "w", encoding="utf-8") as f:
    f.write(cpp_code)

print(f"PortalManager.cpp sincronizado com sucesso!")
