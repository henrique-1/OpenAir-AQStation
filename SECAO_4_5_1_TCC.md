# 4.5.1 Desenvolvimento do Sistema Embarcado para o Circuito Projetado

O desenvolvimento do firmware para o microcontrolador ESP32 foi projetado sob os paradigmas de computação embarcada de alta confiabilidade: **arquitetura orientada a máquina de estados finitos (FSM)**, **execução não-bloqueante** (*event-driven* baseada em `millis()` e interrupções por hardware, sem a utilização de `delay()`), e **gestão preventiva da memória dinâmica (Heap)** para evitar problemas de fragmentação em operações contínuas (*24/7*).

---

### 1. Provisão de Rede Wi-Fi, Captive Portal e Persistência Não-Volátil (NVS)

Para permitir a operação em campo sem a necessidade de reprogramação via cabo serial, o sistema implementa o modo não-configurado (`MODE_UNCONFIGURED`). Ao detectar ausência de parâmetros ou comando de *Factory Reset*, o microcontrolador atua como Ponto de Acesso Wi-Fi (*Access Point* - AP) e disponibiliza um portal cativo (*Captive Portal*) para comissionamento.

#### 1.1 Criação do Ponto de Acesso e Servidor Web (Captive Portal)

O arquivo `PortalManager.cpp` inicializa a interface Wi-Fi em modo `WIFI_AP`, gera um SSID derivado dos dois últimos bytes do endereço MAC físico do ESP32 (ex: `OpenAir-Station-B21A`) e inicia um servidor DNS na porta 53 configurado para interceptar qualquer domínio requisitado pelo cliente (`*`) e redirecioná-lo para o IP local (`192.168.4.1`).

```cpp
void PortalManager::begin() {
    _configSaved = false;

    uint8_t mac[6];
    char apSsid[32];
    WiFi.mode(WIFI_AP);
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
```

Para garantir compatibilidade com sistemas Android, iOS e Windows, são registradas rotas de detecção de conectividade restrita (como `/generate_204`, `/hotspot-detect.html`, `/ncsi.txt` e `onNotFound()`), as quais disparam a abertura automática do navegador no dispositivo móvel do usuário:

```cpp
void PortalManager::setupRoutes() {
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/save", [this]() { handleSave(); });

    // Endpoints de detecção de Captive Portal
    _server.on("/generate_204", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/ncsi.txt", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/connecttest.txt", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/redirect", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/success.txt", HTTP_GET, [this]() { handleRoot(); });

    _server.onNotFound([this]() { handleNotFound(); });
}

void PortalManager::handleNotFound() {
    _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    _server.send(302, "text/plain", "");
}
```

A interface web é construída dinamicamente pelo método `buildHtmlPage()`, renderizando um formulário responsivo com campos de configuração específicos para o modo **Satélite** (endereço MAC de destino do enlace ESP-NOW) ou modo **Matriz/Gateway** (SSID, senha da rede Wi-Fi, chave `X-Sensor-Key` e URL do endpoint REST).

#### 1.2 Recepção, Validação e Desligamento Seguro do Ponto de Acesso

Ao submeter o formulário (`handleSave`), os parâmetros são validados (formato do MAC, tamanho das credenciais) e gravados no armazenamento permanente. Em seguida, o AP é encerrado com `WiFi.softAPdisconnect(true)` para economizar energia e eliminar a superfície de ataque por rádio:

