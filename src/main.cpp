#include <Arduino.h>
#include "Config.h"
#include "StateMachine.h"

static StateMachine stateMachine;

void setup() {
    Serial.begin(115200);

    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 1500)) {
        // Aguarda estabilização do canal serial no boot
    }

    Serial.println("\n\n================================================");
    Serial.println("  OpenAir - Estacao de Qualidade do Ar (ESP32)  ");
    Serial.println("================================================");

    stateMachine.begin();
}

void loop() {
    stateMachine.update();
}