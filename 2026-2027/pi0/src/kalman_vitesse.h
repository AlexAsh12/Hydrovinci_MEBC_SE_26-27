/* Vitesse du bateau : filtre de Kalman a 2 etats.
 *
 *   etat     v = vitesse (m/s), b = biais de l'accelerometre (m/s2)
 *   prediction   v <- v + (a_mesuree - b) * dt     (acceleration avant de l'IMU, 5 Hz)
 *   correction   v_gps (vitesse sur le fond du SIM7600, 1 Hz)
 *
 * Le GPS recale la vitesse et, au fil des mesures, le biais de
 * l'accelerometre : le filtre se corrige tout seul. Entre deux mesures GPS
 * (ou si le GPS decroche quelques secondes), l'accelerometre fait le lien.
 * Sans IMU : vitesse GPS lissee. Sans GPS trop longtemps : vitesse inconnue.
 *
 * Fonctions pures (pas d'horloge interne) : testees sur PC. */
#ifndef KALMAN_VITESSE_H
#define KALMAN_VITESSE_H

#include <stdint.h>

typedef enum {
    KV_INCONNUE = 0,   /* jamais de GPS, ou incertitude trop grande */
    KV_GPS_IMU,        /* GPS et accelerometre a jour */
    KV_GPS,            /* GPS seul */
    KV_IMU             /* GPS perdu depuis peu : accelerometre seul */
} kv_source_t;

typedef struct {
    double v, b;              /* etat */
    double p00, p01, p11;     /* covariance (symetrique) */
    uint64_t t_ms;            /* instant de l'etat */
    double acc;               /* derniere acceleration recue (m/s2) */
    uint64_t t_acc_ms;        /* 0 = jamais */
    uint64_t t_gps_ms;        /* 0 = jamais */
    int initialise;
    int rejets;               /* mesures GPS ecartees d'affilee */
} kalman_vitesse_t;

typedef struct {
    double v_ms;              /* >= 0 */
    double sigma_ms;          /* ecart-type estime */
    double biais_ms2;
    kv_source_t source;
} kv_estimation_t;

void kv_init(kalman_vitesse_t *k);

/* Nouvelle acceleration avant (m/s2, gravite retiree) mesuree a l'instant t */
void kv_acceleration(kalman_vitesse_t *k, double acc_ms2, uint64_t t_ms);

/* Nouvelle vitesse GPS (m/s) mesuree a l'instant t. Renvoie 1 si acceptee,
 * 0 si ecartee (valeur aberrante). */
int kv_gps(kalman_vitesse_t *k, double v_ms, uint64_t t_ms);

/* Estimation a l'instant t (extrapolee, ne modifie pas le filtre) */
void kv_estimer(const kalman_vitesse_t *k, uint64_t t_ms, kv_estimation_t *e);

const char *kv_nom_source(kv_source_t s);

#endif /* KALMAN_VITESSE_H */
