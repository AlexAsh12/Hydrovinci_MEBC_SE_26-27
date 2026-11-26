/* Carte reelle : zones et tuiles. */
#include "carte_zones.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define LAT_MAX 85.0511  /* limite de la projection Web Mercator */

int cz_lire(const char *texte, cz_rect_t *rects, int max, int *invalides)
{
    int n = 0, mauvaises = 0;
    const char *ligne = texte;
    while (ligne && *ligne && n < max) {
        const char *fin = strchr(ligne, '\n');
        size_t l = fin ? (size_t)(fin - ligne) : strlen(ligne);
        char copie[256];
        if (l >= sizeof copie) l = sizeof copie - 1;
        memcpy(copie, ligne, l);
        copie[l] = '\0';
        char *diese = strchr(copie, '#');
        if (diese) *diese = '\0';

        cz_rect_t r;
        char reste[8];
        const int lus = sscanf(copie, "%23s %lf %lf %lf %lf %d %d %7s", r.nom, &r.sud, &r.nord, &r.ouest, &r.est,
                               &r.zmin, &r.zmax, reste);
        if (lus == 7 && r.sud < r.nord && r.ouest < r.est && r.sud >= -LAT_MAX && r.nord <= LAT_MAX &&
            r.ouest >= -180.0 && r.est <= 180.0 && r.zmin >= 0 && r.zmin <= r.zmax && r.zmax <= CZ_ZOOM_MAX) {
            rects[n++] = r;
        } else if (lus > 0 && lus != EOF) {
            mauvaises++;
        }
        ligne = fin ? fin + 1 : NULL;
    }
    if (invalides) *invalides = mauvaises;
    return n;
}

int cz_tuile(double lat, double lon, int z, int *x, int *y)
{
    if (z < 0 || z > CZ_ZOOM_MAX || !isfinite(lat) || !isfinite(lon) || fabs(lat) > LAT_MAX || fabs(lon) > 180.0) {
        return 0;
    }
    const double n = (double)(1L << z);
    const double phi = lat * PI / 180.0;
    long tx = (long)floor((lon + 180.0) / 360.0 * n);
    long ty = (long)floor((1.0 - log(tan(phi) + 1.0 / cos(phi)) / PI) / 2.0 * n);
    const long dernier = (1L << z) - 1;
    if (tx < 0) tx = 0;
    if (tx > dernier) tx = dernier;
    if (ty < 0) ty = 0;
    if (ty > dernier) ty = dernier;
    *x = (int)tx;
    *y = (int)ty;
    return 1;
}

void cz_etendue(const cz_rect_t *r, int z, int *x0, int *y0, int *x1, int *y1)
{
    cz_tuile(r->nord, r->ouest, z, x0, y0);  /* coin haut gauche */
    cz_tuile(r->sud, r->est, z, x1, y1);     /* coin bas droit */
}

/* Entier positif ecrit en chiffres seulement, suivi de "fin" */
static int nombre(const char **p, char fin, long max, long *v)
{
    const char *q = *p;
    long n = 0;
    int chiffres = 0;
    while (*q >= '0' && *q <= '9') {
        n = n * 10 + (*q - '0');
        if (++chiffres > 7 || n > max) return 0;
        q++;
    }
    if (!chiffres || *q != fin) return 0;
    *p = q + 1;
    *v = n;
    return 1;
}

int cz_analyser_url(const char *url, int *z, int *x, int *y)
{
    static const char prefixe[] = "/tuiles/";
    if (strncmp(url, prefixe, sizeof prefixe - 1) != 0) return 0;
    const char *p = url + sizeof prefixe - 1;
    long vz, vx, vy;
    if (!nombre(&p, '/', CZ_ZOOM_MAX, &vz)) return 0;
    const long limite = (1L << vz) - 1;
    if (!nombre(&p, '/', limite, &vx) || !nombre(&p, '.', limite, &vy)) return 0;
    if (strcmp(p, "png") != 0) return 0;
    *z = (int)vz;
    *x = (int)vx;
    *y = (int)vy;
    return 1;
}
