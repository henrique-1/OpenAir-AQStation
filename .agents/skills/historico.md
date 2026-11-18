# Documentação Arquitetural e Histórico do Projeto: OpenAir-AQStation

Este documento centraliza as definições arquiteturais, a especificação das responsabilidades de cada componente e o log histórico cumulativo das sessões de desenvolvimento do firmware da estação de monitoramento da qualidade do ar (ESP32).

---

## 1. Visão Geral do Sistema e Arquitetura

O **OpenAir-AQStation** é um sistema embarcado para o microcontrolador ESP32 projetado para operar como um nó de sensoriamento distribuído de qualidade do ar em ambientes urbanos e escolares. O firmware foi concebido sob princípios estritos de modularidade, separação de responsabilidades (SoC) e programação totalmente **não-bloqueante** (sem uso de `delay()` no loop), permitindo o monitoramento simultâneo de múltiplos sensores, servidores web e pilhas de comunicação sem fio sem risco de travamento por Watchdog Timer (WDT).

### Padrão Arquitetural: Máquina de Estados Finitos (FSM)

O dispositivo opera em 3 estados mutuamente exclusivos:

1. **`MODE_UNCONFIGURED` (Modo Portal de Configuração)**: Ativado quando não há parâmetros válidos na memória não-volátil (NVS) ou quando o botão físico de boot é pressionado. O ESP32 inicializa como SoftAP (`OpenAir-Station-XXXX`) com Captive Portal via DNS e servidor HTTP na porta 80. O usuário acessa a página web embutida para definir o código de patrimônio e o papel da estação.
2. **`MODE_SATELLITE` (Modo Satélite Transmissor)**: A estação lê ciclicamente seus sensores ambientais locais e envia os pacotes compactos via **ESP-NOW** para o endereço MAC de destino configurado (outro satélite em cascata ou a Matriz).
3. **`MODE_MATRIX` (Modo Matriz / Gateway)**: A estação conecta-se a uma rede Wi-Fi local e sincroniza o rádio ESP-NOW no mesmo canal da rede. Ela recebe pacotes transmitidos pelas estações satélites (enfileirando-os em uma FIFO estática sem fragmentação de heap), realiza suas próprias medições locais e despacha os dados consolidados via requisições `HTTP POST` para o backend Laravel (`/api/medicoes`).

---

## 2. Topologia de Rede e Comunicação

```text
[Satélite A] (ESP-NOW) ──> [Satélite B] (ESP-NOW) ──> [Estação Matriz Gateway]
                                                               │
                                                       (Wi-Fi / HTTP POST)
                                                               ▼
                                                  [Servidor Nuvem (Laravel)]
```

- **Protocolo ESP-NOW**: Utilizado para interconexão direta entre placas (2.4 GHz) com payload binário estático de ~68 bytes (`struct EspNowPayload`), muito abaixo do limite de 250 bytes da Espressif.
- **Sincronização de Canal de Rádio**: No nó Matriz, o ESP32 utiliza um único transceptor RF. O canal do ESP-NOW é atrelado dinamicamente ao canal atribuído pelo Ponto de Acesso Wi-Fi.
- **Fila Estática na Matriz**: Uma fila circular de tamanho fixo (`RX_QUEUE_SIZE = 16`) alocada estaticamente na memória BSS garante que nenhum `malloc`/`free` ocorra em tempo de execução, prevenindo a fragmentação da memória Heap do ESP32.

---

## 3. Mapeamento de Hardware e Sensores

| Sensor          | Grandeza Medida                                | Pinos ESP32                       | Protocolo / Modo de Operação                                                                          |
| :-------------- | :--------------------------------------------- | :-------------------------------- | :---------------------------------------------------------------------------------------------------- |
| **DHT22**       | Temperatura (-50 a 100°C) e Umidade (0 a 100%) | `GPIO 26` (Data)                  | Barramento digital único, temporização não-bloqueante via `millis()`.                                 |
| **MH-Z19C**     | Dióxido de Carbono (0 a 50.000 ppm)            | `GPIO 16` (RX2) e `GPIO 17` (TX2) | Serial UART2 (`9600 baud, 8N1`), comando NDIR com validação de checksum.                              |
| **DSM501A**     | Material Particulado PM2.5 (0 a 5000 µg/m³)    | `GPIO 27` (Sinal / Vout2)         | Interrupção por hardware (`CHANGE`), cálculo de Low Pulse Occupancy (LPO) acumulado em janela de 30s. |
| **Status LED**  | Diagnóstico visual de estados e heartbeat      | `GPIO 2`                          | Pulso suave em portal (500ms), blink rápido a cada 2s em satélite e 1s em matriz.                     |
| **Botão Reset** | Factory reset / Formatação NVS                 | `GPIO 0` (BOOT)                   | Entrada com Pull-up interno; acionamento contínuo por 3 segundos limpa as configurações.              |

