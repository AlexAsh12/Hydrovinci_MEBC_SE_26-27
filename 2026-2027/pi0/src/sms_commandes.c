/* Commandes recues par SMS. */
#include "sms_commandes.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sim_analyse.h"

#define NOEUDS_PAR_MS 1.943844
#define TAILLE_SMS 160

typedef void (*commande_f)(const sms_contexte_t *ctx, char *r, size_t taille);

static void position(const sms_contexte_t *c, char *r, size_t taille);
static void vitesse(const sms_contexte_t *c, char *r, size_t taille);
static void etat(const sms_contexte_t *c, char *r, size_t taille);
static void aide(const sms_contexte_t *c, char *r, size_t taille);

/* Premier mot du SMS (majuscules ou minuscules) -> commande */
static const struct {
    const char *mot;
    commande_f f;
} commandes[] = {
    {"POSITION", position},
    {"POS", position},
    {"VITESSE", vitesse},
    {"ETAT", etat},
    {"AIDE", aide},
};
#define NB_COMMANDES (sizeof commandes / sizeof commandes[0])

/* Ajout borne a la reponse */
#ifdef __GNUC__
__attribute__((format(printf, 3, 4)))
#endif
static void ajouter(char *r, size_t taille, const char *format, ...)
{
    const size_t pos = strlen(r);
    if (pos + 1 >= taille) return;
    va_list a;
    va_start(a, format);
    vsnprintf(r + pos, taille - pos, format, a);
    va_end(a);
}

static void ligne_vitesse(const sms_contexte_t *c, char *r, size_t taille)
{
    if (c->vitesse_connue) {
        ajouter(r, taille, "Vit %.1f kn (%.1f km/h)", c->vitesse_ms * NOEUDS_PAR_MS, c->vitesse_ms * 3.6);
    } else {
        ajouter(r, taille, "Vit inconnue");
    }
    if (c->cap_connu) ajouter(r, taille, " cap %03.0f", c->cap_deg);
    ajouter(r, taille, "\n");
}

static void position(const sms_contexte_t *c, char *r, size_t taille)
{
    if (!c->gps_fix) {
        ajouter(r, taille, "Hydrovinci : pas de position GPS\n");
        ligne_vitesse(c, r, taille);
        return;
    }
    ajouter(r, taille, "Hydrovinci %s UTC\n", c->heure_utc[0] ? c->heure_utc : "--:--:--");
    ajouter(r, taille, "%.5f,%.5f\n", c->lat, c->lon);
    ligne_vitesse(c, r, taille);
    ajouter(r, taille, "https://maps.google.com/?q=%.5f,%.5f", c->lat, c->lon);
}

static void vitesse(const sms_contexte_t *c, char *r, size_t taille)
{
    ajouter(r, taille, "Hydrovinci\n");
    ligne_vitesse(c, r, taille);
}

static void etat(const sms_contexte_t *c, char *r, size_t taille)
{
    ajouter(r, taille, "Hydrovinci\n");
    for (int i = 0; i < 2; ++i) {
        if (c->soc_dixieme[i] >= 0) ajouter(r, taille, "Batt%d %.0f%% ", i + 1, c->soc_dixieme[i] / 10.0);
        else ajouter(r, taille, "Batt%d ? ", i + 1);
    }
    ajouter(r, taille, "\n");
    if (c->puissance_connue) ajouter(r, taille, "Puiss %.0f W\n", c->puissance_mw / 1000.0);
    ajouter(r, taille, "ESP %s, GPS %s", c->esp_present ? "ok" : "perdu", c->gps_fix ? "ok" : "sans pos.");
    if (c->signal_rssi >= 0 && c->signal_rssi <= 31) ajouter(r, taille, ", reseau %d/31", c->signal_rssi);
    ajouter(r, taille, "\n");
    ligne_vitesse(c, r, taille);
}

static void aide(const sms_contexte_t *c, char *r, size_t taille)
{
    (void)c;
    ajouter(r, taille, "Commandes : POSITION, VITESSE, ETAT, AIDE.\nTout autre texte s'affiche a l'ecran du pilote.");
}

sms_action_t sms_commandes_traiter(const char *texte, const sms_contexte_t *ctx, char *reponse, size_t taille)
{
    for (size_t i = 0; i < NB_COMMANDES; ++i) {
        if (sa_debut_egal(texte, commandes[i].mot)) {
            char brut[512] = "";
            commandes[i].f(ctx, brut, sizeof brut);
            /* Supprime le saut de ligne final et tient dans un SMS */
            size_t n = strlen(brut);
            while (n > 0 && brut[n - 1] == '\n') brut[--n] = '\0';
            sa_vers_ascii(brut, reponse, taille, TAILLE_SMS);
            return SMS_REPONDRE;
        }
    }
    if (taille) reponse[0] = '\0';
    return SMS_AFFICHER;
}

int sms_numero_autorise(const char *numero, const char *liste, size_t taille_elt, int n)
{
    if (!numero[0]) return 0;
    for (int i = 0; i < n; ++i) {
        const char *autorise = liste + (size_t)i * taille_elt;
        if (autorise[0] && sa_numeros_egaux(numero, autorise)) return 1;
    }
    return 0;
}
