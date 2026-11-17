/*
 * Pilote BNO085 en C pur : protocole SHTP (CEVA SH-2) sur I2C.
 *
 * Paquet SHTP : u16 longueur (en-tete compris, bit 15 = suite), u8 canal,
 * u8 sequence, puis les donnees. Chaque lecture I2C recommence par un
 * en-tete de 4 octets, suivi des octets pas encore lus du paquet.
 *
 * Fonctionnement non bloquant (machine a etats) :
 *   RESET     : RST a 0 pendant 10 ms
 *   DEMARRAGE : on vide les paquets du capteur jusqu'a "reset complete"
 *   MARCHE    : on lit les rapports quand INT est a 0
 * Plus aucune mesure pendant 2 s -> nouveau reset.
 */
#include "bno085.h"

#include <math.h>
#include <string.h>

#include "app.h"

#define CANAL_COMMANDE 0
#define CANAL_EXECUTABLE 1
#define CANAL_CONTROLE 2
#define CANAL_RAPPORTS 3
#define NB_CANAUX 6

#define EXE_RESET_COMPLETE 0x01
#define CMD_SET_FEATURE 0xFD
#define RAPPORT_ROTATION 0x05
#define RAPPORT_HORODATAGE 0xFB
#define RAPPORT_RECALAGE 0xFA

#define DUREE_RESET_MS 10u
#define DELAI_DEMARRAGE_MS 1000u
#define DELAI_SANS_MESURE_MS 2000u
#define PAQUETS_MAX_PAR_TICK 4

/* Le tampon I2C de l'Arduino ESP32 fait 128 octets : on lit par morceaux */
#define MORCEAU_I2C 124u

typedef enum { ETAT_RESET, ETAT_DEMARRAGE, ETAT_MARCHE } etat_t;

static etat_t etat;
static uint32_t debut_etat_ms;
static uint32_t derniere_mesure_ms;
static uint32_t periode_us;
static uint8_t seq_tx[NB_CANAUX];
static int paquets_recus;

static uint8_t paquet[256];
static uint16_t paquet_len;
static uint8_t paquet_canal;

static bno085_mesure_t mesure;
static bool mesure_valide;
static bno085_stats_t stats;

/* ------------------------------------------------------------------ */
/* Transport SHTP                                                      */
/* ------------------------------------------------------------------ */

static bool envoyer_paquet(uint8_t canal, const uint8_t *data, uint8_t len)
{
    uint8_t b[4 + 32];
    const uint16_t total = (uint16_t)(len + 4u);
    if (len > 32 || canal >= NB_CANAUX) {
        return false;
    }
    b[0] = (uint8_t)(total & 0xFF);
    b[1] = (uint8_t)(total >> 8);
    b[2] = canal;
    b[3] = seq_tx[canal]++;
    memcpy(b + 4, data, len);
    if (!plateforme_imu_i2c_ecrire(BNO085_ADRESSE_I2C, b, total)) {
        stats.erreurs_i2c++;
        return false;
    }
    return true;
}

/* Lit un paquet complet. Les octets au-dela de sizeof paquet sont lus
 * puis jetes (ex : l'annonce de demarrage, inutile ici). */
static bool lire_paquet(void)
{
    uint8_t entete[4];
    uint8_t morceau[MORCEAU_I2C + 4];

    if (!plateforme_imu_i2c_lire(BNO085_ADRESSE_I2C, entete, sizeof entete)) {
        stats.erreurs_i2c++;
        return false;
    }
    const uint16_t total = (uint16_t)((entete[0] | (entete[1] << 8)) & 0x7FFF);
    if (total <= 4 || total == 0x7FFF) {
        return false;  /* rien a lire */
    }

    uint16_t reste = (uint16_t)(total - 4);
    uint16_t garde = 0;
    while (reste > 0) {
        const uint16_t n = reste > MORCEAU_I2C ? (uint16_t)MORCEAU_I2C : reste;
        if (!plateforme_imu_i2c_lire(BNO085_ADRESSE_I2C, morceau, (size_t)n + 4)) {
            stats.erreurs_i2c++;
            return false;
        }
        const uint16_t place = (uint16_t)(sizeof paquet - garde);
        const uint16_t copie = n < place ? n : place;
        memcpy(paquet + garde, morceau + 4, copie);
        garde = (uint16_t)(garde + copie);
        reste = (uint16_t)(reste - n);
    }
    paquet_len = garde;
    paquet_canal = entete[2];
    paquets_recus++;
    return true;
}

