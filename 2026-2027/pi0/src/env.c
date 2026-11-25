/* Lecture du fichier .env. */
#include "env.h"

#include <stdio.h>
#include <string.h>

#define TAILLE_MAX_ENV 8192

static int espace(char c)
{
    return c == ' ' || c == '\t';
}

int env_chercher(const char *texte, const char *cle, char *valeur, size_t taille)
{
    const size_t lcle = strlen(cle);
    if (taille == 0) {
        return 0;
    }
    valeur[0] = '\0';

    const char *ligne = texte;
    while (ligne && *ligne) {
        const char *fin = strchr(ligne, '\n');
        const size_t lligne = fin ? (size_t)(fin - ligne) : strlen(ligne);
        const char *p = ligne;
        const char *const limite = ligne + lligne;

        while (p < limite && espace(*p)) p++;
        if (limite - p > 7 && strncmp(p, "export ", 7) == 0) {
            p += 7;
            while (p < limite && espace(*p)) p++;
        }
        if (p < limite && *p != '#' && (size_t)(limite - p) > lcle && strncmp(p, cle, lcle) == 0) {
            const char *q = p + lcle;
            while (q < limite && espace(*q)) q++;
            if (q < limite && *q == '=') {
                q++;
                while (q < limite && espace(*q)) q++;
                const char *debut = q, *stop = limite;
                if (q < limite && (*q == '\'' || *q == '"')) {
                    const char guillemet = *q;
                    debut = q + 1;
                    stop = debut;
                    while (stop < limite && *stop != guillemet) stop++;
                } else {
                    /* Sans guillemets : fin de ligne, sans '\r' ni espaces finaux,
                     * un ' #' commence un commentaire */
                    const char *c = debut;
                    while (c < limite && !(*c == '#' && c > debut && espace(c[-1]))) c++;
                    stop = c;
                    while (stop > debut && (espace(stop[-1]) || stop[-1] == '\r')) stop--;
                }
                size_t n = (size_t)(stop - debut);
                if (n >= taille) n = taille - 1;
                memcpy(valeur, debut, n);
                valeur[n] = '\0';
                return 1;
            }
        }
        ligne = fin ? fin + 1 : NULL;
    }
    return 0;
}

int env_lire(const char *chemin, const char *cle, char *valeur, size_t taille)
{
    static char texte[TAILLE_MAX_ENV + 1];
    if (taille) valeur[0] = '\0';
    FILE *f = fopen(chemin, "rb");
    if (!f) {
        return 0;
    }
    const size_t n = fread(texte, 1, TAILLE_MAX_ENV, f);
    fclose(f);
    texte[n] = '\0';
    const int trouve = env_chercher(texte, cle, valeur, taille);
    memset(texte, 0, sizeof texte);  /* le .env contient d'autres secrets */
    return trouve;
}

int env_decouper_liste(const char *liste, char *elements, size_t taille_elt, int max)
{
    int n = 0;
    const char *p = liste;
    while (*p && n < max) {
        while (*p == ',' || *p == ';' || espace(*p)) p++;
        if (!*p) break;
        const char *debut = p;
        while (*p && *p != ',' && *p != ';') p++;
        const char *fin = p;
        while (fin > debut && espace(fin[-1])) fin--;  /* espaces de fin */
        size_t l = (size_t)(fin - debut);
        if (l >= taille_elt) l = taille_elt - 1;
        memcpy(elements + (size_t)n * taille_elt, debut, l);
        elements[(size_t)n * taille_elt + l] = '\0';
        n++;
    }
    return n;
}
