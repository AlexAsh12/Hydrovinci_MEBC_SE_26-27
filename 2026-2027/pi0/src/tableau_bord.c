/*
 * Tableau de bord : recoit les donnees de l'ESP32 sur l'UART et les sert a
 * un navigateur (page web + flux temps reel "Server-Sent Events").
 *
 *   ./tableau_bord [--port /dev/serial0] [--vitesse N] [--http 8080]
 *                  [--ecoute 127.0.0.1] [--web DIR]
 *
 * Un seul processus, une seule boucle poll() : pas de thread, pas d'allocation
 * apres le demarrage.
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
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "protocole.h"
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
enum { D_HELLO, D_HB, D_IMU, D_BMS1, D_BMS2, D_POWER, D_TEMP, D_PILOT, NB_DERNIERS };
static char derniers[NB_DERNIERS][TAILLE_EVENEMENT];

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

/* Texte venant de l'ESP : on ne garde que l'ASCII imprimable, echappe */
static void j_texte(json_t *s, const char *t)
{
    j(s, "\"");
    for (; *t; ++t) {
        const unsigned char ch = (unsigned char)*t;
        if (ch == '"' || ch == '\\') {
            j(s, "\\%c", ch);
        } else if (ch >= 0x20 && ch < 0x7F) {
            j(s, "%c", ch);
        }
    }
    j(s, "\"");
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
        j(&s, "{\"t\":\"imu\",\"roulis\":%d,\"tangage\":%d,\"cap\":%u,\"precision\":%d}",
          m.roulis_cdeg, m.tangage_cdeg, m.cap_cdeg,
          m.precision == PROTO_IMU_PRECISION_INCONNUE ? -1 : (int)m.precision);
        memoriser_et_diffuser(D_IMU, &s);
        break;
    }
    case PROTO_T_BMS: {
        static proto_bms_t m;  /* gros : hors de la pile */
        if (!proto_decoder_bms(t->payload, t->len, &m) || m.id < 1 || m.id > 2) return;
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
            j(&s, "\",\"c\":%d}", m.sondes[i].c_centieme);
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
        j_texte(&s, texte);
        j(&s, "}");
        memoriser_et_diffuser(-1, &s);
        break;
    }
    default:
        break;  /* ACK, TEST... : rien a afficher */
    }
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
    for (size_t i = 0; i < NB_FICHIERS; ++i) {
        if (strcmp(chemin, fichiers[i].url) == 0) {
            char e[1024];
            const int n = snprintf(e, sizeof e,
                                   "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                                   ENTETES_SECURITE "Connection: close\r\n\r\n",
                                   fichiers[i].type, fichiers[i].taille);
            c->etat = REPONSE;
            if (n > 0 && (size_t)n < sizeof e) {
                ajouter(c, e, (size_t)n);
            }
            c->fichier = fichiers[i].data;
            c->fichier_pos = 0;
            c->fichier_len = fichiers[i].taille;
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
           "  --web DOSSIER     dossier des pages (defaut ./web)\n",
           nom, (unsigned long)PROTO_VITESSE_DEFAUT);
}

int main(int argc, char **argv)
{
    const char *adresse = "127.0.0.1";
    const char *dossier_web = "web";
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

    uint64_t dernier_etat = 0, derniere_tentative_uart = maintenant_ms();

    while (!arret) {
        struct pollfd pf[2 + MAX_CLIENTS];
        int idx_client[MAX_CLIENTS];
        nfds_t n = 0;

        pf[n].fd = fd_ecoute;
        pf[n].events = POLLIN;
        ++n;
        const nfds_t idx_uart = n;
        pf[n].fd = fd_uart;  /* -1 : ignore par poll */
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
        if (esp_present && t - derniere_trame_ms > DELAI_ESP_PERDU_MS) {
            esp_present = 0;
            journal("ESP32 perdu (plus rien depuis %u ms)", (unsigned)DELAI_ESP_PERDU_MS);
            diffuser_etat();
        }
        if (t - dernier_etat >= PERIODE_ETAT_MS) {
            dernier_etat = t;
            diffuser_etat();  /* sert aussi de battement de coeur pour les ecrans */
        }
    }

    journal("arret");
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        fermer_client(&clients[i]);
    }
    close(fd_ecoute);
    if (fd_uart >= 0) {
        uart_fermer(fd_uart);
    }
    return 0;
}
