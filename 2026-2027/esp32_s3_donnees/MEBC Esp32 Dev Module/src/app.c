/*
 * Application ESP32 donnees (C pur) - liaison UART avec la Pi.
 * Protocole : 2026-2027/shared/PROTOCOLE_UART.md
 *
 * Pour l'instant :
 *   - HELLO au demarrage, HB + BMS FACTICE chaque seconde
 *   - repond a PING / INFO / CMD (reboot, changement de vitesse, flux de test)
 *   - mode "flux de test" : envoie des trames TEST aussi vite que la
 *     liaison le permet, pour mesurer le debit cote Pi.
 */
#include "app.h"

#include <string.h>

#include <protocole.h>

#define VERSION_FW_MAJEUR 0
#define VERSION_FW_MINEUR 2
#define VERSION_FW_CORRECTIF 0

#if defined(CARTE_WROOM32E)
#define CARTE PROTO_CARTE_WROOM32E
#elif defined(CARTE_S3)
#define CARTE PROTO_CARTE_S3
#else
#define CARTE PROTO_CARTE_INCONNUE
#endif

/* Apres un changement de vitesse, la Pi doit parler sous ce delai, sinon
 * on revient a la vitesse par defaut. */
#define DELAI_CONFIRMATION_VITESSE_MS 3000u
/* A une vitesse non standard, silence de la Pi plus long que ca -> retour
 * a la vitesse par defaut (la Pi a pu redemarrer). */
#define DELAI_SILENCE_PI_MS 10000u
#define DUREE_MAX_FLUX_MS 30000u

static proto_decodeur_t decodeur;
static uint8_t trame[PROTO_MAX_TRAME];
static uint16_t sequence;

static uint32_t vitesse = PROTO_VITESSE_DEFAUT;
static int attente_confirmation;
static uint32_t echeance_confirmation;
static uint32_t derniere_trame_pi_ms;
static uint32_t dernier_envoi_ms;

static struct {
    int actif;
    uint16_t taille;
    uint32_t fin_ms;
    uint32_t compteur;
    uint32_t octets;
} flux;

/* ------------------------------------------------------------------ */
/* Envoi                                                               */
/* ------------------------------------------------------------------ */

/* Le payload est construit directement dans `trame` apres l'en-tete,
 * puis la trame est completee sur place (pas de copie). */
#define PAYLOAD (trame + PROTO_TAILLE_ENTETE)
#define PLACE_PAYLOAD (sizeof trame - PROTO_TAILLE_ENTETE - PROTO_TAILLE_CRC)

static size_t envoyer(uint8_t type, size_t len_payload)
{
    const size_t n = proto_encoder_trame(trame, sizeof trame, type, sequence,
                                         PAYLOAD, (uint16_t)len_payload);
    if (n > 0) {
        sequence++;
        plateforme_ecrire(trame, n);
    }
    return n;
}

static void envoyer_hello(void)
{
    const proto_hello_t m = {PROTO_VERSION, CARTE,
                             {VERSION_FW_MAJEUR, VERSION_FW_MINEUR, VERSION_FW_CORRECTIF}};
    envoyer(PROTO_T_HELLO, proto_encoder_hello(&m, PAYLOAD, PLACE_PAYLOAD));
}

static void envoyer_ack(uint16_t ref, uint8_t erreur)
{
    const proto_ack_t m = {ref, erreur == PROTO_ERR_AUCUNE, erreur};
    envoyer(PROTO_T_ACK, proto_encoder_ack(&m, PAYLOAD, PLACE_PAYLOAD));
}

static void envoyer_hb(uint32_t maintenant)
{
    const proto_hb_t m = {maintenant};
    envoyer(PROTO_T_HB, proto_encoder_hb(&m, PAYLOAD, PLACE_PAYLOAD));
}