```cpp
void PortalManager::handleSave() {
    if (!_server.hasArg("mode")) {
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
            _server.send(400, "text/html", "<h3>Erro: Formato de Endereco MAC invalido.</h3>");
            return;
        }
    } else if (modeVal == MODE_MATRIX) {
        cfg.mode = MODE_MATRIX;
        String ssid = _server.hasArg("ssid") ? _server.arg("ssid") : "";
        String pass = _server.hasArg("password") ? _server.arg("password") : "";
        String url  = _server.hasArg("url") ? _server.arg("url") : "";
        String key  = _server.hasArg("api_key") ? _server.arg("api_key") : "";

        ssid.trim(); pass.trim(); url.trim(); key.trim();
        if (ssid.length() == 0) {
            _server.send(400, "text/html", "<h3>Erro: SSID obrigatorio.</h3>");
            return;
        }

        strncpy(cfg.wifiSsid, ssid.c_str(), sizeof(cfg.wifiSsid) - 1);
        strncpy(cfg.wifiPassword, pass.c_str(), sizeof(cfg.wifiPassword) - 1);
        strncpy(cfg.serverUrl, url.length() > 0 ? url.c_str() : DEFAULT_SERVER_URL, sizeof(cfg.serverUrl) - 1);
        strncpy(cfg.apiKey, key.c_str(), sizeof(cfg.apiKey) - 1);
    }

    cfg.sampleIntervalSec = DEFAULT_SAMPLE_SEC;

    if (_storage.saveConfig(cfg)) {
        _pendingConfig = cfg;
        _configSaved = true;
        _server.send(200, "text/html", "Configuracao Salva com Sucesso!");
    } else {
        _server.send(500, "text/html", "<h3>Erro ao salvar na memoria NVS.</h3>");
    }
}
```

#### 1.3 Persistência em Memória Não-Volátil (NVS - Non-Volatile Storage)

A persistência é gerenciada pela classe `StorageManager` através da biblioteca `Preferences.h`, que faz uso da partição de NVS com mecanismo de *wear-leveling* (nivelamento de desgaste dos blocos de flash), eliminando a vulnerabilidade de degradação precoce da memória associada à biblioteca clássica `EEPROM.h`.

```cpp
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

bool StorageManager::loadConfig(DeviceConfig &config) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, true)) {
        return false;
    }

    config.mode = static_cast<SystemMode>(prefs.getUChar("mode", MODE_UNCONFIGURED));
    String ssid = prefs.getString("wifi_ssid", "");
    String pass = prefs.getString("wifi_pass", "");
    String url  = prefs.getString("server_url", DEFAULT_SERVER_URL);
    String key  = prefs.getString("api_key", "");

    strncpy(config.wifiSsid, ssid.c_str(), sizeof(config.wifiSsid) - 1);
    strncpy(config.wifiPassword, pass.c_str(), sizeof(config.wifiPassword) - 1);
    strncpy(config.serverUrl, url.c_str(), sizeof(config.serverUrl) - 1);
    strncpy(config.apiKey, key.c_str(), sizeof(config.apiKey) - 1);

    prefs.getBytes("target_mac", config.targetMac, 6);
    config.sampleIntervalSec = prefs.getUShort("sample_sec", DEFAULT_SAMPLE_SEC);
    prefs.end();

    if (config.mode == MODE_SATELLITE) {
        // Valida se o MAC de destino não é nulo
        for (int i = 0; i < 6; i++) {
            if (config.targetMac[i] != 0) return true;
        }
        return false;
    } else if (config.mode == MODE_MATRIX) {
        return (strlen(config.wifiSsid) > 0);
    }
    return false;
}
```

---

### 2. Aquisição de Dados dos Sensores e Formulação Matemática

O subsistema de sensoriamento é composto por três transdutores integrados de forma totalmente assíncrona: o sensor de dióxido de carbono MH-Z19C, o sensor de material particulado DSM501A e o sensor termo-higrométrico DHT22.

---

#### 2.1 Sensor de Dióxido de Carbono (CO2) MH-Z19C

O sensor MH-Z19C baseia-se na espectroscopia de infravermelho não-dispersivo (*Non-Dispersive Infrared* - NDIR). A comunicação ocorre via porta serial de hardware do ESP32 (`UART2`, pinos GPIO16 para RX e GPIO17 para TX) a 9600 bps.

##### Frame de Requisição e Resposta
A cada 5000 ms, o ESP32 transmite o comando padrão de 9 bytes:
$$\text{CMD} = [0\text{xFF},\, 0\text{x01},\, 0\text{x86},\, 0\text{x00},\, 0\text{x00},\, 0\text{x00},\, 0\text{x00},\, 0\text{x00},\, 0\text{x79}]$$

