/*
 * Sondes DS18B20 (bus 1-Wire) en C pur.
 *
 * Cycle (machine a etats, une etape par appel de ds18b20_tick) :
 *   RECHERCHE  : liste les sondes presentes (algorithme de recherche ROM)
 *   CONVERSION : "Skip ROM + Convert T" : toutes les sondes mesurent ensemble
 *   ATTENTE    : 750 ms de conversion (12 bits, 0,0625 degC)
 *   LECTURE    : une sonde par appel (environ 10 ms), CRC verifie
 *   PAUSE      : jusqu'a la seconde suivante
 */
#include "ds18b20.h"

#include <string.h>

#include "app.h"

#define CMD_RECHERCHE_ROM 0xF0
#define CMD_MATCH_ROM 0x55
#define CMD_SKIP_ROM 0xCC
#define CMD_CONVERSION 0x44
#define CMD_LIRE_MEMOIRE 0xBE

#define FAMILLE_DS18B20 0x28
#define FAMILLE_DS1822 0x22

#define DUREE_CONVERSION_MS 760u
#define PERIODE_CYCLE_MS 1000u
#define PERIODE_RECHERCHE_CYCLES 10u
#define DELAI_SANS_SONDE_MS 2000u

typedef enum { RECHERCHE, CONVERSION, ATTENTE, LECTURE, PAUSE } etat_t;

static etat_t etat;
static uint32_t debut_ms;
static int lecture;
static uint32_t cycles;
static bool termine;
static uint32_t erreurs;

static ds18b20_sonde_t sondes[DS18B20_MAX_SONDES];
static int nb;

/* ------------------------------------------------------------------ */
/* Octets et CRC                                                       */
/* ------------------------------------------------------------------ */

static void ecrire_octet(uint8_t o)
{
    for (int i = 0; i < 8; ++i) {
        plateforme_1w_bit((o >> i) & 1);
    }
}

static uint8_t lire_octet(void)
{
    uint8_t o = 0;
    for (int i = 0; i < 8; ++i) {
        if (plateforme_1w_bit(1)) {
            o |= (uint8_t)(1u << i);
        }
    }
    return o;
}

