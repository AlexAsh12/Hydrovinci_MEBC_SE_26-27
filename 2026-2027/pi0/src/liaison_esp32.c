/*
 * Liaison UART Pi <-> ESP32 (protocole binaire v2).
 * Protocole : 2026-2027/shared/PROTOCOLE_UART.md
 *
 * Deux modes :
 *   ./liaison_esp32            moniteur : affiche tout ce que l'ESP envoie,
 *                              ping toutes les 2 s, statistiques toutes les 10 s
 *   ./liaison_esp32 --banc     banc de debit : pour chaque vitesse UART, fait
 *                              envoyer a l'ESP un flux continu de trames TEST,
 *                              verifie chaque octet et mesure le debit
 *
 * Options : --port /dev/serial0  --vitesse N  --taille N  --duree MS
 *           --max N (vitesse max du banc)  --brut  --reboot-esp
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "protocole.h"
#include "uart.h"

/* ------------------------------------------------------------------ */
/* Etat global                                                         */
/* ------------------------------------------------------------------ */

static int fd = -1;
static proto_decodeur_t dec;
static uint16_t seq_tx;
static int option_brut;
static volatile sig_atomic_t arret;

/* Octets lus mais pas encore donnes au decodeur */
static uint8_t rx[4096];
static size_t rx_pos, rx_n;

static uint64_t maintenant_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static double maintenant_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static const char *heure(void)
{
    static char txt[16];
    time_t t = time(NULL);
    strftime(txt, sizeof txt, "%H:%M:%S", localtime(&t));
    return txt;
}

static void sur_signal(int sig)
{
    (void)sig;
    arret = 1;
}

/* ------------------------------------------------------------------ */
/* Emission / reception                                                */
/* ------------------------------------------------------------------ */

static int envoyer(uint8_t type, const uint8_t *payload, size_t len, uint16_t *seq)
{
    uint8_t t[PROTO_MAX_TRAME];
    const size_t n = proto_encoder_trame(t, sizeof t, type, seq_tx, payload, (uint16_t)len);
    if (n == 0) {
        return -1;
    }
    if (seq) {
        *seq = seq_tx;
    }
    seq_tx++;
    return uart_ecrire(fd, t, n);
}

static int envoyer_cmd(const proto_cmd_t *c, uint16_t *seq)
{
    uint8_t p[16];
    return envoyer(PROTO_T_CMD, p, proto_encoder_cmd(c, p, sizeof p), seq);
}

static void reinitialiser_reception(void)
{
    uart_vider_reception(fd);
    proto_decodeur_init(&dec);
    rx_pos = rx_n = 0;
}

static void afficher_brut(const uint8_t *data, size_t n)
{
    printf("[%s] brut (%zu octets) :", heure(), n);
    for (size_t i = 0; i < n && i < 48; ++i) {
        printf(" %02X", data[i]);
    }
    printf(n > 48 ? " ...\n" : "\n");
}

/* Prochaine trame valide deja recue (sans attendre). */
static int prochaine_trame(proto_trame_t *t)
{
    for (;;) {
        if (proto_decodeur_suivante(&dec, t)) {
            return 1;
        }
        if (rx_pos >= rx_n) {
            return 0;
        }
        const size_t n = proto_decodeur_ajouter(&dec, rx + rx_pos, rx_n - rx_pos);
        if (n == 0) {
            proto_decodeur_init(&dec);  /* securite, ne devrait jamais arriver */
        }
        rx_pos += n;
    }
}

/* Lit l'UART pendant au plus `timeout_ms` et passe chaque trame valide a
 * `rappel`. S'arrete des que `rappel` renvoie une valeur non nulle (qui est
 * alors renvoyee). Renvoie 0 au bout du delai, -1 si erreur de lecture. */
typedef int (*rappel_t)(const proto_trame_t *t, void *ctx);