/* ------------------------------------------------------------------ */
/* SH-2                                                                */
/* ------------------------------------------------------------------ */

static bool activer_rotation(void)
{
    uint8_t c[17];
    memset(c, 0, sizeof c);
    c[0] = CMD_SET_FEATURE;
    c[1] = RAPPORT_ROTATION;
    c[5] = (uint8_t)(periode_us);
    c[6] = (uint8_t)(periode_us >> 8);
    c[7] = (uint8_t)(periode_us >> 16);
    c[8] = (uint8_t)(periode_us >> 24);
    return envoyer_paquet(CANAL_CONTROLE, c, sizeof c);
}

static int16_t l_i16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

static int32_t arrondi(float x) { return (int32_t)(x < 0 ? x - 0.5f : x + 0.5f); }

/* Quaternion (Q14) -> angles. La matrice R(q) passe du repere de la carte
 * au repere terrestre Est / Nord / Haut. */
static void convertir(const uint8_t *r, uint32_t maintenant)
{
    const float x = l_i16(r + 4) / 16384.0f;
    const float y = l_i16(r + 6) / 16384.0f;
    const float z = l_i16(r + 8) / 16384.0f;
    const float w = l_i16(r + 10) / 16384.0f;

    /* Axe avant (Y de la carte) dans le repere terrestre */
    const float avant_est = 2.0f * (x * y - w * z);
    const float avant_nord = 1.0f - 2.0f * (x * x + z * z);
    float avant_haut = 2.0f * (y * z + w * x);
    /* Composantes verticales des axes X (tribord) et Z (haut) */
    const float tribord_haut = 2.0f * (x * z - w * y);
    const float haut_haut = 1.0f - 2.0f * (x * x + y * y);

    if (avant_haut > 1.0f) avant_haut = 1.0f;
    if (avant_haut < -1.0f) avant_haut = -1.0f;

    const float deg = 57.29577951f;
    const float tangage = asinf(avant_haut) * deg;
    const float roulis = atan2f(-tribord_haut, haut_haut) * deg;
    float cap = atan2f(avant_est, avant_nord) * deg;
    if (cap < 0.0f) cap += 360.0f;

    int32_t cap_cdeg = arrondi(cap * 100.0f);
    if (cap_cdeg >= 36000) cap_cdeg -= 36000;

    mesure.roulis_cdeg = (int16_t)arrondi(roulis * 100.0f);
    mesure.tangage_cdeg = (int16_t)arrondi(tangage * 100.0f);
    mesure.cap_cdeg = (uint16_t)cap_cdeg;
    mesure.precision = (uint8_t)(r[2] & 0x03);
    mesure.date_ms = maintenant;
    mesure_valide = true;
    derniere_mesure_ms = maintenant;
    stats.mesures++;
}

static void traiter_rapports(uint32_t maintenant)
{
    uint16_t i = 0;
    while (i < paquet_len) {
        const uint8_t id = paquet[i];
        if (id == RAPPORT_HORODATAGE || id == RAPPORT_RECALAGE) {
            i = (uint16_t)(i + 5);
        } else if (id == RAPPORT_ROTATION) {
            if (i + 14 > paquet_len) break;
            convertir(paquet + i, maintenant);
            i = (uint16_t)(i + 14);
        } else {
            break;  /* rapport non demande : on ignore la suite du paquet */
        }
    }
}

