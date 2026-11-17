/*
 * Protocole UART binaire ESP32 <-> Raspberry Pi - version 2.
 * Specification : 2026-2027/shared/PROTOCOLE_UART.md
 *
 * C99 pur, sans allocation dynamique. Le MEME fichier est compile sur
 * l'ESP32 (PlatformIO : lib_extra_dirs = ../../shared/c) et sur la Pi (gcc).
 *
 * Trame :  AA 55 | LEN (2) | TYPE (1) | SEQ (2) | PAYLOAD (LEN) | CRC (2)
 * Entiers en little-endian. CRC-16/CCITT-FALSE calcule sur LEN..PAYLOAD.
 */
#ifndef PROTOCOLE_H
#define PROTOCOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constantes                                                          */
/* ------------------------------------------------------------------ */

#define PROTO_VERSION          2
#define PROTO_SOF0             0xAA
#define PROTO_SOF1             0x55
#define PROTO_TAILLE_ENTETE    7     /* SOF(2) + LEN(2) + TYPE(1) + SEQ(2) */
#define PROTO_TAILLE_CRC       2
#define PROTO_MAX_PAYLOAD      1024
#define PROTO_MAX_TRAME        (PROTO_TAILLE_ENTETE + PROTO_MAX_PAYLOAD + PROTO_TAILLE_CRC)

#define PROTO_VITESSE_DEFAUT   115200u  /* vitesse au demarrage et de repli */
#define PROTO_TIMEOUT_LIAISON_MS 3000u  /* plus de trame valide -> liaison perdue */

/* Types de messages */
enum {
    /* ESP32 -> Pi */
    PROTO_T_HELLO      = 0x01,
    PROTO_T_HB         = 0x02,
    PROTO_T_BMS        = 0x10,
    PROTO_T_TEMP       = 0x11,
    PROTO_T_POWER      = 0x12,
    PROTO_T_IMU        = 0x13,
    PROTO_T_PILOT      = 0x14,
    PROTO_T_ALERT      = 0x20,
    PROTO_T_LOG        = 0x21,
    PROTO_T_ACK        = 0x30,
    PROTO_T_TEST       = 0x70,
    PROTO_T_STREAM_FIN = 0x71,
    /* Pi -> ESP32 */
    PROTO_T_PING       = 0x80,
    PROTO_T_INFO       = 0x81,
    PROTO_T_CMD        = 0x82
};

/* Cartes (champ "carte" de HELLO) */
enum { PROTO_CARTE_INCONNUE = 0, PROTO_CARTE_WROOM32E = 1, PROTO_CARTE_S3 = 2 };

/* Commandes (premier octet du payload de CMD) */
enum {
    PROTO_CMD_REBOOT   = 1,
    PROTO_CMD_SET_LED  = 2,
    PROTO_CMD_SET_BAUD = 3,
    PROTO_CMD_STREAM   = 4
};

/* Codes d'erreur de ACK */
enum {
    PROTO_ERR_AUCUNE          = 0,
    PROTO_ERR_CMD_INCONNUE    = 1,
    PROTO_ERR_ARG_INVALIDE    = 2,
    PROTO_ERR_NON_DISPONIBLE  = 3
};

/* Codes d'alerte */
enum {
    PROTO_ALERTE_TEMP_HAUTE    = 1,
    PROTO_ALERTE_CELLULE_BASSE = 2,
    PROTO_ALERTE_DESEQUILIBRE  = 3,
    PROTO_ALERTE_BMS_PERDU     = 4,
    PROTO_ALERTE_CAN_PERDU     = 5
};

/* Valeur "capteur indisponible" pour les temperatures */
#define PROTO_TEMP_INVALIDE INT16_MIN

/* ------------------------------------------------------------------ */
/* CRC et trames                                                       */
/* ------------------------------------------------------------------ */

/* CRC-16/CCITT-FALSE incremental : commencer avec crc = 0xFFFF.
 * Valeur de test : "123456789" -> 0x29B1. */
uint16_t proto_crc16(uint16_t crc, const uint8_t *data, size_t len);

/* Construit une trame complete dans `sortie`.
 * Renvoie sa taille en octets, ou 0 si `taille_sortie` est trop petit ou
 * si `len` depasse PROTO_MAX_PAYLOAD. */
size_t proto_encoder_trame(uint8_t *sortie, size_t taille_sortie, uint8_t type,
                           uint16_t seq, const uint8_t *payload, uint16_t len);

/* Trame decodee. `payload` pointe dans le tampon du decodeur : il reste
 * valable jusqu'au prochain appel a proto_decodeur_ajouter(). */
typedef struct {
    uint8_t type;
    uint16_t seq;
    uint16_t len;
    const uint8_t *payload;
} proto_trame_t;