---

## 4. Integração com a API Laravel de Telemetria

- **Rota**: `POST /api/medicoes`
- **Cabeçalhos Obrigatórios**:
  - `Content-Type: application/json`
  - `Accept: application/json`
  - `X-Sensor-Key: <TELEMETRY_API_KEY>`
- **Corpo da Requisição**:
  ```json
  {
    "patrimonio": "OAir-Estacao-1-0001",
    "temperatura": 24.5,
    "umidade": 65.0,
    "co2": 450,
    "poeira": 12.3
  }
  ```
- **Controle de Vazão (Rate Limiting)**: A camada `WiFiManager` implementa controle de taxa assegurando que o limite de 60 requisições por minuto por IP seja respeitado.

---

## 5. Inventário de Arquivos e Responsabilidades

### Configurações e Núcleo

- **`platformio.ini`**: Configuração do ambiente PlatformIO para a placa `esp32doit-devkit-v1`, velocidade de monitor serial de 115200 bauds e dependências externas (`DHT sensor library`, `Adafruit Unified Sensor`, `ArduinoJson`).
- **`include/Config.h`**: Ponto único da verdade para mapeamento de pinos de hardware, enum de estados do sistema, constantes de temporização e definição das structs de dados (`DeviceConfig`, `SensorData` e `EspNowPayload`).
- **`src/main.cpp`**: Ponto de entrada padrão Arduino (`setup` e `loop`). Inicializa o console de diagnóstico e delega o loop inteiramente para `stateMachine.update()`, mantendo o código limpo e sem aninhamento de regras de negócio.

### Gerenciamento de Persistência e Portal

- **`include/StorageManager.h` e `src/StorageManager.cpp`**: Abstração da memória não-volátil NVS utilizando exclusivamente a biblioteca nativa `Preferences.h`. Gerencia salvamento, leitura, validação de integridade e formatação de credenciais, endereços MAC e código de patrimônio, além de impedir o vazamento de senhas em logs.
- **`include/PortalManager.h` e `src/PortalManager.cpp`**: Responsável pelo Captive Portal no modo não configurado. Sobe um SoftAP com servidor DNS e WebServer HTTP. Renderiza interface HTML responsiva, valida entradas de formulário e encerra explicitamente o rádio AP após o salvamento para proteção contra acessos indevidos.

### Orquestração da Máquina de Estados

- **`include/StateMachine.h` e `src/StateMachine.cpp`**: Gerenciador do ciclo de vida da placa. Controla as transições entre estados, monitora o botão de factory reset (3s) e executa os handlers específicos de cada modo (`handleUnconfigured`, `handleSatellite`, `handleMatrix`). Contém a fila estática de pacotes recebidos e invoca `yield()` para proteção do WDT.

### Sensores e Medição Física

- **`include/sensors/DHT22Driver.h` e `src/sensors/DHT22Driver.cpp`**: Driver não-bloqueante para leitura periódica de temperatura e umidade relativa com filtragem de valores `NaN` e validação de limites físicos.
- **`include/sensors/MHZ19CDriver.h` e `src/sensors/MHZ19CDriver.cpp`**: Driver serial assíncrono para o sensor óptico NDIR MH-Z19C. Envia comandos de interrogação a cada 5s e processa pacotes de resposta de 9 bytes checando paridade/checksum.
- **`include/sensors/DSM501ADriver.h` e `src/sensors/DSM501ADriver.cpp`**: Driver não-bloqueante de material particulado por interrupção de hardware (`attachInterrupt`). Mede o tempo em nível baixo (LPO) ao longo de janelas de 30 segundos e aplica curva polinomial para cálculo de concentração de massa (µg/m³), respeitando 1 minuto de aquecimento térmico do sensor.
- **`include/SensorManager.h` e `src/SensorManager.cpp`**: Fachada que agrega os três drivers de sensores, fornecendo uma interface única `getSensorData()` para consulta das grandezas atualizadas.

