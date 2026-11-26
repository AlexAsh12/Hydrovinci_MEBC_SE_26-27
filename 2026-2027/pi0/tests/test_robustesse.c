/*
 * Tests de robustesse : des millions d'entrees aleatoires ou malveillantes
 * envoyees a chaque module. Un module ne doit jamais planter, ni ecrire hors
 * de ses tampons, ni sortir de ses limites, quoi que lui envoient l'ESP32,
 * le module SIM7600, un SMS, le .env ou un navigateur.
 *
 *   make robustesse           avec AddressSanitizer + UBSan (detecte les
 *                             debordements et comportements indefinis)
 *   ./test_robustesse [N]     N = multiplicateur du nombre d'essais (defaut 1)
 *
 * Generateur pseudo-aleatoire a graine fixe : un echec se reproduit a
 * l'identique.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "carte_zones.h"
#include "env.h"
#include "kalman_vitesse.h"
#include "protocole.h"
#include "sim_analyse.h"
#include "sim_moteur.h"
#include "sms_commandes.h"
#include "trace.h"

static long echecs, verifs;
static int facteur = 1;

#define VERIF(cond)                                                                   \
    do {                                                                              \
        verifs++;                                                                     \
        if (!(cond)) {                                                                \
            if (echecs++ < 20) printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #cond); \
        }                                                                             \
    } while (0)

/* ------------------------------------------------------------------ */
/* Hasard reproductible                                                */
/* ------------------------------------------------------------------ */

static uint64_t graine = 0x9E3779B97F4A7C15ull;

static uint32_t hasard(void)
{
    graine ^= graine << 13;
    graine ^= graine >> 7;
    graine ^= graine << 17;
    return (uint32_t)(graine >> 16);
}

static uint32_t entre(uint32_t n)
{
    return n ? hasard() % n : 0;
}

/* Morceaux de vraies reponses du SIM7600, melanges a des octets quelconques */
static const char *const morceaux[] = {
    "\r\n", "\r", "\n", "OK", "ERROR", "> ", ">", "RDY", "+CMTI: \"SM\",3", "+CPIN: READY", "+CPIN: SIM PIN",
    "+CME ERROR: SIM not inserted", "+CMS ERROR: 500", "+CSQ: 18,99", "+CSQ: 99,99", "+CREG: 0,1", "+CEREG: 2,5,\"1A\"",
    "+COPS: 0,0,\"Orange F\",7", "+COPS: 0,0,\"", "+CGPSINFO: 4717.112671,N,00833.914843,E,261126,143205.0,1,5,9",
    "+CGPSINFO: ,,,,,,,,", "+CGPSINFO: 9999.9,N,99999,E,,,,,", "+CMGL: 1,\"REC UNREAD\",\"+33612345678\",,\"x\"",
    "+CMGL: 99999999999,\"\",\"", "+CMGL: ", "00480065", "D83D", "\"", ",,,,,,,,,,,,,,,,,,,,", "AT+CGPSINFO",
    "+CMGS: 4", "SMS DONE", "\x1A", "\x1B", "\0", "\xFF\xFE",
};
#define NB_MORCEAUX (sizeof morceaux / sizeof morceaux[0])

/* Remplit avec un melange de morceaux et d'octets quelconques */
static size_t brouiller(char *t, size_t taille)
{
    size_t n = 0;
    const size_t voulu = entre((uint32_t)taille);
    while (n < voulu) {
        if (entre(3) == 0) {
            t[n++] = (char)hasard();
        } else {
            const char *m = morceaux[entre(NB_MORCEAUX)];
            const size_t l = m[0] ? strlen(m) : 1;
            if (n + l > voulu) break;
            memcpy(t + n, m, l);
            n += l;
        }
    }
    return n;
}

/* Chaine terminee aleatoire (au plus taille - 1 caracteres) */
static void chaine(char *t, size_t taille)
{
    size_t n = brouiller(t, taille - 1);
    for (size_t i = 0; i < n; ++i) {
        if (t[i] == '\0') t[i] = ' ';
    }
    t[n] = '\0';
}

/* Tampon de sortie avec gardes : verifie qu'une fonction n'ecrit jamais
 * au-dela de la taille annoncee et laisse une chaine terminee */
#define GARDE 16
static unsigned char sortie[1024 + GARDE];

static void preparer(size_t taille)
{
    memset(sortie, 0xA5, taille + GARDE);
}

static int intacte(size_t taille)
{
    for (size_t i = taille; i < taille + GARDE; ++i) {
        if (sortie[i] != 0xA5) return 0;
    }
    return taille == 0 || memchr(sortie, '\0', taille) != NULL;
}

