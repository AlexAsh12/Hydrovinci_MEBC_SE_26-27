/* Vitesse du bateau : filtre de Kalman GPS + accelerometre. */
#include "kalman_vitesse.h"

#include <math.h>

/* Reglages (unites SI). Bruits exprimes en densite : variance ajoutee = q * dt. */
#define Q_ACC_IMU      0.04     /* (0,2 m/s2)^2 : bruit de l'accelerometre integre */
#define Q_ACC_SANS_IMU 0.25     /* (0,5 m/s2)^2 : accelerations du bateau non mesurees */
#define Q_BIAIS        1e-4     /* (0,01 m/s2)^2 par seconde : derive lente du biais */
#define R_GPS          0.0625   /* (0,25 m/s)^2 : bruit de la vitesse GPS */
#define P_V_INITIAL    R_GPS
#define P_B_INITIAL    0.09     /* (0,3 m/s2)^2 : biais inconnu au depart */
#define BIAIS_MAX      1.0      /* m/s2 */
#define PORTE_SIGMA2   25.0     /* mesure ecartee au-dela de 5 ecarts-types */
#define REJETS_MAX     3        /* puis on repart de la mesure GPS */
#define ACC_PERIMEE_MS 600u     /* IMU a 5 Hz : 3 trames perdues */
#define GPS_PERIME_MS  3000u    /* GPS a 1 Hz : 3 mesures perdues */
#define SIGMA_MAX      1.0      /* au-dela (~2 noeuds), vitesse inconnue */
#define PAS_MAX_S      0.5      /* prediction decoupee en pas de 0,5 s au plus */

void kv_init(kalman_vitesse_t *k)
{
    k->v = k->b = 0.0;
    k->p00 = P_V_INITIAL;
    k->p01 = 0.0;
    k->p11 = P_B_INITIAL;
    k->t_ms = 0;
    k->acc = 0.0;
    k->t_acc_ms = 0;
    k->t_gps_ms = 0;
    k->initialise = 0;
    k->rejets = 0;
}

static int acc_a_jour(const kalman_vitesse_t *k, uint64_t t)
{
    return k->t_acc_ms && t >= k->t_acc_ms && t - k->t_acc_ms <= ACC_PERIMEE_MS;
}

/* Fait avancer l'etat (v, b, P) de dt secondes.
 * F = [1 -dt ; 0 1] avec l'acceleration mesuree, F = I sans IMU. */
static void predire_pas(double *v, double b, double *p00, double *p01, double *p11, double dt, int avec_imu,
                        double acc)
{
    if (avec_imu) {
        *v += (acc - b) * dt;
        const double n00 = *p00 - 2.0 * dt * *p01 + dt * dt * *p11 + Q_ACC_IMU * dt;
        const double n01 = *p01 - dt * *p11;
        *p00 = n00;
        *p01 = n01;
    } else {
        *p00 += Q_ACC_SANS_IMU * dt;
    }
    *p11 += Q_BIAIS * dt;
}

/* L'acceleration recue en t_acc vaut jusqu'a t_acc + ACC_PERIMEE_MS ; au-dela,
 * on predit sans IMU (vitesse constante, incertitude qui grandit vite). */
static void predire_jusqua(kalman_vitesse_t *k, uint64_t t)
{
    if (!k->initialise || t <= k->t_ms) {
        if (t > k->t_ms) k->t_ms = t;
        return;
    }
    uint64_t courant = k->t_ms;
    while (courant < t) {
        const uint64_t fin_imu = k->t_acc_ms + ACC_PERIMEE_MS;
        const int imu = k->t_acc_ms && courant >= k->t_acc_ms && courant < fin_imu;
        uint64_t fin = t;
        if (imu) {
            if (fin > fin_imu) fin = fin_imu;
            if (fin - courant > (uint64_t)(PAS_MAX_S * 1000.0)) fin = courant + (uint64_t)(PAS_MAX_S * 1000.0);
        }
        predire_pas(&k->v, k->b, &k->p00, &k->p01, &k->p11, (double)(fin - courant) / 1000.0, imu, k->acc);
        courant = fin;
    }
    k->t_ms = t;
}