### Comunicação Sem Fio

- **`include/EspNowManager.h` e `src/EspNowManager.cpp`**: Camada de transporte para o protocolo ESP-NOW. Registra callbacks de transmissão e recepção com compatibilidade para o ESP32 Arduino Core 2.x e 3.x, registra pares (peers) dinamicamente e valida integridade de pacotes recebidos.
- **`include/WiFiManager.h` e `src/WiFiManager.cpp`**: Gerenciador de conexão Wi-Fi com rotina de reconexão assíncrona por Exponential Backoff. Implementa o cliente HTTP REST que despacha o payload JSON autenticado via `X-Sensor-Key` para o endpoint `/api/medicoes` do Laravel, tratando os códigos de retorno `201`, `401`, `404`, `422` e `429`.

---

## 6. Diretrizes de Segurança e Manutenibilidade

1. **Memória Estática (Zero Heap Fragmentation)**: Toda a montagem de payloads e buffers de transmissão usa `char[]` de tamanho fixo ou `snprintf`.
2. **Watchdog Timer (WDT)**: Nenhuma rotina bloqueia a execução por mais de alguns milissegundos. Chamadas frequentes a `yield()` garantem a alimentação do WDT.
3. **Isolamento de Credenciais**: Senhas de rede Wi-Fi e a chave secreta `X-Sensor-Key` são armazenadas na NVS protegida e mascaradas em qualquer saída serial de diagnóstico.
4. **Fechamento do AP de Configuração**: O rádio Wi-Fi em modo Access Point é desativado (`WiFi.softAPdisconnect(true)`) assim que a configuração inicial é persistida.

---

## 7. Histórico de Alterações e Log de Sessões (Changelog)

### Sessão: 01 de Outubro de 2026 (Estruturação Inicial do Projeto, Máquina de Estados e Integração de Sensores)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Definida e implementada a arquitetura completa do firmware da Estação de Monitoramento da Qualidade do Ar baseada em Máquina de Estados Finitos (FSM) com os modos `MODE_UNCONFIGURED`, `MODE_SATELLITE` e `MODE_MATRIX`.
- Desenvolvido o gerenciador de armazenamento persistente não-volátil (`StorageManager`) utilizando a biblioteca `Preferences.h` (NVS) para salvar o modo de operação, código de patrimônio, MAC de destino, credenciais Wi-Fi, chave de telemetria e URL da API.
- Implementado o Captive Portal (`PortalManager`) com servidor DNS e WebServer HTTP servindo interface responsiva para comissionamento em campo, desligando o AP automaticamente após o salvamento da configuração.
- Criados drivers modulares e 100% não-bloqueantes para todos os sensores especificados:
  - **DHT22**: Leitura periódica sem delay no `GPIO 26`.
  - **MH-Z19C**: Comunicação serial assíncrona NDIR via UART2 nos pinos `GPIO 16` (RX) e `GPIO 17` (TX) com cálculo de checksum.
  - **DSM501A**: Medição de Low Pulse Occupancy (LPO) no `GPIO 27` via interrupções de hardware (`attachInterrupt`) com cálculo de concentração em µg/m³ e proteção de aquecimento de 60 segundos.
- Desenvolvido o `EspNowManager` para transmissão leve ponto a ponto e topologia em cascata (mesh), com compatibilidade retroativa para ESP-IDF 4.x/5.x.
- Desenvolvido o `WiFiManager` com rotina de reconexão assíncrona (Exponential Backoff) e cliente HTTP POST integrado ao endpoint `/api/medicoes` do Laravel, com autenticação pelo cabeçalho `X-Sensor-Key` e controle de taxa.
- Integrado o `StateMachine` orquestrando leitura local, repasse de pacotes de satélites através de fila circular estática (zero fragmentação de heap) e monitoramento de botão de factory reset (BOOT mantido por 3s).

#### 2. Arquivos Modificados e Criados (Se aplicável)

