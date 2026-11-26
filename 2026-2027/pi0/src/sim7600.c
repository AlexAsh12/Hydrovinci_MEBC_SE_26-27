/* Module SIM7600 en USB : recherche du port AT et branchement a chaud. */
#include "sim7600.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "uart.h"

#define DOSSIER_PAR_ID "/dev/serial/by-id"
#define PERIODE_RECHERCHE_MS 3000u
#define DELAI_SONDAGE_MS 600u
#define ATTENTE_APRES_PERTE_MS 2000u

__attribute__((format(printf, 2, 3)))
static void noter(sim7600_t *s, const char *format, ...)
{
    if (!s->util.journal) return;
    char texte[300];
    va_list a;
    va_start(a, format);
    vsnprintf(texte, sizeof texte, format, a);
    va_end(a);
    s->util.journal(s->util.ctx, texte);
}

/* ------------------------------------------------------------------ */
/* Rappels du moteur -> port serie / programme                         */
/* ------------------------------------------------------------------ */

/* Ecriture non bloquante bornee : un module qui n'avale plus rien ne doit
 * jamais figer le tableau de bord. */
static int ecrire_port(void *ctx, const char *data, size_t n)
{
    sim7600_t *s = ctx;
    if (s->fd < 0 || s->sondage) return -1;
    size_t envoye = 0;
    for (int essais = 0; envoye < n && essais < 10;) {
        const ssize_t k = write(s->fd, data + envoye, n - envoye);
        if (k > 0) {
            envoye += (size_t)k;
            continue;
        }
        if (k < 0 && errno != EAGAIN && errno != EINTR) return -1;
        struct pollfd p = {s->fd, POLLOUT, 0};
        poll(&p, 1, 20);
        essais++;
    }
    return envoye == n ? 0 : -1;
}

static void sur_gps(void *ctx, const sim_gps_t *g, int fix)
{
    sim7600_t *s = ctx;
    if (s->util.sur_gps) s->util.sur_gps(s->util.ctx, g, fix);
}

static void sur_sms(void *ctx, const char *numero, const char *texte, const char *horodatage)
{
    sim7600_t *s = ctx;
    if (s->util.sur_sms) s->util.sur_sms(s->util.ctx, numero, texte, horodatage);
}

static void journal(void *ctx, const char *texte)
{
    sim7600_t *s = ctx;
    noter(s, "SIM7600 : %s", texte);
}

/* ------------------------------------------------------------------ */
/* Recherche du port                                                   */
/* ------------------------------------------------------------------ */

static int contient(const char *texte, const char *mot)
{
    const size_t n = strlen(mot);
    for (; *texte; ++texte) {
        size_t i = 0;
        while (i < n && texte[i] && tolower((unsigned char)texte[i]) == mot[i]) i++;
        if (i == n) return 1;
    }
    return 0;
}