static int pomper(int timeout_ms, rappel_t rappel, void *ctx)
{
    const uint64_t fin = maintenant_ms() + (uint64_t)timeout_ms;
    for (;;) {
        proto_trame_t t;
        while (prochaine_trame(&t)) {
            const int r = rappel ? rappel(&t, ctx) : 0;
            if (r) {
                return r;
            }
        }
        const uint64_t mtn = maintenant_ms();
        if (mtn >= fin || arret) {
            return 0;
        }
        const uint64_t reste = fin - mtn;
        const ssize_t n = uart_lire(fd, rx, sizeof rx, reste > 50 ? 50 : (int)reste);
        if (n < 0) {
            perror("lecture du port serie");
            return -1;
        }
        rx_pos = 0;
        rx_n = (size_t)n;
        if (option_brut && n > 0) {
            afficher_brut(rx, (size_t)n);
        }
    }
}

/* --- Attente d'un ACK precis (et memorisation du HELLO au passage) --- */

typedef struct {
    uint16_t ref;
    proto_ack_t ack;
    int hello_recu;
    proto_hello_t hello;
} attente_t;

static int rappel_attente(const proto_trame_t *t, void *ctx)
{
    attente_t *a = (attente_t *)ctx;
    if (t->type == PROTO_T_HELLO && proto_decoder_hello(t->payload, t->len, &a->hello)) {
        a->hello_recu = 1;
    }
    if (t->type == PROTO_T_ACK && proto_decoder_ack(t->payload, t->len, &a->ack) && a->ack.ref == a->ref) {
        return 1;
    }
    return 0;
}

/* Envoie un message et attend son ACK. Renvoie 1 si ACK recu (dans *ack). */
static int requete(uint8_t type, const proto_cmd_t *cmd, int timeout_ms, attente_t *a)
{
    memset(a, 0, sizeof *a);
    const int r = cmd ? envoyer_cmd(cmd, &a->ref) : envoyer(type, NULL, 0, &a->ref);
    if (r != 0) {
        return 0;
    }
    return pomper(timeout_ms, rappel_attente, a) == 1;
}

static int ping(int timeout_ms)
{
    attente_t a;
    return requete(PROTO_T_PING, NULL, timeout_ms, &a) && a.ack.ok;
}

/* ------------------------------------------------------------------ */
/* Changement de vitesse synchronise                                   */
/* ------------------------------------------------------------------ */

static uint32_t vitesse_actuelle = PROTO_VITESSE_DEFAUT;

/* Renvoie 0 si OK, sinon un code d'erreur (ESP et Pi sont alors remis
 * a la vitesse par defaut si possible). */
static int basculer(uint32_t vitesse)
{
    if (vitesse == vitesse_actuelle) {
        return ping(500) ? 0 : -1;
    }
    proto_cmd_t c;
    memset(&c, 0, sizeof c);
    c.commande = PROTO_CMD_SET_BAUD;
    c.arg.baud.vitesse = vitesse;
    attente_t a;
    if (!requete(PROTO_T_CMD, &c, 1000, &a) || !a.ack.ok) {
        return -1;  /* l'ESP refuse ou ne repond pas : on reste a la vitesse actuelle */
    }
    /* L'ESP bascule juste apres avoir envoye l'ACK */
    uart_attendre_emission(fd);
    usleep(20000);
    if (uart_changer_vitesse(fd, vitesse) != 0) {
        fprintf(stderr, "La Pi ne gere pas %u bauds\n", (unsigned)vitesse);
        return -2;
    }
    vitesse_actuelle = vitesse;
    reinitialiser_reception();
    for (int essai = 0; essai < 3; ++essai) {
        if (ping(500)) {
            return 0;
        }
    }
    return -3;
}

/* Remet tout le monde a la vitesse par defaut apres un echec : l'ESP y
 * revient seul au bout de 3 s sans trame valide. */
