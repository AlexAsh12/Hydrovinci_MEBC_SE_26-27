/*
 * Tableau de bord : recoit les donnees de l'ESP32 sur l'UART et du module
 * 4G/GPS SIM7600 en USB, et les sert a un navigateur (page web + flux temps
 * reel "Server-Sent Events").
 *
 *   ./tableau_bord [--port /dev/serial0] [--vitesse N] [--http 8080]
 *                  [--ecoute 127.0.0.1] [--web DIR] [--sans-sim] ...
 *
 * Un seul processus, une seule boucle poll() : pas de thread, pas d'allocation
 * apres le demarrage. L'ESP32 et le SIM7600 sont independants : l'un peut
 * etre absent, debranche ou rebranche sans gener l'autre.
 *
 * Vitesse : filtre de Kalman (kalman_vitesse.c) qui fusionne la vitesse GPS
 * du SIM7600 et l'acceleration avant de l'IMU de l'ESP32.
 *
 * Securite :
 *   - n'ecoute que sur 127.0.0.1 par defaut (ecran de la Pi uniquement)
 *   - lecture seule : rien de ce que le navigateur envoie ne va a l'ESP
 *   - seuls quelques fichiers fixes, charges en memoire au demarrage, sont
 *     servis : aucun chemin venant du navigateur ne touche le disque
 *   - requete limitee en taille et en temps, nombre de clients limite,
 *     un client trop lent est deconnecte (la memoire ne gonfle jamais)
 */
#define _GNU_SOURCE  /* accept4 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "env.h"
#include "kalman_vitesse.h"
#include "protocole.h"
#include "sim7600.h"
#include "sms_commandes.h"
#include "trace.h"
#include "uart.h"

#define MAX_CLIENTS 8
#define TAILLE_REQUETE 2048
#define TAILLE_SORTIE 16384
#define DELAI_REQUETE_MS 5000u
#define DELAI_ESP_PERDU_MS PROTO_TIMEOUT_LIAISON_MS
#define PERIODE_ETAT_MS 1000u
#define PERIODE_REOUVERTURE_UART_MS 2000u
#define TAILLE_MAX_FICHIER (512u * 1024u)
#define TAILLE_EVENEMENT 1600

/* ------------------------------------------------------------------ */
/* Fichiers servis (liste fermee)                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *url;
    const char *fichier;
    const char *type;
    char *data;
    size_t taille;
} fichier_t;

static fichier_t fichiers[] = {
    {"/", "index.html", "text/html; charset=utf-8", NULL, 0},
    {"/tableau.css", "tableau.css", "text/css; charset=utf-8", NULL, 0},
    {"/tableau.js", "tableau.js", "text/javascript; charset=utf-8", NULL, 0},
    {"/bateau.js", "bateau.js", "text/javascript; charset=utf-8", NULL, 0},
    {"/carte.js", "carte.js", "text/javascript; charset=utf-8", NULL, 0},
};
#define NB_FICHIERS (sizeof fichiers / sizeof fichiers[0])

/* ------------------------------------------------------------------ */
/* Clients HTTP                                                        */
/* ------------------------------------------------------------------ */

typedef enum { LIBRE, REQUETE, REPONSE, FLUX } etat_client_t;

typedef struct {
    int fd;
    etat_client_t etat;
    uint64_t debut_ms;
    char requete[TAILLE_REQUETE];
    size_t requete_len;
    char sortie[TAILLE_SORTIE];
    size_t sortie_debut, sortie_fin;
    const char *fichier;          /* corps de reponse, envoye sans copie */
    size_t fichier_pos, fichier_len;
} client_t;

static client_t clients[MAX_CLIENTS];
static int fd_ecoute = -1;

/* ------------------------------------------------------------------ */
/* Etat de la liaison ESP                                              */
/* ------------------------------------------------------------------ */

static int fd_uart = -1;
static const char *port_uart = "/dev/serial0";
static uint32_t vitesse_uart = PROTO_VITESSE_DEFAUT;
static proto_decodeur_t decodeur;
static uint64_t derniere_trame_ms;
static int esp_present;

/* Dernier evenement de chaque sorte, envoye a tout nouveau client */
enum {
    D_HELLO, D_HB, D_IMU, D_MOUV, D_BMS1, D_BMS2, D_POWER, D_TEMP, D_PILOT,
    D_CARTE, D_SIM, D_GPS, D_VITESSE, D_SMS, NB_DERNIERS
};
static char derniers[NB_DERNIERS][TAILLE_EVENEMENT];

/* Derniers etats de l'ESP utiles aux reponses SMS */
static int soc_dixieme[2] = {-1, -1};
static long puissance_mw;
static uint64_t t_puissance;

/* ------------------------------------------------------------------ */
/* Module 4G / GPS et vitesse                                          */
/* ------------------------------------------------------------------ */

#define PERIODE_VITESSE_MS 200u
#define PAS_TRACE_M 3.0
#define TAILLE_TRACE_JSON 40960   /* TRACE_MAX points de 25 caracteres au plus */
#define MAX_NUMEROS 8
#define TAILLE_MAX_FOND (8u * 1024u * 1024u)

static sim7600_t sim;
static int sim_actif = 1;
static kalman_vitesse_t kalman;
static trace_t trace;
static sim_gps_t dernier_gps;
static int gps_fix;
static char numeros[MAX_NUMEROS][SIM_TAILLE_NUMERO];  /* SIM_NUMEROS du .env */
static int nb_numeros;
static char trace_json_client[MAX_CLIENTS][TAILLE_TRACE_JSON];

/* Fond de carte facultatif (carte.conf) */
static struct {
    char *data;
    size_t taille;
    const char *type;
    double nord, sud, ouest, est;
} fond;

static volatile sig_atomic_t arret;

static uint64_t maintenant_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

__attribute__((format(printf, 1, 2)))
static void journal(const char *format, ...)
{
    char h[16];
    time_t t = time(NULL);
    strftime(h, sizeof h, "%H:%M:%S", localtime(&t));
    printf("%s ", h);
    va_list a;
    va_start(a, format);
    vprintf(format, a);
    va_end(a);
    putchar('\n');
    fflush(stdout);
}

