/* Tests des modules de la Pi sans materiel : .env, analyse AT, Kalman,
 * trace GPS, commandes SMS et dialogue avec un faux module SIM7600.
 * Compilent sur Linux (make test) et sur PC. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "carte_zones.h"
#include "env.h"
#include "kalman_vitesse.h"
#include "sim_analyse.h"
#include "sim_moteur.h"
#include "sms_commandes.h"
#include "trace.h"

static int echecs, verifs;

#define VERIF(cond)                                                              \
    do {                                                                         \
        verifs++;                                                                \
        if (!(cond)) {                                                           \
            echecs++;                                                            \
            printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                        \
    } while (0)
#define PROCHE(a, b, tol) VERIF(fabs((double)(a) - (double)(b)) <= (tol))
#define TEXTE(a, b) VERIF(strcmp((a), (b)) == 0)

/* ------------------------------------------------------------------ */

static void test_env(void)
{
    const char *f =
        "# commentaire\r\n"
        "PI_HOTE=hydro.local\r\n"
        "SIM_NUMEROS_TEST=faux\n"
        "SIM_NUMEROS = '+33612345678, 0698765432'\n"
        "export SIM_PIN=\"1234\"\n"
        "VIDE=\n"
        "AVEC_COMMENTAIRE=abc # note\n";
    char v[64];
    VERIF(env_chercher(f, "PI_HOTE", v, sizeof v) && strcmp(v, "hydro.local") == 0);
    VERIF(env_chercher(f, "SIM_NUMEROS", v, sizeof v));
    TEXTE(v, "+33612345678, 0698765432");
    VERIF(env_chercher(f, "SIM_PIN", v, sizeof v));
    TEXTE(v, "1234");
    VERIF(env_chercher(f, "VIDE", v, sizeof v) && v[0] == '\0');
    VERIF(env_chercher(f, "AVEC_COMMENTAIRE", v, sizeof v));
    TEXTE(v, "abc");
    VERIF(!env_chercher(f, "ABSENT", v, sizeof v) && v[0] == '\0');
    VERIF(!env_chercher(f, "SIM", v, sizeof v));  /* pas un prefixe */

    char liste[4][24];
    VERIF(env_decouper_liste("+33612345678, 0698765432;+3361111 ", &liste[0][0], 24, 4) == 3);
    TEXTE(liste[1], "0698765432");
    TEXTE(liste[2], "+3361111");
    VERIF(env_decouper_liste(" +33 6 12 34 56 78 ,06 98", &liste[0][0], 24, 4) == 2);
    TEXTE(liste[0], "+33 6 12 34 56 78");
    TEXTE(liste[1], "06 98");
    VERIF(env_decouper_liste("", &liste[0][0], 24, 4) == 0);
}