/* Donnees FACTICES, en attendant les vrais BMS. */
static void envoyer_bms_factice(uint32_t maintenant)
{
    proto_bms_t m;
    memset(&m, 0, sizeof m);
    m.id = 1;
    m.ok = 1;
    m.tension_cv = 4860;
    m.courant_ca = -1250;
    m.soc_dixieme = 750;
    m.restant_cah = 7350;
    m.nb_cellules = 15;
    for (uint8_t i = 0; i < m.nb_cellules; ++i) {
        m.cellules_mv[i] = (uint16_t)(3200 + 10 * ((maintenant / 1000 + i) % 5));
    }
    m.nb_temps = 2;
    m.temps_dixieme[0] = 250;
    m.temps_dixieme[1] = 261;
    envoyer(PROTO_T_BMS, proto_encoder_bms(&m, PAYLOAD, PLACE_PAYLOAD));
}

static void revenir_vitesse_defaut(const char *raison)
{
    plateforme_log("[UART] %s : retour a %lu bauds\n", raison, (unsigned long)PROTO_VITESSE_DEFAUT);
    plateforme_attendre_fin_emission();
    vitesse = PROTO_VITESSE_DEFAUT;
    attente_confirmation = 0;
    flux.actif = 0;
    plateforme_changer_vitesse(vitesse);
    proto_decodeur_init(&decodeur);
}

/* ------------------------------------------------------------------ */
/* Reception                                                           */
/* ------------------------------------------------------------------ */

static void traiter_commande(const proto_trame_t *t, uint32_t maintenant)
{
    proto_cmd_t cmd;
    if (!proto_decoder_cmd(t->payload, t->len, &cmd)) {
        envoyer_ack(t->seq, PROTO_ERR_ARG_INVALIDE);
        return;
    }

    switch (cmd.commande) {
    case PROTO_CMD_REBOOT:
        envoyer_ack(t->seq, PROTO_ERR_AUCUNE);
        plateforme_attendre_fin_emission();
        plateforme_redemarrer();
        break;

    case PROTO_CMD_SET_LED:
        envoyer_ack(t->seq, PROTO_ERR_NON_DISPONIBLE);  /* pas de voyants sur ce montage */
        break;

    case PROTO_CMD_SET_BAUD:
        if (!proto_vitesse_valide(cmd.arg.baud.vitesse)) {
            envoyer_ack(t->seq, PROTO_ERR_ARG_INVALIDE);
            break;
        }
        /* L'ACK part a l'ancienne vitesse, puis on bascule */
        envoyer_ack(t->seq, PROTO_ERR_AUCUNE);
        plateforme_attendre_fin_emission();
        vitesse = cmd.arg.baud.vitesse;
        plateforme_changer_vitesse(vitesse);
        proto_decodeur_init(&decodeur);
        attente_confirmation = vitesse != PROTO_VITESSE_DEFAUT;
        echeance_confirmation = maintenant + DELAI_CONFIRMATION_VITESSE_MS;
        plateforme_log("[UART] vitesse -> %lu bauds\n", (unsigned long)vitesse);
        break;

    case PROTO_CMD_STREAM:
        if (cmd.arg.stream.taille_payload < 4 || cmd.arg.stream.taille_payload > PROTO_MAX_PAYLOAD ||
            cmd.arg.stream.duree_ms == 0 || cmd.arg.stream.duree_ms > DUREE_MAX_FLUX_MS) {
            envoyer_ack(t->seq, PROTO_ERR_ARG_INVALIDE);
            break;
        }
        envoyer_ack(t->seq, PROTO_ERR_AUCUNE);
        flux.actif = 1;
        flux.taille = cmd.arg.stream.taille_payload;
        flux.fin_ms = maintenant + cmd.arg.stream.duree_ms;
        flux.compteur = 0;
        flux.octets = 0;
        plateforme_log("[UART] flux de test : %u octets/trame pendant %lu ms a %lu bauds\n",
                       (unsigned)flux.taille, (unsigned long)cmd.arg.stream.duree_ms,
                       (unsigned long)vitesse);
        break;

    default:
        envoyer_ack(t->seq, PROTO_ERR_CMD_INCONNUE);
        break;
    }
}

