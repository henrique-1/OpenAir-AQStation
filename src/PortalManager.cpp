#include "PortalManager.h"
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
static const char* authModeToString(wifi_auth_mode_t auth, bool &secure) {
    switch (auth) {
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
    }
}

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
        body{font-family:sans-serif;padding:20px;text-align:center;background:#f0f9ff;color:#0369a1}
        .card{background:white;padding:30px;border-radius:12px;box-shadow:0 4px 6px rgba(0,0,0,0.1);max-width:400px;margin:auto}
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

static const char PORTAL_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>OpenAir - Configuração da Estação</title>
    <style>
        :root {
            /* Paleta Oficial OpenAir */
            --blue-dianne-950: #043948;
            --blue-dianne-900: #0f4c5c; /* Cor principal */
            --blue-dianne-800: #0f687b;
            --blue-dianne-500: #00cfe1;
            --blue-dianne-50:  #ebffff;

            --persian-green-500: #2a9d8f; /* Cor secundária */
            --persian-green-600: #208177;
            --persian-green-50:  #f2fbf9;

            --dodger-blue-400: #4da3ff;
            --dodger-blue-500: #3588fc;

            --athens-gray-50:  #f5f7f9;
            --athens-gray-100: #e9eef3;
            --athens-gray-200: #d5dfe8;
            --athens-gray-300: #b8c9d8;
            --athens-gray-500: #556c80;
            --athens-gray-700: #2f3e4c;
            --athens-gray-800: #232f3a;

            --ebony-clay-950:  #2b2d42;
            --black-haze-50:   #f8fafc;

            /* Cores de status de sinal */
            --signal-excellent: #2ecc71;
            --signal-good: #2a9d8f;
            --signal-fair: #f1c40f;
            --signal-weak: #e74c3c;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            font-family: 'Inter', system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, var(--athens-gray-50) 0%, #e2e8f0 100%);
            min-height: 100vh;
            padding: 24px 16px;
            color: var(--athens-gray-800);
            display: flex;
            align-items: center;
            justify-content: center;
        }

        .container {
            width: 100%;
            max-width: 480px;
            background: #ffffff;
            padding: 28px 24px;
            border-radius: 16px;
            box-shadow: 0 10px 25px -5px rgba(15, 76, 92, 0.1), 0 8px 10px -6px rgba(15, 76, 92, 0.05);
            border: 1px solid var(--athens-gray-200);
        }

        /* Topo: Logo & Ícone */
        .brand-header {
            text-align: center;
            margin-bottom: 20px;
        }

        .brand-images {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 12px;
            margin-bottom: 12px;
        }

        .brand-icon {
            width: 48px;
            height: 48px;
            object-fit: contain;
            border-radius: 10px;
            filter: drop-shadow(0 2px 4px rgba(15, 76, 92, 0.15));
        }

        .brand-logo {
            height: 38px;
            max-width: 220px;
            object-fit: contain;
        }

        .badge-preview {
            background: var(--blue-dianne-50);
            color: var(--blue-dianne-900);
            border: 1px solid var(--blue-dianne-200, #9ffbff);
            font-size: 11px;
            font-weight: 600;
            padding: 3px 10px;
            border-radius: 9999px;
            display: inline-block;
            margin-bottom: 8px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        h1 {
            font-size: 1.25rem;
            font-weight: 700;
            color: var(--blue-dianne-900);
            margin-bottom: 4px;
        }

        .subtitle {
            font-size: 0.85rem;
            color: var(--athens-gray-500);
            margin-bottom: 16px;
        }

        /* Card MAC Address */
        .mac-info {
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: var(--blue-dianne-50);
            border: 1px solid #c2f5f9;
            border-radius: 10px;
            padding: 10px 14px;
            font-size: 13px;
            color: var(--blue-dianne-900);
            margin-bottom: 20px;
        }

        .mac-value {
            font-family: monospace;
            font-weight: 700;
            font-size: 13px;
            background: #ffffff;
            padding: 3px 8px;
            border-radius: 6px;
            border: 1px solid var(--athens-gray-300);
            letter-spacing: 0.5px;
        }

        /* Seletor de Modo */
        .section-label {
            display: block;
            font-size: 13px;
            font-weight: 600;
            color: var(--athens-gray-700);
            margin-bottom: 8px;
        }

        .mode-select {
            display: flex;
            gap: 12px;
            margin-bottom: 20px;
        }

        .mode-option {
            flex: 1;
            border: 2px solid var(--athens-gray-200);
            background: var(--athens-gray-50);
            padding: 12px 10px;
            border-radius: 10px;
            text-align: center;
            cursor: pointer;
            transition: all 0.2s ease;
            user-select: none;
        }

        .mode-option:hover {
            border-color: var(--persian-green-500);
            background: #ffffff;
        }

        .mode-option.active {
            border-color: var(--blue-dianne-900);
            background: #ffffff;
            box-shadow: 0 4px 12px rgba(15, 76, 92, 0.12);
        }

        .mode-option strong {
            display: block;
            font-size: 14px;
            color: var(--blue-dianne-900);
            margin-bottom: 2px;
        }

        .mode-option .mode-desc {
            font-size: 11px;
            color: var(--athens-gray-500);
        }

        /* Formulário & Inputs */
        label {
            display: block;
            font-size: 13px;
            font-weight: 600;
            margin-bottom: 6px;
            color: var(--athens-gray-700);
        }

        .input-group {
            margin-bottom: 16px;
            position: relative;
        }

        input[type='text'], input[type='password'] {
            width: 100%;
            padding: 10px 14px;
            border: 1.5px solid var(--athens-gray-300);
            border-radius: 8px;
            font-size: 14px;
            color: var(--athens-gray-800);
            background: #ffffff;
            outline: none;
            transition: border-color 0.2s, box-shadow 0.2s;
            font-family: inherit;
        }

        input[type='text']:focus, input[type='password']:focus {
            border-color: var(--blue-dianne-900);
            box-shadow: 0 0 0 3px rgba(15, 76, 92, 0.12);
        }

        .input-mac {
            font-family: monospace !important;
            letter-spacing: 1px;
            font-weight: 600;
            text-transform: uppercase;
        }

        .input-hint {
            font-size: 11px;
            color: var(--athens-gray-500);
            margin-top: 4px;
            display: block;
        }

        /* Componente Dinâmico de Scan Wi-Fi */
        .wifi-scanner-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 6px;
        }

        .btn-scan {
            display: inline-flex;
            align-items: center;
            gap: 4px;
            background: transparent;
            border: none;
            color: var(--persian-green-600);
            font-size: 12px;
            font-weight: 600;
            cursor: pointer;
            padding: 4px 6px;
            border-radius: 6px;
            transition: background 0.2s, color 0.2s;
        }

        .btn-scan:hover {
            background: var(--persian-green-50);
            color: var(--persian-green-500);
        }

        .btn-scan svg {
            width: 14px;
            height: 14px;
            transition: transform 0.5s ease;
        }

        .btn-scan.scanning svg {
            animation: spin 0.8s linear infinite;
        }

        @keyframes spin {
            from { transform: rotate(0deg); }
            to { transform: rotate(360deg); }
        }

        /* Dropdown customizado de redes Wi-Fi */
        .wifi-dropdown {
            position: relative;
            margin-bottom: 14px;
        }

        .wifi-selected {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 10px 14px;
            border: 1.5px solid var(--athens-gray-300);
            border-radius: 8px;
            background: #ffffff;
            cursor: pointer;
            font-size: 14px;
            transition: all 0.2s;
        }

        .wifi-selected:hover {
            border-color: var(--blue-dianne-900);
        }

        .wifi-selected.open {
            border-color: var(--blue-dianne-900);
            box-shadow: 0 0 0 3px rgba(15, 76, 92, 0.12);
            border-bottom-left-radius: 0;
            border-bottom-right-radius: 0;
        }

        .wifi-selected-info {
            display: flex;
            align-items: center;
            gap: 10px;
            overflow: hidden;
        }

        .wifi-menu {
            display: none;
            position: absolute;
            top: 100%;
            left: 0;
            right: 0;
            background: #ffffff;
            border: 1.5px solid var(--blue-dianne-900);
            border-top: none;
            border-bottom-left-radius: 8px;
            border-bottom-right-radius: 8px;
            max-height: 220px;
            overflow-y: auto;
            z-index: 50;
            box-shadow: 0 8px 16px rgba(15, 76, 92, 0.12);
        }

        .wifi-menu.show {
            display: block;
        }

        .wifi-item {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 10px 14px;
            cursor: pointer;
            border-bottom: 1px solid var(--athens-gray-100);
            transition: background 0.15s;
        }

        .wifi-item:last-child {
            border-bottom: none;
        }

        .wifi-item:hover {
            background: var(--blue-dianne-50);
        }

        .wifi-item-left {
            display: flex;
            align-items: center;
            gap: 10px;
        }

        .wifi-details {
            display: flex;
            flex-direction: column;
            text-align: left;
        }

        .wifi-name {
            font-size: 13px;
            font-weight: 600;
            color: var(--athens-gray-800);
        }

        .wifi-security {
            font-size: 11px;
            color: var(--athens-gray-500);
            margin-top: 1px;
        }

        .wifi-item-right {
            display: flex;
            align-items: center;
            gap: 6px;
        }

        .icon-signal {
            width: 18px;
            height: 18px;
            flex-shrink: 0;
        }

        .icon-lock {
            width: 15px;
            height: 15px;
            color: var(--athens-gray-500);
        }

        .icon-chevron {
            width: 18px;
            height: 18px;
            color: var(--athens-gray-500);
            transition: transform 0.2s;
        }

        .wifi-selected.open .icon-chevron {
            transform: rotate(180deg);
        }

        .wifi-custom-option {
            background: var(--athens-gray-50);
            color: var(--persian-green-600);
            font-weight: 600;
            font-size: 12px;
            border-top: 1px dashed var(--athens-gray-300);
        }

        .wifi-custom-option:hover {
            background: var(--persian-green-50);
        }

        /* Botão Principal */
        .btn-submit {
            width: 100%;
            background: linear-gradient(135deg, var(--blue-dianne-900) 0%, var(--persian-green-600) 100%);
            color: #ffffff;
            border: none;
            padding: 13px;
            font-size: 15px;
            font-weight: 600;
            border-radius: 8px;
            cursor: pointer;
            margin-top: 12px;
            transition: all 0.25s ease;
            box-shadow: 0 4px 10px rgba(15, 76, 92, 0.2);
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
        }

        .btn-submit:hover {
            background: linear-gradient(135deg, var(--blue-dianne-950) 0%, var(--persian-green-500) 100%);
            box-shadow: 0 6px 14px rgba(15, 76, 92, 0.3);
            transform: translateY(-1px);
        }

        .btn-submit:active {
            transform: translateY(0);
        }

        .hidden {
            display: none !important;
        }
    </style>
</head>
<body>

    <div class="container">
        <!-- Cabeçalho com Logos e Título -->
        <div class="brand-header">
            
            <div class="brand-images">
                <!-- Ícone da Marca (WebP com fallback local) -->
                <img src="/images/1.webp" alt="Ícone OpenAir" class="brand-icon" onerror="this.onerror=null; this.src='./images/1.webp';">
                <!-- Imagem do Projeto (Logo Horizontal) -->
                <img src="/images/3.webp" alt="OpenAir" class="brand-logo" onerror="this.onerror=null; this.src='./images/3.webp';">
            </div>
            <h1>Estação de Qualidade do Ar</h1>
            <p class="subtitle">Portal de Comissionamento e Conexão</p>
        </div>

        <!-- Identificador Físico (MAC) -->
        <div class="mac-info">
            <span><strong>Identificador (MAC):</strong></span>
            <span class="mac-value">%MAC_ADDR%</span>
        </div>

        <form method="POST" action="/save" onsubmit="return validateForm(event)">
            <!-- Seletor de Modo -->
            <label class="section-label">Selecione a Função da Estação:</label>
            <div class="mode-select">
                <div id="opt-sat" class="mode-option active" onclick="setMode(1)">
                    <strong>Satélite</strong>
                    <div class="mode-desc">Envia via ESP-NOW</div>
                </div>
                <div id="opt-mat" class="mode-option" onclick="setMode(2)">
                    <strong>Matriz</strong>
                    <div class="mode-desc">Gateway WiFi + API</div>
                </div>
            </div>
            <input type="hidden" name="mode" id="mode-input" value="1">

            <!-- ================= CAMPOS MODO SATÉLITE ================= -->
            <div id="sec-sat">
                <div class="input-group">
                    <label for="target_mac">Endereço MAC de Destino (Próximo Nó / Matriz):</label>
                    <input type="text" id="target_mac" name="target_mac" 
                           class="input-mac"
                           placeholder="AA:BB:CC:DD:EE:FF" 
                           maxlength="17" 
                           autocomplete="off" 
                           spellcheck="false"
                           oninput="applyMacMask(this)">
                    <span class="input-hint">Formato: 6 pares hexadecimais separados por dois pontos (:).</span>
                </div>
            </div>

            <!-- ================= CAMPOS MODO MATRIZ ================= -->
            <div id="sec-mat" class="hidden">
                <!-- Seletor Dinâmico de Redes Wi-Fi -->
                <div class="wifi-scanner-header">
                    <label for="ssid">Rede Wi-Fi (SSID):</label>
                    <button type="button" class="btn-scan" id="btn-scan-wifi" onclick="scanWifiNetworks()">
                        <!-- Heroicon: Arrow Path -->
                        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor">
                            <path stroke-linecap="round" stroke-linejoin="round" d="M16.023 9.348h4.992v-.001M2.985 19.644v-4.992m0 0h4.992m-4.993 0 3.181 3.183a8.25 8.25 0 0 0 13.803-3.7M4.031 9.865a8.25 8.25 0 0 1 13.803-3.7l3.181 3.182m0-4.991v4.99" />
                        </svg>
                        <span id="scan-text">Buscar Redes</span>
                    </button>
                </div>

                <!-- Dropdown de Redes -->
                <div class="wifi-dropdown" id="wifi-dropdown">
                    <div class="wifi-selected" id="wifi-selected" onclick="toggleWifiDropdown()">
                        <div class="wifi-selected-info" id="wifi-selected-info">
                            <span style="color: var(--athens-gray-500)">Selecione uma rede Wi-Fi...</span>
                        </div>
                        <!-- Heroicon: Chevron Down -->
                        <svg class="icon-chevron" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor">
                            <path stroke-linecap="round" stroke-linejoin="round" d="m19.5 8.25-7.5 7.5-7.5-7.5" />
                        </svg>
                    </div>

                    <!-- Lista de Redes Detectadas -->
                    <div class="wifi-menu" id="wifi-menu">
                        <!-- Itens renderizados dinamicamente via JS -->
                    </div>
                </div>

                <!-- Campo manual de SSID (quando o usuário escolhe inserir manualmente ou fallback) -->
                <div class="input-group hidden" id="manual-ssid-group">
                    <label for="ssid-manual">Nome da Rede Manual (SSID):</label>
                    <input type="text" id="ssid-manual" placeholder="Digite o SSID da rede oculta" maxlength="32" oninput="updateSsidValue(this.value)">
                </div>

                <!-- Input real que é enviado no formulário -->
                <input type="hidden" id="ssid" name="ssid" value="">

                <div class="input-group">
                    <label for="password">Senha da Rede Wi-Fi:</label>
                    <input type="password" id="password" name="password" placeholder="Digite a senha da rede" maxlength="64">
                    <span class="input-hint" id="password-hint">Deixe em branco se a rede for aberta.</span>
                </div>

                <div class="input-group">
                    <label for="api_key">Chave de Telemetria (X-Sensor-Key):</label>
                    <input type="password" id="api_key" name="api_key" placeholder="Digite a chave da API" maxlength="64">
                </div>

                <div class="input-group">
                    <label for="url">URL do Endpoint da API:</label>
                    <input type="text" id="url" name="url" 
                           value="%SERVER_URL%" 
                           placeholder="https://openair.bissoli.tech/api/medicoes" 
                           maxlength="120">
                </div>
            </div>

            <button type="submit" class="btn-submit">
                <!-- Heroicon: Check -->
                <svg style="width: 18px; height: 18px;" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2.5" stroke="currentColor">
                    <path stroke-linecap="round" stroke-linejoin="round" d="m4.5 12.75 6 6 9-13.5" />
                </svg>
                Salvar e Iniciar Estação
            </button>
        </form>
    </div>

    <script>
        // =========================================================================
        // Alternância de Modo (Satélite vs Matriz)
        // =========================================================================
        function setMode(m) {
            document.getElementById('mode-input').value = m;
            const optSat = document.getElementById('opt-sat');
            const optMat = document.getElementById('opt-mat');
            const secSat = document.getElementById('sec-sat');
            const secMat = document.getElementById('sec-mat');

            if (m === 1) {
                optSat.classList.add('active');
                optMat.classList.remove('active');
                secSat.classList.remove('hidden');
                secMat.classList.add('hidden');
            } else {
                optMat.classList.add('active');
                optSat.classList.remove('active');
                secMat.classList.remove('hidden');
                secSat.classList.add('hidden');
                // Se a lista de redes estiver vazia na primeira vez, executa o scan
                if (scannedNetworks.length === 0) {
                    scanWifiNetworks();
                }
            }
        }

        // =========================================================================
        // Máscara de Endereço MAC (AA:BB:CC:DD:EE:FF)
        // =========================================================================
        function applyMacMask(input) {
            // Remove tudo que não for dígito hexadecimal
            let val = input.value.toUpperCase().replace(/[^0-9A-F]/g, '');
            if (val.length > 12) {
                val = val.substring(0, 12);
            }

            // Agrupa em pares separados por ':'
            let formatted = '';
            for (let i = 0; i < val.length; i += 2) {
                if (i > 0) formatted += ':';
                formatted += val.substr(i, 2);
            }
            input.value = formatted;
        }

        // =========================================================================
        // Sistema Dinâmico de Scan Wi-Fi com Heroicons
        // =========================================================================
        let scannedNetworks = [];

        // SVGs Inline dos Heroicons (Não precisam ser baixados da internet!)
        function getWifiHeroicon(rssi) {
            let color = 'var(--signal-excellent)';
            if (rssi < -80) {
                color = 'var(--signal-weak)';
            } else if (rssi < -70) {
                color = 'var(--signal-fair)';
            } else if (rssi < -60) {
                color = 'var(--signal-good)';
            }

            // Heroicon: Wifi (24x24 outline)
            return `<svg class="icon-signal" style="color: ${color}" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.8" stroke="currentColor">
                <path stroke-linecap="round" stroke-linejoin="round" d="M8.288 15.038a5.25 5.25 0 0 1 7.424 0M5.106 11.856c3.807-3.808 9.98-3.808 13.788 0M1.924 8.674c5.565-5.565 14.587-5.565 20.152 0M12.53 18.22l-.53.53-.53-.53a.75.75 0 0 1 1.06 0Z" />
            </svg>`;
        }

        function getLockHeroicon(isSecure) {
            if (!isSecure) return '';
            // Heroicon: Lock Closed
            return `<svg class="icon-lock" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.8" stroke="currentColor" title="Rede protegida">
                <path stroke-linecap="round" stroke-linejoin="round" d="M16.5 10.5V6.75a4.5 4.5 0 1 0-9 0v3.75m-.75 11.25h10.5a2.25 2.25 0 0 0 2.25-2.25v-6.75a2.25 2.25 0 0 0-2.25-2.25H6.75a2.25 2.25 0 0 0-2.25 2.25v6.75a2.25 2.25 0 0 0 2.25 2.25Z" />
            </svg>`;
        }

        // Executa busca de redes (Simulação local ou endpoint real /scan do ESP32)
        function scanWifiNetworks() {
            const btn = document.getElementById('btn-scan-wifi');
            const scanText = document.getElementById('scan-text');
            btn.classList.add('scanning');
            scanText.textContent = "Buscando...";

            // Se estiver em ambiente real do ESP32 (HTTP), tenta buscar /scan
            if (window.location.protocol.startsWith('http') && !window.location.pathname.endsWith('portal_preview.html')) {
                fetch('/scan')
                    .then(r => {
                        if (!r.ok) throw new Error("HTTP " + r.status);
                        return r.json();
                    })
                    .then(data => {
                        scannedNetworks = Array.isArray(data) ? data : [];
                        renderWifiMenu();
                    })
                    .catch(err => {
                        console.warn("Scan endpoint indisponível:", err);
                        loadMockNetworks();
                    })
                    .finally(finishScan);
            } else {
                // Simulação local realista para testes no PC
                setTimeout(() => {
                    loadMockNetworks();
                    finishScan();
                }, 800);
            }

            function finishScan() {
                btn.classList.remove('scanning');
                scanText.textContent = "Buscar Redes";
            }
        }

        function loadMockNetworks() {
            scannedNetworks = [
                { ssid: "OpenAir-Laboratorio-5G", rssi: -45, security: "WPA2-PSK", secure: true },
                { ssid: "IFSP-Academico", rssi: -58, security: "WPA2-Enterprise", secure: true },
                { ssid: "IFSP-Visitantes", rssi: -66, security: "Aberta", secure: false },
                { ssid: "Rede_Pesquisa_IoT", rssi: -74, security: "WPA3-SAE", secure: true },
                { ssid: "Estacao_Metereologica_Ext", rssi: -84, security: "WPA2-PSK", secure: true }
            ];
            renderWifiMenu();
        }

        function renderWifiMenu() {
            const menu = document.getElementById('wifi-menu');
            let html = '';

            if (!scannedNetworks || scannedNetworks.length === 0) {
                html += `
                    <div class="wifi-item" style="cursor: default; background: var(--athens-gray-50)">
                        <span style="font-size: 12px; color: var(--athens-gray-500)">Nenhuma rede Wi-Fi encontrada.</span>
                    </div>
                `;
            } else {
                scannedNetworks.forEach(net => {
                    const signalIcon = getWifiHeroicon(net.rssi);
                    const lockIcon = getLockHeroicon(net.secure);
                    const safeSsid = net.ssid.replace(/'/g, "\\'");

                    html += `
                        <div class="wifi-item" onclick="selectWifi('${safeSsid}', ${net.secure}, ${net.rssi}, '${net.security}')">
                            <div class="wifi-item-left">
                                ${signalIcon}
                                <div class="wifi-details">
                                    <span class="wifi-name">${net.ssid}</span>
                                    <span class="wifi-security">${net.security} &bull; ${net.rssi} dBm</span>
                                </div>
                            </div>
                            <div class="wifi-item-right">
                                ${lockIcon}
                            </div>
                        </div>
                    `;
                });
            }

            // Opção manual
            html += `
                <div class="wifi-item wifi-custom-option" onclick="selectManualWifi()">
                    <div class="wifi-item-left">
                        <svg class="icon-signal" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor">
                            <path stroke-linecap="round" stroke-linejoin="round" d="M12 4.5v15m7.5-7.5h-15" />
                        </svg>
                        <span class="wifi-name">Outra rede (Digitar SSID oculto...)</span>
                    </div>
                </div>
            `;

            menu.innerHTML = html;
        }

        function toggleWifiDropdown() {
            const selected = document.getElementById('wifi-selected');
            const menu = document.getElementById('wifi-menu');
            selected.classList.toggle('open');
            menu.classList.toggle('show');
        }

        function selectWifi(ssid, secure, rssi, security) {
            document.getElementById('ssid').value = ssid;
            document.getElementById('manual-ssid-group').classList.add('hidden');

            const info = document.getElementById('wifi-selected-info');
            info.innerHTML = `
                ${getWifiHeroicon(rssi)}
                <div class="wifi-details">
                    <span class="wifi-name">${ssid}</span>
                    <span class="wifi-security">${security}</span>
                </div>
            `;

            toggleWifiDropdown();

            // Configura campo de senha
            const passInput = document.getElementById('password');
            const passHint = document.getElementById('password-hint');

            if (!secure) {
                passInput.value = '';
                passInput.placeholder = "Rede aberta (senha não necessária)";
                passInput.disabled = true;
                passHint.textContent = "Esta rede é aberta. O campo de senha foi desabilitado.";
            } else {
                passInput.disabled = false;
                passInput.placeholder = "Digite a senha do Wi-Fi";
                passHint.textContent = "Informe a senha para conectar a esta rede.";
                passInput.focus();
            }
        }

        function selectManualWifi() {
            document.getElementById('ssid').value = '';
            document.getElementById('manual-ssid-group').classList.remove('hidden');

            const info = document.getElementById('wifi-selected-info');
            info.innerHTML = `
                <span class="wifi-name" style="color: var(--persian-green-600)">Rede manual / oculta</span>
            `;

            toggleWifiDropdown();

            const passInput = document.getElementById('password');
            passInput.disabled = false;
            passInput.placeholder = "Senha da rede";
            document.getElementById('password-hint').textContent = "Digite a senha caso a rede possua criptografia.";
            document.getElementById('ssid-manual').focus();
        }

        function updateSsidValue(val) {
            document.getElementById('ssid').value = val;
        }

        // Fecha o dropdown se clicar fora
        document.addEventListener('click', function(e) {
            const dropdown = document.getElementById('wifi-dropdown');
            if (!dropdown.contains(e.target)) {
                document.getElementById('wifi-selected').classList.remove('open');
                document.getElementById('wifi-menu').classList.remove('show');
            }
        });

        // =========================================================================
        // Validação e Submissão do Formulário
        // =========================================================================
        function validateForm(event) {
            const mode = document.getElementById('mode-input').value;

            if (mode === '1') {
                const mac = document.getElementById('target_mac').value.trim();
                if (!mac || mac.length !== 17) {
                    alert("Atenção: Por favor preencha um endereço MAC válido (AA:BB:CC:DD:EE:FF).");
                    if (event) event.preventDefault();
                    return false;
                }
            } else {
                const ssid = document.getElementById('ssid').value.trim();
                if (!ssid) {
                    alert("Atenção: Selecione ou digite o nome da rede Wi-Fi (SSID).");
                    if (event) event.preventDefault();
                    return false;
                }
            }

            // Em ambiente de teste local (arquivo ou preview), previne envio real e exibe resumo
            if (window.location.protocol === 'file:' || window.location.pathname.endsWith('portal_preview.html')) {
                if (event) event.preventDefault();
                const pass = document.getElementById('password').value;
                const url = document.getElementById('url').value;
                const targetMac = document.getElementById('target_mac').value;
                let summary = (mode === '1') 
                    ? `Modo: Satélite\nMAC Destino: ${targetMac}` 
                    : `Modo: Matriz\nRede: ${document.getElementById('ssid').value}\nSenha: ${pass ? '••••••••' : '(sem senha)'}\nEndpoint: ${url}`;
                alert("Simulação de Envio bem-sucedida!\n\n" + summary);
                return false;
            }

            return true;
        }

        // Carrega as redes iniciais apenas no ambiente de teste local
        window.addEventListener('DOMContentLoaded', () => {
            if (window.location.protocol === 'file:' || window.location.pathname.endsWith('portal_preview.html')) {
                loadMockNetworks();
            }
        });
    </script>
</body>
</html>
)rawliteral";

// =============================================================================
// Implementação da Classe PortalManager
// =============================================================================
PortalManager::PortalManager(StorageManager &storage)
    : _storage(storage), _server(80), _isRunning(false), _configSaved(false) {
    memset(&_pendingConfig, 0, sizeof(_pendingConfig));
}

void PortalManager::begin() {
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

    Serial.printf("[PORTAL] Ponto de Acesso criado: %s\n", apSsid);
    Serial.printf("[PORTAL] Servidor de configuracao rodando em: http://%s/\n", apIP.toString().c_str());
}

void PortalManager::update() {
    if (!_isRunning) return;

    _dnsServer.processNextRequest();
    _server.handleClient();
}

void PortalManager::stop() {
    if (!_isRunning) return;

    _server.stop();
    _dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    _isRunning = false;

    Serial.println("[PORTAL] Ponto de Acesso e Servidor Web desligados.");
}

void PortalManager::setupRoutes() {
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/save", [this]() { handleSave(); });
    _server.on("/scan", HTTP_GET, [this]() { handleScan(); });

    // Assets de Imagem estáticos em PROGMEM
    _server.on("/images/1.webp", HTTP_GET, [this]() { handleImageIcon(); });
    _server.on("/images/3.webp", HTTP_GET, [this]() { handleImageLogo(); });
    _server.on("/images/1.png", HTTP_GET, [this]() { handleImageIcon(); });
    _server.on("/images/3.png", HTTP_GET, [this]() { handleImageLogo(); });

    // Endpoints de detecção de Captive Portal
    _server.on("/generate_204", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/ncsi.txt", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/connecttest.txt", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/redirect", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/success.txt", HTTP_GET, [this]() { handleRoot(); });

    _server.onNotFound([this]() { handleNotFound(); });
}

void PortalManager::handleRoot() {
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "-1");
    _server.send(200, "text/html", buildHtmlPage());
}

void PortalManager::handleImageIcon() {
    _server.sendHeader("Cache-Control", "public, max-age=86400");
    _server.send_P(200, "image/webp", (const char*)IMAGE_ICON_WEBP, IMAGE_ICON_WEBP_LEN);
}

void PortalManager::handleImageLogo() {
    _server.sendHeader("Cache-Control", "public, max-age=86400");
    _server.send_P(200, "image/webp", (const char*)IMAGE_LOGO_WEBP, IMAGE_LOGO_WEBP_LEN);
}

void PortalManager::handleScan() {
    Serial.println("[PORTAL] Escaneando redes Wi-Fi...");
    int16_t n = WiFi.scanNetworks(false, true);

    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();

    if (n > 0) {
        struct NetInfo {
            int32_t rssi;
            String security;
            bool secure;
        };
        std::map<String, NetInfo> uniqueNets;

        for (int16_t i = 0; i < n; ++i) {
            String ssid = WiFi.SSID(i);
            ssid.trim();
            if (ssid.length() == 0) continue;

            int32_t rssi = WiFi.RSSI(i);
            bool secure = false;
            const char* secStr = authModeToString(WiFi.encryptionType(i), secure);

            auto it = uniqueNets.find(ssid);
            if (it == uniqueNets.end() || rssi > it->second.rssi) {
                uniqueNets[ssid] = { rssi, String(secStr), secure };
            }
        }

        struct NetEntry {
            String ssid;
            int32_t rssi;
            String security;
            bool secure;
        };
        std::vector<NetEntry> sortedNets;
        sortedNets.reserve(uniqueNets.size());
        for (const auto &pair : uniqueNets) {
            sortedNets.push_back({ pair.first, pair.second.rssi, pair.second.security, pair.second.secure });
        }

        std::sort(sortedNets.begin(), sortedNets.end(), [](const NetEntry &a, const NetEntry &b) {
            return a.rssi > b.rssi;
        });

        for (const auto &net : sortedNets) {
            JsonObject item = array.add<JsonObject>();
            item["ssid"] = net.ssid;
            item["rssi"] = net.rssi;
            item["security"] = net.security;
            item["secure"] = net.secure;
        }

        Serial.printf("[PORTAL] Scan concluido: %d redes unicas encontradas.\n", (int)sortedNets.size());
    } else {
        Serial.println("[PORTAL] Nenhuma rede encontrada.");
    }

    WiFi.scanDelete();

    String responseJson;
    serializeJson(doc, responseJson);

    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.send(200, "application/json", responseJson);
}

void PortalManager::handleSave() {
    Serial.println("\n[PORTAL] Processando submissao em /save...");

    if (!_server.hasArg("mode")) {
        Serial.println("[PORTAL] Erro: parametro 'mode' ausente.");
        _server.send(400, "text/html", "<h3>Erro: Modo de operacao nao selecionado.</h3>");
        return;
    }

    int modeVal = _server.arg("mode").toInt();
    DeviceConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    if (modeVal == MODE_SATELLITE) {
        cfg.mode = MODE_SATELLITE;
        String macStr = _server.hasArg("target_mac") ? _server.arg("target_mac") : "";
        macStr.trim();

        if (!StorageManager::stringToMac(macStr.c_str(), cfg.targetMac)) {
            Serial.printf("[PORTAL] Erro: MAC invalido: '%s'\n", macStr.c_str());
            _server.send(400, "text/html", "<h3>Erro: Formato de Endereco MAC invalido. Use AA:BB:CC:DD:EE:FF</h3>");
            return;
        }
        Serial.printf("[PORTAL] Modo: SATELITE | Destino MAC: %s\n", macStr.c_str());
    } else if (modeVal == MODE_MATRIX) {
        cfg.mode = MODE_MATRIX;
        String ssid = _server.hasArg("ssid") ? _server.arg("ssid") : "";
        String pass = _server.hasArg("password") ? _server.arg("password") : "";
        String url  = _server.hasArg("url") ? _server.arg("url") : "";
        String key  = _server.hasArg("api_key") ? _server.arg("api_key") : "";

        ssid.trim();
        pass.trim();
        url.trim();
        key.trim();

        if (ssid.length() == 0) {
            Serial.println("[PORTAL] Erro: SSID vazio.");
            _server.send(400, "text/html", "<h3>Erro: Nome da Rede Wi-Fi (SSID) e obrigatorio.</h3>");
            return;
        }

        if (url.length() == 0) {
            url = DEFAULT_SERVER_URL;
        }

        strncpy(cfg.wifiSsid, ssid.c_str(), sizeof(cfg.wifiSsid) - 1);
        cfg.wifiSsid[sizeof(cfg.wifiSsid) - 1] = '\0';

        strncpy(cfg.wifiPassword, pass.c_str(), sizeof(cfg.wifiPassword) - 1);
        cfg.wifiPassword[sizeof(cfg.wifiPassword) - 1] = '\0';

        strncpy(cfg.serverUrl, url.c_str(), sizeof(cfg.serverUrl) - 1);
        cfg.serverUrl[sizeof(cfg.serverUrl) - 1] = '\0';

        strncpy(cfg.apiKey, key.c_str(), sizeof(cfg.apiKey) - 1);
        cfg.apiKey[sizeof(cfg.apiKey) - 1] = '\0';

        Serial.printf("[PORTAL] Modo: MATRIZ | Rede: %s | Endpoint: %s\n", cfg.wifiSsid, cfg.serverUrl);
    } else {
        Serial.printf("[PORTAL] Erro: Modo numerico invalido: %d\n", modeVal);
        _server.send(400, "text/html", "<h3>Erro: Modo invalido.</h3>");
        return;
    }

    cfg.sampleIntervalSec = DEFAULT_SAMPLE_SEC;

    // Grava na memória não-volátil (NVS) de forma atômica
    if (_storage.saveConfig(cfg)) {
        _pendingConfig = cfg;
        _configSaved = true;

        _server.send(200, "text/html", SUCCESS_HTML);
        Serial.println("[PORTAL] Sucesso: Gravacao na NVS concluida.");
    } else {
        Serial.println("[PORTAL] ERRO FATAL: Falha ao persistir na NVS.");
        _server.send(500, "text/html", "<h3>Erro ao salvar na memoria NVS. Verifique o console serial.</h3>");
    }
}

void PortalManager::handleNotFound() {
    _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    _server.send(302, "text/plain", "");
}

String PortalManager::buildHtmlPage() {
    uint8_t mac[6];
    char macStr[18];
    // Obtém o MAC físico da interface Station (identificador único da estação)
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    StorageManager::macToString(mac, macStr, sizeof(macStr));

    String html = PORTAL_HTML;
    html.replace("%MAC_ADDR%", macStr);
    html.replace("%SERVER_URL%", DEFAULT_SERVER_URL);
    return html;
}