static void test_analyse(void)
{
    sim_gps_t g;
    VERIF(sa_cgpsinfo("+CGPSINFO: 4717.112671,N,00833.914843,E,261126,143205.0,123.4,5.4,90.0", &g) == 1);
    PROCHE(g.lat, 47.0 + 17.112671 / 60.0, 1e-9);
    PROCHE(g.lon, 8.0 + 33.914843 / 60.0, 1e-9);
    PROCHE(g.vitesse_ms, 5.4 * 0.514444, 1e-6);
    PROCHE(g.altitude_m, 123.4, 1e-9);
    VERIF(g.cap_valide && fabs(g.cap_deg - 90.0) < 1e-9);
    TEXTE(g.heure, "14:32:05");
    TEXTE(g.date, "261126");
    VERIF(sa_cgpsinfo("+CGPSINFO: 4717.1,S,00133.9,W,261126,143205.0,1.0,0.0,", &g) == 1);
    VERIF(g.lat < 0 && g.lon < 0 && !g.cap_valide);
    VERIF(sa_cgpsinfo("+CGPSINFO: ,,,,,,,,", &g) == 0);
    VERIF(sa_cgpsinfo("+CGPSINFO: 4799.0,N,00133.9,E,,,,,", &g) == -1);  /* 99 minutes */
    VERIF(sa_cgpsinfo("+CGPSINFO: 4717.1,X,00133.9,E,,,,,", &g) == -1);
    VERIF(sa_cgpsinfo("+CSQ: 18,99", &g) == -1);

    int v;
    VERIF(sa_csq("+CSQ: 18,99", &v) && v == 18);
    VERIF(sa_csq("+CSQ: 99,99", &v) && v == 99);
    VERIF(!sa_csq("+CSQ: 45,99", &v));
    VERIF(sa_creg("+CREG: 0,1", &v) && v == 1);
    VERIF(sa_creg("+CEREG: 2,5,\"1A2B\",\"01A2B3C4\",7", &v) && v == 5);
    VERIF(sa_creg("+CREG: 3", &v) && v == 3);                    /* notification */
    VERIF(sa_creg("+CREG: 1,\"1A2B\",\"01A2B3C4\"", &v) && v == 1);  /* notification avec position */
    VERIF(sa_enregistre(1) && sa_enregistre(5) && !sa_enregistre(2));

    char nom[32];
    VERIF(sa_cops("+COPS: 0,0,\"Orange F\",7", nom, sizeof nom, &v) && v == 7);
    TEXTE(nom, "Orange F");
    TEXTE(sa_techno(v), "4G");
    VERIF(sa_cops("+COPS: 0", nom, sizeof nom, &v) && nom[0] == '\0' && v == -1);

    VERIF(sa_cpin("+CPIN: READY") == SIM_CARTE_PRETE);
    VERIF(sa_cpin("+CPIN: SIM PIN") == SIM_CARTE_PIN);
    VERIF(sa_cpin("+CPIN: SIM PUK") == SIM_CARTE_BLOQUEE);
    VERIF(sa_cpin("+CME ERROR: SIM not inserted") == SIM_CARTE_ABSENTE);
    VERIF(sa_cpin("+CME ERROR: 10") == SIM_CARTE_ABSENTE);
    VERIF(sa_cpin("+CME ERROR: incorrect password") == SIM_CARTE_INCONNUE);
    VERIF(sa_cpin("OK") == SIM_CARTE_INCONNUE);

    int idx;
    char statut[24], num[24], horo[32];
    VERIF(sa_cmgl("+CMGL: 3,\"REC UNREAD\",\"+33612345678\",,\"26/11/26,14:32:05+04\"", &idx, statut,
                  sizeof statut, num, sizeof num, horo, sizeof horo));
    VERIF(idx == 3);
    TEXTE(statut, "REC UNREAD");
    TEXTE(num, "+33612345678");
    TEXTE(horo, "26/11/26,14:32:05+04");
    VERIF(sa_cmti("+CMTI: \"SM\",4") && !sa_cmti("+CMGL: 1"));

    char t[64];
    sa_decoder_texte("00480065006C006C006F", t, sizeof t);
    TEXTE(t, "Hello");
    sa_decoder_texte("00E9007400E9", t, sizeof t);
    TEXTE(t, "\xC3\xA9t\xC3\xA9");
    sa_decoder_texte("D83DDC4D", t, sizeof t);
    TEXTE(t, "\xF0\x9F\x91\x8D");
    sa_decoder_texte("CAFE", t, sizeof t);  /* texte en clair qui ressemble a de l'hexa */
    TEXTE(t, "CAFE");
    sa_decoder_texte("1234", t, sizeof t);
    TEXTE(t, "1234");
    sa_decoder_texte("Bien recu\r\nA+", t, sizeof t);
    TEXTE(t, "Bien recu\nA+");

    sa_vers_ascii("P\xC3\xA9trole \xC3\xA0 15\xC2\xB0 \xF0\x9F\x91\x8D", t, sizeof t, 160);
    TEXTE(t, "Petrole a 15o ?");
    sa_vers_ascii("abcdef", t, sizeof t, 3);
    TEXTE(t, "abc");

    VERIF(sa_numero_valide("+33612345678") && sa_numero_valide("0612345678"));
    VERIF(!sa_numero_valide("06\"1;AT") && !sa_numero_valide("+") && !sa_numero_valide("12"));
    VERIF(sa_numeros_egaux("+33612345678", "0612345678"));
    VERIF(sa_numeros_egaux("+33 6 12 34 56 78", "06.12.34.56.78"));
    VERIF(!sa_numeros_egaux("+33612345678", "0612345679"));
    VERIF(!sa_numeros_egaux("", "0612345678"));
    VERIF(sa_debut_egal("position", "POSITION") && sa_debut_egal("  Pos ?", "POS"));
    VERIF(!sa_debut_egal("POSITIONS", "POSITION") && !sa_debut_egal("POSITION", "POS"));
}

/* ------------------------------------------------------------------ */