A resposta é processada sem bloqueio em `MHZ19CDriver.cpp`. O cálculo do *Checksum* de integridade e a reconstrução da concentração de $\text{CO}_2$ em partes por milhão (ppm) utilizam as seguintes equações:

$$\text{Checksum Esperado} = \left( 0\text{xFF} - \sum_{i=1}^{7} \text{Byte}[i] \right) + 1$$

$$\text{Concentração}_{\text{CO}_2} = (\text{Byte}[2] \times 256) + \text{Byte}[3] = (\text{Byte}[2] \ll 8) \mid \text{Byte}[3]$$

Adicionalmente, a razão de saturação da ventilação relativa ao Valor Máximo Recomendável (VMR) de $1000\text{ ppm}$ estabelecido pela norma **ANVISA RE nº 09/2003** para Qualidade do Ar Interior (QAI) é calculada por:

$$\text{Razão de Saturação de Ventilação (\%)} = \left( \frac{\text{Concentração}_{\text{CO}_2}}{1000\,\text{ppm}} \right) \times 100$$

##### Trecho de Código (`MHZ19CDriver.cpp`):
```cpp
void MHZ19CDriver::sendReadCommand() {
    while (_serial.available()) { _serial.read(); }
    const uint8_t cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
    _serial.write(cmd, 9);
    _reqSentMs = millis();
    _awaitingReply = true;
}

bool MHZ19CDriver::parseResponse() {
    uint8_t buf[9];
    _serial.readBytes(buf, 9);

    if (buf[0] != 0xFF || buf[1] != 0x86) return false;

    uint8_t expectedChecksum = calculateChecksum(buf);
    if (buf[8] != expectedChecksum) return false;

    int ppm = (static_cast<int>(buf[2]) << 8) | buf[3];

    if (ppm >= 0 && ppm <= 50000) {
        _co2 = ppm;
        _valid = true;
        return true;
    }
    return false;
}

uint8_t MHZ19CDriver::calculateChecksum(const uint8_t *packet) {
    uint8_t sum = 0;
    for (int i = 1; i < 8; i++) {
        sum += packet[i];
    }
    return static_cast<uint8_t>(0xFF - sum + 1);
}

float MHZ19CDriver::getSaturacaoVentilacao() const {
    if (!_valid) return 0.0f;
    return (_co2 / ANVISA_CO2_VMR_PPM) * 100.0f; // ANVISA_CO2_VMR_PPM = 1000.0f
}
```

---

#### 2.2 Sensor de Material Particulado DSM501A ($\text{PM}_{2.5}$)

O sensor DSM501A detecta partículas em suspensão por espalhamento óptico (*light scattering*). A saída de sinal no pino GPIO33 opera por modulação de largura de pulso negativo (*Low Pulse Occupancy* - LPO). 

Para não paralisar o microcontrolador com funções como `pulseIn()`, foi implementada uma **rotina de serviço de interrupção por hardware (ISR)** na transição de borda (`CHANGE`), acumulando a contagem de microssegundos em nível lógico baixo via `micros()`.

##### Formulação Matemática e Calibração Física
1. **Taxa de Ocupação em Nível Baixo (LPO Ratio):**
   $$\text{Ratio}_{\text{LPO}} = \left( \frac{\Delta t_{\text{Low}}}{\Delta T_{\text{Janela}}} \right) \times 100$$
   Em que $\Delta T_{\text{Janela}} = 30000\text{ ms}$ (30 segundos) e $\Delta t_{\text{Low}}$ é o somatório do tempo em nível lógico baixo na janela.

2. **Equação Polinomial de Concentração Numérica:**
   A curva de resposta do sensor correlaciona a taxa percentual de LPO à contagem de partículas em unidades de partículas por $0,01\text{ pé cúbico}$ ($\text{pcs} / 0,01\text{ cf}$), dada pelo polinômio de terceiro grau (datasheet DSM501A e calibração experimental):
   $$N_p = 1{,}1 \cdot (\text{Ratio}_{\text{LPO}})^3 - 3{,}8 \cdot (\text{Ratio}_{\text{LPO}})^2 + 520{,}0 \cdot (\text{Ratio}_{\text{LPO}}) + 0{,}62 \quad [\text{pcs} / 0{,}01\text{ cf}]$$