/* CRC-8 Dallas/Maxim (polynome x^8 + x^5 + x^4 + 1, reflechi : 0x8C) */
static uint8_t crc8(const uint8_t *p, int n)
{
    uint8_t crc = 0;
    while (n--) {
        uint8_t o = *p++;
        for (int i = 0; i < 8; ++i) {
            const uint8_t m = (uint8_t)((crc ^ o) & 1);
            crc >>= 1;
            if (m) crc ^= 0x8C;
            o >>= 1;
        }
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* Recherche des sondes (Maxim, note d'application 187)                */
/* ------------------------------------------------------------------ */

static uint8_t rom_courante[8];
static int derniere_divergence;
static bool dernier_appareil;

static bool chercher_suivant(void)
{
    if (dernier_appareil || !plateforme_1w_reset()) {
        return false;
    }
    ecrire_octet(CMD_RECHERCHE_ROM);

    int dernier_zero = 0;
    for (int n = 1; n <= 64; ++n) {
        const int bit = plateforme_1w_bit(1);
        const int complement = plateforme_1w_bit(1);
        if (bit && complement) {
            return false;  /* plus personne ne repond */
        }
        const int octet = (n - 1) / 8;
        const uint8_t masque = (uint8_t)(1u << ((n - 1) % 8));
        int direction;
        if (bit != complement) {
            direction = bit;  /* toutes les sondes restantes sont d'accord */
        } else {
            /* divergence : deux sondes different sur ce bit */
            if (n < derniere_divergence) {
                direction = (rom_courante[octet] & masque) != 0;
            } else {
                direction = n == derniere_divergence;
            }
            if (!direction) {
                dernier_zero = n;
            }
        }
        if (direction) {
            rom_courante[octet] |= masque;
        } else {
            rom_courante[octet] &= (uint8_t)~masque;
        }
        plateforme_1w_bit(direction);
    }
    derniere_divergence = dernier_zero;
    dernier_appareil = derniere_divergence == 0;
    return crc8(rom_courante, 7) == rom_courante[7];
}

static void rechercher(void)
{
    ds18b20_sonde_t trouvees[DS18B20_MAX_SONDES];
    int n = 0;

    memset(rom_courante, 0, sizeof rom_courante);
    derniere_divergence = 0;
    dernier_appareil = false;
    while (n < DS18B20_MAX_SONDES && chercher_suivant()) {
        const uint8_t famille = rom_courante[0];
        if (famille != FAMILLE_DS18B20 && famille != FAMILLE_DS1822) {
            continue;  /* autre composant 1-Wire : ignore */
        }
        memcpy(trouvees[n].rom, rom_courante, 8);
        trouvees[n].c_centieme = DS18B20_INVALIDE;
        /* Une sonde deja connue garde sa derniere valeur */
        for (int i = 0; i < nb; ++i) {
            if (memcmp(sondes[i].rom, rom_courante, 8) == 0) {
                trouvees[n].c_centieme = sondes[i].c_centieme;
            }
        }
        n++;
    }

    if (n != nb) {
        plateforme_log("[TEMP] %d sonde(s) sur le bus 1-Wire\n", n);
        for (int i = 0; i < n; ++i) {
            const uint8_t *r = trouvees[i].rom;
            plateforme_log("[TEMP]   %02X%02X%02X%02X%02X%02X%02X%02X\n", r[0], r[1], r[2], r[3], r[4],
                           r[5], r[6], r[7]);
        }
    }
    memcpy(sondes, trouvees, sizeof trouvees);
    nb = n;
}

/* ------------------------------------------------------------------ */
/* Mesure                                                              */
/* ------------------------------------------------------------------ */

static int16_t lire_sonde(const uint8_t rom[8])
{
    uint8_t m[9];
    if (!plateforme_1w_reset()) {
        return DS18B20_INVALIDE;
    }
    ecrire_octet(CMD_MATCH_ROM);
    for (int i = 0; i < 8; ++i) {
        ecrire_octet(rom[i]);
    }
    ecrire_octet(CMD_LIRE_MEMOIRE);
    bool que_des_un = true;
    for (int i = 0; i < 9; ++i) {
        m[i] = lire_octet();
        que_des_un = que_des_un && m[i] == 0xFF;
    }
    /* Que des 1 : sonde debranchee entre-temps (le CRC pourrait passer) */
    if (que_des_un || crc8(m, 8) != m[8]) {
        return DS18B20_INVALIDE;
    }
    /* En 1/16 degC -> 0,01 degC, arrondi */
    const int32_t brut = (int16_t)(m[0] | (m[1] << 8));
    const int32_t c = (brut * 100 + (brut >= 0 ? 8 : -8)) / 16;
    return (int16_t)c;
}

void ds18b20_init(void)
{
    nb = 0;
    etat = RECHERCHE;
    debut_ms = plateforme_millis();
}

void ds18b20_tick(uint32_t maintenant)
{
    switch (etat) {
    case RECHERCHE:
        rechercher();
        if (nb == 0) {
            termine = true;  /* on signale aussi l'absence de sonde */
            debut_ms = maintenant;
            etat = PAUSE;
        } else {
            etat = CONVERSION;
        }
        return;

    case CONVERSION:
        debut_ms = maintenant;
        if (!plateforme_1w_reset()) {
            /* bus muet : toutes les sondes ont disparu */
            for (int i = 0; i < nb; ++i) sondes[i].c_centieme = DS18B20_INVALIDE;
            erreurs++;
            termine = true;
            etat = PAUSE;
            cycles = PERIODE_RECHERCHE_CYCLES - 1;  /* nouvelle recherche au prochain cycle */
            return;
        }
        ecrire_octet(CMD_SKIP_ROM);
        ecrire_octet(CMD_CONVERSION);
        etat = ATTENTE;
        return;

    case ATTENTE:
        if (maintenant - debut_ms >= DUREE_CONVERSION_MS) {
            lecture = 0;
            etat = LECTURE;
        }
        return;

    case LECTURE:
        if (lecture < nb) {
            const int16_t c = lire_sonde(sondes[lecture].rom);
            if (c == DS18B20_INVALIDE) erreurs++;
            sondes[lecture].c_centieme = c;
            lecture++;
            return;
        }
        termine = true;
        etat = PAUSE;
        return;

    case PAUSE: {
        const uint32_t periode = nb ? PERIODE_CYCLE_MS : DELAI_SANS_SONDE_MS;
        if (maintenant - debut_ms >= periode) {
            cycles++;
            if (nb == 0 || cycles % PERIODE_RECHERCHE_CYCLES == 0) {
                etat = RECHERCHE;
            } else {
                etat = CONVERSION;
            }
        }
        return;
    }
    }
}

bool ds18b20_cycle_termine(void)
{
    const bool t = termine;
    termine = false;
    return t;
}

int ds18b20_nb(void) { return nb; }

const ds18b20_sonde_t *ds18b20_sonde(int i) { return (i >= 0 && i < nb) ? &sondes[i] : 0; }

uint32_t ds18b20_erreurs(void) { return erreurs; }