void kv_acceleration(kalman_vitesse_t *k, double acc_ms2, uint64_t t_ms)
{
    if (!isfinite(acc_ms2) || fabs(acc_ms2) > 50.0) return;
    predire_jusqua(k, t_ms);  /* avec l'acceleration precedente */
    k->acc = acc_ms2;
    k->t_acc_ms = t_ms ? t_ms : 1;
}

static void repartir(kalman_vitesse_t *k, double v, uint64_t t)
{
    k->v = v;
    k->p00 = P_V_INITIAL;
    k->p01 = 0.0;
    if (!k->initialise) {
        k->b = 0.0;
        k->p11 = P_B_INITIAL;
    }
    k->t_ms = t;
    k->initialise = 1;
    k->rejets = 0;
}

int kv_gps(kalman_vitesse_t *k, double v_ms, uint64_t t_ms)
{
    if (!isfinite(v_ms) || v_ms < 0.0 || v_ms > 60.0) return 0;
    if (!k->initialise) {
        repartir(k, v_ms, t_ms);
        k->t_gps_ms = t_ms ? t_ms : 1;
        return 1;
    }
    predire_jusqua(k, t_ms);

    const double innovation = v_ms - k->v;
    const double s = k->p00 + R_GPS;
    if (innovation * innovation > PORTE_SIGMA2 * s) {
        if (++k->rejets < REJETS_MAX) return 0;
        repartir(k, v_ms, t_ms);  /* le filtre s'est perdu : on se recale */
        k->t_gps_ms = t_ms ? t_ms : 1;
        return 1;
    }
    k->rejets = 0;

    /* Gain K = P H' / S avec H = [1 0] */
    const double k0 = k->p00 / s, k1 = k->p01 / s;
    k->v += k0 * innovation;
    k->b += k1 * innovation;
    if (k->b > BIAIS_MAX) k->b = BIAIS_MAX;
    if (k->b < -BIAIS_MAX) k->b = -BIAIS_MAX;

    /* P = (I - K H) P */
    const double p00 = (1.0 - k0) * k->p00;
    const double p01 = (1.0 - k0) * k->p01;
    const double p11 = k->p11 - k1 * k->p01;
    k->p00 = p00;
    k->p01 = p01;
    k->p11 = p11 > 1e-9 ? p11 : 1e-9;

    k->t_gps_ms = t_ms ? t_ms : 1;
    return 1;
}

void kv_estimer(const kalman_vitesse_t *k, uint64_t t_ms, kv_estimation_t *e)
{
    e->v_ms = 0.0;
    e->sigma_ms = 0.0;
    e->biais_ms2 = k->b;
    e->source = KV_INCONNUE;
    if (!k->initialise) return;

    /* Une mesure horodatee juste apres t (autre lecture d'horloge) compte
     * comme recue a t */
    if (t_ms < k->t_ms) t_ms = k->t_ms;
    if (t_ms < k->t_acc_ms) t_ms = k->t_acc_ms;
    if (t_ms < k->t_gps_ms) t_ms = k->t_gps_ms;

    kalman_vitesse_t copie = *k;
    predire_jusqua(&copie, t_ms);

    const int gps = k->t_gps_ms && t_ms >= k->t_gps_ms && t_ms - k->t_gps_ms <= GPS_PERIME_MS;
    const int imu = acc_a_jour(k, t_ms);
    e->v_ms = copie.v > 0.0 ? copie.v : 0.0;
    e->sigma_ms = sqrt(copie.p00 > 0.0 ? copie.p00 : 0.0);

    if (e->sigma_ms > SIGMA_MAX) e->source = KV_INCONNUE;
    else if (gps) e->source = imu ? KV_GPS_IMU : KV_GPS;
    else e->source = imu ? KV_IMU : KV_INCONNUE;
}

const char *kv_nom_source(kv_source_t s)
{
    switch (s) {
    case KV_GPS_IMU: return "gps+imu";
    case KV_GPS: return "gps";
    case KV_IMU: return "imu";
    default: return "aucune";
    }
}
