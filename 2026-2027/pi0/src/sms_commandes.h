/* Commandes recues par SMS (fonctions pures, testees sur PC).
 *
 * Seuls les numeros du .env (SIM_NUMEROS) sont ecoutes. Un SMS qui commence
 * par une commande recoit une reponse ; tout autre texte est affiche a
 * l'ecran du pilote. Pour ajouter une commande : une ligne dans le tableau
 * de sms_commandes.c et une fonction qui ecrit la reponse. */
#ifndef SMS_COMMANDES_H
#define SMS_COMMANDES_H

#include <stddef.h>

/* Ce que le bateau sait au moment de repondre */
typedef struct {
    int gps_fix;
    double lat, lon;
    char heure_utc[9];          /* "hh:mm:ss", vide si inconnue */
    int vitesse_connue;
    double vitesse_ms;
    int cap_connu;
    double cap_deg;
    int esp_present;
    int soc_dixieme[2];         /* batteries 1 et 2, -1 si inconnu */
    int puissance_connue;
    long puissance_mw;
    int signal_rssi;            /* 0..31, -1 si inconnu */
} sms_contexte_t;

typedef enum {
    SMS_AFFICHER = 0,   /* message libre : a afficher a l'ecran */
    SMS_REPONDRE        /* commande reconnue : envoyer "reponse" */
} sms_action_t;

/* Analyse le texte recu. Si SMS_REPONDRE, la reponse (ASCII, 160 caracteres
 * au plus) est ecrite dans reponse. */
sms_action_t sms_commandes_traiter(const char *texte, const sms_contexte_t *ctx, char *reponse, size_t taille);

/* Le numero fait-il partie de la liste ? (comparaison sur les 9 derniers chiffres) */
int sms_numero_autorise(const char *numero, const char *liste, size_t taille_elt, int n);

#endif /* SMS_COMMANDES_H */