static void test_kalman(void)
{
    kalman_vitesse_t k;
    kv_estimation_t e;
    kv_init(&k);
    kv_estimer(&k, 1000, &e);
    VERIF(e.source == KV_INCONNUE);
    kv_acceleration(&k, 0.5, 1000);  /* sans GPS : toujours inconnue */
    kv_estimer(&k, 1100, &e);
    VERIF(e.source == KV_INCONNUE);

    /* Vitesse constante 5 m/s, accelerometre biaise de +0,2 m/s2 */
    kv_init(&k);
    uint64_t t = 1000;
    for (int i = 0; i < 300 * 5; ++i, t += 200) {
        kv_acceleration(&k, 0.2, t);
        if (i % 5 == 0) kv_gps(&k, 5.0, t);
    }
    kv_estimer(&k, t, &e);
    PROCHE(e.v_ms, 5.0, 0.1);
    PROCHE(e.biais_ms2, 0.2, 0.03);
    VERIF(e.source == KV_GPS_IMU);
    VERIF(e.sigma_ms < 0.35);

    /* GPS coupe : le biais appris garde la vitesse juste quelques secondes */
    const uint64_t coupure = t;
    for (int i = 0; i < 5 * 5; ++i, t += 200) kv_acceleration(&k, 0.2, t);
    kv_estimer(&k, t, &e);
    PROCHE(e.v_ms, 5.0, 0.15);
    VERIF(e.source == KV_IMU);
    for (int i = 0; i < 60 * 5; ++i, t += 200) kv_acceleration(&k, 0.2, t);
    kv_estimer(&k, t, &e);
    VERIF(e.source == KV_INCONNUE);  /* incertitude trop grande apres 1 min */
    VERIF(t - coupure > 60000);

    /* Acceleration reelle de 1 m/s2 : suivie entre deux mesures GPS */
    kv_init(&k);
    t = 1000;
    double vraie = 2.0;
    for (int i = 0; i < 30 * 5; ++i, t += 200) {  /* 30 s a vitesse constante */
        kv_acceleration(&k, 0.0, t);
        if (i % 5 == 0) kv_gps(&k, vraie, t);
    }
    for (int i = 0; i < 4 * 5; ++i, t += 200) {  /* 4 s d'acceleration */
        kv_acceleration(&k, 1.0, t);
        if (i % 5 == 0) kv_gps(&k, vraie, t);
        vraie += 0.2;
    }
    kv_estimer(&k, t, &e);
    PROCHE(e.v_ms, vraie, 0.35);

    /* Sans IMU : vitesse GPS lissee, source "gps" */
    kv_init(&k);
    t = 1000;
    for (int i = 0; i < 20; ++i, t += 1000) kv_gps(&k, 3.0 + ((i % 2) ? 0.1 : -0.1), t);
    kv_estimer(&k, t - 1000, &e);
    PROCHE(e.v_ms, 3.0, 0.15);
    VERIF(e.source == KV_GPS);
    kv_estimer(&k, t - 10, &e);  /* 0,99 s apres la mesure, sans IMU : toujours connue */
    VERIF(e.source == KV_GPS);
    kv_estimer(&k, t - 1001, &e);  /* horloge lue juste avant la mesure */
    VERIF(e.source == KV_GPS);
    kv_estimer(&k, t + 5000, &e);  /* GPS perime, pas d'IMU */
    VERIF(e.source == KV_INCONNUE);

    /* Valeur aberrante ecartee, puis recalage si elle persiste */
    kv_init(&k);
    t = 1000;
    for (int i = 0; i < 10; ++i, t += 1000) kv_gps(&k, 4.0, t);
    VERIF(kv_gps(&k, 30.0, t) == 0);
    kv_estimer(&k, t, &e);
    PROCHE(e.v_ms, 4.0, 0.2);
    t += 1000;
    VERIF(kv_gps(&k, 12.0, t) == 0);
    t += 1000;
    VERIF(kv_gps(&k, 12.0, t) == 1);  /* 3e d'affilee : recalage */
    kv_estimer(&k, t, &e);
    PROCHE(e.v_ms, 12.0, 0.01);
    VERIF(kv_gps(&k, -1.0, t) == 0);
}

/* ------------------------------------------------------------------ */

static trace_t trace;  /* gros : hors de la pile */