/* ------------------------------------------------------------------ */
/* Analyse AT, textes SMS, .env, carte                                 */
/* ------------------------------------------------------------------ */

static void analyseurs(void)
{
    printf("- analyse des reponses AT, textes, .env, carte\n");
    char ligne[600];
    for (long k = 0; k < 100000L * facteur; ++k) {
        chaine(ligne, sizeof ligne);
        sim_gps_t g;
        int v, idx;
        const int r = sa_cgpsinfo(ligne, &g);
        VERIF(r >= -1 && r <= 1);
        if (r == 1) {
            VERIF(isfinite(g.lat) && fabs(g.lat) <= 90.0 && isfinite(g.lon) && fabs(g.lon) <= 180.0);
            VERIF(g.vitesse_ms >= 0.0 && g.vitesse_ms < 200.0);
            VERIF(memchr(g.heure, '\0', sizeof g.heure) != NULL && memchr(g.date, '\0', sizeof g.date) != NULL);
        }
        if (sa_csq(ligne, &v)) VERIF((v >= 0 && v <= 31) || v == 99);
        if (sa_creg(ligne, &v)) VERIF(v >= 0 && v <= 11);
        VERIF(sa_cpin(ligne) <= SIM_CARTE_ABSENTE);

        const size_t t1 = 1 + entre(40), t2 = 1 + entre(40), t3 = 1 + entre(40);
        char a[41 + GARDE], b[41 + GARDE], c[41 + GARDE];
        memset(a, 0x5A, sizeof a);
        memset(b, 0x5A, sizeof b);
        memset(c, 0x5A, sizeof c);
        if (sa_cmgl(ligne, &idx, a, t1, b, t2, c, t3)) {
            VERIF(idx >= 0 && idx <= 9999);
            VERIF(memchr(a, '\0', t1) && memchr(b, '\0', t2) && memchr(c, '\0', t3));
        }
        for (size_t i = 0; i < GARDE; ++i) {
            VERIF((unsigned char)a[t1 + i] == 0x5A && (unsigned char)b[t2 + i] == 0x5A &&
                  (unsigned char)c[t3 + i] == 0x5A);
        }
        if (sa_cops(ligne, a, t1, &v)) VERIF(memchr(a, '\0', t1) != NULL);

        /* Petites tailles de sortie : 1, 2, 3 octets... */
        const size_t t = entre(64);
        preparer(t);
        sa_decoder_texte(ligne, (char *)sortie, t);
        VERIF(intacte(t));
        preparer(t);
        sa_vers_ascii(ligne, (char *)sortie, t, entre(200));
        VERIF(intacte(t));
        if (t) {
            for (const unsigned char *p = sortie; *p; ++p) VERIF(*p == '\n' || (*p >= 0x20 && *p < 0x7F));
        }
        (void)sa_numero_valide(ligne);
        (void)sa_numeros_egaux(ligne, "+33612345678");
        (void)sa_debut_egal(ligne, "POSITION");

        preparer(t);
        env_chercher(ligne, "SIM_NUMEROS", (char *)sortie, t);
        VERIF(intacte(t));
        if (t >= 2) {
            const size_t te = 1 + entre(24);
            preparer(4 * te);
            const int n = env_decouper_liste(ligne, (char *)sortie, te, 4);
            VERIF(n >= 0 && n <= 4);
            for (int i = 0; i < n; ++i) VERIF(memchr(sortie + (size_t)i * te, '\0', te) != NULL);
            for (size_t i = 4 * te; i < 4 * te + GARDE; ++i) VERIF(sortie[i] == 0xA5);
        }

        cz_rect_t rects[4];
        const int nr = cz_lire(ligne, rects, 4, NULL);
        VERIF(nr >= 0 && nr <= 4);
        for (int i = 0; i < nr; ++i) VERIF(rects[i].zmin <= rects[i].zmax && rects[i].zmax <= CZ_ZOOM_MAX);
        int z, x, y;
        if (cz_analyser_url(ligne, &z, &x, &y)) {
            VERIF(z >= 0 && z <= CZ_ZOOM_MAX && x >= 0 && y >= 0 && x < (1 << z) && y < (1 << z));
            VERIF(strstr(ligne, "..") == NULL);
        }
    }

    /* URL de tuile : chemins piegés */
    static const char *const pieges[] = {
        "/tuiles/16/1/1.png/../../etc", "/tuiles/1/2/3.png", "/tuiles/99999999/1/1.png",
        "/tuiles/16/9999999999/1.png", "/tuiles/+1/1/1.png", "/tuiles/ 1/1/1.png", "/tuiles/1/1/1.PNG",
    };
    for (size_t i = 0; i < sizeof pieges / sizeof pieges[0]; ++i) {
        int z, x, y;
        VERIF(!cz_analyser_url(pieges[i], &z, &x, &y));
    }
    for (long k = 0; k < 200000L * facteur; ++k) {
        int x, y;
        const double lat = (double)(int32_t)hasard() / 1e7, lon = (double)(int32_t)hasard() / 1e7;
        const int z = (int)entre(25) - 2;
        if (cz_tuile(lat, lon, z, &x, &y)) VERIF(x >= 0 && y >= 0 && x < (1 << z) && y < (1 << z));
    }
    int x, y;
    VERIF(!cz_tuile(NAN, 0, 10, &x, &y) && !cz_tuile(0, INFINITY, 10, &x, &y));
}