typedef struct {
    uint8_t tampon[2 * PROTO_MAX_TRAME];
    size_t debut;
    size_t fin;
    uint32_t trames_ok;
    uint32_t erreurs_crc;      /* trames rejetees par le CRC */
    uint32_t octets_ignores;   /* octets jetes en cherchant un debut de trame */
} proto_decodeur_t;

void proto_decodeur_init(proto_decodeur_t *d);

/* Copie des octets recus dans le decodeur. Renvoie le nombre d'octets
 * acceptes (peut etre < len si le tampon est plein : appeler alors
 * proto_decodeur_suivante() puis recommencer avec le reste). */
size_t proto_decodeur_ajouter(proto_decodeur_t *d, const uint8_t *data, size_t len);

/* Extrait la prochaine trame valide. Renvoie false s'il n'y en a pas
 * (encore) de complete. A appeler en boucle apres chaque ajout. */
bool proto_decodeur_suivante(proto_decodeur_t *d, proto_trame_t *trame);

/* ------------------------------------------------------------------ */
/* Lecture / ecriture little-endian des payloads                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *p;
    size_t taille;
    size_t pos;
    bool erreur;   /* passe a true si on ecrit au-dela de `taille` */
} proto_ecrivain_t;

typedef struct {
    const uint8_t *p;
    size_t taille;
    size_t pos;
    bool erreur;   /* passe a true si on lit au-dela de `taille` */
} proto_lecteur_t;

void proto_ecrivain_init(proto_ecrivain_t *e, uint8_t *p, size_t taille);
void proto_e_u8(proto_ecrivain_t *e, uint8_t v);
void proto_e_u16(proto_ecrivain_t *e, uint16_t v);
void proto_e_i16(proto_ecrivain_t *e, int16_t v);
void proto_e_u32(proto_ecrivain_t *e, uint32_t v);
void proto_e_i32(proto_ecrivain_t *e, int32_t v);
void proto_e_octets(proto_ecrivain_t *e, const uint8_t *src, size_t n);

void proto_lecteur_init(proto_lecteur_t *l, const uint8_t *p, size_t taille);
uint8_t proto_l_u8(proto_lecteur_t *l);
uint16_t proto_l_u16(proto_lecteur_t *l);
int16_t proto_l_i16(proto_lecteur_t *l);
uint32_t proto_l_u32(proto_lecteur_t *l);
int32_t proto_l_i32(proto_lecteur_t *l);
void proto_l_octets(proto_lecteur_t *l, uint8_t *dst, size_t n);

/* ------------------------------------------------------------------ */
/* Messages                                                            */
/* Chaque message a une fonction d'encodage (struct -> payload, renvoie */
/* la taille ou 0 si erreur) et de decodage (payload -> struct, renvoie */
/* false si le payload est trop court ou incoherent).                  */
/* Les octets en plus a la fin d'un payload sont ignores (evolution).  */
/* ------------------------------------------------------------------ */

#define PROTO_MAX_CELLULES 24
#define PROTO_MAX_TEMPS_BMS 8
#define PROTO_MAX_SONDES 16

typedef struct {
    uint8_t version;      /* PROTO_VERSION de l'emetteur */
    uint8_t carte;        /* PROTO_CARTE_xxx */
    uint8_t fw[3];        /* majeur, mineur, correctif */
} proto_hello_t;

typedef struct {
    uint32_t uptime_ms;
} proto_hb_t;

typedef struct {
    uint8_t id;                    /* 1 ou 2 */
    uint8_t ok;                    /* 0 = BMS muet (le reste n'a pas de sens) */
    uint16_t tension_cv;           /* tension pack, en 0,01 V */
    int32_t courant_ca;            /* courant, en 0,01 A (negatif = decharge) */
    uint16_t soc_dixieme;          /* etat de charge, en 0,1 % */
    uint16_t restant_cah;          /* capacite restante, en 0,01 Ah */
    uint8_t nb_cellules;
    uint16_t cellules_mv[PROTO_MAX_CELLULES];
    uint8_t nb_temps;
    int16_t temps_dixieme[PROTO_MAX_TEMPS_BMS];  /* en 0,1 degC */
} proto_bms_t;

typedef struct {
    uint8_t nb;
    struct {
        uint8_t rom[8];            /* adresse 1-Wire unique */
        int16_t c_centieme;        /* en 0,01 degC, PROTO_TEMP_INVALIDE si echec */
    } sondes[PROTO_MAX_SONDES];
} proto_temp_t;

typedef struct {
    uint16_t tension_mv;
    int32_t courant_ma;
    int32_t puissance_mw;
} proto_power_t;

