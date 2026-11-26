/* Carte reelle : zones de navigation (carte/zones.conf) et tuiles Plan IGN
 * stockees sur la Pi (carte/tuiles/Z/X/Y.png, decoupage Web Mercator
 * standard). Fonctions pures, testees sur PC. */
#ifndef CARTE_ZONES_H
#define CARTE_ZONES_H

#include <stddef.h>

#define CZ_TAILLE_NOM 24
#define CZ_ZOOM_MAX 19

typedef struct {
    char nom[CZ_TAILLE_NOM];
    double sud, nord, ouest, est;   /* degres */
    int zmin, zmax;
} cz_rect_t;

/* Lit le texte de zones.conf : "nom sud nord ouest est zmin zmax" par ligne,
 * '#' = commentaire. Les lignes invalides sont ignorees et comptees dans
 * *invalides (si non NULL). Renvoie le nombre de rectangles lus (max au plus). */
int cz_lire(const char *texte, cz_rect_t *rects, int max, int *invalides);

/* Tuile contenant (lat, lon) au zoom z. Renvoie 0 si hors limites. */
int cz_tuile(double lat, double lon, int z, int *x, int *y);

/* Tuiles d'un rectangle au zoom z : x0..x1, y0..y1 inclus */
void cz_etendue(const cz_rect_t *r, int z, int *x0, int *y0, int *x1, int *y1);

/* "/tuiles/16/34119/23899.png" -> z, x, y verifies. 1 si valide.
 * Seuls des chiffres sont acceptes : aucun chemin venant du navigateur
 * ne peut sortir du dossier des tuiles. */
int cz_analyser_url(const char *url, int *z, int *x, int *y);

#endif /* CARTE_ZONES_H */