3. **Conversão Volumétrica e Gravimétrica ($\mu\text{g/m}^3$):**
   A conversão de contagem de partículas para concentração em massa ($\text{PM}_{2.5}$) adota o modelo de partícula esférica equivalente ($r = 0{,}44\,\mu\text{m}$, densidade típica de aerossol $\rho = 1{,}65\text{ g/cm}^3 = 1{,}65 \times 10^{12}\,\mu\text{g/m}^3$):
   $$V_p = \frac{4}{3} \pi r^3 \approx \frac{4}{3} \pi (0{,}44 \times 10^{-6}\,\text{m})^3 \approx 3{,}5682 \times 10^{-19}\,\text{m}^3$$
   $$m_p = \rho \cdot V_p \approx (1{,}65 \times 10^{12}\,\mu\text{g/m}^3) \cdot (3{,}5682 \times 10^{-19}\,\text{m}^3) \approx 5{,}8875 \times 10^{-7}\,\mu\text{g}$$
   
   O fator de expansão volumétrica de $0{,}01\text{ cf}$ ($283{,}17\text{ mL}$) para $1\text{ m}^3$ é:
   $$K_{\text{vol}} = \frac{1}{0{,}000283168\,\text{m}^3} \approx 3531{,}5\,\text{m}^{-3}$$

   Dessa forma, a massa de $\text{PM}_{2.5}$ em microgramas por metro cúbico ($\mu\text{g/m}^3$) é calculada por:
   $$\text{PM}_{2.5}\,(\mu\text{g/m}^3) = N_p \times K_{\text{vol}} \times m_p = N_p \times 3531{,}5 \times (5{,}8875 \times 10^{-7})$$

##### Trecho de Código (`DSM501ADriver.cpp`):
```cpp
void DSM501ADriver::handleInterrupt() {
    int level = digitalRead(_signalPin);
    uint32_t nowUs = micros();

    if (level == LOW) {
        _pulseStartUs = nowUs;
    } else {
        if (_pulseStartUs > 0) {
            uint32_t diff = nowUs - _pulseStartUs;
            _accumulatedLowUs += diff;
            _pulseStartUs = 0;
        }
    }
}

void DSM501ADriver::update() {
    unsigned long now = millis();

    // Período obrigatório de aquecimento do elemento resistivo térmico (60s)
    if (now - _bootTimeMs < WARMUP_TIME_MS) return;

    if (now - _windowStartMs >= SAMPLE_INTERVAL_MS) {
        // Seção crítica para leitura atômica e reset dos contadores
        noInterrupts();
        uint32_t lowUs = _accumulatedLowUs;
        _accumulatedLowUs = 0;
        _pulseStartUs = 0;
        interrupts();

        unsigned long actualWindowMs = now - _windowStartMs;
        _windowStartMs = now;

        // Cálculo da razão percentual de Low Pulse Occupancy (LPO)
        float lowMs = lowUs / 1000.0f;
        float ratio = (lowMs / (float)actualWindowMs) * 100.0f;
        if (ratio > 100.0f) ratio = 100.0f;
        if (ratio < 0.0f) ratio = 0.0f;
        _lastRatio = ratio;

        // Curva polinomial característica (pcs / 0.01 cf)
        float particleCount = 0.0f;
        if (ratio > 0.0f) {
            particleCount = 1.1f * pow(ratio, 3) - 3.8f * pow(ratio, 2) + 520.0f * ratio + 0.62f;
            if (particleCount < 0.0f) particleCount = 0.0f;
        }

        // Conversão gravimétrica em massa (µg/m³)
        float pm25 = particleCount * VOLUMETRIC_FACTOR * PARTICLE_MASS_UG;
        if (pm25 >= 0.0f && pm25 <= 5000.0f) {
            _poeira = pm25;
            _valid = true;
        }
    }
}
```

---

#### 2.3 Sensor de Temperatura e Umidade Relativa DHT22