static int comparer(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* Liste les ports serie USB du module, tries : "...-if00-port0", "-if01-"...
 * Le premier qui repond a "AT" est le port de commandes (if02 en mode USB
 * par defaut, if04 en mode RNDIS) : le port GPS NMEA et le port de
 * diagnostic ne repondent pas. */
static void chercher_candidats(sim7600_t *s)
{
    s->nb_candidats = 0;
    s->idx = 0;
    if (s->chemin_force) {
        snprintf(s->candidats[0], SIM7600_TAILLE_CHEMIN, "%s", s->chemin_force);
        s->nb_candidats = 1;
        return;
    }
    DIR *d = opendir(DOSSIER_PAR_ID);
    if (!d) return;  /* aucun port serie USB branche */
    const struct dirent *e;
    while ((e = readdir(d)) != NULL && s->nb_candidats < SIM7600_MAX_CANDIDATS) {
        if (contient(e->d_name, "simtech") || contient(e->d_name, "simcom") || contient(e->d_name, "sim7600")) {
            const size_t l = strlen(e->d_name);
            if (sizeof DOSSIER_PAR_ID + l >= SIM7600_TAILLE_CHEMIN) continue;  /* nom trop long : ignore */
            char *c = s->candidats[s->nb_candidats++];
            memcpy(c, DOSSIER_PAR_ID "/", sizeof DOSSIER_PAR_ID);
            memcpy(c + sizeof DOSSIER_PAR_ID, e->d_name, l + 1);
        }
    }
    closedir(d);
    qsort(s->candidats, (size_t)s->nb_candidats, SIM7600_TAILLE_CHEMIN, comparer);
}

static void fermer_fd(sim7600_t *s)
{
    if (s->fd >= 0) uart_fermer(s->fd);
    s->fd = -1;
    s->sondage = 0;
}

/* Port perdu (debranche, erreur, module muet) */
static void perdre(sim7600_t *s, const char *raison, uint64_t t)
{
    const int adopte = s->fd >= 0 && !s->sondage;
    fermer_fd(s);
    if (adopte) {
        noter(s, "SIM7600 : %s, nouvelle recherche", raison);
        sim_moteur_ferme(&s->moteur);
        s->absent_signale = 0;
    }
    s->nb_candidats = 0;
    s->prochaine_recherche = t + ATTENTE_APRES_PERTE_MS;
}

static void essayer_suivant(sim7600_t *s, uint64_t t)
{
    while (s->idx < s->nb_candidats) {
        const char *chemin = s->candidats[s->idx++];
        const int fd = uart_ouvrir(chemin, 115200);  /* USB : la vitesse est ignoree */
        if (fd < 0) continue;
        s->fd = fd;
        s->sondage = 1;
        s->debut_sondage = t;
        s->nr = 0;
        snprintf(s->chemin, sizeof s->chemin, "%s", chemin);
        if (write(fd, "AT\r", 3) != 3) {
            fermer_fd(s);
            continue;
        }
        return;
    }
    /* Aucun port ne repond */
    if (!s->absent_signale) {
        noter(s, s->nb_candidats ? "SIM7600 : branche mais ne repond pas encore"
                                 : "SIM7600 : absent (le tableau de bord continue sans 4G ni GPS)");
        s->absent_signale = 1;
    }
    s->nb_candidats = 0;
    s->prochaine_recherche = t + PERIODE_RECHERCHE_MS;
}

/* ------------------------------------------------------------------ */
/* Interface                                                           */
/* ------------------------------------------------------------------ */

void sim7600_init(sim7600_t *s, const char *chemin_force, const sim_rappels_t *r)
{
    memset(s, 0, sizeof *s);
    s->fd = -1;
    s->chemin_force = chemin_force;
    if (r) s->util = *r;
    const sim_rappels_t interne = {s, ecrire_port, sur_gps, sur_sms, journal};
    sim_moteur_init(&s->moteur, &interne);
}

int sim7600_fd(const sim7600_t *s)
{
    return s->fd;
}

void sim7600_lire(sim7600_t *s, short revents, uint64_t t)
{
    if (s->fd < 0) return;
    char tampon[1024];
    const ssize_t n = read(s->fd, tampon, sizeof tampon);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        perdre(s, "port en erreur (debranche ?)", t);
        return;
    }
    if (n == 0) {
        if (revents & (POLLHUP | POLLERR | POLLNVAL)) perdre(s, "debranche", t);
        return;
    }

    if (s->sondage) {
        const size_t place = sizeof s->reponse - 1 - s->nr;
        const size_t k = (size_t)n < place ? (size_t)n : place;
        memcpy(s->reponse + s->nr, tampon, k);
        s->nr += k;
        s->reponse[s->nr] = '\0';
        if (strstr(s->reponse, "OK\r")) {  /* reponse finale "\r\nOK\r\n" */
            s->sondage = 0;
            s->absent_signale = 0;
            noter(s, "SIM7600 : port de commandes %s", s->chemin);
            uart_vider_reception(s->fd);
            sim_moteur_ouvert(&s->moteur, t);
        }
        return;
    }
    sim_moteur_recevoir(&s->moteur, tampon, (size_t)n, t);
}

void sim7600_tick(sim7600_t *s, uint64_t t)
{
    if (s->fd >= 0 && s->sondage) {
        if (t - s->debut_sondage >= DELAI_SONDAGE_MS) {
            fermer_fd(s);           /* pas ce port-la */
            essayer_suivant(s, t);
        }
        return;
    }
    if (s->fd >= 0) {
        sim_moteur_tick(&s->moteur, t);
        if (sim_moteur_muet(&s->moteur)) perdre(s, "ne repond plus", t);
        return;
    }
    if (t >= s->prochaine_recherche) {
        chercher_candidats(s);
        essayer_suivant(s, t);
    }
}

void sim7600_fermer(sim7600_t *s)
{
    fermer_fd(s);
    sim_moteur_ferme(&s->moteur);
}