/* ------------------------------------------------------------------ */
/* Dialogue AT : module qui repond n'importe quoi                      */
/* ------------------------------------------------------------------ */

static long octets_ecrits, sms_recus_bruts;
static int ecriture_hs;

static int ecrire_rien(void *ctx, const char *d, size_t n)
{
    (void)ctx;
    /* Toute commande se termine par CR, un corps de SMS par Ctrl+Z, ou ECHAP */
    VERIF(n > 0 && n <= 200);
    VERIF(d[n - 1] == '\r' || d[n - 1] == 0x1A || d[n - 1] == 0x1B);
    for (size_t i = 0; i + 1 < n; ++i) VERIF(d[i] != '\r' && d[i] != 0x1A && d[i] != 0x1B);
    octets_ecrits += (long)n;
    return ecriture_hs && entre(4) == 0 ? -1 : 0;
}

static void gps_recu(void *ctx, const sim_gps_t *g, int fix)
{
    (void)ctx;
    VERIF(fix ? g != NULL : g == NULL);
    if (g) VERIF(isfinite(g->lat) && isfinite(g->lon) && fabs(g->lat) <= 90.0 && fabs(g->lon) <= 180.0);
}

static sim_moteur_t moteur;

static void sms_recu(void *ctx, const char *numero, const char *texte, const char *horodatage)
{
    (void)ctx;
    sms_recus_bruts++;
    VERIF(strlen(numero) < SIM_TAILLE_NUMERO && strlen(horodatage) < 32 && strlen(texte) < SIM_TAILLE_TEXTE_RECU);
    /* Comme tableau_bord : reponse a l'expediteur depuis le rappel */
    sim_moteur_envoyer_sms(&moteur, numero, texte, 0);
}

static void rien(void *ctx, const char *t)
{
    (void)ctx;
    VERIF(strlen(t) < 200);
}

static void moteur_fou(void)
{
    printf("- dialogue AT avec un module qui repond n'importe quoi\n");
    const sim_rappels_t r = {NULL, ecrire_rien, gps_recu, sms_recu, rien};
    sim_moteur_init(&moteur, &r);
    sim_moteur_definir_pin(&moteur, "1234");
    uint64_t t = 1000;
    sim_moteur_ouvert(&moteur, t);
    char tampon[300];
    for (long k = 0; k < 200000L * facteur; ++k) {
        switch (entre(200)) {
        case 0: sim_moteur_ferme(&moteur); break;
        case 1: sim_moteur_ouvert(&moteur, t); break;
        case 2: ecriture_hs = !ecriture_hs; break;
        case 3: {
            char num[40], texte[400];
            chaine(num, sizeof num);
            chaine(texte, sizeof texte);
            sim_moteur_envoyer_sms(&moteur, entre(2) ? "+33612345678" : num, texte, t);
            break;
        }
        case 4: t -= entre(50); break;              /* horloge relue un peu avant */
        case 5: t += 3600000u; break;               /* une heure sans rien */
        default: break;
        }
        const size_t n = brouiller(tampon, sizeof tampon);
        sim_moteur_recevoir(&moteur, tampon, n, t);
        t += entre(400);
        sim_moteur_tick(&moteur, t);

        VERIF(moteur.nb_file >= 0 && moteur.nb_file <= SIM_FILE_ENVOI);
        VERIF(moteur.nb_recus >= 0 && moteur.nb_recus <= SIM_MAX_RECUS);
        VERIF(moteur.nb_a_supprimer >= 0 && moteur.nb_a_supprimer <= SIM_MAX_SUPPRESSIONS);
        VERIF(moteur.ligne_len < sizeof moteur.ligne);
        VERIF(moteur.carte <= SIM_CARTE_ABSENTE && moteur.modem <= SIM_MODEM_PRET);
        VERIF(moteur.rssi >= -1 && moteur.rssi <= 31);
        VERIF(moteur.etape_init >= 0 && moteur.etape_init <= 3 && moteur.etape_sms >= 0 && moteur.etape_sms <= 5);
        VERIF(memchr(moteur.operateur, '\0', sizeof moteur.operateur) != NULL);
        for (int i = 0; i < moteur.nb_file; ++i) VERIF(sa_numero_valide(moteur.file[i].numero));
        if (moteur.modem == SIM_MODEM_ABSENT) VERIF(!moteur.gps_fix && moteur.cmd == 0);
    }
    printf("  %ld octets de commandes, %ld SMS remontes\n", octets_ecrits, sms_recus_bruts);
}

