/*
 * Test de charge de tableau_bord, a lancer sur la Pi pendant que le service
 * tourne :
 *   make stress_tableau && ./stress_tableau [--duree 20] [--port 8080]
 *
 * Un ecran normal suit le flux /flux pendant que d'autres connexions
 * attaquent le serveur :
 *   - connexions muettes ou qui envoient un octet toutes les 200 ms
 *   - en-tetes geants, octets quelconques
 *   - un client qui ouvre /flux et ne lit jamais
 *   - rafales de requetes (page, tuiles de carte, trace, chemins pieges)
 *   - plus de connexions que le serveur n'en accepte (16)
 *
 * Reussi si l'ecran normal recoit ses donnees sans trou de plus de 1,5 s
 * pendant toute l'attaque et si le serveur repond normalement apres.
 * Avec --pid $(pidof tableau_bord), affiche aussi le temps processeur
 * consomme par le serveur.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_CNX 64
#define TROU_MAX_MS 1500.0

typedef enum { FLUX, MUET, LENT, INACTIF, GEANT, POUBELLE, REQUETE, NB_ROLES } role_t;
static const char *const noms_roles[NB_ROLES] = {"ecran normal", "flux jamais lu", "octet par octet",
                                                 "connexion muette", "en-tete geant", "octets quelconques",
                                                 "requetes"};
static const int nb_par_role[NB_ROLES] = {1, 2, 6, 10, 2, 2, 8};

static const char *const requetes[] = {
    "GET / HTTP/1.1\r\nHost: pi\r\n\r\n",
    "GET /tableau.js HTTP/1.1\r\nHost: pi\r\n\r\n",
    "GET /trace HTTP/1.1\r\nHost: pi\r\n\r\n",
    "GET /tuiles/16/34119/23899.png HTTP/1.1\r\nHost: pi\r\n\r\n",
    "GET /tuiles/15/16580/11265.png HTTP/1.1\r\nHost: pi\r\n\r\n",
    "GET /tuiles/16/1/1.png HTTP/1.1\r\nHost: pi\r\n\r\n",
    "GET /tuiles/16/../../../etc/passwd HTTP/1.1\r\nHost: pi\r\n\r\n",
    "GET /../../.env HTTP/1.1\r\nHost: pi\r\n\r\n",
    "POST /flux HTTP/1.1\r\nHost: pi\r\nContent-Length: 5\r\n\r\nhello",
    "GET /inconnu HTTP/1.1\r\nHost: pi\r\n\r\n",
};
#define NB_REQUETES (sizeof requetes / sizeof requetes[0])

typedef struct {
    int fd;
    role_t role;
    double debut, dernier_envoi;
    char envoi[12000];
    size_t a_envoyer, envoye;
    char recu[256];
    size_t nr;
} cnx_t;

static cnx_t cnx[MAX_CNX];
static int nb_cnx;
static int port = 8080;

/* Statistiques */
static long ouvertures[NB_ROLES], fermees_par_serveur[NB_ROLES];
static long statuts[6];   /* 2xx, 3xx, 4xx, 5xx, autre, refusee */
static double latence_max_ms;
static long evenements;
static double dernier_evenement, trou_max_ms;
static int flux_actif;
static char reste[8];
static size_t nreste;

static double maintenant_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static int connecter(void)
{
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    return fd;
}

static void preparer(cnx_t *c, double t)
{
    c->fd = connecter();
    c->debut = c->dernier_envoi = t;
    c->envoye = 0;
    c->nr = 0;
    ouvertures[c->role]++;
    switch (c->role) {
    case FLUX:
    case MUET:
        c->a_envoyer = (size_t)snprintf(c->envoi, sizeof c->envoi, "GET /flux HTTP/1.1\r\nHost: pi\r\n\r\n");
        break;
    case LENT:
        c->a_envoyer = (size_t)snprintf(c->envoi, sizeof c->envoi, "GET / HTTP/1.1\r\nHost: pi\r\nX-Lent: oui\r\n\r\n");
        break;
    case INACTIF:
        c->a_envoyer = 0;
        break;
    case GEANT: {
        size_t n = (size_t)snprintf(c->envoi, sizeof c->envoi, "GET / HTTP/1.1\r\nX-Geant: ");
        memset(c->envoi + n, 'a', 10000);
        n += 10000;
        memcpy(c->envoi + n, "\r\n\r\n", 4);
        c->a_envoyer = n + 4;
        break;
    }
    case POUBELLE:
        for (size_t i = 0; i < 600; ++i) c->envoi[i] = (char)(rand() & 0xFF);
        c->a_envoyer = 600;
        break;
    case REQUETE: {
        const char *r = requetes[rand() % (int)NB_REQUETES];
        c->a_envoyer = strlen(r);
        memcpy(c->envoi, r, c->a_envoyer);
        break;
    }
    default:
        break;
    }
}