- `platformio.ini`: Adicionados parâmetros `monitor_speed = 115200` e biblioteca `ArduinoJson`.
- `include/Config.h`: Definição de pinos dos sensores, enum de estados, structs `DeviceConfig`, `SensorData` e `EspNowPayload`.
- `include/StorageManager.h`: Interface do gerenciador de persistência NVS com `Preferences.h`.
- `src/StorageManager.cpp`: Implementação de leitura/gravação segura de credenciais, patrimônio e conversão de MAC.
- `include/PortalManager.h`: Interface do Captive Portal DNS/HTTP.
- `src/PortalManager.cpp`: Implementação do AP de configuração, rotas HTTP e página de comissionamento.
- `include/sensors/DHT22Driver.h`: Interface do driver do sensor DHT22.
- `src/sensors/DHT22Driver.cpp`: Implementação de leitura assíncrona do DHT22.
- `include/sensors/MHZ19CDriver.h`: Interface do driver UART do MH-Z19C.
- `src/sensors/MHZ19CDriver.cpp`: Implementação do driver assíncrono NDIR CO2 com checksum.
- `include/sensors/DSM501ADriver.h`: Interface do driver do DSM501A.
- `src/sensors/DSM501ADriver.cpp`: Implementação do cálculo de LPO via interrupção de hardware.
- `include/SensorManager.h`: Interface da fachada de sensores.
- `src/SensorManager.cpp`: Implementação do orquestrador de amostragem de dados ambientais.
- `include/EspNowManager.h`: Interface da pilha de comunicação ESP-NOW.
- `src/EspNowManager.cpp`: Implementação de envio, recebimento e registro de peers ESP-NOW.
- `include/WiFiManager.h`: Interface do cliente Wi-Fi e HTTP de telemetria.
- `src/WiFiManager.cpp`: Implementação de reconexão com backoff e envio de JSON para `/api/medicoes`.
- `include/StateMachine.h`: Interface da máquina de estados com fila estática de recepção.
- `src/StateMachine.cpp`: Implementação dos ciclos de vida e despacho de estados.
- `src/main.cpp`: Ponto de entrada modular delegando a execução para a FSM.
- `.agents/skills/historico.md`: Criação do documento central de histórico e detalhamento arquitetural.

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

### Sessão: 01 de Outubro de 2026 (Auditoria e Validação do Status dos Passos de Desenvolvimento)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Realizada auditoria completa do codebase para responder ao questionamento sobre o progresso das quatro etapas planejadas.
- Verificado que todos os 4 passos foram 100% implementados no código-fonte:
  - Passo 1 (Base e FSM): Concluído (`Config.h`, `StateMachine.h/.cpp`, `main.cpp`).
  - Passo 2 (NVS + Captive Portal): Concluído (`StorageManager.h/.cpp`, `PortalManager.h/.cpp` com campos de patrimônio e chave de API).
  - Passo 3 (Drivers de Sensores): Concluído (`DHT22Driver`, `MHZ19CDriver`, `DSM501ADriver` não-bloqueantes via hardware interrupt e `SensorManager`).
  - Passo 4 (ESP-NOW e Gateway HTTP): Concluído (`EspNowManager`, `WiFiManager` com backoff e HTTP POST para o endpoint Laravel `/api/medicoes`).
- Orientado o usuário sobre a prontidão do firmware para os testes práticos de compilação, gravação física (flash) e validação em bancada.

#### 2. Arquivos Modificados e Criados (Se aplicável)

- Nenhum arquivo modificado - Apenas documentação/consulta

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

### Sessão: 01 de Outubro de 2026 (Auditoria do main.cpp e Instruções de Compilação PlatformIO)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Auditado o arquivo `src/main.cpp`: identificado que ele ainda continha o template padrão de criação do PlatformIO (`myFunction`), sem nenhuma responsabilidade ligada ao projeto.
- Substituído o conteúdo de `src/main.cpp` pelo ponto de entrada oficial do firmware, cuja responsabilidade exclusiva é inicializar a porta serial e orquestrar a máquina de estados (`stateMachine.begin()` e `stateMachine.update()`), respeitando a Diretriz 1 do `AGENTS.md`.
- Documentado o guia passo a passo para compilação do projeto ESP32 utilizando a interface visual do VS Code (PlatformIO IDE) e via linha de comando (`pio run` / `~/.platformio/penv/bin/pio run`).

#### 2. Arquivos Modificados e Criados (Se aplicável)

- `src/main.cpp`: Substituição do template padrão pela inicialização e loop da `StateMachine`.

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