static void test_trace(void)
{
    trace_init(&trace, 3.0);
    VERIF(trace_ajouter(&trace, 47.0, -1.0));
    VERIF(!trace_ajouter(&trace, 47.00001, -1.0));  /* 1,1 m : trop pres */
    VERIF(trace_ajouter(&trace, 47.0001, -1.0));    /* 11 m */
    VERIF(!trace_ajouter(&trace, 91.0, 0.0));
    PROCHE(trace_distance_m(47.0, -1.0, 47.001, -1.0), 111.2, 0.5);
    VERIF(trace.n == 2);

    trace_effacer(&trace);
    for (int i = 0; i < TRACE_MAX + 1; ++i) trace_ajouter(&trace, 47.0 + i * 0.0001, -1.0);
    VERIF(trace.n == TRACE_MAX / 2 + 1);
    PROCHE(trace.pas_m, 6.0, 1e-9);
    VERIF(trace.p[0].lat_e7 == 470000000);
    VERIF(trace.p[trace.n - 1].lat_e7 == (int32_t)lround((47.0 + TRACE_MAX * 0.0001) * 1e7));

    static char json[64 * 1024];
    VERIF(trace_json(&trace, json, sizeof json) > 0);
    VERIF(strncmp(json, "{\"pas\":6,\"points\":[[470000000,-10000000],", 41) == 0);
    VERIF(trace_json(&trace, json, 20) == 0);
}

/* ------------------------------------------------------------------ */

static void test_carte(void)
{
    const char *conf =
        "# commentaire\n"
        "monaco    43.7180 43.7450 7.4050 7.4450 13 16   # port\n"
        "\n"
        "nanterre  48.8850 48.9030 2.1550 2.1850 15 16\n"
        "faux      48.9 48.8 2.1 2.2 13 16\n"          /* sud > nord */
        "court     48.8 48.9 2.1\n"
        "trop      48.8 48.9 2.1 2.2 13 20\n";         /* zoom > 19 */
    cz_rect_t r[8];
    int invalides = -1;
    VERIF(cz_lire(conf, r, 8, &invalides) == 2);
    VERIF(invalides == 3);
    TEXTE(r[0].nom, "monaco");
    PROCHE(r[0].est, 7.445, 1e-12);
    VERIF(r[0].zmin == 13 && r[0].zmax == 16);
    TEXTE(r[1].nom, "nanterre");
    VERIF(cz_lire(conf, r, 1, NULL) == 1);

    int x, y;
    /* Tuiles telechargees pour le port Hercule (carte/tuiles/16/34119/...) */
    VERIF(cz_tuile(43.7350, 7.4235, 16, &x, &y) && x == 34119);
    VERIF(cz_tuile(0.0, 0.0, 1, &x, &y) && x == 1 && y == 1);
    VERIF(cz_tuile(85.0, -180.0, 3, &x, &y) && x == 0 && y == 0);
    VERIF(cz_tuile(-85.0, 179.9999, 3, &x, &y) && x == 7 && y == 7);
    VERIF(!cz_tuile(89.0, 0.0, 3, &x, &y) && !cz_tuile(0.0, 0.0, 20, &x, &y));
    int x0, y0, x1, y1;
    cz_etendue(&r[0], 16, &x0, &y0, &x1, &y1);
    VERIF(x0 <= x1 && y0 <= y1 && x1 - x0 < 10 && y1 - y0 < 10);

    int z;
    VERIF(cz_analyser_url("/tuiles/16/34119/23899.png", &z, &x, &y) && z == 16 && x == 34119 && y == 23899);
    VERIF(!cz_analyser_url("/tuiles/16/34119/23899.tmp.png", &z, &x, &y));
    VERIF(!cz_analyser_url("/tuiles/16/../../etc/passwd", &z, &x, &y));
    VERIF(!cz_analyser_url("/tuiles/2/4/0.png", &z, &x, &y));        /* x >= 2^z */
    VERIF(!cz_analyser_url("/tuiles/20/0/0.png", &z, &x, &y));
    VERIF(!cz_analyser_url("/tuiles/16/-1/0.png", &z, &x, &y));
    VERIF(!cz_analyser_url("/tuiles/16/1/2.png?x", &z, &x, &y));
    VERIF(!cz_analyser_url("/tuiles//1/2.png", &z, &x, &y));
}

/* ------------------------------------------------------------------ */