O sensor capacitivo DHT22 (comunicação *Single-Bus* no pino GPIO26) fornece leituras de temperatura e umidade relativa do ar. Devido à sua inércia física, a amostragem respeita um intervalo mínimo não-bloqueante de $2500\text{ ms}$ (acima do patamar de $2000\text{ ms}$ exigido pelo fabricante).

##### Trecho de Código (`DHT22Driver.cpp`):
```cpp
void DHT22Driver::update() {
    unsigned long now = millis();
    if (now - _lastReadMs < MIN_INTERVAL_MS) {
        return;
    }
    _lastReadMs = now;

    float t = _dht.readTemperature();
    float h = _dht.readHumidity();

    // Validação de sanidade e faixas físicas operacionais
    if (!isnan(t) && !isnan(h) && t >= -50.0f && t <= 100.0f && h >= 0.0f && h <= 100.0f) {
        _temperature = t;
        _humidity = h;
        _valid = true;
    } else {
        _valid = false;
        Serial.println("[DHT22] Aviso: Falha na leitura ou valor fora da faixa aceitavel.");
    }
}
```

---

### 3. Comunicação e Envio dos Dados Coletados via API REST

No nó Matriz/Gateway (`MODE_MATRIX`), os dados coletados localmente e os pacotes retransmitidos pelos nós Satélites via rádio ESP-NOW são submetidos à API em nuvem através do método HTTP POST no endpoint `/api/medicoes`.

#### 3.1 Resiliência de Conexão com Backoff Exponencial
Caso o roteador Wi-Fi fique inacessível, o módulo `WiFiManager.cpp` implementa um mecanismo de reconexão não-bloqueante com algoritmo de recuo exponencial (*Exponential Backoff*). O intervalo entre tentativas parte de $5000\text{ ms}$ e dobra a cada falha até atingir o teto de $60000\text{ ms}$, evitando sobrecarga desnecessária na CPU:

```cpp
void WiFiManager::update() {
    bool currentStatus = (WiFi.status() == WL_CONNECTED);

    if (currentStatus && !_isConnected) {
        _isConnected = true;
        _reconnectIntervalMs = INITIAL_RECONNECT_MS; // Reseta backoff para 5s
    } else if (!currentStatus && _isConnected) {
        _isConnected = false;
        Serial.println("[WIFI] Conexao perdida. Iniciando reconexao com backoff.");
    }

    if (!_isConnected) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= _reconnectIntervalMs) {
            attemptReconnect();
            // Exponential Backoff
            _reconnectIntervalMs *= 2;
            if (_reconnectIntervalMs > MAX_RECONNECT_MS) {
                _reconnectIntervalMs = MAX_RECONNECT_MS; // Teto de 60s
            }
        }
    }
}
```

#### 3.2 Montagem do Payload e Despacho HTTP POST
A função `WiFiManager::sendTelemetry` realiza o envio. Para prevenir a fragmentação da memória Heap do microcontrolador (causa primária de *crashes* e *kernel panics* em dispositivos ESP32), a formatação do JSON dispensa o uso de objetos `String` dinâmicos concatenados, sendo realizada em um *buffer* estático em pilha (`char jsonPayload[256]`) por meio de `snprintf`.

A requisição inclui controle de fluxo (*rate limiting* de no máximo 1 requisição/segundo), cabeçalhos de aceitação de resposta e o cabeçalho seguro de autenticação `X-Sensor-Key`.

