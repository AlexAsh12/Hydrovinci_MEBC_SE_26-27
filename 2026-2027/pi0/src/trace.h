/* Trace GPS du bateau, en memoire (taille fixe, aucune allocation).
 * Un point est garde tous les "pas" metres. Quand la trace est pleine, un
 * point sur deux est retire et le pas double : toute la sortie reste
 * visible, avec moins de detail sur les plus anciennes. */
#ifndef TRACE_H
#define TRACE_H

#include <stddef.h>
#include <stdint.h>

#define TRACE_MAX 1500

typedef struct {
    int32_t lat_e7, lon_e7;   /* degres x 1e7 */
} trace_point_t;

typedef struct {
    trace_point_t p[TRACE_MAX];
    int n;
    double pas_m;
    double pas_initial_m;
} trace_t;

void trace_init(trace_t *t, double pas_m);
void trace_effacer(trace_t *t);

/* Ajoute la position si elle est a plus de pas_m du dernier point.
 * Renvoie 1 si un point a ete ajoute. */
int trace_ajouter(trace_t *t, double lat, double lon);

/* Distance approchee en metres (suffisante sur quelques kilometres) */
double trace_distance_m(double lat1, double lon1, double lat2, double lon2);

/* JSON {"pas":3,"points":[[lat_e7,lon_e7],...]}. Renvoie la longueur ecrite,
 * ou 0 si le tampon est trop petit. */
size_t trace_json(const trace_t *t, char *tampon, size_t taille);

#endif /* TRACE_H */
