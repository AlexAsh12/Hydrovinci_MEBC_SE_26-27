/*
 * Pilote BNO085 (IMU 9 axes) en C pur, protocole SHTP sur I2C.
 * L'acces au bus I2C et aux broches INT / RST est fourni par la plateforme
 * (fonctions plateforme_imu_xxx dans app.h).
 *
 * Le capteur fournit le "Rotation Vector" (fusion accel + gyro + magneto),
 * converti en roulis / tangage / cap. Axes de la carte Adafruit :
 *   Y = avant du bateau, X = tribord, Z = haut.
 */
#ifndef BNO085_H
#define BNO085_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BNO085_ADRESSE_I2C 0x4A

typedef struct {
    int16_t roulis_cdeg;   /* + = tribord vers le bas */
    int16_t tangage_cdeg;  /* + = avant vers le haut */
    uint16_t cap_cdeg;     /* 0 a 35999, 0 = nord magnetique, sens horaire */
    uint8_t precision;     /* 0 non fiable, 1 faible, 2 moyenne, 3 haute (calibration magneto) */
    uint32_t date_ms;      /* moment de reception */
} bno085_mesure_t;

typedef struct {
    uint32_t mesures;
    uint32_t redemarrages;   /* resets faits par l'ESP */
    uint32_t resets_capteur; /* redemarrages spontanes du capteur */
    uint32_t erreurs_i2c;
} bno085_stats_t;

/* periode_ms : intervalle entre deux mesures demande au capteur */
void bno085_init(uint32_t periode_ms);
void bno085_tick(uint32_t maintenant);

/* Derniere mesure si elle date de moins de age_max_ms, sinon false */
bool bno085_mesure(uint32_t maintenant, uint32_t age_max_ms, bno085_mesure_t *m);
const bno085_stats_t *bno085_stats(void);
bool bno085_en_marche(void);

#ifdef __cplusplus
}
#endif

#endif /* BNO085_H */
