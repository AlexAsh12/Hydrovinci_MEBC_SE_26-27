/*
-----------------------But : récupérer les données d'un BMS-----------------------


Trame hexadécimale envoyée sur les broches RX et TX de la ESP32 --> lire sur ces broches
J'ai oublié le formatage de la trame...

C++ : plus rapide, plus fiable, plus répandu

Note : trouver un moyen autre que d'utiliser sleep() et des fonctions async pour notre solution

*/

#include <iostream>
#include <stdint.h>

using namespace std;

//entraînement hahaaha
int main(){
    cout <<"Bonjour tout le monde"<< endl;

    return 0;
}

struct dataBMS
{
    float tension_bat;
    float amp_bat;
    uint8_t pourcent_bat;
    int8_t temp_bat;
};

//Lire broche RX(input) pour les sorties des bms
//Possiblement envoyer par TX(output) si le bms a besoin d'une activation

//framework arduino setup-loop ou esp-idf ? Avec tous les fichiers uart.h à importer

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#define UART_BMS        UART_NUM_2
#define UART_PI         UART_NUM_0 // Lien USB/UART vers le Raspberry Pi Zero
#define PIN_TX_BMS      GPIO_NUM_17
#define PIN_RX_BMS      GPIO_NUM_16
#define PIN_DE_RE       GPIO_NUM_4
#define NOMINAL_CAP_AH  100.0f     // À adapter selon ta batterie

struct DonneesBMS {
    uint8_t id;
    uint8_t numCells;
    float cells[24];       // Jusqu'à 24 cellules en série
    uint8_t numTemps;
    float temps[8];
    float voltage;
    float currentA;
    float capacityAh;
    float soc;
    bool connecte;
    bool alerteCritique;
};

// Fonction utilitaire : convertit 'len' caractères ASCII hexadécimaux en entier
uint32_t hexToInt(const char* str, uint8_t len) {
    char buf[9] = {0};
    if (len > 8) len = 8;
    memcpy(buf, str, len);
    return (uint32_t)strtoul(buf, NULL, 16);
}

// Équivalent C++ optimisé et sécurisé de decode_xdz_frame()
bool decode_xdz_frame(const char* rawFrame, DonneesBMS* out) {
    // 1. Recherche du caractère de début '~'
    const char* start = strchr(rawFrame, '~');
    if (!start) return false;
    start++; // On saute le '~'

    size_t len = strlen(start);
    if (len < 24) return false; // Trame trop courte

    // 2. Extraction du nombre de cellules (index 16 à 18)
    out->numCells = (uint8_t)hexToInt(&start[16], 2);
    if (out->numCells == 0 || out->numCells > 24) return false;

    size_t ptr = 18;
    float vMin = 99.0f, vMax = 0.0f;

    for (uint8_t i = 0; i < out->numCells; i++) {
        if (ptr + 4 > len) return false;
        float vCell = hexToInt(&start[ptr], 4) / 1000.0f;
        out->cells[i] = vCell;
        if (vCell < vMin) vMin = vCell;
        if (vCell > vMax) vMax = vCell;
        ptr += 4;
    }

    // 3. Extraction des températures
    if (ptr + 2 > len) return false;
    out->numTemps = (uint8_t)hexToInt(&start[ptr], 2);
    if (out->numTemps > 8) return false;
    ptr += 2;

    for (uint8_t i = 0; i < out->numTemps; i++) {
        if (ptr + 4 > len) return false;
        int32_t tRaw = (int32_t)hexToInt(&start[ptr], 4);
        out->temps[i] = (tRaw - 2731) / 10.0f;
        ptr += 4;
    }

    // 4. Courant (int16_t gère automatiquement le complément à 2 en C++ !)
    if (ptr + 12 > len) return false;
    int16_t currRaw = (int16_t)hexToInt(&start[ptr], 4);
    out->currentA = currRaw / 100.0f;
    ptr += 4;

    // 5. Tension totale, capacité et SOC
    out->voltage = hexToInt(&start[ptr], 4) / 1000.0f;
    ptr += 4;
    out->capacityAh = (hexToInt(&start[ptr], 4) * 10) / 1000.0f;
    out->soc = (out->capacityAh / NOMINAL_CAP_AH) * 100.0f;

    // 6. Détection d'alarme immédiate sur l'ESP32
    float deltaMv = (vMax - vMin) * 1000.0f;
    out->alerteCritique = (deltaMv > 100.0f || vMin < 3.0f);
    out->connecte = true;

    return true;
}

// Tâche temps réel ESP32 : interroge les BMS à 5 Hz et transmet au Pi Zero
void tache_passerelle_bms(void *pvParameters) {
    const char* reqBMS1 = "~01014642E00201FD36\r";
    // Note : pour BMS_2 sur le même bus RS485, l'adresse change (ex: ~01024642E00202...)
    char rxBuffer[256];
    DonneesBMS bms1 = { .id = 1 };

    while (true) {
        // Vide le buffer RX avant d'envoyer une nouvelle requête
        uart_flush_input(UART_BMS);
        
        // Envoi de la requête RS485
        uart_write_bytes(UART_BMS, reqBMS1, strlen(reqBMS1));

        // Attente non-bloquante (pour les autres tâches) de la réponse (timeout 120 ms)
        int rxLen = uart_read_bytes(UART_BMS, (uint8_t*)rxBuffer, sizeof(rxBuffer) - 1, pdMS_TO_TICKS(120));

        if (rxLen > 0) {
            rxBuffer[rxLen] = '\0'; // Termine la chaîne C
            if (!decode_xdz_frame(rxBuffer, &bms1)) {
                bms1.connecte = false;
            }
        } else {
            bms1.connecte = false;
        }

        // Envoi formaté (JSON compact) vers le Raspberry Pi Zero via UART/USB
        printf("{\"id\":%d,\"conn\":%d,\"v\":%.2f,\"i\":%.2f,\"soc\":%.1f,\"alert\":%d}\n",
               bms1.id, bms1.connecte, bms1.voltage, bms1.currentA, bms1.soc, bms1.alerteCritique);

        // Cadence de 150 ms (~6.6 Hz) sans bloquer le reste de l'ESP32
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}