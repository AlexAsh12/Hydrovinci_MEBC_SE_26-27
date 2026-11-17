// Execute les tests du protocole sur l'ESP32 et affiche le resultat
// sur le port USB (relance toutes les 5 s pour pouvoir le lire a tout moment).
#include <Arduino.h>

extern "C" int test_protocole_executer(void);

void setup() {
    Serial.begin(115200);
    delay(500);
}

void loop() {
    const unsigned long debut = micros();
    const int echecs = test_protocole_executer();
    fflush(stdout);
    Serial.printf("=== TESTS %s (%d echec(s), %lu ms) ===\n\n", echecs == 0 ? "OK" : "EN ECHEC",
                  echecs, (micros() - debut) / 1000);
    delay(5000);
}