static void test_sms_commandes(void)
{
    sms_contexte_t c;
    memset(&c, 0, sizeof c);
    c.gps_fix = 1;
    c.lat = 47.2183;
    c.lon = -1.5536;
    strcpy(c.heure_utc, "14:32:05");
    c.vitesse_connue = 1;
    c.vitesse_ms = 5.0;
    c.cap_connu = 1;
    c.cap_deg = 87.0;
    c.esp_present = 1;
    c.soc_dixieme[0] = 853;
    c.soc_dixieme[1] = -1;
    c.signal_rssi = 18;

    char r[200];
    VERIF(sms_commandes_traiter("position", &c, r, sizeof r) == SMS_REPONDRE);
    VERIF(strstr(r, "47.21830,-1.55360") != NULL);
    VERIF(strstr(r, "9.7 kn (18.0 km/h) cap 087") != NULL);
    VERIF(strlen(r) <= 160);
    VERIF(sms_commandes_traiter(" Pos", &c, r, sizeof r) == SMS_REPONDRE);
    VERIF(sms_commandes_traiter("ETAT", &c, r, sizeof r) == SMS_REPONDRE);
    VERIF(strstr(r, "Batt1 85% Batt2 ?") != NULL && strstr(r, "reseau 18/31") != NULL);
    VERIF(strlen(r) <= 160);
    VERIF(sms_commandes_traiter("aide", &c, r, sizeof r) == SMS_REPONDRE && strlen(r) <= 160);
    c.gps_fix = 0;
    c.vitesse_connue = 0;
    VERIF(sms_commandes_traiter("VITESSE", &c, r, sizeof r) == SMS_REPONDRE);
    VERIF(strstr(r, "Vit inconnue") != NULL);
    VERIF(sms_commandes_traiter("Position svp", &c, r, sizeof r) == SMS_REPONDRE);
    VERIF(strstr(r, "pas de position") != NULL);
    VERIF(sms_commandes_traiter("Rentrez au port", &c, r, sizeof r) == SMS_AFFICHER && r[0] == '\0');

    char liste[2][24] = {"+33612345678", "0698765432"};
    VERIF(sms_numero_autorise("0612345678", &liste[0][0], 24, 2));
    VERIF(sms_numero_autorise("+33698765432", &liste[0][0], 24, 2));
    VERIF(!sms_numero_autorise("+33600000000", &liste[0][0], 24, 2));
    VERIF(!sms_numero_autorise("", &liste[0][0], 24, 2));
}

/* ------------------------------------------------------------------ */
/* Faux SIM7600                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    char recu[2048];       /* ce que le moteur a ecrit, pas encore traite */
    size_t nr;
    char sortie[8192];     /* ce que le module va repondre */
    size_t ns;
    int echo, carte, gps_on, fix, reseau, muet, saisie;
    char pin[8];
    struct {
        int index;
        char numero[24], texte[200];
    } stockes[4];
    int nb_stockes;
    char envoyes[4][200];
    char envoyes_a[4][24];
    int nb_envoyes;
    char numero_saisie[24];
    int nb_cgpsinfo, nb_cmgs, nb_codes_pin;
} faux_t;

static faux_t faux;

/* Evenements remontes par le moteur */
static int nb_gps, dernier_fix, nb_sms;
static sim_gps_t dernier_gps;
static char dernier_sms[400], dernier_numero[24];
static char sms_lus[8][400];

static void sortir(const char *s)
{
    const size_t n = strlen(s);
    if (faux.ns + n < sizeof faux.sortie) {
        memcpy(faux.sortie + faux.ns, s, n);
        faux.ns += n;
    }
}

static void reponse(const char *info, const char *final)
{
    if (info) {
        sortir("\r\n");
        sortir(info);
        sortir("\r\n");
    }
    sortir("\r\n");
    sortir(final);
    sortir("\r\n");
}

static int sim_prete(void)
{
    if (faux.carte == 1) return 1;
    reponse(NULL, faux.carte == 0 ? "+CME ERROR: SIM not inserted" : "+CME ERROR: SIM PIN required");
    return 0;
}