### Sessão: 01 de Outubro de 2026 (Correção de Sincronismo do Config.h e Resolução de Erros de Build)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Analisado o log de erro emitido durante a execução do comando `platformio run`.
- Diagnosticado que o arquivo `include/Config.h` no disco não estava sincronizado com as definições mais recentes exigidas pelos novos módulos (`patrimonio`, `poeira`, `PIN_DSM501A_SIGNAL`, `dhtValido`, `DEFAULT_SERVER_URL`).
- Atualizado e sincronizado integralmente o `include/Config.h` com a pinagem final dos sensores e os campos exatos do payload e structs de configuração.

#### 2. Arquivos Modificados e Criados (Se aplicável)

- `include/Config.h`: Sincronização dos nomes dos membros das structs (`SensorData`, `EspNowPayload`, `DeviceConfig`) e das constantes de pinos e URLs.

#### 3. Testes, Formatação e Compilação (Se aplicável)

- PlatformIO Build (Reportado pelo usuário): Corrigidos os erros de compilação em `EspNowManager.cpp`, `SensorManager.cpp` e `WiFiManager.cpp`.

### Sessão: 01 de Outubro de 2026 (Compilação e Geração do Binário do Firmware com Sucesso)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Validado o ciclo completo de build do projeto após sincronização das definições do `Config.h`.
- Compilação realizada com sucesso através do PlatformIO para o target `esp32doit-devkit-v1`.
- Estatísticas de consumo de recursos no microcontrolador:
  - RAM estática: 14.6% (47.992 bytes de 327.680 bytes disponíveis).
  - Flash ROM: 63.8% (836.569 bytes de 1.310.720 bytes disponíveis).
- Gerado com sucesso o arquivo de imagem do firmware `firmware.bin` pronto para upload na placa.

#### 2. Arquivos Modificados e Criados (Se aplicável)

- Nenhum arquivo modificado - Apenas documentação/consulta

#### 3. Testes, Formatação e Compilação (Se aplicável)

- `platformio run`: Sucesso absoluto (`[SUCCESS] Took 12.18 seconds`). Binário ELF e imagem binária gerados sem erros ou advertências impeditivas.

### Sessão: 01 de Outubro de 2026 (Gravação do Firmware no ESP32 Concluída com Sucesso)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Gravação do firmware realizada com sucesso via porta serial `/dev/ttyACM0` no microcontrolador ESP32-D0WD-V3 (MAC de fábrica detectado: `F0:24:F9:0E:6D:40`).
- O dispositivo reiniciou via hardware reset e está executando o firmware em modo de inicialização limpa (NVS não configurada).
- Instruído o usuário sobre o processo de monitoramento via Serial Monitor (`115200 baud`) e conexão ao Captive Portal via Wi-Fi para comissionamento inicial da estação.

#### 2. Arquivos Modificados e Criados (Se aplicável)

- Nenhum arquivo modificado - Apenas documentação/consulta

#### 3. Testes, Formatação e Compilação (Se aplicável)

- `platformio run --target upload`: Gravação física de 843.152 bytes na memória Flash concluída com sucesso (`[SUCCESS] Took 19.52 seconds`).

### Sessão: 01 de Outubro de 2026 (Consulta sobre Credenciais do Ponto de Acesso de Configuração)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Informadas as credenciais padrão do Ponto de Acesso Wi-Fi do Captive Portal configuradas no arquivo `include/Config.h`.
- SSID padrão: `OpenAir-Station-XXXX` (onde XXXX corresponde aos dois últimos bytes do endereço MAC da interface AP).
- Senha de acesso (WPA2-PSK): `openair123` (`DEFAULT_AP_PASSWORD`).
- Endereço IP padrão do gateway do portal: `192.168.4.1`.

#### 2. Arquivos Modificados e Criados (Se aplicável)

- Nenhum arquivo modificado - Apenas documentação/consulta

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

### Sessão: 01 de Outubro de 2026 (Diagnóstico da Execução do Portal e Tratamento de Rotas)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Analisado o log do terminal serial e a captura de tela enviada pelo usuário:
  1. Identificado que a placa em execução ainda estava rodando o binário antigo (a tela capturada não continha os campos de patrimônio e chave de API adicionados recentemente).
  2. Identificado que a placa re-enumerou de `/dev/ttyACM0` para `/dev/ttyACM1`, exigindo nova execução de upload.
  3. Esclarecido que as mensagens `_handleRequest(): request handler not found` observadas no terminal são sondagens automáticas que smartphones e computadores disparam em segundo plano ao detectar Wi-Fi sem internet (endpoints como `/connecttest.txt`, `/redirect`, etc.).
  4. Expandido o mapeamento de rotas em `src/PortalManager.cpp` para cobrir os probes conhecidos e configurada a rota `/save` para aceitar qualquer verbo HTTP (POST/GET), garantindo máxima resiliência com navegadores móveis.

