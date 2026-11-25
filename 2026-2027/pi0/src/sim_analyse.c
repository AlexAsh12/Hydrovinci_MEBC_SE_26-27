/* Analyse des reponses AT du module SIM7600. */
#include "sim_analyse.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define NOEUD_MS 0.514444

/* Copie bornee, toujours terminee */
static void copier(char *dst, size_t taille, const char *src, size_t n)
{
    if (taille == 0) return;
    if (n >= taille) n = taille - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Saute "+XXX:" et les espaces. NULL si la ligne ne commence pas par prefixe. */
static const char *apres(const char *ligne, const char *prefixe)
{
    const size_t n = strlen(prefixe);
    if (strncmp(ligne, prefixe, n) != 0) return NULL;
    ligne += n;
    while (*ligne == ' ') ligne++;
    return ligne;
}

/* Decoupe sur les virgules en respectant les guillemets ; les guillemets
 * sont retires. Les champs vides sont gardes. Renvoie le nombre de champs. */
#define MAX_CHAMPS 12
#define TAILLE_CHAMP 48
static int champs(const char *p, char sortie[MAX_CHAMPS][TAILLE_CHAMP])
{
    int n = 0;
    for (;;) {
        char *c = sortie[n];
        size_t l = 0;
        int guillemets = 0;
        while (*p && (guillemets || *p != ',')) {
            if (*p == '"') {
                guillemets = !guillemets;
            } else if (l < TAILLE_CHAMP - 1 && *p != '\r' && *p != '\n') {
                c[l++] = *p;
            }
            p++;
        }
        c[l] = '\0';
        n++;
        if (*p != ',' || n == MAX_CHAMPS) return n;
        p++;
    }
}

/* Entier sans signe sur tout le champ. -1 si invalide. */
static long entier(const char *s)
{
    if (!*s) return -1;
    char *fin;
    const long v = strtol(s, &fin, 10);
    return (*fin == '\0' && v >= 0) ? v : -1;
}

/* Nombre decimal sur tout le champ. 0 si invalide. */
static int decimal(const char *s, double *v)
{
    if (!*s) return 0;
    char *fin;
    *v = strtod(s, &fin);
    return *fin == '\0';
}

/* "4717.112671" (ddmm.mmmm) -> degres */
static int degres_minutes(const char *s, double max, double *deg)
{
    double v;
    if (!decimal(s, &v) || v < 0) return 0;
    const double d = (double)(long)(v / 100.0);
    const double m = v - d * 100.0;
    if (m >= 60.0) return 0;
    *deg = d + m / 60.0;
    return *deg <= max;
}

int sa_cgpsinfo(const char *ligne, sim_gps_t *g)
{
    const char *p = apres(ligne, "+CGPSINFO:");
    if (!p) return -1;
    char f[MAX_CHAMPS][TAILLE_CHAMP];
    const int n = champs(p, f);
    if (n < 9) return -1;
    if (!f[0][0] && !f[2][0]) return 0;  /* pas de position */

    memset(g, 0, sizeof *g);
    if (!degres_minutes(f[0], 90.0, &g->lat) || !degres_minutes(f[2], 180.0, &g->lon)) return -1;
    if (strcmp(f[1], "S") == 0) g->lat = -g->lat;
    else if (strcmp(f[1], "N") != 0) return -1;
    if (strcmp(f[3], "W") == 0) g->lon = -g->lon;
    else if (strcmp(f[3], "E") != 0) return -1;

    if (strlen(f[4]) == 6) copier(g->date, sizeof g->date, f[4], 6);
    if (strlen(f[5]) >= 6 && isdigit((unsigned char)f[5][0])) {
        g->heure[0] = f[5][0];
        g->heure[1] = f[5][1];
        g->heure[2] = ':';
        g->heure[3] = f[5][2];
        g->heure[4] = f[5][3];
        g->heure[5] = ':';
        g->heure[6] = f[5][4];
        g->heure[7] = f[5][5];
        g->heure[8] = '\0';
    }
    double v;
    if (decimal(f[6], &v)) g->altitude_m = v;
    if (decimal(f[7], &v) && v >= 0 && v < 200.0) g->vitesse_ms = v * NOEUD_MS;
    if (decimal(f[8], &v) && v >= 0 && v <= 360.0) {
        g->cap_deg = v >= 360.0 ? 0.0 : v;
        g->cap_valide = 1;
    }
    return 1;
}

int sa_csq(const char *ligne, int *rssi)
{
    const char *p = apres(ligne, "+CSQ:");
    if (!p) return 0;
    char f[MAX_CHAMPS][TAILLE_CHAMP];
    if (champs(p, f) < 1) return 0;
    const long r = entier(f[0]);
    if (r < 0 || (r > 31 && r != 99)) return 0;
    *rssi = (int)r;
    return 1;
}

int sa_creg(const char *ligne, int *stat)
{
    const char *p = apres(ligne, "+CREG:");
    if (!p) p = apres(ligne, "+CEREG:");
    if (!p) p = apres(ligne, "+CGREG:");
    if (!p) return 0;
    char f[MAX_CHAMPS][TAILLE_CHAMP];
    const int n = champs(p, f);
    /* Reponse a la question : "n,stat[,...]" ; notification : "stat[,...]".
     * Une notification avec position a un 2e champ entre guillemets (non numerique). */
    long s = -1;
    if (n >= 2 && entier(f[1]) >= 0) s = entier(f[1]);
    else if (n >= 1) s = entier(f[0]);
    if (s < 0 || s > 11) return 0;
    *stat = (int)s;
    return 1;
}

int sa_enregistre(int stat)
{
    return stat == 1 || stat == 5;
}

int sa_cops(const char *ligne, char *nom, size_t taille, int *techno)
{
    const char *p = apres(ligne, "+COPS:");
    if (!p) return 0;
    char f[MAX_CHAMPS][TAILLE_CHAMP];
    const int n = champs(p, f);
    if (n < 1 || entier(f[0]) < 0) return 0;
    copier(nom, taille, n >= 3 ? f[2] : "", n >= 3 ? strlen(f[2]) : 0);
    *techno = n >= 4 ? (int)entier(f[3]) : -1;
    return 1;
}

const char *sa_techno(int techno)
{
    switch (techno) {
    case 0: case 1: case 3: return "2G";
    case 2: case 4: case 5: case 6: return "3G";
    case 7: case 9: return "4G";
    default: return "";
    }
}

static int contient_sans_casse(const char *texte, const char *mot)
{
    const size_t n = strlen(mot);
    for (; *texte; ++texte) {
        size_t i = 0;
        while (i < n && texte[i] && toupper((unsigned char)texte[i]) == toupper((unsigned char)mot[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

sim_carte_t sa_cpin(const char *ligne)
{
    const char *p = apres(ligne, "+CPIN:");
    if (p) {
        if (strncmp(p, "READY", 5) == 0) return SIM_CARTE_PRETE;
        if (strncmp(p, "SIM PIN", 7) == 0) return SIM_CARTE_PIN;
        if (strncmp(p, "SIM PUK", 7) == 0) return SIM_CARTE_BLOQUEE;
        if (contient_sans_casse(p, "NOT INSERTED")) return SIM_CARTE_ABSENTE;
        return SIM_CARTE_INCONNUE;
    }
    p = apres(ligne, "+CME ERROR:");
    if (p) {
        /* CMEE=2 : texte ; CMEE=1 : 10 = pas de SIM, 11 = PIN, 12 = PUK, 13 = SIM HS */
        if (contient_sans_casse(p, "not inserted") || strcmp(p, "10") == 0 ||
            contient_sans_casse(p, "SIM failure") || strcmp(p, "13") == 0) {
            return SIM_CARTE_ABSENTE;
        }
        if (contient_sans_casse(p, "PUK") || strcmp(p, "12") == 0) return SIM_CARTE_BLOQUEE;
        if (contient_sans_casse(p, "PIN required") || strcmp(p, "11") == 0) return SIM_CARTE_PIN;
    }
    return SIM_CARTE_INCONNUE;
}

int sa_cmgl(const char *ligne, int *index, char *statut, size_t tstatut, char *numero, size_t tnumero,
            char *horodatage, size_t thoro)
{
    const char *p = apres(ligne, "+CMGL:");
    if (!p) return 0;
    char f[MAX_CHAMPS][TAILLE_CHAMP];
    const int n = champs(p, f);
    const long i = entier(f[0]);
    if (n < 2 || i < 0 || i > 9999) return 0;
    *index = (int)i;
    copier(statut, tstatut, f[1], strlen(f[1]));
    copier(numero, tnumero, n >= 3 ? f[2] : "", n >= 3 ? strlen(f[2]) : 0);
    copier(horodatage, thoro, n >= 5 ? f[4] : "", n >= 5 ? strlen(f[4]) : 0);
    return 1;
}

int sa_cmti(const char *ligne)
{
    return apres(ligne, "+CMTI:") != NULL;
}

/* ------------------------------------------------------------------ */
/* Texte des SMS                                                       */
/* ------------------------------------------------------------------ */

static int val_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static unsigned unite(const char *s)
{
    return (unsigned)(val_hex(s[0]) << 12 | val_hex(s[1]) << 8 | val_hex(s[2]) << 4 | val_hex(s[3]));
}

/* Le texte est-il de l'UCS2 en hexadecimal ? Les messages en clair qui
 * ressemblent a de l'hexadecimal ("CAFE", "1234") donnent des caracteres
 * improbables : on n'accepte que latin, ponctuation et emojis. */
static int est_ucs2(const char *s, size_t n)
{
    if (n < 4 || n % 4 != 0) return 0;
    for (size_t i = 0; i < n; ++i) {
        if (val_hex(s[i]) < 0) return 0;
    }
    for (size_t i = 0; i < n; i += 4) {
        const unsigned u = unite(s + i);
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 4 >= n) return 0;
            const unsigned bas = unite(s + i + 4);
            if (bas < 0xDC00 || bas > 0xDFFF) return 0;
            i += 4;
            continue;
        }
        if (!(u <= 0x024F || (u >= 0x2000 && u <= 0x20CF) || (u >= 0x2190 && u <= 0x27BF))) return 0;
    }
    return 1;
}

/* Ajoute un point de code en UTF-8. Faux si plus de place. */
static int utf8(char *sortie, size_t taille, size_t *pos, unsigned cp)
{
    char o[4];
    size_t n;
    if (cp < 0x80) { o[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) {
        o[0] = (char)(0xE0 | cp >> 12); o[1] = (char)(0x80 | (cp >> 6 & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); n = 3;
    } else {
        o[0] = (char)(0xF0 | cp >> 18); o[1] = (char)(0x80 | (cp >> 12 & 0x3F));
        o[2] = (char)(0x80 | (cp >> 6 & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); n = 4;
    }
    if (*pos + n >= taille) return 0;
    memcpy(sortie + *pos, o, n);
    *pos += n;
    return 1;
}

void sa_decoder_texte(const char *entree, char *sortie, size_t taille)
{
    if (taille == 0) return;
    size_t pos = 0;
    const size_t n = strlen(entree);
    if (est_ucs2(entree, n)) {
        for (size_t i = 0; i < n; i += 4) {
            unsigned cp = unite(entree + i);
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (unite(entree + i + 4) - 0xDC00);
                i += 4;
            }
            if (cp == '\r') continue;
            if (cp < 0x20 && cp != '\n') cp = ' ';
            if (!utf8(sortie, taille, &pos, cp)) break;
        }
    } else {
        for (size_t i = 0; i < n && pos + 1 < taille; ++i) {
            const unsigned char c = (unsigned char)entree[i];
            if (c == '\r') continue;
            sortie[pos++] = (c < 0x20 && c != '\n') ? ' ' : (char)c;
        }
    }
    sortie[pos] = '\0';
}

void sa_vers_ascii(const char *utf8_entree, char *sortie, size_t taille, size_t max)
{
    /* Latin-1 0xC0..0xFF -> lettre sans accent ('?' si aucune) */
    static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    if (taille == 0) return;
    if (max + 1 < taille) taille = max + 1;
    size_t pos = 0;
    const unsigned char *p = (const unsigned char *)utf8_entree;
    while (*p && pos + 1 < taille) {
        const unsigned char c = *p;
        if (c == '\n' || (c >= 0x20 && c < 0x7F)) {
            sortie[pos++] = (char)c;
            p++;
        } else if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
            sortie[pos++] = latin1[p[1] - 0x80];
            p += 2;
        } else if (c == 0xC2 && p[1] == 0xB0) {  /* degre */
            sortie[pos++] = 'o';
            p += 2;
        } else {
            /* Autre caractere : un seul '?' pour toute la sequence UTF-8 */
            if (c >= 0x20) sortie[pos++] = '?';
            p++;
            while ((*p & 0xC0) == 0x80) p++;
        }
    }
    sortie[pos] = '\0';
}

int sa_numero_valide(const char *numero)
{
    const char *p = numero;
    if (*p == '+') p++;
    size_t n = 0;
    for (; *p; ++p, ++n) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return n >= 3 && n <= 20;
}

/* Chiffres seuls, au plus taille - 1 */
static size_t chiffres(const char *s, char *c, size_t taille)
{
    size_t n = 0;
    for (; *s && n + 1 < taille; ++s) {
        if (isdigit((unsigned char)*s)) c[n++] = *s;
    }
    c[n] = '\0';
    return n;
}

int sa_numeros_egaux(const char *a, const char *b)
{
    char ca[32], cb[32];
    const size_t na = chiffres(a, ca, sizeof ca), nb = chiffres(b, cb, sizeof cb);
    if (na < 9 || nb < 9) {
        return na > 0 && na == nb && strcmp(ca, cb) == 0;
    }
    return strcmp(ca + na - 9, cb + nb - 9) == 0;
}

int sa_debut_egal(const char *texte, const char *mot)
{
    while (*texte == ' ' || *texte == '\n' || *texte == '\t') texte++;
    const size_t n = strlen(mot);
    for (size_t i = 0; i < n; ++i) {
        if (toupper((unsigned char)texte[i]) != toupper((unsigned char)mot[i])) return 0;
    }
    return !isalnum((unsigned char)texte[n]);
}