static void faux_commande(const char *c)
{
    if (faux.muet) return;
    if (faux.echo) {
        sortir(c);
        sortir("\r");
    }
    char l[300];
    if (!strcmp(c, "ATE0")) { faux.echo = 0; reponse(NULL, "OK"); }
    else if (!strcmp(c, "AT+CMEE=2")) reponse(NULL, "OK");
    else if (!strcmp(c, "AT+CGPS=1")) {
        if (faux.gps_on) reponse(NULL, "ERROR");
        else { faux.gps_on = 1; reponse(NULL, "OK"); }
    } else if (!strcmp(c, "AT+CPIN?")) {
        if (faux.carte == 0) reponse(NULL, "+CME ERROR: SIM not inserted");
        else reponse(faux.carte == 1 ? "+CPIN: READY" : "+CPIN: SIM PIN", "OK");
    } else if (!strncmp(c, "AT+CPIN=\"", 9)) {
        faux.nb_codes_pin++;
        if (faux.carte == 2 && !strncmp(c + 9, faux.pin, strlen(faux.pin))) {
            faux.carte = 1;
            reponse(NULL, "OK");
            sortir("\r\n+CPIN: READY\r\n\r\nSMS DONE\r\n");
        } else {
            reponse(NULL, "+CME ERROR: incorrect password");
        }
    } else if (!strncmp(c, "AT+CMGF", 7) || !strncmp(c, "AT+CSCS", 7) || !strncmp(c, "AT+CSMP", 7) ||
               !strncmp(c, "AT+CPMS", 7) || !strncmp(c, "AT+CNMI", 7)) {
        if (sim_prete()) reponse(NULL, "OK");
    } else if (!strcmp(c, "AT+CGPSINFO")) {
        faux.nb_cgpsinfo++;
        if (!faux.gps_on) reponse(NULL, "ERROR");
        else reponse(faux.fix ? "+CGPSINFO: 4713.098000,N,00133.216000,W,261126,143205.0,12.0,9.7,87.5"
                              : "+CGPSINFO: ,,,,,,,,",
                     "OK");
    } else if (!strcmp(c, "AT+CMGL=\"ALL\"")) {
        if (!sim_prete()) return;
        /* Comme le vrai module : en-tete, texte, puis ligne vide avant OK */
        sortir("\r\n");
        for (int i = 0; i < faux.nb_stockes; ++i) {
            snprintf(l, sizeof l, "+CMGL: %d,\"REC UNREAD\",\"%s\",,\"26/11/26,14:32:05+04\"\r\n%s\r\n",
                     faux.stockes[i].index, faux.stockes[i].numero, faux.stockes[i].texte);
            sortir(l);
        }
        sortir("\r\nOK\r\n");
    } else if (!strncmp(c, "AT+CMGD=", 8)) {
        int idx = 0;
        sscanf(c + 8, "%d", &idx);
        for (int i = 0; i < faux.nb_stockes; ++i) {
            if (faux.stockes[i].index == idx) {
                faux.stockes[i] = faux.stockes[--faux.nb_stockes];
                break;
            }
        }
        reponse(NULL, "OK");
    } else if (!strncmp(c, "AT+CMGS=\"", 9)) {
        faux.nb_cmgs++;
        if (!sim_prete()) return;
        if (!faux.reseau) { reponse(NULL, "+CMS ERROR: 331"); return; }
        snprintf(faux.numero_saisie, sizeof faux.numero_saisie, "%.*s", (int)strcspn(c + 9, "\""), c + 9);
        faux.saisie = 1;
        sortir("\r\n> ");
    } else if (!strcmp(c, "AT+CSQ")) reponse("+CSQ: 18,99", "OK");
    else if (!strcmp(c, "AT+CREG?")) reponse(faux.reseau ? "+CREG: 0,1" : "+CREG: 0,2", "OK");
    else if (!strcmp(c, "AT+CEREG?")) reponse(faux.reseau ? "+CEREG: 0,1" : "+CEREG: 0,2", "OK");
    else if (!strcmp(c, "AT+COPS?")) reponse(faux.reseau ? "+COPS: 0,0,\"Orange F\",7" : "+COPS: 0", "OK");
    else reponse(NULL, "ERROR");
}

static int faux_ecrire(void *ctx, const char *data, size_t n)
{
    (void)ctx;
    for (size_t i = 0; i < n; ++i) {
        const char c = data[i];
        if (faux.saisie) {
            if (c == 0x1A) {
                faux.recu[faux.nr] = '\0';
                if (faux.nb_envoyes < 4) {
                    const size_t l = faux.nr < 199 ? faux.nr : 199;
                    memcpy(faux.envoyes[faux.nb_envoyes], faux.recu, l);
                    faux.envoyes[faux.nb_envoyes][l] = '\0';
                    snprintf(faux.envoyes_a[faux.nb_envoyes], 24, "%s", faux.numero_saisie);
                    faux.nb_envoyes++;
                }
                faux.nr = 0;
                faux.saisie = 0;
                reponse("+CMGS: 7", "OK");
            } else if (c == 0x1B) {
                faux.nr = 0;
                faux.saisie = 0;
                reponse(NULL, "OK");
            } else if (faux.nr + 1 < sizeof faux.recu) {
                faux.recu[faux.nr++] = c;
            }
            continue;
        }
        if (c == '\r') {
            faux.recu[faux.nr] = '\0';
            faux.nr = 0;
            faux_commande(faux.recu);
        } else if (faux.nr + 1 < sizeof faux.recu) {
            faux.recu[faux.nr++] = c;
        }
    }
    return 0;
}

static void sur_gps(void *ctx, const sim_gps_t *g, int fix)
{
    (void)ctx;
    nb_gps++;
    dernier_fix = fix;
    if (g) dernier_gps = *g;
}