#### 2. Arquivos Modificados e Criados (Se aplicável)

- `src/PortalManager.cpp`: Tratamento de sondagens de conectividade e flexibilização de rota `/save`.

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

### Sessão: 01 de Outubro de 2026 (Remoção do Campo Patrimônio do Portal e Identificação Automática via MAC)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Atendida a solicitação do usuário para remover o campo manual de cadastro de patrimônio da interface web.
- Configurado o ESP32 para obter de forma nativa e automática o seu próprio endereço MAC físico (`WIFI_IF_STA`) e utilizá-lo como o identificador primário (`patrimonio`) enviado na carga útil JSON para o endpoint `/api/medicoes` do Laravel.
- Corrigida a estrutura `EspNowPayload` para trafegar o `stationMac` (`char stationMac[18]`), permitindo que a Matriz identifique com precisão de qual satélite originou cada medição antes de reencaminhar ao backend.
- Refatorado o `StorageManager` para utilizar instâncias locais e atômicas de `Preferences` em cada método, eliminando permanentemente qualquer possibilidade de lock de handle ou erro de gravação na memória NVS.
- Atualizado o HTML do Captive Portal (`PortalManager`) para exibir em destaque o MAC da estação como identificador único, simplificando o processo de comissionamento em campo (o usuário agora só precisa definir o papel e as credenciais de rede/chave).

#### 2. Arquivos Modificados e Criados (Se aplicável)

- `include/Config.h`: Atualizada a struct `EspNowPayload` com o campo `stationMac[18]` e removida a obrigatoriedade de patrimônio manual.
- `include/StorageManager.h`: Remoção de variável de membro persistente do Preferences.
- `src/StorageManager.cpp`: Operações atômicas com `Preferences` local e recuperação automática de partições.
- `src/PortalManager.cpp`: Remoção do campo visual de patrimônio e atualização dos handlers de formulário.
- `src/EspNowManager.cpp`: Atualização dos logs e transporte do MAC de origem.
- `src/StateMachine.cpp`: Injeção automática do MAC físico da interface Station nas rotinas de telemetria dos modos Satélite e Matriz.

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

### Sessão: 01 de Outubro de 2026 (Detalhamento dos Dados Medidos e Diagnóstico de Conexão com o Servidor)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Explicadas ao usuário as 4 grandezas ambientais que o firmware coleta simultaneamente:
  1. **Temperatura** (em °C, via sensor DHT22 no GPIO 26).
  2. **Umidade Relativa** (em %, via sensor DHT22 no GPIO 26).
  3. **Dióxido de Carbono - CO2** (em ppm, via sensor NDIR MH-Z19C na UART2 GPIO 16/17).
  4. **Poeira / Material Particulado PM2.5** (em µg/m³, via sensor DSM501A por medição de Low Pulse Occupancy no GPIO 27).
- Aprimorados os logs seriais de telemetria em `StateMachine.cpp` e `WiFiManager.cpp` para exibir detalhadamente na tela o valor de cada sensor e a string JSON exata transmitida a cada ciclo de 30 segundos.
- Diagnosticado o motivo do erro `[HTTP Erro -1]: connection refused` na tentativa de conexão da Matriz: o servidor Laravel estava rodando com `php artisan serve` padrão, que escuta estritamente em `127.0.0.1` (localhost), recusando conexões de dispositivos externos da rede local (como o ESP32 em `10.181.84.6`). Orientado o usuário a iniciar o servidor com o argumento `--host=0.0.0.0`.

#### 2. Arquivos Modificados e Criados (Se aplicável)

- `src/StateMachine.cpp`: Exibição discriminada dos valores numéricos das 4 grandezas no terminal.
- `src/WiFiManager.cpp`: Log explícito do JSON exato que é despachado via HTTP POST.

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