/* ------------------------------------------------------------------ */
/* Machine a etats                                                     */
/* ------------------------------------------------------------------ */

static void changer_etat(etat_t e, uint32_t maintenant)
{
    etat = e;
    debut_etat_ms = maintenant;
}

static void lancer_reset(uint32_t maintenant)
{
    plateforme_imu_rst(0);
    memset(seq_tx, 0, sizeof seq_tx);
    paquets_recus = 0;
    stats.redemarrages++;
    changer_etat(ETAT_RESET, maintenant);
}

static void configurer(uint32_t maintenant)
{
    if (activer_rotation()) {
        plateforme_log("[IMU] BNO085 pret, mesure toutes les %lu ms\n", (unsigned long)(periode_us / 1000u));
    } else {
        plateforme_log("[IMU] echec de configuration (I2C)\n");
    }
    derniere_mesure_ms = maintenant;
    changer_etat(ETAT_MARCHE, maintenant);
}

void bno085_init(uint32_t periode_ms)
{
    periode_us = periode_ms * 1000u;
    mesure_valide = false;
    lancer_reset(plateforme_millis());
    memset(&stats, 0, sizeof stats);  /* le reset de demarrage ne compte pas */
}

void bno085_tick(uint32_t maintenant)
{
    switch (etat) {
    case ETAT_RESET:
        if (maintenant - debut_etat_ms >= DUREE_RESET_MS) {
            plateforme_imu_rst(1);
            changer_etat(ETAT_DEMARRAGE, maintenant);
        }
        return;

    case ETAT_DEMARRAGE:
        for (int k = 0; k < PAQUETS_MAX_PAR_TICK && plateforme_imu_int_actif(); ++k) {
            if (!lire_paquet()) break;
            if (paquet_canal == CANAL_EXECUTABLE && paquet_len > 0 && paquet[0] == EXE_RESET_COMPLETE) {
                configurer(maintenant);
                return;
            }
        }
        if (maintenant - debut_etat_ms > DELAI_DEMARRAGE_MS) {
            if (paquets_recus > 0) {
                configurer(maintenant);  /* "reset complete" rate, on tente quand meme */
            } else {
                plateforme_log("[IMU] BNO085 muet (verifier SDA/SCL/INT/RST, adresse 0x%02X)\n",
                               BNO085_ADRESSE_I2C);
                lancer_reset(maintenant);
            }
        }
        return;

    case ETAT_MARCHE:
        for (int k = 0; k < PAQUETS_MAX_PAR_TICK && plateforme_imu_int_actif(); ++k) {
            if (!lire_paquet()) break;
            if (paquet_canal == CANAL_RAPPORTS) {
                traiter_rapports(maintenant);
            } else if (paquet_canal == CANAL_EXECUTABLE && paquet_len > 0 &&
                       paquet[0] == EXE_RESET_COMPLETE) {
                /* Le capteur a redemarre tout seul : il a oublie sa configuration */
                stats.resets_capteur++;
                plateforme_log("[IMU] le BNO085 a redemarre, reconfiguration\n");
                configurer(maintenant);
                return;
            }
        }
        if (maintenant - derniere_mesure_ms > DELAI_SANS_MESURE_MS) {
            plateforme_log("[IMU] plus de mesure depuis %lu ms : reset du BNO085\n",
                           (unsigned long)DELAI_SANS_MESURE_MS);
            lancer_reset(maintenant);
        }
        return;
    }
}

bool bno085_mesure(uint32_t maintenant, uint32_t age_max_ms, bno085_mesure_t *m)
{
    if (!mesure_valide || maintenant - mesure.date_ms > age_max_ms) {
        return false;
    }
    *m = mesure;
    return true;
}

const bno085_stats_t *bno085_stats(void) { return &stats; }

bool bno085_en_marche(void) { return etat == ETAT_MARCHE; }