##### Trecho de Código (`WiFiManager.cpp`):
```cpp
bool WiFiManager::sendTelemetry(const char *macAddress, float temp, float umid, int co2, float poeira) {
    if (!_isConnected) {
        Serial.println("[HTTP Telemetria] Wi-Fi desconectado. Envio abortado.");
        return false;
    }

    // Controle de vazão (Rate Limiting: intervalo mínimo de 1000 ms)
    unsigned long now = millis();
    if (now - _lastHttpSendMs < MIN_HTTP_INTERVAL_MS) {
        Serial.println("[HTTP Telemetria] Aguardando controle de vazao.");
        return false;
    }

    HTTPClient http;
    http.setTimeout(DEFAULT_HTTP_TIMEOUT_MS); // 5000 ms

    if (!http.begin(_serverUrl)) {
        Serial.println("[HTTP Telemetria] ERRO: Falha ao iniciar conexao com o endpoint.");
        return false;
    }

    // Cabeçalhos HTTP da API
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("X-Sensor-Key", _apiKey);

    // Buffer estático para mitigação de fragmentação de memória Heap
    char jsonPayload[256];
    snprintf(jsonPayload, sizeof(jsonPayload),
             "{\"mac_address\":\"%s\",\"temperatura\":%.2f,\"umidade\":%.2f,\"co2\":%d,\"poeira\":%.2f}",
             macAddress, temp, umid, co2, poeira);

    Serial.printf("[HTTP POST] Enviando telemetria (MAC: %s) para %s ...\n", macAddress, _serverUrl);

    int httpCode = http.POST(reinterpret_cast<uint8_t*>(jsonPayload), strlen(jsonPayload));
    _lastHttpSendMs = millis();

    bool success = false;
    if (httpCode == HTTP_CODE_CREATED) { // Resposta 201 Created
        Serial.println("[HTTP Resposta 201]: Medicao registrada com sucesso.");
        success = true;
    } else {
        Serial.printf("[HTTP Erro %d]: Falha no envio da telemetria.\n", httpCode);
    }

    http.end();
    return success;
}
```

#### 3.3 Orquestração pela Máquina de Estados (Gateway e Fila Circular)
Na função `handleMatrix()` da máquina de estados (`StateMachine.cpp`), os pacotes recebidos dos nós satélites são colocados em uma fila circular FIFO (`_rxQueue`) para posterior transmissão à API sem perder leituras locais:

```cpp
// 1. Processa leituras recebidas dos satélites via ESP-NOW na fila
EspNowPayload satPayload;
if (dequeuePayload(satPayload)) {
    _wifi.sendTelemetry(satPayload.stationMac,
                        satPayload.temperatura,
                        satPayload.umidade,
                        satPayload.co2,
                        satPayload.poeira);
}

// 2. Coleta e despacha medição dos sensores locais da própria Matriz
if (now - _lastSensorSampleMs >= intervalMs) {
    _lastSensorSampleMs = now;
    SensorData localData;
    if (_sensors.getSensorData(localData)) {
        uint8_t baseMac[6];
        char localMacStr[18];
        esp_wifi_get_mac(WIFI_IF_STA, baseMac);
        StorageManager::macToString(baseMac, localMacStr, sizeof(localMacStr));

        _wifi.sendTelemetry(localMacStr,
                            localData.temperatura,
                            localData.umidade,
                            localData.co2,
                            localData.poeira);
    }
}
```

---

### Resumo das Variáveis e Métricas do Sistema

| Sensor / Interface | Grandeza / Sinal | Princípio / Protocolo | Faixa Aceita na API | Fórmula Principal |
| :--- | :--- | :--- | :--- | :--- |
| **MH-Z19C** | Dióxido de Carbono ($\text{CO}_2$) | Óptico NDIR (UART 9600 bps) | $0 \text{ a } 50.000\text{ ppm}$ | $\text{ppm} = (\text{buf}[2] \ll 8) \mid \text{buf}[3]$ |
| **DSM501A** | Material Particulado ($\text{PM}_{2.5}$) | Espalhamento de Luz (LPO via ISR) | $0{,}0 \text{ a } 5.000{,}0\,\mu\text{g/m}^3$ | $N_p = 1{,}1(\%L)^3 - 3{,}8(\%L)^2 + 520(\%L) + 0{,}62$<br>$\text{PM}_{2.5} = N_p \cdot K_{\text{vol}} \cdot m_p$ |
| **DHT22** | Temperatura e Umidade | Sensor Capacitivo (Single-Bus) | $-50 \text{ a } 100\,^\circ\text{C}$<br>$0 \text{ a } 100\,\%$ | Leitura direta calibrada digitalmente |
| **HTTP Client** | Telemetria REST | HTTP POST (JSON em buffer estático) | Códigos 201 Created | Cabeçalho `X-Sensor-Key` e *Rate Limiting* de $1\text{ s}$ |
