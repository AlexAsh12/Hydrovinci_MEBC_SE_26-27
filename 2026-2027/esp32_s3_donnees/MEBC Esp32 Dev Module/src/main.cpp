// ESP32 donnees - firmware de TEST de la liaison UART avec la Pi.
// Protocole : 2026-2027/shared/PROTOCOLE_UART.md
//
// Envoie a la Pi :
//   - "hello" au demarrage et sur demande ("info")
//   - "hb" toutes les secondes
//   - un "bms" FACTICE toutes les secondes (teste les trames longues)
// Repond aux messages de la Pi : "ping", "info", "cmd" (reboot).
//
// Le port USB (Serial) affiche ce qui se passe, pour le debug.

#include <Arduino.h>
#include <protocole_uart.h>

#include "broches.h"

namespace {

const char* const VERSION_FW = "0.1.0-test-liaison";
#if defined(CARTE_WROOM32E)
const char* const NOM_CARTE = "esp32-wroom32e";
#else
const char* const NOM_CARTE = "esp32-s3";
#endif

HardwareSerial& portPi = Serial2;
protocole_uart::Liaison liaison(portPi);

unsigned long dernierEnvoiMs = 0;
bool piVue = false;

// --- Lecture minimale des messages de la Pi (sans librairie JSON) ---
// Les messages de la Pi sont compacts : {"t":"ping","n":12}

// Copie la valeur texte du champ `cle` dans `sortie`. Renvoie false si absent.
bool lireTexte(const char* json, const char* cle, char* sortie, size_t taille) {
    char motif[24];
    snprintf(motif, sizeof motif, "\"%s\":\"", cle);
    const char* debut = strstr(json, motif);
    if (!debut) return false;
    debut += strlen(motif);
    const char* fin = strchr(debut, '"');
    if (!fin || static_cast<size_t>(fin - debut) >= taille) return false;
    memcpy(sortie, debut, fin - debut);
    sortie[fin - debut] = '\0';
    return true;
}

// Lit la valeur entiere du champ `cle`. Renvoie -1 si absent.
long lireEntier(const char* json, const char* cle) {
    char motif[24];
    snprintf(motif, sizeof motif, "\"%s\":", cle);
    const char* debut = strstr(json, motif);
    if (!debut) return -1;
    return strtol(debut + strlen(motif), nullptr, 10);
}

// --- Envoi des messages ---

void envoyer(const char* json) {
    if (!liaison.envoyer(json)) {
        Serial.printf("[UART] Message trop long, non envoye : %.60s...\n", json);
    }
}

void envoyerHello() {
    char json[160];
    snprintf(json, sizeof json,
             "{\"t\":\"hello\",\"n\":%u,\"proto\":%d,\"fw\":\"%s\",\"board\":\"%s\"}",
             liaison.prochaine_sequence(), protocole_uart::VERSION_PROTOCOLE, VERSION_FW, NOM_CARTE);
    envoyer(json);
}

void envoyerAck(long ref, bool ok, const char* erreur) {
    char json[128];
    if (ok) {
        snprintf(json, sizeof json, "{\"t\":\"ack\",\"n\":%u,\"ref\":%ld,\"ok\":true,\"err\":null}",
                 liaison.prochaine_sequence(), ref);
    } else {
        snprintf(json, sizeof json, "{\"t\":\"ack\",\"n\":%u,\"ref\":%ld,\"ok\":false,\"err\":\"%s\"}",
                 liaison.prochaine_sequence(), ref, erreur);
    }
    envoyer(json);
}

void envoyerHeartbeat() {
    char json[64];
    snprintf(json, sizeof json, "{\"t\":\"hb\",\"n\":%u,\"up\":%lu}",
             liaison.prochaine_sequence(), millis());
    envoyer(json);
}

// Donnees FACTICES, uniquement pour tester une trame de taille reelle.
void envoyerBmsFactice() {
    char cellules[160];
    size_t pos = 0;
    for (int i = 0; i < 15; ++i) {
        const float v = 3.20f + 0.01f * static_cast<float>((millis() / 1000 + i) % 5);
        pos += snprintf(cellules + pos, sizeof cellules - pos, "%s%.3f", i ? "," : "", v);
    }
    char json[400];
    snprintf(json, sizeof json,
             "{\"t\":\"bms\",\"n\":%u,\"id\":1,\"ok\":true,\"voltage\":48.6,\"current\":-12.5,"
             "\"soc\":75.0,\"remaining_ah\":73.5,\"cells\":[%s],\"temps\":[25.0,26.1]}",
             liaison.prochaine_sequence(), cellules);
    envoyer(json);
}

// --- Traitement des messages de la Pi ---

void traiter(const char* json) {
    char type[16];
    if (!lireTexte(json, "t", type, sizeof type)) return;
    const long n = lireEntier(json, "n");

    if (!piVue) {
        piVue = true;
        Serial.println("[UART] Premier message recu de la Pi");
    }
    Serial.printf("[UART] <- %s\n", json);

    if (strcmp(type, "ping") == 0) {
        envoyerAck(n, true, nullptr);
    } else if (strcmp(type, "info") == 0) {
        envoyerHello();
        envoyerAck(n, true, nullptr);
    } else if (strcmp(type, "cmd") == 0) {
        char nom[24];
        if (lireTexte(json, "name", nom, sizeof nom) && strcmp(nom, "reboot") == 0) {
            envoyerAck(n, true, nullptr);
            portPi.flush();
            delay(50);
            ESP.restart();
        } else {
            envoyerAck(n, false, "unknown command");
        }
    }
    // Type inconnu : ignore sans erreur (voir PROTOCOLE_UART.md, section 2)
}

}  // namespace

void setup() {
    Serial.begin(115200);  // USB : logs de debug
    portPi.begin(VITESSE_UART_PI, SERIAL_8N1, PIN_UART_PI_RX, PIN_UART_PI_TX);
    delay(100);

    Serial.printf("\n[BOOT] %s %s - UART Pi : RX=%d TX=%d @ %lu bauds\n",
                  NOM_CARTE, VERSION_FW, PIN_UART_PI_RX, PIN_UART_PI_TX, VITESSE_UART_PI);
    envoyerHello();
}

void loop() {
    // Lire tout ce qui est arrive de la Pi
    while (const char* json = liaison.recevoir()) {
        traiter(json);
    }

    const unsigned long maintenant = millis();
    if (maintenant - dernierEnvoiMs >= 1000) {
        dernierEnvoiMs = maintenant;
        envoyerHeartbeat();
        envoyerBmsFactice();

        const auto& dec = liaison.decodeur();
        Serial.printf("[UART] -> hb + bms | recu de la Pi : %lu ok, %lu erreurs%s\n",
                      static_cast<unsigned long>(dec.trames_ok()),
                      static_cast<unsigned long>(dec.trames_erreur()),
                      piVue && liaison.ms_depuis_derniere_trame() > protocole_uart::TIMEOUT_LIAISON_MS
                          ? " | Pi silencieuse" : "");
    }
}