static void sur_sms(void *ctx, const char *numero, const char *texte, const char *horodatage)
{
    (void)ctx;
    (void)horodatage;
    if (nb_sms < 8) snprintf(sms_lus[nb_sms], sizeof sms_lus[0], "%s", texte);
    nb_sms++;
    snprintf(dernier_sms, sizeof dernier_sms, "%s", texte);
    snprintf(dernier_numero, sizeof dernier_numero, "%s", numero);
}

static int bavard;
static void journal(void *ctx, const char *texte)
{
    (void)ctx;
    if (bavard) printf("    [sim] %s\n", texte);
}

static sim_moteur_t moteur;

/* Fait tourner le moteur et le faux module pendant "duree" ms */
static void tourner(uint64_t *t, uint64_t duree)
{
    const uint64_t fin = *t + duree;
    for (; *t < fin; *t += 20) {
        sim_moteur_tick(&moteur, *t);
        while (faux.ns > 0) {
            char copie[sizeof faux.sortie];
            const size_t n = faux.ns;
            memcpy(copie, faux.sortie, n);
            faux.ns = 0;
            /* Livre en petits morceaux, comme un vrai port serie */
            for (size_t i = 0; i < n; i += 7) {
                sim_moteur_recevoir(&moteur, copie + i, n - i < 7 ? n - i : 7, *t);
            }
        }
    }
}

static void nouveau_faux(int carte)
{
    memset(&faux, 0, sizeof faux);
    faux.echo = 1;
    faux.carte = carte;
    nb_gps = dernier_fix = nb_sms = 0;
    dernier_sms[0] = dernier_numero[0] = '\0';
    const sim_rappels_t r = {NULL, faux_ecrire, sur_gps, sur_sms, journal};
    sim_moteur_init(&moteur, &r);
}