/* ------------------------------------------------------------------ */
/* Kalman, trace, commandes SMS                                        */
/* ------------------------------------------------------------------ */

static double valeur_folle(void)
{
    switch (entre(12)) {
    case 0: return NAN;
    case 1: return INFINITY;
    case 2: return -INFINITY;
    case 3: return 1e300;
    case 4: return -1e300;
    case 5: return 0.0;
    default: return ((double)(int32_t)hasard()) / 1e6;
    }
}

static trace_t trace;
static char json[64 * 1024 + GARDE];

static void calculs(void)
{
    printf("- Kalman, trace GPS, commandes SMS avec des valeurs folles\n");
    kalman_vitesse_t k;
    kv_init(&k);
    uint64_t t = 1000000;
    for (long i = 0; i < 500000L * facteur; ++i) {
        switch (entre(4)) {
        case 0: t += entre(5) == 0 ? 7200000u : entre(300); break;
        case 1: t -= entre(20); break;  /* horloges lues dans un autre ordre */
        default: t += entre(250); break;
        }
        if (entre(2)) kv_acceleration(&k, valeur_folle(), t);
        else kv_gps(&k, valeur_folle(), t);
        if (entre(100000) == 0) kv_init(&k);
        kv_estimation_t e;
        kv_estimer(&k, t + entre(5000), &e);
        VERIF(isfinite(e.v_ms) && e.v_ms >= 0.0 && isfinite(e.sigma_ms) && e.sigma_ms >= 0.0);
        VERIF(isfinite(k.v) && isfinite(k.b) && isfinite(k.p00) && isfinite(k.p11) && k.p00 >= 0.0);
        VERIF(e.source <= KV_IMU);
    }

    trace_init(&trace, 3.0);
    for (long i = 0; i < 100000L * facteur; ++i) {
        trace_ajouter(&trace, entre(10) ? 47.0 + valeur_folle() * 1e-6 : valeur_folle(),
                      entre(10) ? 2.0 + valeur_folle() * 1e-6 : valeur_folle());
        VERIF(trace.n >= 0 && trace.n <= TRACE_MAX);
        if (entre(20000) == 0) {
            const size_t taille = entre(sizeof json - GARDE);
            memset(json, 0xA5, taille + GARDE);
            const size_t n = trace_json(&trace, json, taille);
            VERIF(taille == 0 ? n == 0 : n < taille);
            for (size_t j = taille; j < taille + GARDE; ++j) VERIF((unsigned char)json[j] == 0xA5);
        }
    }
    for (int i = 0; i < trace.n; ++i) {
        VERIF(abs(trace.p[i].lat_e7) <= 900000000 && trace.p[i].lon_e7 >= -1800000000 &&
              trace.p[i].lon_e7 <= 1800000000);
    }

    char texte[300], reponse[200 + GARDE];
    for (long i = 0; i < 200000L * facteur; ++i) {
        sms_contexte_t c;
        c.gps_fix = (int)entre(2);
        c.lat = valeur_folle();
        c.lon = valeur_folle();
        chaine(c.heure_utc, sizeof c.heure_utc);
        c.vitesse_connue = (int)entre(2);
        c.vitesse_ms = valeur_folle();
        c.cap_connu = (int)entre(2);
        c.cap_deg = valeur_folle();
        c.esp_present = (int)entre(2);
        c.soc_dixieme[0] = (int)hasard() - 1000;
        c.soc_dixieme[1] = -1;
        c.puissance_connue = (int)entre(2);
        c.puissance_mw = (long)(int32_t)hasard();
        c.signal_rssi = (int)entre(120) - 10;
        chaine(texte, sizeof texte);
        if (entre(3) == 0) memcpy(texte, "POSITION", 9);
        memset(reponse, 0xA5, sizeof reponse);
        const size_t taille = 1 + entre(200);
        if (sms_commandes_traiter(texte, &c, reponse, taille) == SMS_REPONDRE) {
            VERIF(strlen(reponse) <= 160 && strlen(reponse) < taille);
            for (const char *p = reponse; *p; ++p) VERIF(*p == '\n' || (*p >= 0x20 && *p < 0x7F));
        }
        for (size_t j = taille; j < taille + GARDE && j < sizeof reponse; ++j) {
            VERIF((unsigned char)reponse[j] == 0xA5);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Trames de l'ESP32 : octets parasites et contenus aberrants           */
/* ------------------------------------------------------------------ */

static void trames_esp(void)
{
    printf("- trames ESP32 aberrantes (CRC valide, contenu quelconque)\n");
    static proto_decodeur_t d;
    proto_decodeur_init(&d);
    uint8_t charge[PROTO_MAX_PAYLOAD], trame[PROTO_MAX_TRAME], bruit[256];
    long trames = 0;
    for (long i = 0; i < 100000L * facteur; ++i) {
        /* Une trame bien formee (CRC juste) au contenu aleatoire, entouree de bruit */
        const uint16_t len = (uint16_t)entre(entre(10) ? 64 : PROTO_MAX_PAYLOAD + 1);
        for (uint16_t j = 0; j < len; ++j) charge[j] = (uint8_t)hasard();
        const size_t n = proto_encoder_trame(trame, sizeof trame, (uint8_t)hasard(), (uint16_t)i, charge, len);
        const size_t nb = entre(sizeof bruit);
        for (size_t j = 0; j < nb; ++j) bruit[j] = (uint8_t)hasard();

        const uint8_t *morceaux_esp[2] = {bruit, trame};
        const size_t tailles[2] = {nb, n};
        for (int m = 0; m < 2; ++m) {
            size_t pos = 0;
            while (pos < tailles[m]) {
                pos += proto_decodeur_ajouter(&d, morceaux_esp[m] + pos, tailles[m] - pos);
                proto_trame_t t;
                while (proto_decodeur_suivante(&d, &t)) {
                    trames++;
                    VERIF(t.len <= PROTO_MAX_PAYLOAD);
                    /* Tous les decodeurs sur tout contenu : comme traiter_trame() */
                    static proto_bms_t bms;
                    static proto_temp_t temp;
                    proto_hello_t he;
                    proto_hb_t hb;
                    proto_imu_t imu;
                    proto_mouvement_t mv;
                    proto_power_t pw;
                    proto_pilot_t pi;
                    proto_alert_t al;
                    char niveau, texte[256];
                    (void)proto_decoder_hello(t.payload, t.len, &he);
                    (void)proto_decoder_hb(t.payload, t.len, &hb);
                    (void)proto_decoder_imu(t.payload, t.len, &imu);
                    (void)proto_decoder_mouvement(t.payload, t.len, &mv);
                    (void)proto_decoder_power(t.payload, t.len, &pw);
                    (void)proto_decoder_pilot(t.payload, t.len, &pi);
                    (void)proto_decoder_alert(t.payload, t.len, &al);
                    if (proto_decoder_bms(t.payload, t.len, &bms)) {
                        VERIF(bms.nb_cellules <= PROTO_MAX_CELLULES && bms.nb_temps <= PROTO_MAX_TEMPS_BMS);
                    }
                    if (proto_decoder_temp(t.payload, t.len, &temp)) VERIF(temp.nb <= PROTO_MAX_SONDES);
                    if (proto_decoder_log(t.payload, t.len, &niveau, texte, sizeof texte)) {
                        VERIF(memchr(texte, '\0', sizeof texte) != NULL);
                    }
                }
            }
        }
    }
    printf("  %ld trames decodees, %lu rejetees par le CRC\n", trames, (unsigned long)d.erreurs_crc);
}

int main(int argc, char **argv)
{
    if (argc > 1) facteur = atoi(argv[1]) > 0 ? atoi(argv[1]) : 1;
    printf("Tests de robustesse (x%d)\n", facteur);
    analyseurs();
    moteur_fou();
    calculs();
    trames_esp();
    printf("RESULTAT : %ld verifications, %ld echec(s)\n", verifs, echecs);
    return echecs ? 1 : 0;
}
