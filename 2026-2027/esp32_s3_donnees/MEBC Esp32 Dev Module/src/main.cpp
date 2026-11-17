// Point d'entree Arduino : uniquement le lien avec le materiel.
// Toute la logique est en C dans app.c.

#include <Arduino.h>
#include <stdarg.h>

#include "app.h"
#include "broches.h"
#include <protocole.h>

static HardwareSerial& portPi = Serial2;

extern "C" {

uint32_t plateforme_millis(void) { return millis(); }

void plateforme_ecrire(const uint8_t* data, size_t len) { portPi.write(data, len); }

size_t plateforme_place_tx(void) { return static_cast<size_t>(portPi.availableForWrite()); }

void plateforme_attendre_fin_emission(void) { portPi.flush(); }

void plateforme_changer_vitesse(uint32_t vitesse) { portPi.updateBaudRate(vitesse); }

void plateforme_redemarrer(void) {
    delay(20);
    ESP.restart();
}

void plateforme_log(const char* format, ...) {
    char ligne[160];
    va_list args;
    va_start(args, format);
    vsnprintf(ligne, sizeof ligne, format, args);
    va_end(args);
    Serial.print(ligne);
}

}  // extern "C"

void setup() {
    Serial.begin(115200);  // USB : logs de debug
    // Tampons agrandis pour le haut debit (a regler AVANT begin)
    portPi.setRxBufferSize(4096);
    portPi.setTxBufferSize(8192);
    portPi.begin(PROTO_VITESSE_DEFAUT, SERIAL_8N1, PIN_UART_PI_RX, PIN_UART_PI_TX);
    delay(100);
    app_init();
}

void loop() {
    uint8_t tampon[256];
    int dispo;
    while ((dispo = portPi.available()) > 0) {
        const size_t n = portPi.read(tampon, dispo < static_cast<int>(sizeof tampon) ? dispo : sizeof tampon);
        app_octets_recus(tampon, n);
    }
    app_tick();
}
