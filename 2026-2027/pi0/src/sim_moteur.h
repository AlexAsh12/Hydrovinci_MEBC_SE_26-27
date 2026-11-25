/* Dialogue AT avec le module SIM7600 (sans entree/sortie : teste sur PC).
 *
 * Le moteur recoit les octets du port AT (sim_moteur_recevoir) et ecrit ses
 * commandes par le rappel "ecrire". Une seule commande a la fois, jamais de
 * blocage : tout avance a chaque appel de sim_moteur_tick().
 *
 * Ce qu'il fait tout seul, une fois le port ouvert :
 *   - configure le module (echo coupe, erreurs en clair, GPS allume)
 *   - surveille la carte SIM (code PIN du .env si demande, une seule fois)
 *   - demande la position GPS chaque seconde (AT+CGPSINFO)
 *   - releve les SMS recus (notification +CMTI et toutes les 15 s), puis
 *     les efface de la carte SIM
 *   - envoie les SMS en file d'attente quand le reseau est la
 *   - releve signal, reseau et operateur
 * Le module peut etre debranche, sans carte SIM, sans reseau ou sans GPS :
 * chaque fonction continue avec ce qui marche. */
#ifndef SIM_MOTEUR_H
#define SIM_MOTEUR_H

#include <stddef.h>
#include <stdint.h>

#include "sim_analyse.h"

#define SIM_TAILLE_NUMERO 24
#define SIM_TAILLE_TEXTE_ENVOI 161   /* 160 caracteres + fin */
#define SIM_TAILLE_TEXTE_RECU 400
#define SIM_FILE_ENVOI 8
#define SIM_MAX_RECUS 8
#define SIM_MAX_SUPPRESSIONS 16

typedef struct {
    void *ctx;
    /* Ecrit sur le port AT. 0 si OK. */
    int (*ecrire)(void *ctx, const char *data, size_t len);
    /* Reponse GPS : fix = 1 avec g valide, fix = 0 sans position (g = NULL) */
    void (*sur_gps)(void *ctx, const sim_gps_t *g, int fix);
    /* SMS recu (texte UTF-8, horodatage du reseau "aa/mm/jj,hh:mm:ss+zz") */
    void (*sur_sms)(void *ctx, const char *numero, const char *texte, const char *horodatage);
    /* Evenement a journaliser (changement d'etat) */
    void (*journal)(void *ctx, const char *texte);
} sim_rappels_t;

typedef enum { SIM_MODEM_ABSENT = 0, SIM_MODEM_INIT, SIM_MODEM_PRET } sim_modem_t;

typedef struct {
    sim_rappels_t r;

    /* Etat lisible par le reste du programme */
    sim_modem_t modem;
    sim_carte_t carte;
    int rssi;                 /* 0..31, -1 inconnu */
    int reseau;               /* enregistre sur le reseau mobile */
    char operateur[32];
    int techno;               /* AT+COPS, -1 inconnue */
    int gps_actif;            /* le module repond a AT+CGPSINFO */
    int gps_fix;
    uint32_t sms_envoyes, sms_echecs, sms_recus;

    /* Interne */
    char pin[12];
    int pin_essaye;           /* depuis le dernier (re)branchement */
    int pin_refuse;           /* code faux : plus jamais essaye (garde au rebranchement) */
    int etape_init, etape_sms, sms_pret;
    int stat_creg, stat_cereg;

    char ligne[SIM_TAILLE_TEXTE_RECU + 64];
    size_t ligne_len;
    int ligne_trop_longue;
    int lf_vides;             /* lignes vides depuis la derniere ligne utile */

    int cmd;                  /* commande en cours, 0 = aucune */
    char cmd_texte[64];
    uint64_t cmd_debut, cmd_delai;
    int cmd_invite;           /* AT+CMGS : attend l'invite "> " */
    int cmd_info;             /* une reponse utile a ete vue */
    int sans_reponse;         /* commandes sans reponse d'affilee */

    uint64_t proch_gps, proch_gps_on, proch_cmgl, proch_csq, proch_creg, proch_cops, proch_cpin,
        proch_init_sms;
    uint64_t dernier_fix;
    int creg_suivant;         /* alterne CREG / CEREG */

    struct {
        int index;
        int a_lire;           /* REC READ / REC UNREAD */
        char numero[SIM_TAILLE_NUMERO];
        char horodatage[32];
        char texte[SIM_TAILLE_TEXTE_RECU];
    } recus[SIM_MAX_RECUS];
    int nb_recus;
    int recu_en_cours;        /* -1 : la ligne suivante n'est pas un corps de SMS */
    int a_supprimer[SIM_MAX_SUPPRESSIONS];
    int nb_a_supprimer;

    struct {
        char numero[SIM_TAILLE_NUMERO];
        char texte[SIM_TAILLE_TEXTE_ENVOI];
        uint64_t cree;
        uint64_t pas_avant;
        int essais;
    } file[SIM_FILE_ENVOI];
    int nb_file;
} sim_moteur_t;

void sim_moteur_init(sim_moteur_t *m, const sim_rappels_t *r);
/* Code PIN de la carte (facultatif, vide = aucun) */
void sim_moteur_definir_pin(sim_moteur_t *m, const char *pin);

/* Le port AT vient d'etre ouvert / a disparu */
void sim_moteur_ouvert(sim_moteur_t *m, uint64_t t);
void sim_moteur_ferme(sim_moteur_t *m);

void sim_moteur_recevoir(sim_moteur_t *m, const char *data, size_t n, uint64_t t);
void sim_moteur_tick(sim_moteur_t *m, uint64_t t);

/* Vrai si le module ne repond plus (le port doit etre ferme et recherche) */
int sim_moteur_muet(const sim_moteur_t *m);

/* Met un SMS en file d'attente (texte UTF-8, converti en ASCII et coupe a
 * 160 caracteres). Il part des que carte, reseau et module sont prets, et
 * est abandonne apres 15 minutes. 0 si accepte, -1 sinon (numero invalide,
 * file pleine). */
int sim_moteur_envoyer_sms(sim_moteur_t *m, const char *numero, const char *texte, uint64_t t);

/* "...5678" : numero masque pour le journal et l'ecran */
void sim_masquer_numero(const char *numero, char *sortie, size_t taille);

#endif /* SIM_MOTEUR_H */
