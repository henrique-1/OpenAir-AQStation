# 🤖 Diretrizes do Projeto: Estação de Monitoramento de Qualidade do Ar (ESP32)

Este documento define as diretrizes de arquitetura, segurança e boas práticas de código para este projeto IoT. Agentes autônomos (como Google Antigravity) e desenvolvedores devem seguir estritamente estas regras durante refatorações, code-reviews e implementações.

## 1. Arquitetura e Padrões de Projeto (Clean Code)

- **Máquina de Estados (State Machine):** O sistema opera em três estados (`MODE_UNCONFIGURED`, `MODE_SATELLITE`, `MODE_MATRIX`). O fluxo principal (`loop`) não deve conter lógica complexa aninhada; delegue funções para _handlers_ específicos de cada estado.
- **Não-Bloqueante (Non-blocking):** É **ESTRITAMENTE PROIBIDO** o uso de `delay()` fora do setup inicial. Todas as temporizações no `loop()` devem usar `millis()` ou _Hardware Timers_. O ESP32 precisa processar o servidor web e manter o WiFi/ESP-NOW vivos sem interrupções.
- **Separação de Responsabilidades (SoC):** Lógicas de interface Web (Captive Portal), comunicação rádio (ESP-NOW/WiFi) e leitura de sensores (I2C/Serial) devem estar em funções/módulos separados.

## 2. Gerenciamento de Memória (Prevenção de Falhas)

- **Strings e Heap Fragmentation:** Evite a concatenação excessiva de objetos `String` do C++. Sempre que possível, utilize `char array` (C-strings estáticas) para bufferização ou use `String::reserve()` caso precise montar grandes blocos de texto dinâmicos (como JSONs).
- **Armazenamento Não-Volátil (NVS):** O armazenamento de credenciais deve ser feito exclusivamente pela biblioteca `Preferences.h`. O uso de `EEPROM.h` é proibido devido à ausência de _wear-leveling_ eficiente.

## 3. Segurança e Auditoria (Security & NVS)

- **Proteção de Credenciais:** SSIDs, senhas de Wi-Fi e endereços MAC jamais devem trafegar sem sanitização ou serem salvos em texto claro em logs (`Serial.print`).
- **Validação no Captive Portal:** Qualquer input recebido via requisição HTTP (`server.arg()`) deve ser tratado para evitar Buffer Overflows ou Injeções.
- **Rede e Roteamento:** O modo AP (Access Point) do Captive Portal deve ser desligado explicitamente logo após a configuração bem-sucedida, minimizando a superfície de ataque físico.

## 4. Resiliência e Tolerância a Falhas (IoT Resiliency)

- **Watchdog Timer (WDT):** Operações pesadas (como reconexões de WiFi ou tentativas de leitura de sensores bloqueantes) devem ceder processamento usando `yield()` ou `vTaskDelay()` para não estourar o WDT do ESP32.
- **Reconexão Inteligente (Backoff):** Caso a estação Matriz perca o sinal do WiFi, ela não deve travar. Implemente rotinas assíncronas para tentar reconectar periodicamente sem parar de escutar as estações Satélites via ESP-NOW.
- **Tratamento de Retornos:** Sempre valide o `esp_err_t` retornado por funções da API nativa da Espressif (ex: `esp_now_init()`, `esp_now_send()`). Nunca presuma que um envio via rádio foi bem-sucedido.

## 5. Integração com Antigravity Skills

As ferramentas instaladas (`state-machine-linter`, `nvs-security-audit`, `iot-resiliency-check`, etc.) farão uso destas diretrizes. Ao sugerir refatorações, os agentes devem priorizar estabilidade em longo prazo e baixíssimo consumo de recursos.
