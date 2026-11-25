/* Trace GPS du bateau. */
#include "trace.h"

#include <math.h>
#include <stdio.h>

#define RAYON_TERRE_M 6371000.0
#define PI 3.14159265358979323846

void trace_init(trace_t *t, double pas_m)
{
    t->n = 0;
    t->pas_initial_m = pas_m > 0.1 ? pas_m : 0.1;
    t->pas_m = t->pas_initial_m;
}

void trace_effacer(trace_t *t)
{
    t->n = 0;
    t->pas_m = t->pas_initial_m;
}

double trace_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    const double r = PI / 180.0;
    const double x = (lon2 - lon1) * r * cos((lat1 + lat2) * 0.5 * r);
    const double y = (lat2 - lat1) * r;
    return sqrt(x * x + y * y) * RAYON_TERRE_M;
}

int trace_ajouter(trace_t *t, double lat, double lon)
{
    if (!isfinite(lat) || !isfinite(lon) || fabs(lat) > 90.0 || fabs(lon) > 180.0) return 0;
    if (t->n > 0) {
        const trace_point_t *d = &t->p[t->n - 1];
        if (trace_distance_m(d->lat_e7 / 1e7, d->lon_e7 / 1e7, lat, lon) < t->pas_m) return 0;
    }
    if (t->n == TRACE_MAX) {
        /* Garde les points pairs (le premier compris), le dernier est remplace par le nouveau */
        int j = 0;
        for (int i = 0; i < t->n; i += 2) t->p[j++] = t->p[i];
        t->n = j;
        t->pas_m *= 2.0;
    }
    t->p[t->n].lat_e7 = (int32_t)lround(lat * 1e7);
    t->p[t->n].lon_e7 = (int32_t)lround(lon * 1e7);
    t->n++;
    return 1;
}

size_t trace_json(const trace_t *t, char *tampon, size_t taille)
{
    size_t pos = 0;
    int n = snprintf(tampon, taille, "{\"pas\":%.0f,\"points\":[", t->pas_m);
    if (n < 0 || (size_t)n >= taille) return 0;
    pos = (size_t)n;
    for (int i = 0; i < t->n; ++i) {
        n = snprintf(tampon + pos, taille - pos, "%s[%ld,%ld]", i ? "," : "", (long)t->p[i].lat_e7,
                     (long)t->p[i].lon_e7);
        if (n < 0 || (size_t)n >= taille - pos) return 0;
        pos += (size_t)n;
    }
    n = snprintf(tampon + pos, taille - pos, "]}");
    if (n < 0 || (size_t)n >= taille - pos) return 0;
    return pos + (size_t)n;
}
