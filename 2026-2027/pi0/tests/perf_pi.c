/*
 * Mesures de performance des traitements de la Pi, a lancer sur la Pi :
 *   make perf
 *
 * Pour chaque traitement : debit mesure, debit reellement necessaire sur le
 * bateau, et part du temps processeur (un coeur) qu'il consomme en
 * fonctionnement normal. Tout doit rester tres loin de 100 %.
 */
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "kalman_vitesse.h"
#include "protocole.h"
#include "sim_moteur.h"
#include "sms_commandes.h"
#include "trace.h"

static double maintenant_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static double charge_totale;

/* par_seconde : operations faites par seconde ; besoin : operations par
 * seconde sur le bateau */
static void resultat(const char *nom, double par_seconde, double besoin, const char *unite)
{
    const double part = 100.0 * besoin / par_seconde;
    charge_totale += part;
    printf("%-34s %12.0f %-9s besoin %8.0f  -> %7.4f %% d'un coeur\n", nom, par_seconde, unite, besoin, part);
}

/* Fait tourner f pendant au moins 0,5 s, renvoie operations par seconde */
#define MESURER(ops_par_tour, corps)                                  \
    ({                                                                \
        double _debut = maintenant_s(), _fin;                         \
        long _tours = 0;                                              \
        do {                                                          \
            for (int _k = 0; _k < 64; ++_k) { corps; }                \
            _tours += 64;                                             \
            _fin = maintenant_s();                                    \
        } while (_fin - _debut < 0.5);                                \
        (double)_tours * (ops_par_tour) / (_fin - _debut);            \
    })

static volatile double puits;   /* empeche le compilateur de tout supprimer */

static int ecrire_rien(void *c, const char *d, size_t n)
{
    (void)c;
    (void)d;
    (void)n;
    return 0;
}

int main(void)
{
    printf("Mesures de performance (un coeur de la Pi)\n\n");
    printf("%-34s %12s %-9s %15s\n", "traitement", "capacite", "", "besoin reel");

    /* Liaison ESP32 : 921600 bauds = 92 160 octets/s au maximum */
    static uint8_t flux[64 * 1024];
    size_t taille_flux = 0;
    {
        proto_imu_t imu = {123, -45, 9000, 3, 250};
        uint8_t charge[64], trame[128];
        uint16_t seq = 0;
        while (taille_flux + sizeof trame < sizeof flux) {
            const size_t l = proto_encoder_imu(&imu, charge, sizeof charge);
            taille_flux += proto_encoder_trame(flux + taille_flux, sizeof flux - taille_flux, 0x13, seq++, charge,
                                               (uint16_t)l);
        }
    }
    static proto_decodeur_t d;
    const double octets_s = MESURER((double)taille_flux, {
        proto_decodeur_init(&d);
        size_t pos = 0;
        while (pos < taille_flux) {
            pos += proto_decodeur_ajouter(&d, flux + pos, taille_flux - pos);
            proto_trame_t t;
            proto_imu_t m;
            while (proto_decodeur_suivante(&d, &t)) puits += proto_decoder_imu(t.payload, t.len, &m);
        }
    });
    resultat("decodage trames ESP32 (octets)", octets_s, 92160.0, "octets/s");

    /* Kalman : 5 accelerations + 1 GPS + 5 estimations par seconde */
    kalman_vitesse_t k;
    kv_init(&k);
    uint64_t t = 1000;
    const double kalman_s = MESURER(1.0, {
        t += 200;
        kv_acceleration(&k, 0.1, t);
        if (t % 1000 == 0) kv_gps(&k, 3.0, t);
        kv_estimation_t e;
        kv_estimer(&k, t, &e);
        puits += e.v_ms;
    });
    resultat("filtre de Kalman (pas de 200 ms)", kalman_s, 5.0, "pas/s");

    /* Dialogue AT : ~6 lignes par seconde (GPS, signal, reseau...) */
    static sim_moteur_t mo;
    const sim_rappels_t r = {NULL, ecrire_rien, NULL, NULL, NULL};
    sim_moteur_init(&mo, &r);
    sim_moteur_ouvert(&mo, 1);
    static const char reponse[] = "\r\n+CGPSINFO: 4717.112671,N,00833.914843,E,261126,143205.0,123.4,5.4,90.0\r\n\r\nOK\r\n";
    uint64_t tm = 1;
    const double lignes_s = MESURER(2.0, {
        tm += 1000;
        sim_moteur_tick(&mo, tm);
        sim_moteur_recevoir(&mo, reponse, sizeof reponse - 1, tm);
    });
    resultat("lignes AT du SIM7600", lignes_s, 6.0, "lignes/s");

    /* Trace : un point par seconde ; JSON complet a chaque ouverture d'ecran */
    static trace_t tr;
    trace_init(&tr, 3.0);
    double lat = 47.0;
    const double points_s = MESURER(1.0, {
        lat += 0.0001;
        trace_ajouter(&tr, lat, 2.0);
    });
    resultat("ajout d'un point de trace", points_s, 1.0, "points/s");
    trace_init(&tr, 3.0);
    for (int i = 0; i < TRACE_MAX; ++i) trace_ajouter(&tr, 47.0 + i * 0.0001, -1.0 - i * 0.0001);
    static char json[64 * 1024];
    const double json_s = MESURER(1.0, { puits += (double)trace_json(&tr, json, sizeof json); });
    resultat("trace complete en JSON (1500 pts)", json_s, 0.1, "/s");

    /* Reponse a un SMS */
    sms_contexte_t c;
    memset(&c, 0, sizeof c);
    c.gps_fix = c.vitesse_connue = c.cap_connu = 1;
    c.lat = 43.7347;
    c.lon = 7.4206;
    c.vitesse_ms = 4.2;
    strcpy(c.heure_utc, "14:32:05");
    c.soc_dixieme[0] = c.soc_dixieme[1] = 850;
    char rep[200];
    const double sms_s = MESURER(1.0, { puits += sms_commandes_traiter("POSITION", &c, rep, sizeof rep); });
    resultat("reponse a un SMS POSITION", sms_s, 0.01, "/s");

    printf("\nCharge totale des traitements : %.3f %% d'un coeur (la Pi Zero 2W en a 4)\n", charge_totale);
    printf("Le reste du temps sert au serveur web et a Chromium.\n");
    return 0;
}