static void test_moteur(void)
{
    uint64_t t = 1000;

    /* Demarrage complet, GPS sans position puis avec */
    nouveau_faux(1);
    sim_moteur_ouvert(&moteur, t);
    tourner(&t, 3000);
    VERIF(moteur.modem == SIM_MODEM_PRET);
    VERIF(moteur.carte == SIM_CARTE_PRETE);
    VERIF(moteur.sms_pret);
    VERIF(moteur.gps_actif && !moteur.gps_fix);
    VERIF(moteur.rssi == 18);
    VERIF(faux.gps_on && !faux.echo);
    faux.fix = 1;
    faux.reseau = 1;
    tourner(&t, 12000);
    VERIF(moteur.gps_fix && dernier_fix == 1);
    PROCHE(dernier_gps.lat, 47.0 + 13.098 / 60.0, 1e-9);
    PROCHE(dernier_gps.lon, -(1.0 + 33.216 / 60.0), 1e-9);
    VERIF(faux.nb_cgpsinfo >= 12);  /* environ une par seconde */
    VERIF(moteur.reseau);
    TEXTE(moteur.operateur, "Orange F");
    faux.fix = 0;
    tourner(&t, 1500);
    VERIF(!moteur.gps_fix && dernier_fix == 0);

    /* SMS recus sur notification puis effaces : texte "OK", texte sur deux
     * lignes dont la 2e est "OK", texte UCS2, et texte vide en dernier */
    faux.stockes[0].index = 4;
    strcpy(faux.stockes[0].numero, "+33612345678");
    strcpy(faux.stockes[0].texte, "OK");
    faux.stockes[1].index = 6;
    strcpy(faux.stockes[1].numero, "+33612345678");
    strcpy(faux.stockes[1].texte, "Ligne 1\nOK");
    faux.stockes[2].index = 5;
    strcpy(faux.stockes[2].numero, "+33698765432");
    strcpy(faux.stockes[2].texte, "0052006500740069006E00E9");  /* "Retine" accentue, en UCS2 */
    faux.stockes[3].index = 7;
    strcpy(faux.stockes[3].numero, "+33698765432");
    faux.stockes[3].texte[0] = '\0';
    faux.nb_stockes = 4;
    const uint32_t envoyes_avant = moteur.sms_envoyes;
    sortir("\r\n+CMTI: \"SM\",5\r\n");
    tourner(&t, 1000);  /* bien moins que le delai de 10 s d'AT+CMGL */
    VERIF(nb_sms == 4);
    TEXTE(sms_lus[0], "OK");
    TEXTE(sms_lus[1], "Ligne 1\nOK");
    TEXTE(sms_lus[2], "Retin\xC3\xA9");
    TEXTE(sms_lus[3], "");
    TEXTE(dernier_numero, "+33698765432");
    VERIF(faux.nb_stockes == 0);
    VERIF(moteur.nb_a_supprimer == 0);
    VERIF(moteur.sans_reponse == 0);
    VERIF(moteur.sms_envoyes == envoyes_avant);

    /* Envoi d'un SMS : invite "> ", corps, Ctrl+Z */
    /* Horodate 5 ms apres le prochain tick (autre lecture d'horloge) : ne doit
     * pas passer pour un SMS trop vieux */
    VERIF(sim_moteur_envoyer_sms(&moteur, "+33612345678", "Position : 47\xC2\xB0", t + 5) == 0);
    VERIF(sim_moteur_envoyer_sms(&moteur, "06\";AT+CHUP", "x", t) == -1);  /* injection refusee */
    tourner(&t, 1500);
    VERIF(faux.nb_envoyes == 1);
    TEXTE(faux.envoyes[0], "Position : 47o");
    TEXTE(faux.envoyes_a[0], "+33612345678");
    VERIF(moteur.sms_envoyes == 1 && moteur.nb_file == 0);
    VERIF(faux.nb_cgpsinfo > 0);

    /* Sans reseau : le SMS attend, puis part quand le reseau revient */
    faux.reseau = 0;
    tourner(&t, 11000);
    VERIF(!moteur.reseau);
    VERIF(sim_moteur_envoyer_sms(&moteur, "0612345678", "Coucou", t) == 0);
    tourner(&t, 5000);
    VERIF(faux.nb_envoyes == 1 && moteur.nb_file == 1);
    faux.reseau = 1;
    tourner(&t, 12000);
    VERIF(faux.nb_envoyes == 2 && moteur.nb_file == 0);

    /* Redemarrage du module (RDY) : reconfiguration */
    faux.echo = 1;
    faux.gps_on = 0;
    sortir("\r\nRDY\r\n");
    tourner(&t, 3000);
    VERIF(moteur.modem == SIM_MODEM_PRET && moteur.sms_pret && faux.gps_on && !faux.echo);

    /* Module muet : detecte apres 3 commandes sans reponse */
    faux.muet = 1;
    tourner(&t, 15000);
    VERIF(sim_moteur_muet(&moteur));
    sim_moteur_ferme(&moteur);
    VERIF(moteur.modem == SIM_MODEM_ABSENT && !sim_moteur_muet(&moteur));

    /* Sans carte SIM : le GPS marche quand meme, pas de SMS */
    nouveau_faux(0);
    faux.fix = 1;
    t += 1000;
    sim_moteur_ouvert(&moteur, t);
    tourner(&t, 5000);
    VERIF(moteur.carte == SIM_CARTE_ABSENTE);
    VERIF(!moteur.sms_pret);
    VERIF(moteur.gps_fix);
    VERIF(sim_moteur_envoyer_sms(&moteur, "0612345678", "x", t) == 0);
    tourner(&t, 3000);
    VERIF(faux.nb_cmgs == 0);

    /* Code PIN : essaye une seule fois */
    nouveau_faux(2);
    strcpy(faux.pin, "1234");
    sim_moteur_definir_pin(&moteur, "1234");
    t += 1000;
    sim_moteur_ouvert(&moteur, t);
    tourner(&t, 5000);
    VERIF(moteur.carte == SIM_CARTE_PRETE && moteur.sms_pret);

    nouveau_faux(2);
    strcpy(faux.pin, "1234");
    sim_moteur_definir_pin(&moteur, "0000");
    t += 1000;
    sim_moteur_ouvert(&moteur, t);
    tourner(&t, 20000);
    VERIF(moteur.carte == SIM_CARTE_PIN && moteur.pin_essaye);
    VERIF(faux.nb_codes_pin == 1);
    /* Module rebranche : le code faux n'est pas renvoye (3 essais = carte bloquee) */
    sim_moteur_ferme(&moteur);
    t += 1000;
    sim_moteur_ouvert(&moteur, t);
    tourner(&t, 15000);
    VERIF(moteur.carte == SIM_CARTE_PIN);
    VERIF(faux.nb_codes_pin == 1);

    char m[16];
    sim_masquer_numero("+33 6 12 34 56 78", m, sizeof m);
    TEXTE(m, "...5678");
}

int main(int argc, char **argv)
{
    bavard = argc > 1 && strcmp(argv[1], "-v") == 0;
    test_env();
    test_analyse();
    test_kalman();
    test_trace();
    test_carte();
    test_sms_commandes();
    test_moteur();
    printf("%d verifications, %d echec(s)\n", verifs, echecs);
    return echecs ? 1 : 0;
}
