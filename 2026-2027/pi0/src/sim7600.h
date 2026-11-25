/* Module 4G/GPS SIM7600G-H branche en USB sur la Pi (pogo pins du HAT (B)).
 *
 * Trouve tout seul le port AT parmi les ports serie USB du module
 * (/dev/serial/by-id/usb-SimTech...), le surveille et le rouvre s'il est
 * debranche puis rebranche : aucune configuration, et le reste du tableau
 * de bord continue sans lui. Le dialogue AT est dans sim_moteur.c. */
#ifndef SIM7600_H
#define SIM7600_H

#include <stdint.h>

#include "sim_moteur.h"

#define SIM7600_MAX_CANDIDATS 8
#define SIM7600_TAILLE_CHEMIN 256

typedef struct {
    sim_moteur_t moteur;
    sim_rappels_t util;               /* rappels du programme (ecrire est ignore) */
    int fd;
    int sondage;                      /* fd ouvert pour essai, pas encore adopte */
    char chemin[SIM7600_TAILLE_CHEMIN];
    const char *chemin_force;
    char candidats[SIM7600_MAX_CANDIDATS][SIM7600_TAILLE_CHEMIN];
    int nb_candidats, idx;
    uint64_t debut_sondage, prochaine_recherche;
    char reponse[128];
    size_t nr;
    int absent_signale;
} sim7600_t;

/* chemin_force : port AT impose (option --sim-port), NULL = recherche auto */
void sim7600_init(sim7600_t *s, const char *chemin_force, const sim_rappels_t *r);
/* Descripteur a surveiller (POLLIN), -1 si aucun */
int sim7600_fd(const sim7600_t *s);
/* A appeler quand poll() signale quelque chose sur sim7600_fd() */
void sim7600_lire(sim7600_t *s, short revents, uint64_t t);
/* A appeler souvent (chaque tour de boucle) */
void sim7600_tick(sim7600_t *s, uint64_t t);
void sim7600_fermer(sim7600_t *s);

#endif /* SIM7600_H */
