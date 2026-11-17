// Protocole UART ESP32 <-> Pi, cote ESP32 (C++).
// Specification : shared/PROTOCOLE_UART.md
//
// Trame = une ligne : <JSON>*<CRC16 en 4 hex majuscules>\n
//
// Cette librairie ne fait que le "transport" (cadrage + CRC). Le contenu
// JSON est construit et lu par le programme (par exemple avec ArduinoJson).
//
// Utilisation dans platformio.ini du projet ESP :
//     lib_extra_dirs = ../shared/esp
//
// Exemple (Arduino) :
//     #include <protocole_uart.h>
//     protocole_uart::Liaison liaison(Serial2);
//
//     void setup() { Serial2.begin(115200, SERIAL_8N1, 16, 17); }
//
//     void loop() {
//         char json[64];
//         snprintf(json, sizeof json, "{\"t\":\"hb\",\"n\":%u,\"up\":%lu}",
//                  liaison.prochaine_sequence(), (unsigned long)millis());
//         liaison.envoyer(json);
//
//         const char* recu = liaison.recevoir();   // non bloquant
//         if (recu) { /* deserializeJson(doc, recu) ... */ }
//     }

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace protocole_uart {

constexpr int VERSION_PROTOCOLE = 1;
constexpr size_t TAILLE_MAX_LIGNE = 1024;
constexpr uint32_t TIMEOUT_LIAISON_MS = 3000;

// CRC-16/CCITT-FALSE : poly 0x1021, init 0xFFFF, sans reflexion, xorout 0.
// Valeur de test : "123456789" -> 0x29B1.
uint16_t crc16_ccitt(const uint8_t* data, size_t len);

// Ecrit "<json>*XXXX\n" dans `sortie` (termine par '\0').
// Renvoie le nombre d'octets de la trame (sans le '\0'),
// ou 0 si `sortie` est trop petit ou si le JSON contient un '\n'.
size_t encoder_trame(const char* json, char* sortie, size_t taille_sortie);

// Recoit les octets un par un et reconstruit les trames.
class Decodeur {
public:
    // Ajoute un octet recu. Quand une trame complete et valide (CRC correct)
    // vient d'arriver, renvoie le JSON (chaine terminee par '\0', valable
    // jusqu'au prochain appel). Sinon renvoie nullptr.
    const char* ajouter(uint8_t octet);

    uint32_t trames_ok() const { return trames_ok_; }
    uint32_t trames_erreur() const { return trames_erreur_; }

private:
    const char* terminer_ligne();

    char tampon_[TAILLE_MAX_LIGNE + 1];
    size_t longueur_ = 0;
    bool trop_long_ = false;
    uint32_t trames_ok_ = 0;
    uint32_t trames_erreur_ = 0;
};

}  // namespace protocole_uart

#ifdef ARDUINO
#include <Arduino.h>

namespace protocole_uart {

// Liaison complete sur un port serie Arduino (Serial2, Serial1...).
class Liaison {
public:
    explicit Liaison(Stream& port) : port_(port) {}

    // Numero de sequence a mettre dans le champ "n" du prochain message.
    uint16_t prochaine_sequence() { return sequence_++; }

    // Envoie un message JSON (doit deja contenir "t" et "n").
    // Renvoie false si le message est trop long.
    bool envoyer(const char* json);

    // Lit les octets disponibles sans bloquer. Renvoie le JSON d'une trame
    // valide, ou nullptr. A appeler souvent dans loop().
    const char* recevoir();

    // Temps (ms) depuis la derniere trame valide recue de la Pi.
    uint32_t ms_depuis_derniere_trame() const { return millis() - derniere_trame_ms_; }

    const Decodeur& decodeur() const { return decodeur_; }

private:
    Stream& port_;
    Decodeur decodeur_;
    uint16_t sequence_ = 0;
    uint32_t derniere_trame_ms_ = 0;
    char sortie_[TAILLE_MAX_LIGNE + 7];  // JSON + '*' + 4 hex + '\n' + '\0'
};

}  // namespace protocole_uart
#endif  // ARDUINO