static void sur_signal(int sig)
{
    (void)sig;
    arret = 1;
}

/* ------------------------------------------------------------------ */
/* Sortie vers les clients                                             */
/* ------------------------------------------------------------------ */

static void fermer_client(client_t *c)
{
    if (c->etat != LIBRE) {
        close(c->fd);
        if (c->etat == FLUX) {
            journal("ecran deconnecte");
        }
    }
    c->etat = LIBRE;
    c->fd = -1;
}

/* Ajoute des octets a envoyer. Faux si le client ne suit pas. */
static int ajouter(client_t *c, const char *data, size_t len)
{
    if (c->sortie_fin + len > sizeof c->sortie && c->sortie_debut > 0) {
        memmove(c->sortie, c->sortie + c->sortie_debut, c->sortie_fin - c->sortie_debut);
        c->sortie_fin -= c->sortie_debut;
        c->sortie_debut = 0;
    }
    if (c->sortie_fin + len > sizeof c->sortie) {
        return 0;
    }
    memcpy(c->sortie + c->sortie_fin, data, len);
    c->sortie_fin += len;
    return 1;
}

/* Envoie ce qui peut l'etre sans bloquer. Faux si la connexion est morte. */
static int vider(client_t *c)
{
    while (c->sortie_debut < c->sortie_fin) {
        const ssize_t n = send(c->fd, c->sortie + c->sortie_debut, c->sortie_fin - c->sortie_debut,
                               MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0) {
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        }
        c->sortie_debut += (size_t)n;
    }
    c->sortie_debut = c->sortie_fin = 0;

    while (c->fichier && c->fichier_pos < c->fichier_len) {
        const ssize_t n = send(c->fd, c->fichier + c->fichier_pos, c->fichier_len - c->fichier_pos,
                               MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0) {
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        }
        c->fichier_pos += (size_t)n;
    }

    if (c->etat == REPONSE) {
        return 0;  /* reponse complete : on ferme (Connection: close) */
    }
    return 1;
}

static int a_envoyer(const client_t *c)
{
    return c->sortie_debut < c->sortie_fin || (c->fichier && c->fichier_pos < c->fichier_len);
}

