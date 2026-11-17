// Implementation du protocole UART. Specification : shared/PROTOCOLE_UART.md
// Doit rester strictement equivalente a shared/python/protocole_uart.py.

#include "protocole_uart.h"

#include <string.h>

namespace protocole_uart {

namespace {

const char HEXA[] = "0123456789ABCDEF";

// Convertit un caractere hexadecimal majuscule ou minuscule. -1 si invalide.
int valeur_hexa(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

}  // namespace

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = static_cast<uint16_t>(crc ^ (data[i] << 8));
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

size_t encoder_trame(const char* json, char* sortie, size_t taille_sortie) {
    const size_t n = strlen(json);
    // JSON + '*' + 4 hex + '\n' + '\0'
    if (n == 0 || n > TAILLE_MAX_LIGNE - 5 || taille_sortie < n + 7) return 0;
    if (memchr(json, '\n', n) != nullptr) return 0;

    const uint16_t crc = crc16_ccitt(reinterpret_cast<const uint8_t*>(json), n);
    memcpy(sortie, json, n);
    sortie[n] = '*';
    sortie[n + 1] = HEXA[(crc >> 12) & 0xF];
    sortie[n + 2] = HEXA[(crc >> 8) & 0xF];
    sortie[n + 3] = HEXA[(crc >> 4) & 0xF];
    sortie[n + 4] = HEXA[crc & 0xF];
    sortie[n + 5] = '\n';
    sortie[n + 6] = '\0';
    return n + 6;
}

const char* Decodeur::ajouter(uint8_t octet) {
    if (octet == '\n') {
        const char* resultat = nullptr;
        if (trop_long_) {
            ++trames_erreur_;
        } else if (longueur_ > 0) {
            resultat = terminer_ligne();
            if (resultat) {
                ++trames_ok_;
            } else {
                ++trames_erreur_;
            }
        }
        longueur_ = 0;
        trop_long_ = false;
        return resultat;
    }

    if (!trop_long_) {
        if (longueur_ < TAILLE_MAX_LIGNE) {
            tampon_[longueur_++] = static_cast<char>(octet);
        } else {
            // Ligne trop longue : on jette tout jusqu'au prochain '\n'
            longueur_ = 0;
            trop_long_ = true;
        }
    }
    return nullptr;
}

// Verifie la ligne complete (sans '\n') et renvoie le JSON, ou nullptr.
const char* Decodeur::terminer_ligne() {
    size_t fin = longueur_;
    if (fin > 0 && tampon_[fin - 1] == '\r') --fin;

    // Une trame commence toujours par '{' : on ignore les parasites qui precedent
    size_t debut = 0;
    while (debut < fin && tampon_[debut] != '{') ++debut;
    if (debut == fin) return nullptr;

    // Dernier '*' de la ligne, suivi d'exactement 4 caracteres hexa
    size_t etoile = fin;
    for (size_t i = fin; i > debut; --i) {
        if (tampon_[i - 1] == '*') {
            etoile = i - 1;
            break;
        }
    }
    if (etoile == fin || fin - etoile != 5) return nullptr;

    uint16_t crc_recu = 0;
    for (size_t i = etoile + 1; i < fin; ++i) {
        const int v = valeur_hexa(tampon_[i]);
        if (v < 0) return nullptr;
        crc_recu = static_cast<uint16_t>((crc_recu << 4) | v);
    }

    const uint8_t* corps = reinterpret_cast<const uint8_t*>(tampon_ + debut);
    if (crc16_ccitt(corps, etoile - debut) != crc_recu) return nullptr;

    tampon_[etoile] = '\0';
    return tampon_ + debut;
}

#ifdef ARDUINO

bool Liaison::envoyer(const char* json) {
    const size_t n = encoder_trame(json, sortie_, sizeof sortie_);
    if (n == 0) return false;
    port_.write(reinterpret_cast<const uint8_t*>(sortie_), n);
    return true;
}

const char* Liaison::recevoir() {
    while (port_.available() > 0) {
        const char* json = decodeur_.ajouter(static_cast<uint8_t>(port_.read()));
        if (json) {
            derniere_trame_ms_ = millis();
            return json;  // les octets restants seront lus au prochain appel
        }
    }
    return nullptr;
}

#endif  // ARDUINO

}  // namespace protocole_uart