static void traiter(const proto_trame_t *t, uint32_t maintenant)
{
    derniere_trame_pi_ms = maintenant;
    attente_confirmation = 0;  /* la Pi nous parle a la bonne vitesse */

    switch (t->type) {
    case PROTO_T_PING:
        envoyer_ack(t->seq, PROTO_ERR_AUCUNE);
        break;
    case PROTO_T_INFO:
        envoyer_hello();
        envoyer_ack(t->seq, PROTO_ERR_AUCUNE);
        break;
    case PROTO_T_CMD:
        traiter_commande(t, maintenant);
        break;
    default:
        break;  /* type inconnu : ignore (compatibilite) */
    }
}

void app_octets_recus(const uint8_t *data, size_t len)
{
    const uint32_t maintenant = plateforme_millis();
    size_t pos = 0;
    while (pos < len) {
        pos += proto_decodeur_ajouter(&decodeur, data + pos, len - pos);
        proto_trame_t t;
        while (proto_decodeur_suivante(&decodeur, &t)) {
            traiter(&t, maintenant);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Boucle                                                              */
/* ------------------------------------------------------------------ */

static void continuer_flux(uint32_t maintenant)
{
    const size_t taille_trame = PROTO_TAILLE_ENTETE + flux.taille + PROTO_TAILLE_CRC;

    /* On remplit le tampon d'emission sans jamais bloquer */
    while ((int32_t)(flux.fin_ms - plateforme_millis()) > 0 && plateforme_place_tx() >= taille_trame) {
        proto_encoder_test(flux.compteur, PAYLOAD, flux.taille);
        flux.octets += (uint32_t)envoyer(PROTO_T_TEST, flux.taille);
        flux.compteur++;
    }

    if ((int32_t)(flux.fin_ms - maintenant) <= 0) {
        const proto_stream_fin_t fin = {flux.compteur, flux.octets};
        envoyer(PROTO_T_STREAM_FIN, proto_encoder_stream_fin(&fin, PAYLOAD, PLACE_PAYLOAD));
        flux.actif = 0;
        derniere_trame_pi_ms = maintenant;  /* la Pi se taisait pendant le flux */
        plateforme_log("[UART] flux termine : %lu trames, %lu octets\n",
                       (unsigned long)flux.compteur, (unsigned long)flux.octets);
    }
}

void app_init(void)
{
    proto_decodeur_init(&decodeur);
    plateforme_log("\n[BOOT] ESP32 donnees fw %d.%d.%d - protocole v%d - %lu bauds\n",
                   VERSION_FW_MAJEUR, VERSION_FW_MINEUR, VERSION_FW_CORRECTIF, PROTO_VERSION,
                   (unsigned long)vitesse);
    envoyer_hello();
}

void app_tick(void)
{
    const uint32_t maintenant = plateforme_millis();

    if (flux.actif) {
        continuer_flux(maintenant);
        return;
    }

    /* Securites de vitesse : ne jamais rester "perdu" a une vitesse que la Pi n'utilise pas */
    if (attente_confirmation && (int32_t)(maintenant - echeance_confirmation) > 0) {
        revenir_vitesse_defaut("pas de confirmation de la Pi");
    } else if (vitesse != PROTO_VITESSE_DEFAUT && maintenant - derniere_trame_pi_ms > DELAI_SILENCE_PI_MS) {
        revenir_vitesse_defaut("Pi silencieuse");
    }

    if (maintenant - dernier_envoi_ms >= 1000u) {
        dernier_envoi_ms = maintenant;
        envoyer_hb(maintenant);
        envoyer_bms_factice(maintenant);
        plateforme_log("[UART] %lu bauds | recu de la Pi : %lu ok, %lu erreurs CRC\n",
                       (unsigned long)vitesse, (unsigned long)decodeur.trames_ok,
                       (unsigned long)decodeur.erreurs_crc);
    }
}