/* Envoie un evenement "data: <json>\n\n" a tous les ecrans */
static void diffuser(const char *json)
{
    char ev[TAILLE_EVENEMENT + 16];
    const int n = snprintf(ev, sizeof ev, "data: %s\n\n", json);
    if (n <= 0 || (size_t)n >= sizeof ev) {
        return;
    }
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        client_t *c = &clients[i];
        if (c->etat != FLUX) {
            continue;
        }
        if (!ajouter(c, ev, (size_t)n) || !vider(c)) {
            journal("ecran trop lent ou parti : deconnecte");
            fermer_client(c);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Trames ESP -> JSON                                                  */
/* ------------------------------------------------------------------ */

/* Ecrivain JSON borne : un depassement donne une chaine vide */
typedef struct {
    char *p;
    size_t taille, pos;
    int erreur;
} json_t;

__attribute__((format(printf, 2, 3)))
static void j(json_t *s, const char *format, ...)
{
    if (s->erreur) {
        return;
    }
    va_list a;
    va_start(a, format);
    const int n = vsnprintf(s->p + s->pos, s->taille - s->pos, format, a);
    va_end(a);
    if (n < 0 || (size_t)n >= s->taille - s->pos) {
        s->erreur = 1;
        s->p[0] = '\0';
        return;
    }
    s->pos += (size_t)n;
}

/* Chaine JSON echappee. Les caracteres de controle sont toujours retires ;
 * utf8 = 0 : ASCII imprimable seulement (texte venant de l'ESP),
 * utf8 = 1 : accents permis (noms des sondes, fichier local). */
static void j_texte(json_t *s, const char *t, int utf8)
{
    j(s, "\"");
    for (; *t; ++t) {
        const unsigned char ch = (unsigned char)*t;
        if (ch == '"' || ch == '\\') {
            j(s, "\\%c", ch);
        } else if ((ch >= 0x20 && ch < 0x7F) || (utf8 && ch >= 0x80)) {
            j(s, "%c", ch);
        }
    }
    j(s, "\"");
}

/* ------------------------------------------------------------------ */
/* Noms des sondes (fichier optionnel, lu au demarrage)                */
/* ------------------------------------------------------------------ */

#define MAX_NOMS 32
#define TAILLE_NOM 32

static struct {
    uint8_t rom[8];
    char nom[TAILLE_NOM];
} noms[MAX_NOMS];
static int nb_noms;

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Lignes : "28DC9C9F110000AB Moteur". '#' = commentaire. */
static void charger_noms(const char *chemin)
{
    FILE *f = fopen(chemin, "r");
    if (!f) {
        journal("pas de fichier %s : sondes affichees par leur adresse", chemin);
        return;
    }
    char ligne[256];
    int numero = 0;
    while (fgets(ligne, sizeof ligne, f) && nb_noms < MAX_NOMS) {
        numero++;
        char *p = ligne;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        uint8_t rom[8];
        int ok = 1;
        for (int k = 0; k < 8 && ok; ++k) {
            const int h = hex(p[2 * k]), l = h < 0 ? -1 : hex(p[2 * k + 1]);
            ok = h >= 0 && l >= 0;
            rom[k] = (uint8_t)(h * 16 + l);
        }
        char *nom = p + 16;
        if (!ok || (*nom != ' ' && *nom != '\t')) {
            journal("%s ligne %d ignoree (attendu : adresse de 16 caracteres puis nom)", chemin, numero);
            continue;
        }
        while (*nom == ' ' || *nom == '\t') nom++;
        size_t n = strcspn(nom, "\r\n");
        while (n > 0 && (nom[n - 1] == ' ' || nom[n - 1] == '\t')) n--;
        if (n == 0) continue;
        if (n >= TAILLE_NOM) n = TAILLE_NOM - 1;
        memcpy(noms[nb_noms].rom, rom, 8);
        memcpy(noms[nb_noms].nom, nom, n);
        noms[nb_noms].nom[n] = '\0';
        nb_noms++;
    }
    fclose(f);
    journal("%d nom(s) de sonde lu(s) dans %s", nb_noms, chemin);
}

static const char *nom_sonde(const uint8_t rom[8])
{
    for (int i = 0; i < nb_noms; ++i) {
        if (memcmp(noms[i].rom, rom, 8) == 0) return noms[i].nom;
    }
    return NULL;
}

static void memoriser_et_diffuser(int sorte, const json_t *s)
{
    if (s->erreur) {
        return;
    }
    if (sorte >= 0) {
        memcpy(derniers[sorte], s->p, s->pos + 1);
    }
    diffuser(s->p);
}

static void traiter_trame(const proto_trame_t *t)
{
    char tampon[TAILLE_EVENEMENT];
    json_t s = {tampon, sizeof tampon, 0, 0};
    tampon[0] = '\0';

    switch (t->type) {
    case PROTO_T_HELLO: {
        proto_hello_t m;
        if (!proto_decoder_hello(t->payload, t->len, &m)) return;
        j(&s, "{\"t\":\"hello\",\"proto\":%u,\"carte\":%u,\"fw\":\"%u.%u.%u\"}", m.version, m.carte,
          m.fw[0], m.fw[1], m.fw[2]);
        memoriser_et_diffuser(D_HELLO, &s);
        break;
    }
    case PROTO_T_HB: {
        proto_hb_t m;
        if (!proto_decoder_hb(t->payload, t->len, &m)) return;
        j(&s, "{\"t\":\"hb\",\"uptime\":%lu}", (unsigned long)m.uptime_ms);
        memoriser_et_diffuser(D_HB, &s);
        break;
    }
    case PROTO_T_IMU: {
        proto_imu_t m;
        if (!proto_decoder_imu(t->payload, t->len, &m)) return;
        j(&s, "{\"t\":\"imu\",\"roulis\":%d,\"tangage\":%d,\"cap\":%u,\"precision\":%d,\"incertitude\":%d}",
          m.roulis_cdeg, m.tangage_cdeg, m.cap_cdeg,
          m.precision == PROTO_IMU_PRECISION_INCONNUE ? -1 : (int)m.precision,
          m.incertitude_cdeg == PROTO_IMU_INCERTITUDE_INCONNUE ? -1 : (int)m.incertitude_cdeg);
        memoriser_et_diffuser(D_IMU, &s);
        break;
    }
    case PROTO_T_MOUVEMENT: {
        proto_mouvement_t m;
        if (!proto_decoder_mouvement(t->payload, t->len, &m)) return;
        kv_acceleration(&kalman, m.acc_avant_cms2 / 100.0, maintenant_ms());
        j(&s, "{\"t\":\"mouv\",\"acc_avant\":%d,\"acc_lateral\":%d,\"acc_vertical\":%d,"
              "\"vit_roulis\":%d,\"vit_tangage\":%d,\"giration\":%d}",
          m.acc_avant_cms2, m.acc_lateral_cms2, m.acc_vertical_cms2, m.vit_roulis_ddps,
          m.vit_tangage_ddps, m.giration_ddps);
        memoriser_et_diffuser(D_MOUV, &s);
        break;
    }
    case PROTO_T_BMS: {
        static proto_bms_t m;  /* gros : hors de la pile */
        if (!proto_decoder_bms(t->payload, t->len, &m) || m.id < 1 || m.id > 2) return;
        soc_dixieme[m.id - 1] = m.ok ? (int)m.soc_dixieme : -1;
        j(&s, "{\"t\":\"bms\",\"id\":%u,\"ok\":%u,\"tension\":%u,\"courant\":%ld,\"soc\":%u,"
              "\"restant\":%u,\"cellules\":[",
          m.id, m.ok, m.tension_cv, (long)m.courant_ca, m.soc_dixieme, m.restant_cah);
        for (unsigned i = 0; i < m.nb_cellules; ++i) {
            j(&s, i ? ",%u" : "%u", m.cellules_mv[i]);
        }
        j(&s, "],\"temps\":[");
        for (unsigned i = 0; i < m.nb_temps; ++i) {
            j(&s, i ? ",%d" : "%d", m.temps_dixieme[i]);
        }
        j(&s, "]}");
        memoriser_et_diffuser(m.id == 1 ? D_BMS1 : D_BMS2, &s);
        break;
    }
    case PROTO_T_POWER: {
        proto_power_t m;
        if (!proto_decoder_power(t->payload, t->len, &m)) return;
        puissance_mw = (long)m.puissance_mw;
        t_puissance = maintenant_ms();
        j(&s, "{\"t\":\"power\",\"mv\":%u,\"ma\":%ld,\"mw\":%ld}", m.tension_mv, (long)m.courant_ma,
          (long)m.puissance_mw);
        memoriser_et_diffuser(D_POWER, &s);
        break;
    }
    case PROTO_T_TEMP: {
        static proto_temp_t m;
        if (!proto_decoder_temp(t->payload, t->len, &m)) return;
        j(&s, "{\"t\":\"temp\",\"sondes\":[");
        for (unsigned i = 0; i < m.nb; ++i) {
            j(&s, "%s{\"rom\":\"", i ? "," : "");
            for (int k = 0; k < 8; ++k) {
                j(&s, "%02X", m.sondes[i].rom[k]);
            }
            j(&s, "\",\"c\":%d", m.sondes[i].c_centieme);
            const char *nom = nom_sonde(m.sondes[i].rom);
            if (nom) {
                j(&s, ",\"nom\":");
                j_texte(&s, nom, 1);
            }
            j(&s, "}");
        }
        j(&s, "]}");
        memoriser_et_diffuser(D_TEMP, &s);
        break;
    }
    case PROTO_T_PILOT: {
        proto_pilot_t m;
        if (!proto_decoder_pilot(t->payload, t->len, &m)) return;
        j(&s, "{\"t\":\"pilot\",\"ok\":%u,\"boutons\":%u,\"voyants\":%u}", m.ok, m.boutons, m.voyants);
        memoriser_et_diffuser(D_PILOT, &s);
        break;
    }
    case PROTO_T_ALERT: {
        proto_alert_t m;
        if (!proto_decoder_alert(t->payload, t->len, &m)) return;
        j(&s, "{\"t\":\"alerte\",\"code\":%u,\"niveau\":%u,\"source\":%u,\"valeur\":%ld}", m.code,
          m.niveau, m.source, (long)m.valeur);
        memoriser_et_diffuser(-1, &s);
        break;
    }
    case PROTO_T_LOG: {
        char niveau, texte[256];
        if (!proto_decoder_log(t->payload, t->len, &niveau, texte, sizeof texte)) return;
        j(&s, "{\"t\":\"log\",\"niveau\":\"%c\",\"texte\":", (niveau >= 0x20 && niveau < 0x7F) ? niveau : '?');
        j_texte(&s, texte, 0);
        j(&s, "}");
        memoriser_et_diffuser(-1, &s);
        break;
    }
    default:
        break;  /* ACK, TEST... : rien a afficher */
    }
}

/* ------------------------------------------------------------------ */
/* SIM7600 : GPS, vitesse, SMS                                         */
/* ------------------------------------------------------------------ */

static void sur_gps(void *ctx, const sim_gps_t *g, int fix)
{
    (void)ctx;
    char tampon[TAILLE_EVENEMENT];
    json_t s = {tampon, sizeof tampon, 0, 0};
    tampon[0] = '\0';
    if (!fix || !g) {
        gps_fix = 0;
        j(&s, "{\"t\":\"gps\",\"fix\":0}");
        memoriser_et_diffuser(D_GPS, &s);
        return;
    }
    dernier_gps = *g;
    gps_fix = 1;
    kv_gps(&kalman, g->vitesse_ms, maintenant_ms());
    const int point = trace_ajouter(&trace, g->lat, g->lon);
    j(&s, "{\"t\":\"gps\",\"fix\":1,\"lat\":%.7f,\"lon\":%.7f,\"vit\":%.2f,\"cap\":%.1f,\"alt\":%.1f,\"heure\":",
      g->lat, g->lon, g->vitesse_ms, g->cap_valide ? g->cap_deg : -1.0, g->altitude_m);
    j_texte(&s, g->heure, 0);
    j(&s, ",\"pt\":%d}", point);
    memoriser_et_diffuser(D_GPS, &s);
}

static void diffuser_vitesse(uint64_t t)
{
    kv_estimation_t e;
    kv_estimer(&kalman, t, &e);
    char tampon[160];
    json_t s = {tampon, sizeof tampon, 0, 0};
    tampon[0] = '\0';
    if (e.source == KV_INCONNUE) {
        j(&s, "{\"t\":\"vitesse\",\"src\":\"aucune\"}");
    } else {
        j(&s, "{\"t\":\"vitesse\",\"src\":\"%s\",\"ms\":%.3f,\"sigma\":%.3f}", kv_nom_source(e.source), e.v_ms,
          e.sigma_ms);
    }
    memoriser_et_diffuser(D_VITESSE, &s);
}

static void remplir_contexte(sms_contexte_t *c, uint64_t t)
{
    memset(c, 0, sizeof *c);
    c->gps_fix = gps_fix;
    c->lat = dernier_gps.lat;
    c->lon = dernier_gps.lon;
    snprintf(c->heure_utc, sizeof c->heure_utc, "%s", dernier_gps.heure);
    kv_estimation_t e;
    kv_estimer(&kalman, t, &e);
    c->vitesse_connue = e.source != KV_INCONNUE;
    c->vitesse_ms = e.v_ms;
    c->cap_connu = gps_fix && dernier_gps.cap_valide;
    c->cap_deg = dernier_gps.cap_deg;
    c->esp_present = esp_present;
    c->soc_dixieme[0] = soc_dixieme[0];
    c->soc_dixieme[1] = soc_dixieme[1];
    c->puissance_connue = t_puissance && t - t_puissance < 5000u;
    c->puissance_mw = puissance_mw;
    c->signal_rssi = sim.moteur.rssi;
}

/* SMS recu : commande -> reponse ; autre texte -> affiche au pilote.
 * Les numeros absents de SIM_NUMEROS sont ignores (rien a l'ecran). */
static void sur_sms(void *ctx, const char *numero, const char *texte, const char *horodatage)
{
    (void)ctx;
    (void)horodatage;
    char masque[16];
    sim_masquer_numero(numero, masque, sizeof masque);
    if (!sms_numero_autorise(numero, &numeros[0][0], SIM_TAILLE_NUMERO, nb_numeros)) {
        journal("SMS de %s ignore : numero absent de SIM_NUMEROS (.env)", masque);
        return;
    }
    const uint64_t t = maintenant_ms();
    sms_contexte_t c;
    remplir_contexte(&c, t);
    char reponse[200];
    if (sms_commandes_traiter(texte, &c, reponse, sizeof reponse) == SMS_REPONDRE) {
        journal("commande SMS de %s", masque);
        if (sim_moteur_envoyer_sms(&sim.moteur, numero, reponse, t) != 0) {
            journal("reponse SMS impossible : numero invalide ou file d'envoi pleine");
        }
        return;
    }

    /* Message pour le pilote : sauts de ligne -> espaces */
    char propre[SIM_TAILLE_TEXTE_RECU];
    size_t n = 0;
    for (const char *p = texte; *p && n + 1 < sizeof propre; ++p) {
        propre[n++] = *p == '\n' ? ' ' : *p;
    }
    propre[n] = '\0';
    char heure[8];
    const time_t maintenant = time(NULL);
    strftime(heure, sizeof heure, "%H:%M", localtime(&maintenant));

    /* Diffuse en direct ("ancien":0, bandeau a l'ecran) ; un ecran qui se
     * connecte plus tard recoit la version "ancien":1 (pas de bandeau) */
    for (int ancien = 1; ancien >= 0; --ancien) {
        char tampon[TAILLE_EVENEMENT];
        json_t s = {tampon, sizeof tampon, 0, 0};
        tampon[0] = '\0';
        j(&s, "{\"t\":\"sms\",\"ancien\":%d,\"de\":\"%s\",\"heure\":\"%s\",\"texte\":", ancien, masque, heure);
        j_texte(&s, propre, 1);
        j(&s, "}");
        if (s.erreur) return;
        if (ancien) memcpy(derniers[D_SMS], tampon, s.pos + 1);
        else diffuser(tampon);
    }
    journal("message de %s affiche a l'ecran", masque);
}

/* Envoie un SMS a tous les numeros de SIM_NUMEROS. Pret pour la suite
 * (bouton du pilote, alerte critique...) : appeler depuis traiter_trame(). */
__attribute__((unused))
static void envoyer_sms_equipe(const char *texte)
{
    for (int i = 0; i < nb_numeros; ++i) {
        if (sim_moteur_envoyer_sms(&sim.moteur, numeros[i], texte, maintenant_ms()) != 0) {
            journal("SMS a l'equipe : file d'envoi pleine");
            return;
        }
    }
}

static void sim_journal(void *ctx, const char *texte)
{
    (void)ctx;
    journal("%s", texte);
}

static void diffuser_sim(void)
{
    static const char *const cartes[] = {"inconnue", "prete", "pin", "bloquee", "absente"};
    const sim_moteur_t *m = &sim.moteur;
    char tampon[400];
    json_t s = {tampon, sizeof tampon, 0, 0};
    tampon[0] = '\0';
    j(&s, "{\"t\":\"sim\",\"actif\":%d,\"present\":%d,\"carte\":\"%s\",\"reseau\":%d,\"signal\":%d,"
          "\"techno\":\"%s\",\"operateur\":",
      sim_actif, m->modem == SIM_MODEM_PRET, cartes[m->carte], m->reseau, m->rssi, sa_techno(m->techno));
    j_texte(&s, m->operateur, 0);
    j(&s, ",\"gps\":%d,\"fix\":%d,\"attente\":%d,\"envoyes\":%lu,\"recus\":%lu}", m->gps_actif, m->gps_fix,
      m->nb_file, (unsigned long)m->sms_envoyes, (unsigned long)m->sms_recus);
    memoriser_et_diffuser(D_SIM, &s);
}

/* ------------------------------------------------------------------ */
/* Fond de carte (carte.conf, facultatif)                              */
/* ------------------------------------------------------------------ */

static const char *type_image(const char *chemin)
{
    const char *point = strrchr(chemin, '.');
    if (!point) return NULL;
    if (!strcasecmp(point, ".png")) return "image/png";
    if (!strcasecmp(point, ".jpg") || !strcasecmp(point, ".jpeg")) return "image/jpeg";
    if (!strcasecmp(point, ".webp")) return "image/webp";
    return NULL;
}

/* Lignes "cle valeur" : image, nord, sud, ouest, est. '#' = commentaire. */
static void charger_carte(const char *chemin)
{
    char tampon[160];
    json_t s = {tampon, sizeof tampon, 0, 0};
    tampon[0] = '\0';
    j(&s, "{\"t\":\"carte\",\"fond\":0}");
    memoriser_et_diffuser(D_CARTE, &s);

    FILE *f = fopen(chemin, "r");
    if (!f) {
        journal("pas de fichier %s : carte sans fond (trace et grille)", chemin);
        return;
    }
    char ligne[512], image[384] = "";
    double bornes[4] = {0};
    static const char *const cles[4] = {"nord", "sud", "ouest", "est"};
    int vues = 0;
    while (fgets(ligne, sizeof ligne, f)) {
        char cle[16], valeur[384];
        if (ligne[0] == '#' || sscanf(ligne, "%15s %383s", cle, valeur) != 2) continue;
        if (!strcmp(cle, "image")) {
            snprintf(image, sizeof image, "%s", valeur);
            continue;
        }
        for (int i = 0; i < 4; ++i) {
            if (!strcmp(cle, cles[i])) {
                char *fin;
                bornes[i] = strtod(valeur, &fin);
                if (*fin == '\0') vues |= 1 << i;
            }
        }
    }
    fclose(f);

    const double nord = bornes[0], sud = bornes[1], ouest = bornes[2], est = bornes[3];
    if (!image[0] || vues != 15 || !(nord > sud) || !(est > ouest) || nord > 85.0 || sud < -85.0 ||
        ouest < -180.0 || est > 180.0) {
        journal("%s incomplet ou invalide (image, nord > sud, est > ouest) : carte sans fond", chemin);
        return;
    }
    const char *type = type_image(image);
    FILE *fi = type ? fopen(image, "rb") : NULL;
    struct stat st;
    if (!fi || fstat(fileno(fi), &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > TAILLE_MAX_FOND) {
        journal("image de carte %s illisible, trop grosse (8 Mo max) ou pas en png/jpg/webp", image);
        if (fi) fclose(fi);
        return;
    }
    fond.data = malloc((size_t)st.st_size);
    if (!fond.data || fread(fond.data, 1, (size_t)st.st_size, fi) != (size_t)st.st_size) {
        journal("lecture impossible : %s", image);
        free(fond.data);
        fond.data = NULL;
        fclose(fi);
        return;
    }
    fclose(fi);
    fond.taille = (size_t)st.st_size;
    fond.type = type;
    fond.nord = nord;
    fond.sud = sud;
    fond.ouest = ouest;
    fond.est = est;

    s.pos = 0;
    s.erreur = 0;
    j(&s, "{\"t\":\"carte\",\"fond\":1,\"nord\":%.7f,\"sud\":%.7f,\"ouest\":%.7f,\"est\":%.7f}", nord, sud, ouest,
      est);
    memoriser_et_diffuser(D_CARTE, &s);
    journal("fond de carte %s charge (%zu ko)", image, fond.taille / 1024u);
}

/* SIM_NUMEROS et SIM_PIN du .env */
static void charger_env(const char *chemin)
{
    char valeur[512];
    if (!env_lire(chemin, "SIM_NUMEROS", valeur, sizeof valeur) || !valeur[0]) {
        journal("SIM_NUMEROS absent de %s : les SMS recus seront ignores", chemin);
    } else {
        char lus[MAX_NUMEROS][SIM_TAILLE_NUMERO];
        const int n = env_decouper_liste(valeur, &lus[0][0], SIM_TAILLE_NUMERO, MAX_NUMEROS);
        for (int i = 0; i < n; ++i) {
            /* "+33 6 12 34 56 78" ou "06.12.34.56.78" -> chiffres seuls */
            size_t k = 0;
            for (const char *p = lus[i]; *p; ++p) {
                if (*p != ' ' && *p != '.' && *p != '-') lus[i][k++] = *p;
            }
            lus[i][k] = '\0';
            if (sa_numero_valide(lus[i])) {
                memcpy(numeros[nb_numeros++], lus[i], SIM_TAILLE_NUMERO);
            } else {
                journal("SIM_NUMEROS : element %d ignore (attendu +33612345678 ou 0612345678)", i + 1);
            }
        }
        journal("%d numero(s) autorise(s) pour les SMS", nb_numeros);
    }
    if (env_lire(chemin, "SIM_PIN", valeur, sizeof valeur) && valeur[0]) {
        sim_moteur_definir_pin(&sim.moteur, valeur);
    }
    memset(valeur, 0, sizeof valeur);
}

static void diffuser_etat(void)
{
    char tampon[160];
    const uint64_t age = derniere_trame_ms ? maintenant_ms() - derniere_trame_ms : 0;
    snprintf(tampon, sizeof tampon,
             "{\"t\":\"etat\",\"esp\":%d,\"uart\":%d,\"trames\":%lu,\"crc\":%lu,\"age\":%llu}",
             esp_present, fd_uart >= 0, (unsigned long)decodeur.trames_ok,
             (unsigned long)decodeur.erreurs_crc, (unsigned long long)age);
    diffuser(tampon);
}

/* ------------------------------------------------------------------ */
/* UART                                                                */
/* ------------------------------------------------------------------ */

static void ouvrir_uart(void)
{
    fd_uart = uart_ouvrir(port_uart, vitesse_uart);
    if (fd_uart >= 0) {
        uart_vider_reception(fd_uart);
        proto_decodeur_init(&decodeur);
        journal("UART %s ouvert a %lu bauds", port_uart, (unsigned long)vitesse_uart);
    }
}

static void lire_uart(void)
{
    uint8_t tampon[4096];
    const ssize_t n = read(fd_uart, tampon, sizeof tampon);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return;
        }
        journal("UART en erreur (%s) : nouvelle tentative dans 2 s", strerror(errno));
        uart_fermer(fd_uart);
        fd_uart = -1;
        return;
    }
    size_t pos = 0;
    while (pos < (size_t)n) {
        pos += proto_decodeur_ajouter(&decodeur, tampon + pos, (size_t)n - pos);
        proto_trame_t t;
        while (proto_decodeur_suivante(&decodeur, &t)) {
            derniere_trame_ms = maintenant_ms();
            if (!esp_present) {
                esp_present = 1;
                journal("ESP32 present");
                diffuser_etat();
            }
            traiter_trame(&t);
        }
    }
}

/* ------------------------------------------------------------------ */
/* HTTP                                                                */
/* ------------------------------------------------------------------ */

#define ENTETES_SECURITE                                                                         \
    "Cache-Control: no-store\r\n"                                                                \
    "X-Content-Type-Options: nosniff\r\n"                                                        \
    "X-Frame-Options: DENY\r\n"                                                                  \
    "Referrer-Policy: no-referrer\r\n"                                                           \
    "Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self'; "         \
    "connect-src 'self'; img-src 'self'; base-uri 'none'; form-action 'none'; "                  \
    "frame-ancestors 'none'\r\n"

static void repondre_erreur(client_t *c, const char *statut)
{
    char r[1024];
    const int n = snprintf(r, sizeof r,
                           "HTTP/1.1 %s\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n"
                           ENTETES_SECURITE "Connection: close\r\n\r\n%s\n",
                           statut, strlen(statut) + 1, statut);
    c->etat = REPONSE;
    c->fichier = NULL;
    if (n > 0 && (size_t)n < sizeof r) {
        ajouter(c, r, (size_t)n);
    }
}

/* Reponse 200 dont le corps est envoye sans copie (il doit rester valide
 * jusqu'a la fermeture du client) */
static void repondre_donnees(client_t *c, const char *type, const char *data, size_t taille)
{
    char e[1024];
    const int n = snprintf(e, sizeof e,
                           "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                           ENTETES_SECURITE "Connection: close\r\n\r\n",
                           type, taille);
    c->etat = REPONSE;
    if (n > 0 && (size_t)n < sizeof e) {
        ajouter(c, e, (size_t)n);
    }
    c->fichier = data;
    c->fichier_pos = 0;
    c->fichier_len = taille;
}

static void commencer_flux(client_t *c)
{
    static const char entete[] =
        "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n" ENTETES_SECURITE
        "Connection: keep-alive\r\n\r\nretry: 1000\n\n";
    c->etat = FLUX;
    ajouter(c, entete, sizeof entete - 1);
    journal("ecran connecte");

    /* Photo de l'etat courant pour que l'ecran s'affiche tout de suite */
    char ev[TAILLE_EVENEMENT + 16];
    for (int i = 0; i < NB_DERNIERS; ++i) {
        if (derniers[i][0]) {
            const int n = snprintf(ev, sizeof ev, "data: %s\n\n", derniers[i]);
            if (n > 0 && (size_t)n < sizeof ev) {
                ajouter(c, ev, (size_t)n);
            }
        }
    }
}

static void traiter_requete(client_t *c)
{
    char *fin_ligne = strstr(c->requete, "\r\n");
    if (!fin_ligne) {
        repondre_erreur(c, "400 Bad Request");
        return;
    }
    *fin_ligne = '\0';

    /* "GET /chemin HTTP/1.1" */
    char *methode = c->requete;
    char *chemin = strchr(methode, ' ');
    if (!chemin) {
        repondre_erreur(c, "400 Bad Request");
        return;
    }
    *chemin++ = '\0';
    char *version = strchr(chemin, ' ');
    if (!version || strncmp(version + 1, "HTTP/1.", 7) != 0) {
        repondre_erreur(c, "400 Bad Request");
        return;
    }
    *version = '\0';
    if (strcmp(methode, "GET") != 0) {
        repondre_erreur(c, "405 Method Not Allowed");
        return;
    }
    char *requete_param = strchr(chemin, '?');
    if (requete_param) {
        *requete_param = '\0';
    }

    if (strcmp(chemin, "/flux") == 0) {
        commencer_flux(c);
        return;
    }
    if (strcmp(chemin, "/trace") == 0) {
        /* Trace GPS complete, pour un ecran qui (re)demarre ; tampon propre au client */
        char *tampon = trace_json_client[c - clients];
        const size_t n = trace_json(&trace, tampon, TAILLE_TRACE_JSON);
        if (n == 0) {
            repondre_erreur(c, "500 Internal Server Error");
        } else {
            repondre_donnees(c, "application/json", tampon, n);
        }
        return;
    }
    if (strcmp(chemin, "/carte/fond") == 0 && fond.data) {
        repondre_donnees(c, fond.type, fond.data, fond.taille);
        return;
    }
    for (size_t i = 0; i < NB_FICHIERS; ++i) {
        if (strcmp(chemin, fichiers[i].url) == 0) {
            repondre_donnees(c, fichiers[i].type, fichiers[i].data, fichiers[i].taille);
            return;
        }
    }
    repondre_erreur(c, "404 Not Found");
}

static void lire_client(client_t *c)
{
    if (c->etat != REQUETE) {
        /* Un ecran n'a rien a nous dire : on jette, et on detecte la fermeture */
        char poubelle[256];
        const ssize_t n = recv(c->fd, poubelle, sizeof poubelle, MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            fermer_client(c);
        }
        return;
    }

    const ssize_t n = recv(c->fd, c->requete + c->requete_len, sizeof c->requete - 1 - c->requete_len,
                           MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        fermer_client(c);
        return;
    }
    if (n < 0) {
        return;
    }
    c->requete_len += (size_t)n;
    c->requete[c->requete_len] = '\0';

    if (strstr(c->requete, "\r\n\r\n")) {
        traiter_requete(c);
    } else if (c->requete_len >= sizeof c->requete - 1) {
        repondre_erreur(c, "431 Request Header Fields Too Large");
    } else {
        return;
    }
    if (!vider(c)) {
        fermer_client(c);
    }
}

static void accepter(void)
{
    for (;;) {
        const int fd = accept4(fd_ecoute, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            return;
        }
        client_t *libre = NULL;
        for (int i = 0; i < MAX_CLIENTS && !libre; ++i) {
            if (clients[i].etat == LIBRE) {
                libre = &clients[i];
            }
        }
        if (!libre) {
            close(fd);  /* trop de clients */
            continue;
        }
        memset(libre, 0, sizeof *libre);
        libre->fd = fd;
        libre->etat = REQUETE;
        libre->debut_ms = maintenant_ms();
    }
}

static int ouvrir_ecoute(const char *adresse, int port)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, adresse, &a.sin_addr) != 1) {
        fprintf(stderr, "Adresse d'ecoute invalide : %s\n", adresse);
        return -1;
    }
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    const int un = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &un, sizeof un);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || listen(fd, 16) < 0) {
        fprintf(stderr, "Impossible d'ecouter sur %s:%d : %s\n", adresse, port, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* ------------------------------------------------------------------ */
/* Demarrage                                                           */
/* ------------------------------------------------------------------ */

static int charger_fichiers(const char *dossier)
{
    for (size_t i = 0; i < NB_FICHIERS; ++i) {
        char chemin[512];
        snprintf(chemin, sizeof chemin, "%s/%s", dossier, fichiers[i].fichier);
        FILE *f = fopen(chemin, "rb");
        struct stat st;
        if (!f || fstat(fileno(f), &st) != 0 || st.st_size <= 0 ||
            (size_t)st.st_size > TAILLE_MAX_FICHIER) {
            fprintf(stderr, "Fichier web manquant ou invalide : %s\n", chemin);
            if (f) fclose(f);
            return -1;
        }
        fichiers[i].taille = (size_t)st.st_size;
        fichiers[i].data = malloc(fichiers[i].taille);
        if (!fichiers[i].data || fread(fichiers[i].data, 1, fichiers[i].taille, f) != fichiers[i].taille) {
            fprintf(stderr, "Lecture impossible : %s\n", chemin);
            fclose(f);
            return -1;
        }
        fclose(f);
    }
    return 0;
}

static void usage(const char *nom)
{
    printf("Usage : %s [options]\n"
           "  --port CHEMIN     port serie de l'ESP32 (defaut /dev/serial0)\n"
           "  --vitesse N       vitesse UART (defaut %lu)\n"
           "  --http N          port HTTP (defaut 8080)\n"
           "  --ecoute ADRESSE  adresse d'ecoute (defaut 127.0.0.1 = ecran local seulement,\n"
           "                    0.0.0.0 = accessible depuis le reseau)\n"
           "  --web DOSSIER     dossier des pages (defaut ./web)\n"
           "  --sondes FICHIER  noms des sondes de temperature (defaut ./sondes.conf, optionnel)\n"
           "  --carte FICHIER   fond de carte GPS (defaut ./carte.conf, optionnel)\n"
           "  --env FICHIER     secrets : SIM_NUMEROS, SIM_PIN (defaut ../../.env)\n"
           "  --sim-port CHEMIN port AT du SIM7600 (defaut : recherche automatique)\n"
           "  --sans-sim        ne pas utiliser le module 4G/GPS\n",
           nom, (unsigned long)PROTO_VITESSE_DEFAUT);
}

int main(int argc, char **argv)
{
    const char *adresse = "127.0.0.1";
    const char *dossier_web = "web";
    const char *fichier_noms = "sondes.conf";
    const char *fichier_carte = "carte.conf";
    const char *fichier_env = "../../.env";
    const char *port_sim = NULL;
    int port_http = 8080;

    for (int i = 1; i < argc; ++i) {
        const int reste = i + 1 < argc;
        if (!strcmp(argv[i], "--port") && reste) {
            port_uart = argv[++i];
        } else if (!strcmp(argv[i], "--vitesse") && reste) {
            vitesse_uart = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--http") && reste) {
            port_http = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--ecoute") && reste) {
            adresse = argv[++i];
        } else if (!strcmp(argv[i], "--web") && reste) {
            dossier_web = argv[++i];
        } else if (!strcmp(argv[i], "--sondes") && reste) {
            fichier_noms = argv[++i];
        } else if (!strcmp(argv[i], "--carte") && reste) {
            fichier_carte = argv[++i];
        } else if (!strcmp(argv[i], "--env") && reste) {
            fichier_env = argv[++i];
        } else if (!strcmp(argv[i], "--sim-port") && reste) {
            port_sim = argv[++i];
        } else if (!strcmp(argv[i], "--sans-sim")) {
            sim_actif = 0;
        } else {
            usage(argv[0]);
            return strcmp(argv[i], "--aide") == 0 ? 0 : 2;
        }
    }
    if (port_http <= 0 || port_http > 65535 || !proto_vitesse_valide(vitesse_uart)) {
        usage(argv[0]);
        return 2;
    }

    if (charger_fichiers(dossier_web) != 0) {
        return 1;
    }
    charger_noms(fichier_noms);
    charger_carte(fichier_carte);

    kv_init(&kalman);
    trace_init(&trace, PAS_TRACE_M);
    const sim_rappels_t rappels = {NULL, NULL, sur_gps, sur_sms, sim_journal};
    sim7600_init(&sim, port_sim, &rappels);
    if (sim_actif) {
        charger_env(fichier_env);
    } else {
        journal("module 4G/GPS desactive (--sans-sim)");
    }

    fd_ecoute = ouvrir_ecoute(adresse, port_http);
    if (fd_ecoute < 0) {
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sur_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        clients[i].fd = -1;
    }
    proto_decodeur_init(&decodeur);
    ouvrir_uart();
    journal("tableau de bord sur http://%s:%d/", adresse, port_http);

    uint64_t dernier_etat = 0, derniere_vitesse = 0, derniere_tentative_uart = maintenant_ms();

    while (!arret) {
        struct pollfd pf[3 + MAX_CLIENTS];
        int idx_client[MAX_CLIENTS];
        nfds_t n = 0;

        pf[n].fd = fd_ecoute;
        pf[n].events = POLLIN;
        ++n;
        const nfds_t idx_uart = n;
        pf[n].fd = fd_uart;  /* -1 : ignore par poll */
        pf[n].events = POLLIN;
        ++n;
        const nfds_t idx_sim = n;
        const int fd_sim = sim_actif ? sim7600_fd(&sim) : -1;
        pf[n].fd = fd_sim;
        pf[n].events = POLLIN;
        ++n;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            idx_client[i] = -1;
            if (clients[i].etat == LIBRE) continue;
            idx_client[i] = (int)n;
            pf[n].fd = clients[i].fd;
            pf[n].events = (short)(POLLIN | (a_envoyer(&clients[i]) ? POLLOUT : 0));
            ++n;
        }

        if (poll(pf, n, 100) < 0 && errno != EINTR) {
            perror("poll");
            break;
        }
        const uint64_t t = maintenant_ms();

        if (pf[0].revents & POLLIN) {
            accepter();
        }
        if (fd_uart >= 0 && (pf[idx_uart].revents & (POLLIN | POLLERR | POLLHUP))) {
            lire_uart();
        }
        if (sim_actif) {
            if (fd_sim >= 0 && (pf[idx_sim].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) {
                sim7600_lire(&sim, pf[idx_sim].revents, t);
            }
            sim7600_tick(&sim, t);
        }
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            client_t *c = &clients[i];
            const int k = idx_client[i];
            if (k < 0 || c->etat == LIBRE) continue;
            if (pf[k].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                fermer_client(c);
                continue;
            }
            if (pf[k].revents & POLLIN) {
                lire_client(c);
            }
            if (c->etat != LIBRE && (pf[k].revents & POLLOUT) && !vider(c)) {
                fermer_client(c);
            }
            if (c->etat == REQUETE && t - c->debut_ms > DELAI_REQUETE_MS) {
                fermer_client(c);  /* requete trop lente */
            }
        }

        if (fd_uart < 0 && t - derniere_tentative_uart >= PERIODE_REOUVERTURE_UART_MS) {
            derniere_tentative_uart = t;
            ouvrir_uart();
        }
        /* derniere_trame_ms peut etre posterieure a t (lue apres poll) */
        if (esp_present && t > derniere_trame_ms && t - derniere_trame_ms > DELAI_ESP_PERDU_MS) {
            esp_present = 0;
            journal("ESP32 perdu (plus rien depuis %u ms)", (unsigned)DELAI_ESP_PERDU_MS);
            diffuser_etat();
        }
        if (t - dernier_etat >= PERIODE_ETAT_MS) {
            dernier_etat = t;
            diffuser_etat();  /* sert aussi de battement de coeur pour les ecrans */
            diffuser_sim();
        }
        if (t - derniere_vitesse >= PERIODE_VITESSE_MS) {
            derniere_vitesse = t;
            diffuser_vitesse(maintenant_ms());
        }
    }

    journal("arret");
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        fermer_client(&clients[i]);
    }
    sim7600_fermer(&sim);
    close(fd_ecoute);
    if (fd_uart >= 0) {
        uart_fermer(fd_uart);
    }
    return 0;
}
