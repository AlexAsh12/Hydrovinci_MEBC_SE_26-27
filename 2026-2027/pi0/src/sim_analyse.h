/* Analyse des reponses AT du module SIM7600 (fonctions pures, testees sur PC).
 * Aucune entree/sortie ici : le dialogue est dans sim_moteur.c. */
#ifndef SIM_ANALYSE_H
#define SIM_ANALYSE_H

#include <stddef.h>

/* Position donnee par AT+CGPSINFO */
typedef struct {
    double lat, lon;        /* degres, + = nord / est */
    double altitude_m;
    double vitesse_ms;      /* vitesse sur le fond */
    double cap_deg;         /* route sur le fond, 0 a 360 */
    int cap_valide;         /* le module ne donne pas de route a l'arret */
    char date[7];           /* "jjmmaa" UTC */
    char heure[9];          /* "hh:mm:ss" UTC */
} sim_gps_t;

typedef enum {
    SIM_CARTE_INCONNUE = 0,
    SIM_CARTE_PRETE,
    SIM_CARTE_PIN,          /* code PIN demande */
    SIM_CARTE_BLOQUEE,      /* code PUK demande */
    SIM_CARTE_ABSENTE
} sim_carte_t;

/* "+CGPSINFO: 4717.112671,N,00833.914843,E,261126,143205.0,123.4,5.4,90.0"
 * Renvoie 1 si position valide, 0 si pas de position (champs vides),
 * -1 si la ligne n'est pas une reponse CGPSINFO ou est mal formee. */
int sa_cgpsinfo(const char *ligne, sim_gps_t *g);

/* "+CSQ: 18,99" -> rssi 0..31, 99 = inconnu. Renvoie 1 si reconnu. */
int sa_csq(const char *ligne, int *rssi);

/* "+CREG: 0,1", "+CEREG: 0,5", "+CGREG: 2,1,..." -> stat. Renvoie 1 si reconnu. */
int sa_creg(const char *ligne, int *stat);
/* stat 1 (reseau de l'operateur) ou 5 (itinerance) = enregistre */
int sa_enregistre(int stat);

/* "+COPS: 0,0,\"Orange F\",7" -> nom (vide si absent) et techno (-1 si absente).
 * Renvoie 1 si reconnu. */
int sa_cops(const char *ligne, char *nom, size_t taille, int *techno);
/* Techno AT+COPS -> "2G", "3G", "4G" ou "" */
const char *sa_techno(int techno);

/* "+CPIN: READY" / "+CPIN: SIM PIN" / "+CME ERROR: SIM not inserted"...
 * Renvoie l'etat reconnu, ou SIM_CARTE_INCONNUE si la ligne ne dit rien. */
sim_carte_t sa_cpin(const char *ligne);

/* En-tete d'un SMS liste par AT+CMGL (mode texte) :
 * +CMGL: 3,"REC UNREAD","+33612345678",,"26/11/26,14:32:05+04"
 * Renvoie 1 si reconnu. statut, numero et horodatage peuvent etre vides. */
int sa_cmgl(const char *ligne, int *index, char *statut, size_t tstatut, char *numero, size_t tnumero,
            char *horodatage, size_t thoro);

/* "+CMTI: \"SM\",4" (nouveau SMS) -> 1 si reconnu */
int sa_cmti(const char *ligne);

/* Corps d'un SMS : decode le texte UCS2 (hexadecimal) que certains
 * operateurs envoient, sinon recopie. Sortie UTF-8, caracteres de controle
 * retires (sauf saut de ligne). */
void sa_decoder_texte(const char *entree, char *sortie, size_t taille);

/* Texte UTF-8 -> ASCII imprimable pour l'envoi (e accentue -> e...).
 * Tronque a max caracteres (160 pour un SMS). */
void sa_vers_ascii(const char *utf8, char *sortie, size_t taille, size_t max);

/* Numero acceptable dans une commande AT : '+' facultatif puis 3 a 20 chiffres */
int sa_numero_valide(const char *numero);

/* Meme numero ? Compare les 9 derniers chiffres : "+33612345678" == "0612345678" */
int sa_numeros_egaux(const char *a, const char *b);

/* Comparaison sans casse (ASCII) des n premiers caracteres */
int sa_debut_egal(const char *texte, const char *mot);

#endif /* SIM_ANALYSE_H */