static void terminer(cnx_t *c, double t, int par_serveur)
{
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    if (par_serveur) fermees_par_serveur[c->role]++;
    if (c->role == REQUETE) {
        int code = 0;
        if (c->nr >= 12 && strncmp(c->recu, "HTTP/1.1 ", 9) == 0) code = atoi(c->recu + 9);
        if (code >= 200 && code < 600) statuts[code / 100 - 2]++;
        else if (c->nr == 0) statuts[5]++;   /* acceptee puis fermee : serveur plein */
        else statuts[4]++;
        if (c->nr > 0 && t - c->debut > latence_max_ms) latence_max_ms = t - c->debut;
    }
    if (c->role == FLUX) {
        flux_actif = 0;
        fprintf(stderr, "ATTENTION : le flux de l'ecran normal a ete coupe par le serveur\n");
    }
}

/* Compte les evenements "data: " du flux, meme coupes entre deux lectures */
static void compter_evenements(const char *d, size_t n, double t)
{
    char tampon[4096 + sizeof reste];
    memcpy(tampon, reste, nreste);
    memcpy(tampon + nreste, d, n);
    const size_t total = nreste + n;
    int vu = 0;
    for (size_t i = 0; i + 6 <= total; ++i) {
        if (memcmp(tampon + i, "data: ", 6) == 0) {
            evenements++;
            vu = 1;
        }
    }
    nreste = total < 5 ? total : 5;
    memcpy(reste, tampon + total - nreste, nreste);
    if (vu) {
        if (dernier_evenement > 0 && t - dernier_evenement > trou_max_ms) trou_max_ms = t - dernier_evenement;
        dernier_evenement = t;
    }
}

/* GET / simple et bloquant (2 s max) : le serveur repond-il normalement ? */
static int serveur_repond(void)
{
    const int fd = connecter();
    if (fd < 0) return 0;
    struct pollfd p = {fd, POLLOUT, 0};
    if (poll(&p, 1, 2000) <= 0) { close(fd); return 0; }
    static const char r[] = "GET / HTTP/1.1\r\nHost: pi\r\n\r\n";
    if (send(fd, r, sizeof r - 1, MSG_NOSIGNAL) != (ssize_t)(sizeof r - 1)) { close(fd); return 0; }
    char rep[64] = "";
    p.events = POLLIN;
    size_t n = 0;
    const double debut = maintenant_ms();
    while (n < 12 && maintenant_ms() - debut < 2000) {
        if (poll(&p, 1, 200) > 0) {
            const ssize_t k = recv(fd, rep + n, sizeof rep - 1 - n, 0);
            if (k <= 0) break;
            n += (size_t)k;
        }
    }
    close(fd);
    rep[n] = '\0';
    return strncmp(rep, "HTTP/1.1 200", 12) == 0;
}

static long temps_cpu_ms(int pid)
{
    char chemin[64], ligne[1024];
    snprintf(chemin, sizeof chemin, "/proc/%d/stat", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return -1;
    const size_t n = fread(ligne, 1, sizeof ligne - 1, f);
    fclose(f);
    ligne[n] = '\0';
    const char *p = strrchr(ligne, ')');   /* le nom du programme peut contenir des espaces */
    unsigned long utime = 0, stime = 0;
    if (!p || sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime, &stime) != 2) return -1;
    return (long)((utime + stime) * 1000UL / (unsigned long)sysconf(_SC_CLK_TCK));
}