static int recuperer(void)
{
    uart_changer_vitesse(fd, PROTO_VITESSE_DEFAUT);
    vitesse_actuelle = PROTO_VITESSE_DEFAUT;
    reinitialiser_reception();
    pomper(3500, NULL, NULL);
    for (int essai = 0; essai < 3; ++essai) {
        if (ping(500)) {
            return 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Banc de debit                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t vitesse;
    int synchro_ok;
    int fin_recue;
    proto_stream_fin_t fin;
    uint32_t recues;
    uint32_t perdues;
    uint32_t hors_ordre;
    uint32_t erreurs_contenu;
    uint32_t erreurs_crc;
    uint64_t octets;
    int premiere;
    uint32_t dernier_compteur;
    double t_premiere;
    double t_derniere;
} mesure_t;

static int rappel_flux(const proto_trame_t *t, void *ctx)
{
    mesure_t *m = (mesure_t *)ctx;
    if (t->type == PROTO_T_TEST) {
        const double mtn = maintenant_s();
        uint32_t cpt;
        if (!proto_verifier_test(t->payload, t->len, &cpt)) {
            m->erreurs_contenu++;
        }
        if (m->premiere) {
            m->premiere = 0;
            m->perdues += cpt;  /* trames 0..cpt-1 jamais vues */
            m->t_premiere = mtn;
        } else if (cpt > m->dernier_compteur) {
            m->perdues += cpt - m->dernier_compteur - 1;
        } else {
            m->hors_ordre++;
        }
        m->dernier_compteur = cpt;
        m->recues++;
        m->octets += (uint64_t)t->len + PROTO_TAILLE_ENTETE + PROTO_TAILLE_CRC;
        m->t_derniere = mtn;
        return 0;
    }
    if (t->type == PROTO_T_STREAM_FIN && proto_decoder_stream_fin(t->payload, t->len, &m->fin)) {
        m->fin_recue = 1;
        return 1;
    }
    return 0;
}

static void mesurer_flux(mesure_t *m, uint16_t taille, uint32_t duree_ms)
{
    proto_cmd_t c;
    memset(&c, 0, sizeof c);
    c.commande = PROTO_CMD_STREAM;
    c.arg.stream.taille_payload = taille;
    c.arg.stream.duree_ms = duree_ms;
    attente_t a;
    if (!requete(PROTO_T_CMD, &c, 1000, &a) || !a.ack.ok) {
        return;
    }
    const uint32_t crc_avant = dec.erreurs_crc;
    m->premiere = 1;
    pomper((int)duree_ms + 3000, rappel_flux, m);
    m->erreurs_crc = dec.erreurs_crc - crc_avant;
    if (m->fin_recue && m->fin.trames > m->recues + m->perdues) {
        m->perdues = m->fin.trames - m->recues;  /* trames perdues a la fin du flux */
    }
}

static int mesure_parfaite(const mesure_t *m)
{
    return m->synchro_ok && m->fin_recue && m->recues == m->fin.trames && m->recues > 0 &&
           m->perdues == 0 && m->erreurs_contenu == 0 && m->erreurs_crc == 0 && m->hors_ordre == 0;
}

static int banc(uint16_t taille, uint32_t duree_ms, uint32_t vitesse_max)
{
    attente_t a;
    printf("[%s] Recherche de l'ESP32 a %u bauds...\n", heure(), (unsigned)PROTO_VITESSE_DEFAUT);
    if (!requete(PROTO_T_INFO, NULL, 2000, &a)) {
        printf("Aucune reponse de l'ESP32. Verifier le cablage (GPIO17 -> broche 10, "
               "GPIO16 -> broche 8, GND) et que le firmware est flashe.\n");
        return 1;
    }
    if (a.hello_recu) {
        printf("[%s] ESP32 trouve : carte %u, firmware %u.%u.%u, protocole v%u\n", heure(),
               a.hello.carte, a.hello.fw[0], a.hello.fw[1], a.hello.fw[2], a.hello.version);
        if (a.hello.version != PROTO_VERSION) {
            printf("ATTENTION : protocole ESP v%u, Pi v%u\n", a.hello.version, PROTO_VERSION);
        }
    }
    printf("[%s] Banc : trames de %u octets utiles (%u sur le fil), %u ms par vitesse\n\n",
           heure(), (unsigned)taille, (unsigned)(taille + PROTO_TAILLE_ENTETE + PROTO_TAILLE_CRC),
           (unsigned)duree_ms);

    mesure_t res[16];
    int nb = 0;
    for (int i = 0; PROTO_VITESSES[i] != 0 && nb < 16 && !arret; ++i) {
        const uint32_t v = PROTO_VITESSES[i];
        if (vitesse_max && v > vitesse_max) {
            break;
        }
        mesure_t *m = &res[nb++];
        memset(m, 0, sizeof *m);
        m->vitesse = v;
        printf("[%s] %7u bauds : ", heure(), (unsigned)v);
        fflush(stdout);
        const int b = basculer(v);
        if (b != 0) {
            printf("echec de synchronisation (code %d)\n", b);
            if (b != -1 && recuperer() != 0) {
                printf("ESP32 injoignable apres l'echec, arret du banc.\n");
                break;
            }
            continue;
        }
        m->synchro_ok = 1;
        mesurer_flux(m, taille, duree_ms);
        const double duree = m->t_derniere - m->t_premiere;
        const double debit = duree > 0 ? (double)m->octets / duree / 1000.0 : 0.0;
        printf("%s  %u/%u trames, %.1f ko/s\n", mesure_parfaite(m) ? "OK    " : "ERREUR",
               (unsigned)m->recues, (unsigned)m->fin.trames, debit);
    }

    /* Retour de l'ESP a la vitesse par defaut */
    if (vitesse_actuelle != PROTO_VITESSE_DEFAUT && basculer(PROTO_VITESSE_DEFAUT) != 0) {
        recuperer();
    }

    printf("\n%-9s | %8s | %8s | %7s | %7s | %8s | %10s | %10s\n", "Vitesse", "Envoyees", "Recues",
           "Perdues", "Err CRC", "Err data", "Debit ko/s", "Efficacite");
    printf("----------+----------+----------+---------+---------+----------+------------+-----------\n");
    uint32_t meilleure = 0, avant_meilleure = 0;
    for (int i = 0; i < nb; ++i) {
        const mesure_t *m = &res[i];
        if (!m->synchro_ok) {
            printf("%9u | %s\n", (unsigned)m->vitesse, "synchronisation impossible");
            continue;
        }
        const double duree = m->t_derniere - m->t_premiere;
        const double debit = duree > 0 ? (double)m->octets / duree : 0.0;
        const double max_theorique = m->vitesse / 10.0;  /* 8N1 : 10 bits par octet */
        printf("%9u | %8u | %8u | %7u | %7u | %8u | %10.1f | %9.0f %%%s\n", (unsigned)m->vitesse,
               (unsigned)m->fin.trames, (unsigned)m->recues, (unsigned)m->perdues,
               (unsigned)m->erreurs_crc, (unsigned)m->erreurs_contenu, debit / 1000.0,
               100.0 * debit / max_theorique, mesure_parfaite(m) ? "" : "  <- ERREURS");
        if (mesure_parfaite(m)) {
            avant_meilleure = meilleure;
            meilleure = m->vitesse;
        }
    }
    printf("\n");
    if (meilleure == 0) {
        printf("Aucune vitesse sans erreur.\n");
        return 1;
    }
    printf("Vitesse maximale sans aucune erreur : %u bauds\n", (unsigned)meilleure);
    printf("Vitesse conseillee (avec marge)     : %u bauds\n",
           (unsigned)(avant_meilleure ? avant_meilleure : meilleure));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Moniteur                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t derniere_trame;
    int liaison_ok;
    int seq_connue;
    uint16_t derniere_seq;
    uint32_t recues;
    uint32_t perdues;
    struct { uint16_t seq; uint64_t envoi; int actif; } pings[8];
    double rtt_total;
    double rtt_max;
    uint32_t rtt_nb;
} moniteur_t;

static const char *nom_carte(uint8_t c)
{
    switch (c) {
    case PROTO_CARTE_WROOM32E: return "esp32-wroom32e";
    case PROTO_CARTE_S3: return "esp32-s3";
    default: return "inconnue";
    }
}

static void afficher_bms(const proto_bms_t *b)
{
    if (!b->ok) {
        printf("bms%u  BMS muet\n", b->id);
        return;
    }
    uint16_t vmin = 0xFFFF, vmax = 0;
    for (uint8_t i = 0; i < b->nb_cellules; ++i) {
        if (b->cellules_mv[i] < vmin) vmin = b->cellules_mv[i];
        if (b->cellules_mv[i] > vmax) vmax = b->cellules_mv[i];
    }
    printf("bms%u  %.2f V  %.2f A  SOC %.1f %%  %u cellules", b->id, b->tension_cv / 100.0,
           b->courant_ca / 100.0, b->soc_dixieme / 10.0, b->nb_cellules);
    if (b->nb_cellules) {
        printf(" (%.3f a %.3f V)", vmin / 1000.0, vmax / 1000.0);
    }
    for (uint8_t i = 0; i < b->nb_temps; ++i) {
        printf("%s%.1f", i ? "/" : "  T ", b->temps_dixieme[i] / 10.0);
    }
    printf(b->nb_temps ? " degC\n" : "\n");
}

static int rappel_moniteur(const proto_trame_t *t, void *ctx)
{
    moniteur_t *m = (moniteur_t *)ctx;
    const uint64_t mtn = maintenant_ms();
    m->derniere_trame = mtn;
    if (!m->liaison_ok) {
        m->liaison_ok = 1;
        printf("[%s] *** LIAISON ESP32 OK ***\n", heure());
    }
    if (m->seq_connue && t->seq != (uint16_t)(m->derniere_seq + 1)) {
        if (t->seq == 0) {
            printf("[%s] (l'ESP32 a redemarre)\n", heure());
        } else {
            m->perdues += (uint16_t)(t->seq - m->derniere_seq - 1);
        }
    }
    m->seq_connue = 1;
    m->derniere_seq = t->seq;
    m->recues++;

    printf("[%s] ", heure());
    switch (t->type) {
    case PROTO_T_HELLO: {
        proto_hello_t h;
        if (proto_decoder_hello(t->payload, t->len, &h)) {
            printf("HELLO carte=%s fw=%u.%u.%u protocole=v%u%s\n", nom_carte(h.carte), h.fw[0],
                   h.fw[1], h.fw[2], h.version,
                   h.version != PROTO_VERSION ? "  ATTENTION : version differente de la Pi" : "");
        }
        break;
    }
    case PROTO_T_HB: {
        proto_hb_t h;
        if (proto_decoder_hb(t->payload, t->len, &h)) {
            printf("hb    uptime ESP = %.1f s\n", h.uptime_ms / 1000.0);
        }
        break;
    }
    case PROTO_T_BMS: {
        proto_bms_t b;
        if (proto_decoder_bms(t->payload, t->len, &b)) {
            afficher_bms(&b);
        }
        break;
    }
    case PROTO_T_TEMP: {
        proto_temp_t s;
        if (proto_decoder_temp(t->payload, t->len, &s)) {
            printf("temp  %u sondes", s.nb);
            for (uint8_t i = 0; i < s.nb; ++i) {
                if (s.sondes[i].c_centieme == PROTO_TEMP_INVALIDE) {
                    printf("  [%u] ERREUR", i);
                } else {
                    printf("  [%u] %.2f", i, s.sondes[i].c_centieme / 100.0);
                }
            }
            printf("\n");
        }
        break;
    }
    case PROTO_T_POWER: {
        proto_power_t p;
        if (proto_decoder_power(t->payload, t->len, &p)) {
            printf("power %.2f V  %.3f A  %.2f W\n", p.tension_mv / 1000.0, p.courant_ma / 1000.0,
                   p.puissance_mw / 1000.0);
        }
        break;
    }
    case PROTO_T_IMU: {
        proto_imu_t i;
        if (proto_decoder_imu(t->payload, t->len, &i)) {
            printf("imu   roulis %.2f  tangage %.2f  cap %.2f deg\n", i.roulis_cdeg / 100.0,
                   i.tangage_cdeg / 100.0, i.cap_cdeg / 100.0);
        }
        break;
    }
    case PROTO_T_PILOT: {
        proto_pilot_t p;
        if (proto_decoder_pilot(t->payload, t->len, &p)) {
            printf("pilot %s boutons=0x%02X voyants=0x%02X\n", p.ok ? "ok" : "MUET", p.boutons, p.voyants);
        }
        break;
    }
    case PROTO_T_ALERT: {
        proto_alert_t al;
        if (proto_decoder_alert(t->payload, t->len, &al)) {
            printf("ALERTE code=%u niveau=%u source=0x%02X valeur=%ld\n", al.code, al.niveau,
                   al.source, (long)al.valeur);
        }
        break;
    }
    case PROTO_T_LOG: {
        char niveau, texte[PROTO_MAX_PAYLOAD + 1];
        if (proto_decoder_log(t->payload, t->len, &niveau, texte, sizeof texte)) {
            printf("log   [%c] %s\n", niveau, texte);
        }
        break;
    }
    case PROTO_T_ACK: {
        proto_ack_t a;
        if (proto_decoder_ack(t->payload, t->len, &a)) {
            printf("ack   n=%u : %s", a.ref, a.ok ? "OK" : "ERREUR");
            if (!a.ok) {
                printf(" (code %u)", a.erreur);
            }
            for (int i = 0; i < 8; ++i) {
                if (m->pings[i].actif && m->pings[i].seq == a.ref) {
                    const double rtt = (double)(mtn - m->pings[i].envoi);
                    m->pings[i].actif = 0;
                    m->rtt_total += rtt;
                    m->rtt_nb++;
                    if (rtt > m->rtt_max) m->rtt_max = rtt;
                    printf("  (aller-retour %.0f ms)", rtt);
                }
            }
            printf("\n");
        }
        break;
    }
    case PROTO_T_TEST:
        printf("test  trame de flux (%u octets)\n", t->len);
        break;
    case PROTO_T_STREAM_FIN:
        printf("fin de flux\n");
        break;
    default:
        printf("type 0x%02X inconnu (%u octets), ignore\n", t->type, t->len);
        break;
    }
    fflush(stdout);
    return 0;
}

static int moniteur(uint32_t vitesse, int reboot_esp)
{
    moniteur_t m;
    memset(&m, 0, sizeof m);
    const uint64_t debut = maintenant_ms();

    if (vitesse != PROTO_VITESSE_DEFAUT) {
        printf("[%s] Passage a %u bauds...\n", heure(), (unsigned)vitesse);
        if (basculer(vitesse) != 0) {
            printf("Impossible de passer a %u bauds.\n", (unsigned)vitesse);
            recuperer();
            return 1;
        }
    }
    printf("[%s] Ecoute a %u bauds (Ctrl+C pour arreter)\n", heure(), (unsigned)vitesse_actuelle);
    envoyer(PROTO_T_INFO, NULL, 0, NULL);

    uint64_t prochain_ping = debut, prochaines_stats = debut + 10000, dernier_avertissement = debut;
    int reboot_envoye = 0;

    while (!arret) {
        if (pomper(100, rappel_moniteur, &m) < 0) {
            return 1;
        }
        const uint64_t mtn = maintenant_ms();

        if (m.liaison_ok && mtn - m.derniere_trame > PROTO_TIMEOUT_LIAISON_MS) {
            m.liaison_ok = 0;
            printf("[%s] *** ESP32 PERDU : aucune trame valide depuis %u s ***\n", heure(),
                   PROTO_TIMEOUT_LIAISON_MS / 1000);
        }
        if (m.derniere_trame == 0 && mtn - dernier_avertissement > PROTO_TIMEOUT_LIAISON_MS) {
            dernier_avertissement = mtn;
            printf("[%s] Toujours rien recu... verifier le cablage (GPIO17 ESP -> broche 10 Pi), "
                   "le GND commun et le port\n", heure());
        }

        if (mtn >= prochain_ping) {
            prochain_ping = mtn + 2000;
            uint16_t s;
            if (envoyer(PROTO_T_PING, NULL, 0, &s) == 0) {
                const int i = s % 8;
                m.pings[i].seq = s;
                m.pings[i].envoi = mtn;
                m.pings[i].actif = 1;
            }
        }

        if (reboot_esp && !reboot_envoye && m.liaison_ok && mtn - debut > 5000) {
            reboot_envoye = 1;
            printf("[%s] Envoi de la commande reboot a l'ESP32\n", heure());
            proto_cmd_t c;
            memset(&c, 0, sizeof c);
            c.commande = PROTO_CMD_REBOOT;
            envoyer_cmd(&c, NULL);
        }

        if (mtn >= prochaines_stats) {
            prochaines_stats = mtn + 10000;
            printf("[%s] --- STATS : %u trames recues, %u perdues, %u rejetees (CRC) | ",
                   heure(), (unsigned)m.recues, (unsigned)m.perdues, (unsigned)dec.erreurs_crc);
            if (m.rtt_nb) {
                printf("aller-retour moy %.0f ms, max %.0f ms ---\n", m.rtt_total / m.rtt_nb, m.rtt_max);
            } else {
                printf("aucune reponse aux pings ---\n");
            }
        }
    }

    printf("\n[%s] Arret. %u trames recues, %u perdues, %u rejetees (CRC).\n", heure(),
           (unsigned)m.recues, (unsigned)m.perdues, (unsigned)dec.erreurs_crc);
    if (vitesse_actuelle != PROTO_VITESSE_DEFAUT) {
        arret = 0;
        basculer(PROTO_VITESSE_DEFAUT);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static void usage(const char *nom)
{
    printf("Usage : %s [--banc] [--port P] [--vitesse N] [--taille N] [--duree MS] [--max N]\n"
           "          [--brut] [--reboot-esp]\n"
           "  (sans option)  moniteur : affiche les trames de l'ESP32\n"
           "  --banc         mesure le debit a chaque vitesse UART\n"
           "  --port P       port serie (defaut /dev/serial0)\n"
           "  --vitesse N    vitesse du moniteur (defaut %u)\n"
           "  --taille N     octets utiles par trame du banc (4 a %d, defaut 240)\n"
           "  --duree MS     duree du flux par vitesse (defaut 3000)\n"
           "  --max N        vitesse maximale testee par le banc\n"
           "  --brut         affiche aussi les octets bruts recus\n"
           "  --reboot-esp   (moniteur) teste la commande reboot apres 5 s\n",
           nom, (unsigned)PROTO_VITESSE_DEFAUT, PROTO_MAX_PAYLOAD);
}

int main(int argc, char **argv)
{
    const char *port = "/dev/serial0";
    uint32_t vitesse = PROTO_VITESSE_DEFAUT, vitesse_max = 0, duree = 3000;
    long taille = 240;
    int mode_banc = 0, reboot_esp = 0;

    for (int i = 1; i < argc; ++i) {
        const int a_valeur = i + 1 < argc;
        if (!strcmp(argv[i], "--banc")) mode_banc = 1;
        else if (!strcmp(argv[i], "--brut")) option_brut = 1;
        else if (!strcmp(argv[i], "--reboot-esp")) reboot_esp = 1;
        else if (!strcmp(argv[i], "--port") && a_valeur) port = argv[++i];
        else if (!strcmp(argv[i], "--vitesse") && a_valeur) vitesse = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--max") && a_valeur) vitesse_max = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--duree") && a_valeur) duree = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--taille") && a_valeur) taille = strtol(argv[++i], NULL, 10);
        else {
            usage(argv[0]);
            return !strcmp(argv[i], "--aide") || !strcmp(argv[i], "-h") ? 0 : 2;
        }
    }
    if (!proto_vitesse_valide(vitesse)) {
        fprintf(stderr, "Vitesse %u non geree. Vitesses possibles :", (unsigned)vitesse);
        for (int i = 0; PROTO_VITESSES[i]; ++i) fprintf(stderr, " %u", (unsigned)PROTO_VITESSES[i]);
        fprintf(stderr, "\n");
        return 2;
    }
    if (taille < 4 || taille > PROTO_MAX_PAYLOAD || duree == 0 || duree > 30000) {
        fprintf(stderr, "--taille doit etre entre 4 et %d, --duree entre 1 et 30000\n", PROTO_MAX_PAYLOAD);
        return 2;
    }

    signal(SIGINT, sur_signal);
    signal(SIGTERM, sur_signal);

    fd = uart_ouvrir(port, PROTO_VITESSE_DEFAUT);
    if (fd < 0) {
        fprintf(stderr, "Verifier : UART active (raspi-config), droits (groupe dialout), "
                        "aucun autre programme sur le port.\n");
        return 1;
    }
    proto_decodeur_init(&dec);

    const int code = mode_banc ? banc((uint16_t)taille, duree, vitesse_max) : moniteur(vitesse, reboot_esp);
    uart_fermer(fd);
    return code;
}
