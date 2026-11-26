#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// Máquina de Estados do Sistema (AGENTS.md Diretriz 1)
// =============================================================================
enum SystemMode : uint8_t {
    MODE_UNCONFIGURED = 0,  // Modo Ponto de Acesso (Captive Portal de Configuração)
    MODE_SATELLITE    = 1,  // Modo Satélite (ESP-NOW transmissor para o próximo nó / Matriz)
    MODE_MATRIX       = 2   // Modo Matriz (Gateway: ESP-NOW receptor + WiFi + HTTP Laravel)
};

// =============================================================================
// Definições de Hardware e Pinos (Conexões do Projeto)
// =============================================================================
// Sensor DHT22 (Temperatura e Umidade)
// Pino 1: 3.3V | Pino 2: Data -> GPIO26 | Pino 3: GND
#define PIN_DHT22           26

// Sensor MH-Z19C (Dióxido de Carbono NDIR via UART2)
// Vin: 5V (VIN) | GND: GND
// Pin 5: RXD <- ESP32 GPIO17 (TX2)
// Pin 6: TXD -> ESP32 GPIO16 (RX2)
#define PIN_MHZ19C_RX       16  // ESP32 RX2 (Recebe do TX do MH-Z19C)
#define PIN_MHZ19C_TX       17  // ESP32 TX2 (Envia para o RX do MH-Z19C)
#define MHZ19C_BAUDRATE     9600

// Sensor DSM501A (Material Particulado / Poeira PM2.5)
// Pino 1: GND | Pino 3: 5V (VIN) | Pino 4: Sinal -> GPIO33 (D33)
#define PIN_DSM501A_SIGNAL  33  // Sinal de pulso (Low Pulse Occupancy) na porta D33

// Interface de Operação e Diagnóstico
#define PIN_STATUS_LED      2   // LED onboard
#define PIN_RESET_BUTTON    0   // Botão BOOT do ESP32 para Factory Reset

// =============================================================================
// Constantes de Tempo e Configurações Padrão
// =============================================================================
#define RESET_BUTTON_HOLD_MS    3000   // Segurar botão BOOT por 3s para resetar NVS
#define DEFAULT_SAMPLE_SEC      30     // Janela padrão de amostragem (ideal para o DSM501A LPO)
#define DEFAULT_AP_SSID_PREFIX  "OpenAir-Station-"
#define DEFAULT_SERVER_URL      "https://openair.bissoli.tech/api/medicoes"
#define DEFAULT_HTTP_TIMEOUT_MS 5000   // Timeout para requisição HTTP ao Laravel

// =============================================================================
// Estruturas de Dados do Sistema
// =============================================================================

// Configuração persistente gravada na NVS (Preferences)
struct DeviceConfig {
    SystemMode mode;
    uint8_t    targetMac[6];     // Endereço MAC de destino no modo Satélite
    char       wifiSsid[33];     // SSID da rede Wi-Fi (modo Matriz)
    char       wifiPassword[65]; // Senha da rede Wi-Fi (modo Matriz)
    char       serverUrl[128];   // URL da API Laravel (/api/medicoes)
    char       apiKey[65];       // Chave do cabeçalho X-Sensor-Key
    uint16_t   sampleIntervalSec;// Intervalo de leitura/envio em segundos
};

// Dados lidos dos sensores locais
struct SensorData {
    float    temperatura;        // °C (DHT22: -50.0 a 100.0)
    float    umidade;            // % (DHT22: 0.0 a 100.0)
    int      co2;                // ppm (MH-Z19C: 0 a 50000)
    float    poeira;             // µg/m³ (DSM501A: 0.0 a 5000.0)
    bool     dhtValido;
    bool     mhzValido;
    bool     dsmValido;
    uint32_t timestamp;          // millis() da medição
};

// Carga útil de transmissão via ESP-NOW (Limite de 250 bytes)
// Contém o MAC Address físico da estação originária para identificação única na API
#pragma pack(push, 1)
struct EspNowPayload {
    char     stationMac[18];     // Endereço MAC da estação originária ("AA:BB:CC:DD:EE:FF")
    uint32_t packetId;          // Contador sequencial para rastreabilidade
    float    temperatura;        // °C
    float    umidade;            // %
    int      co2;                // ppm
    float    poeira;             // µg/m³
    uint8_t  hops;               // Contador de saltos para topologia em cascata
};
#pragma pack(pop)

#endif // CONFIG_H