int main(int argc, char **argv)
{
    double duree_s = 20;
    int pid = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--duree") && i + 1 < argc) duree_s = atof(argv[++i]);
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pid") && i + 1 < argc) pid = atoi(argv[++i]);
        else {
            printf("Usage : %s [--duree S] [--port 8080] [--pid PID_tableau_bord]\n", argv[0]);
            return 2;
        }
    }
    srand(12345);

    if (!serveur_repond()) {
        printf("tableau_bord ne repond pas sur 127.0.0.1:%d (service lance ?)\n", port);
        return 1;
    }
    printf("Attaque de 127.0.0.1:%d pendant %.0f s...\n", port, duree_s);
    const long cpu_avant = pid ? temps_cpu_ms(pid) : -1;

    /* L'ecran normal d'abord, comme le kiosque deja connecte */
    for (int r = 0; r < NB_ROLES; ++r) {
        for (int k = 0; k < nb_par_role[r] && nb_cnx < MAX_CNX; ++k) cnx[nb_cnx++].role = (role_t)r;
    }
    double t = maintenant_ms();
    const double debut = t, fin = t + duree_s * 1000.0;
    for (int i = 0; i < nb_cnx; ++i) {
        preparer(&cnx[i], t);
        if (i == 0) {
            flux_actif = cnx[0].fd >= 0;
            usleep(300000);   /* laisse l'ecran normal s'installer */
        }
    }

    while ((t = maintenant_ms()) < fin) {
        struct pollfd pf[MAX_CNX];
        for (int i = 0; i < nb_cnx; ++i) {
            cnx_t *c = &cnx[i];
            pf[i].fd = c->fd;
            pf[i].events = POLLIN;
            const int doit_envoyer = c->envoye < c->a_envoyer &&
                                     (c->role != LENT || t - c->dernier_envoi >= 200.0);
            if (doit_envoyer) pf[i].events |= POLLOUT;
            if (c->role == MUET && c->envoye == c->a_envoyer) pf[i].events = 0;   /* ne lit jamais */
            pf[i].revents = 0;
        }
        poll(pf, (nfds_t)nb_cnx, 20);
        t = maintenant_ms();

        for (int i = 0; i < nb_cnx; ++i) {
            cnx_t *c = &cnx[i];
            if (c->fd < 0) {
                if (c->role != FLUX) preparer(c, t);   /* les attaquants reviennent sans cesse */
                continue;
            }
            if (pf[i].revents & POLLOUT) {
                const size_t n = c->role == LENT ? 1 : c->a_envoyer - c->envoye;
                const ssize_t k = send(c->fd, c->envoi + c->envoye, n, MSG_NOSIGNAL | MSG_DONTWAIT);
                if (k > 0) {
                    c->envoye += (size_t)k;
                    c->dernier_envoi = t;
                } else if (k < 0 && errno != EAGAIN && errno != EINTR) {
                    terminer(c, t, 1);
                    continue;
                }
            }
            if (pf[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                char d[4096];
                const ssize_t k = recv(c->fd, d, sizeof d, MSG_DONTWAIT);
                if (k > 0) {
                    if (c->role == FLUX) compter_evenements(d, (size_t)k, t);
                    const size_t place = sizeof c->recu - 1 - c->nr;
                    const size_t copie = (size_t)k < place ? (size_t)k : place;
                    memcpy(c->recu + c->nr, d, copie);
                    c->nr += copie;
                    c->recu[c->nr] = '\0';
                } else if (k == 0 || (errno != EAGAIN && errno != EINTR)) {
                    terminer(c, t, 1);
                    continue;
                }
            }
            /* Une connexion muette ou lente abandonne d'elle-meme au bout de 8 s */
            if ((c->role == INACTIF || c->role == LENT) && t - c->debut > 8000.0) terminer(c, t, 0);
        }
        if (flux_actif && dernier_evenement > 0 && t - dernier_evenement > trou_max_ms) {
            trou_max_ms = t - dernier_evenement;
        }
    }

    for (int i = 0; i < nb_cnx; ++i) {
        if (cnx[i].fd >= 0) close(cnx[i].fd);
    }
    const double duree_ms = maintenant_ms() - debut;
    usleep(300000);
    const int repond = serveur_repond();
    const long cpu_apres = pid ? temps_cpu_ms(pid) : -1;

    printf("\n%-20s %10s %14s\n", "attaquant", "connexions", "fermees serveur");
    for (int r = 1; r < NB_ROLES; ++r) {
        printf("%-20s %10ld %14ld\n", noms_roles[r], ouvertures[r], fermees_par_serveur[r]);
    }
    printf("\nrequetes : %ld en 2xx, %ld en 4xx, %ld en 5xx, %ld autres, %ld refusees (serveur plein)\n",
           statuts[0], statuts[2], statuts[3], statuts[4], statuts[5]);
    printf("latence max d'une requete : %.0f ms\n", latence_max_ms);
    printf("ecran normal : %ld evenements recus, plus long trou %.0f ms, flux %s\n", evenements, trou_max_ms,
           flux_actif ? "toujours ouvert" : "COUPE");
    if (cpu_avant >= 0 && cpu_apres >= 0) {
        printf("processeur du serveur : %.1f %% d'un coeur pendant l'attaque\n",
               100.0 * (double)(cpu_apres - cpu_avant) / duree_ms);
    }
    printf("serveur apres l'attaque : %s\n", repond ? "repond normalement" : "NE REPOND PLUS");

    const int reussi = repond && flux_actif && evenements > 0 && trou_max_ms <= TROU_MAX_MS;
    printf("\nRESULTAT : %s\n", reussi ? "REUSSI" : "ECHEC");
    return reussi ? 0 : 1;
}