### Sessão: 01 de Outubro de 2026 (Diagnóstico de Erro 422 na API e Comportamento Térmico do MH-Z19C)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Explicado e confirmado o comportamento físico do sensor óptico infravermelho NDIR MH-Z19C: ele necessita de 1 a 3 minutos de pré-aquecimento (_preheat_) após a inicialização para que a lâmpada/filtro térmico se estabilize e passe a fornecer medições reais de CO2 (valores nominais entre 400 e 1000 ppm em ambientes ventilados).
- Diagnosticada a causa do erro `[HTTP Erro 422]` no Laravel:
  1. A rota `POST /api/medicoes` do backend (`MedicaoApiController.php`) exigia obrigatoriamente os campos `patrimonio` ou `estacao_id`, não aceitando `mac_address` isoladamente.
  2. Atualizado o controller `MedicaoApiController.php` para aceitar `mac_address` como chave identificadora e localizar a estação no banco de dados via `where('mac_address', $macValor)`.
  3. Atualizado o `WiFiManager.cpp` para exibir no monitor serial o corpo exato da resposta JSON do servidor (`http.getString()`), evidenciando a mensagem interna caso a estação ainda esteja no status "Planejada".

#### 2. Arquivos Modificados e Criados (Se aplicável)

- `../OpenAir-Metrics/app/Http/Controllers/Api/MedicaoApiController.php`: Suporte a `mac_address` no validador e query de busca da estação.
- `src/sensors/MHZ19CDriver.cpp`: Adicionado feedback serial detalhado para leituras de CO2 e rotina de preaquecimento.
- `src/WiFiManager.cpp`: Exibição do payload JSON de erro retornado pela API Laravel.

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

### Sessão: 01 de Outubro de 2026 (Diagnóstico Detalhado da Falha de Leitura do Sensor NDIR MH-Z19C)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Analisado o comportamento do sensor MH-Z19C com base nos logs seriais do ESP32 (`CO2: 0 ppm` sem disparo de erros de checksum ou cabeçalho).
- Identificado que o buffer da UART2 permanece com 0 bytes (`_serial.available() == 0`), comprovando ausência de dados transmitidos pelo sensor para o ESP32.
- Detalhadas e documentadas as 4 causas principais para a ausência de resposta:
  1. **Inversão de TX/RX (Crossover Serial):** ESP32 GPIO 16 (RX2) deve ser ligado ao TXD do sensor; ESP32 GPIO 17 (TX2) deve ser ligado ao RXD do sensor.
  2. **Pinout Invertido no Conector JST de 7 Pinos:** Pelo datasheet oficial da Winsen, a pinagem é numerada da direita para a esquerda (Pino 1: HD na direita, Pino 7: PWM na esquerda; Pino 4: 5V, Pino 3: GND, Pino 5: RXD, Pino 6: TXD).
  3. **Queda de Tensão na Linha de 5V (Diodo Schottky da Placa ESP32):** O MH-Z19C exige tensão estrita de 4,9V a 5,1V. Alimentação via USB frequentemente resulta em 4,5V no pino VIN, desligando a câmara NDIR por subtensão.
  4. **Tempo de Pré-Aquecimento:** Necessidade de 1 a 3 minutos para estabilização térmica da lâmpada infravermelha.
- Aprimorado o driver `src/sensors/MHZ19CDriver.cpp` com log de diagnóstico em caso de timeout, informando a contagem de bytes residuais na serial e indicando visualmente as ações corretivas.
- Esclarecida a origem do erro HTTP 422 na API Laravel (estação no banco com `status_instalacao` diferente de 'Instalada').

#### 2. Arquivos Modificados e Criados (Se aplicável)

- `src/sensors/MHZ19CDriver.cpp`: Adicionado log de diagnóstico detalhado de timeout (bytes no buffer) e impressão dos bytes de cabeçalho corrompido, com liberação limpa de estado.

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

### Sessão: 01 de Outubro de 2026 (Atualização do Pino do DSM501A e Procedimento de Troca de Rede Wi-Fi)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Esclarecido ao usuário o procedimento de reconfiguração de rede Wi-Fi através do botão físico `BOOT` (GPIO 0):
  1. Manter o botão `BOOT` pressionado por 3 segundos para acionar a rotina `checkResetButton()`.
  2. O ESP32 apaga as configurações da memória NVS e entra no estado `MODE_UNCONFIGURED`.
  3. O Ponto de Acesso `OpenAir-Station-XXXX` é aberto novamente no IP `192.168.4.1`, permitindo configurar um novo SSID, senha e URL de API sem regravar o firmware.