typedef struct {
    int16_t roulis_cdeg;           /* en 0,01 degre */
    int16_t tangage_cdeg;
    uint16_t cap_cdeg;             /* 0 a 35999 */
} proto_imu_t;

typedef struct {
    uint8_t ok;                    /* 0 = noeud pilote muet sur le CAN */
    uint8_t boutons;               /* bit i = bouton i appuye */
    uint8_t voyants;               /* bit i = voyant i allume */
} proto_pilot_t;

typedef struct {
    uint8_t code;                  /* PROTO_ALERTE_xxx */
    uint8_t niveau;                /* 1 info, 2 avertissement, 3 critique */
    uint8_t source;                /* 0 aucune, 1 BMS_1, 2 BMS_2, 0x10+i sonde i */
    int32_t valeur;                /* unite selon le code (voir la spec) */
} proto_alert_t;

typedef struct {
    uint16_t ref;                  /* SEQ de la commande a laquelle on repond */
    uint8_t ok;
    uint8_t erreur;                /* PROTO_ERR_xxx */
} proto_ack_t;

typedef struct {
    uint8_t commande;              /* PROTO_CMD_xxx */
    union {
        struct { uint8_t index; uint8_t allume; } led;
        struct { uint32_t vitesse; } baud;
        struct { uint16_t taille_payload; uint32_t duree_ms; } stream;
    } arg;
} proto_cmd_t;

typedef struct {
    uint32_t trames;
    uint32_t octets;
} proto_stream_fin_t;

size_t proto_encoder_hello(const proto_hello_t *m, uint8_t *p, size_t taille);
bool proto_decoder_hello(const uint8_t *p, size_t len, proto_hello_t *m);
size_t proto_encoder_hb(const proto_hb_t *m, uint8_t *p, size_t taille);
bool proto_decoder_hb(const uint8_t *p, size_t len, proto_hb_t *m);
size_t proto_encoder_bms(const proto_bms_t *m, uint8_t *p, size_t taille);
bool proto_decoder_bms(const uint8_t *p, size_t len, proto_bms_t *m);
size_t proto_encoder_temp(const proto_temp_t *m, uint8_t *p, size_t taille);
bool proto_decoder_temp(const uint8_t *p, size_t len, proto_temp_t *m);
size_t proto_encoder_power(const proto_power_t *m, uint8_t *p, size_t taille);
bool proto_decoder_power(const uint8_t *p, size_t len, proto_power_t *m);
size_t proto_encoder_imu(const proto_imu_t *m, uint8_t *p, size_t taille);
bool proto_decoder_imu(const uint8_t *p, size_t len, proto_imu_t *m);
size_t proto_encoder_pilot(const proto_pilot_t *m, uint8_t *p, size_t taille);
bool proto_decoder_pilot(const uint8_t *p, size_t len, proto_pilot_t *m);
size_t proto_encoder_alert(const proto_alert_t *m, uint8_t *p, size_t taille);
bool proto_decoder_alert(const uint8_t *p, size_t len, proto_alert_t *m);
size_t proto_encoder_ack(const proto_ack_t *m, uint8_t *p, size_t taille);
bool proto_decoder_ack(const uint8_t *p, size_t len, proto_ack_t *m);
size_t proto_encoder_cmd(const proto_cmd_t *m, uint8_t *p, size_t taille);
bool proto_decoder_cmd(const uint8_t *p, size_t len, proto_cmd_t *m);
size_t proto_encoder_stream_fin(const proto_stream_fin_t *m, uint8_t *p, size_t taille);
bool proto_decoder_stream_fin(const uint8_t *p, size_t len, proto_stream_fin_t *m);

/* LOG : niveau ('D', 'I', 'W', 'E') + texte ASCII sans '\0'.
 * Le decodage copie le texte dans `texte` (termine par '\0', tronque si besoin). */
size_t proto_encoder_log(char niveau, const char *texte, uint8_t *p, size_t taille);
bool proto_decoder_log(const uint8_t *p, size_t len, char *niveau, char *texte, size_t taille_texte);

/* TEST (mesure de debit) : compteur (u32) puis octets de remplissage
 * dont la valeur se deduit du compteur, pour verifier le contenu a l'arrivee.
 * `taille` totale >= 4. */
size_t proto_encoder_test(uint32_t compteur, uint8_t *p, size_t taille);
/* Renvoie true si le contenu correspond exactement au motif attendu. */
bool proto_verifier_test(const uint8_t *p, size_t len, uint32_t *compteur);

/* Vitesses UART autorisees (communes ESP32 et Pi), terminees par 0. */
extern const uint32_t PROTO_VITESSES[];
bool proto_vitesse_valide(uint32_t vitesse);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOLE_H */