- Verificadas as conexões físicas informadas pelo usuário:
  - **DSM501A:** Atualizado o pino de leitura de sinal/LPO de GPIO 27 para GPIO 33 (`D33`) em `include/Config.h`.
  - **DHT22:** Confirmada a correspondência com GPIO 26 (`D26`).
  - **MH-Z19C:** Confirmada a ligação cruzada TX -> RX2 (GPIO 16) e RX -> TX2 (GPIO 17).

#### 2. Arquivos Modificados e Criados (Se aplicável)

- `include/Config.h`: Modificada a macro `PIN_DSM501A_SIGNAL` de 27 para 33.

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

### Sessão: 02 de Outubro de 2026 (Correção de Fórmulas de PM2.5 do DSM501A, Saturação de CO2 e Auditoria de Telemetria da API)

#### 1. Resumo Executivo das Entregas (ou da Documentação)

- Realizada análise comparativa entre as diretrizes de cálculo do projeto e o firmware em execução para os sensores DSM501A ($PM_{2,5}$) e MH-Z19C ($CO_2$).
- Explicadas ao usuário as divergências encontradas antes de qualquer alteração de código:
  - **DSM501A:** O driver multiplicava a contagem por um fator genérico `0.045f` em vez de aplicar o fator volumétrico de escala ($3531{,}5$) e a massa teórica da partícula PM2.5.
  - **MH-Z19C:** O driver apenas lia os ppm brutos da serial sem calcular a Razão de Saturação de Ventilação conforme a norma ANVISA RE nº 09/2003 (VMR = 1000 ppm) nem fornecer o enquadramento de Qualidade do Ar Interior (QAI).
- Implementada a conversão correta em `DSM501ADriver`:
  - Definidas as constantes físicas de referência (Roy et al., 2017) para partícula PM2.5 ($r = 0{,}44\ \mu m$, $\rho = 1{,}65 \times 10^{12}\ \mu g/m^3$, $M_p \approx 5{,}8875 \times 10^{-7}\ \mu g$) e o fator volumétrico $3531{,}5$.
  - Aplicada a equação $PM_{2,5} = \text{Concentração} \times 3531{,}5 \times \text{Massa da Partícula}$.
- Implementados os cálculos de ventilação indoor em `MHZ19CDriver`:
  - Criado o enum `IndoorVentilationStatus` (Excelente $\le 700$ ppm, Aceitável $701-1000$ ppm, Inadequada $1001-1500$ ppm, Crítico $> 1500$ ppm).
  - Implementado o método `getSaturacaoVentilacao()` que calcula $\left(\frac{C_{CO_2}}{1000}\right) \times 100$.
  - Implementados métodos de descrição textual estática (`const char*`) sem alocações dinâmicas na heap, propagados via `SensorManager`.
- Mantido o cálculo do IQAr (CONAMA 506/2024) sob responsabilidade do servidor receptor, conforme orientação do usuário.
- Listados e documentados todos os 5 campos do payload JSON (`mac_address`, `temperatura`, `umidade`, `co2`, `poeira`) e os cabeçalhos HTTP (`X-Sensor-Key`, `Content-Type`, `Accept`) enviados via `POST` pelo `WiFiManager` para a API Laravel.

#### 2. Arquivos Modificados e Criados (Se aplicável)

- `include/sensors/DSM501ADriver.h`: Declaração das constantes `VOLUMETRIC_FACTOR` ($3531{,}5$) e `PARTICLE_MASS_UG` ($5{,}8875 \times 10^{-7}\ \mu g$).
- `src/sensors/DSM501ADriver.cpp`: Aplicação da fórmula completa de conversão de concentração de partículas para massa ($PM_{2,5}$ em $\mu g/m^3$).
- `include/sensors/MHZ19CDriver.h`: Declaração do enum `IndoorVentilationStatus`, constante `ANVISA_CO2_VMR_PPM` e métodos `getSaturacaoVentilacao`, `getVentilationStatus` e `getVentilationStatusDescription`.
- `src/sensors/MHZ19CDriver.cpp`: Implementação dos cálculos de razão de saturação de CO2 (%) e status de ventilação da ANVISA.
- `include/SensorManager.h`: Adição de métodos de conveniência para expor a saturação e o status de ventilação indoor.
- `src/StateMachine.cpp`: Atualização do log serial da estação Matriz para exibir o diagnóstico de saturação de CO2 e status de ventilação junto às medições locais.

#### 3. Testes, Formatação e Compilação (Se aplicável)

- N/A

