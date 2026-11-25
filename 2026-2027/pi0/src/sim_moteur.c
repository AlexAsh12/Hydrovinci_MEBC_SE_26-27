/* Dialogue AT avec le module SIM7600. Voir sim_moteur.h. */
#include "sim_moteur.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum {
    CMD_AUCUNE = 0,
    CMD_INIT,
    CMD_INIT_SMS,
    CMD_CPIN,
    CMD_CPIN_CODE,
    CMD_CGPSINFO,
    CMD_CGPS_ON,
    CMD_CMGL,
    CMD_CMGD,
    CMD_CMGS,
    CMD_CSQ,
    CMD_CREG,
    CMD_COPS
};

/* Configuration du module, a chaque branchement ou redemarrage.
 * AT+CGPS=1 repond ERROR si le GPS tourne deja : sans importance. */
static const char *const init_modem[] = {"ATE0", "AT+CMEE=2", "AT+CGPS=1"};
#define NB_INIT_MODEM (int)(sizeof init_modem / sizeof init_modem[0])

/* Configuration des SMS, des que la carte SIM est prete :
 * mode texte, alphabet ASCII, SMS stockes sur la SIM, notification +CMTI */
static const char *const init_sms[] = {
    "AT+CMGF=1", "AT+CSCS=\"IRA\"", "AT+CSMP=17,167,0,0", "AT+CPMS=\"SM\",\"SM\",\"SM\"", "AT+CNMI=2,1,0,0,0",
};
#define NB_INIT_SMS (int)(sizeof init_sms / sizeof init_sms[0])

#define DELAI_DEFAUT_MS 3000u
#define DELAI_CMGL_MS 10000u
#define DELAI_CMGS_MS 60000u
#define PERIODE_GPS_MS 1000u
#define PERIODE_GPS_ON_MS 30000u      /* relance du GPS tant qu'il n'a pas de position */
#define GPS_SANS_FIX_MS 30000u
#define PERIODE_CMGL_MS 15000u
#define PERIODE_CSQ_MS 5000u
#define PERIODE_CREG_MS 5000u         /* CREG et CEREG en alternance */
#define PERIODE_COPS_MS 30000u
#define PERIODE_CPIN_ATTENTE_MS 5000u
#define PERIODE_CPIN_OK_MS 60000u
#define REESSAI_INIT_SMS_MS 10000u
#define DUREE_VIE_SMS_MS (15u * 60u * 1000u)
#define REESSAI_SMS_MS 30000u
#define ESSAIS_SMS 2
#define SANS_REPONSE_MAX 3

/* ------------------------------------------------------------------ */
/* Outils                                                              */
/* ------------------------------------------------------------------ */

#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
static void noter(sim_moteur_t *m, const char *format, ...)
{
    if (!m->r.journal) return;
    char texte[200];
    va_list a;
    va_start(a, format);
    vsnprintf(texte, sizeof texte, format, a);
    va_end(a);
    m->r.journal(m->r.ctx, texte);
}

/* Temps ecoule depuis un instant, 0 si l'instant est posterieur : les heures
 * peuvent venir de deux lectures d'horloge differentes */
static uint64_t ecoule(uint64_t t, uint64_t depuis)
{
    return t > depuis ? t - depuis : 0;
}

static void copier(char *dst, size_t taille, const char *src)
{
    if (!taille) return;
    size_t n = strlen(src);
    if (n >= taille) n = taille - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void sim_masquer_numero(const char *numero, char *sortie, size_t taille)
{
    char c[32];
    size_t n = 0;
    for (; *numero && n + 1 < sizeof c; ++numero) {
        if (isdigit((unsigned char)*numero)) c[n++] = *numero;
    }
    c[n] = '\0';
    snprintf(sortie, taille, "...%s", n > 4 ? c + n - 4 : c);
}

static int ecrire(sim_moteur_t *m, const char *data, size_t n)
{
    if (!m->r.ecrire || m->r.ecrire(m->r.ctx, data, n) != 0) {
        m->sans_reponse = SANS_REPONSE_MAX;  /* port inutilisable : a rouvrir */
        return -1;
    }
    return 0;
}

static void envoyer(sim_moteur_t *m, int type, const char *texte, uint64_t delai, uint64_t t)
{
    copier(m->cmd_texte, sizeof m->cmd_texte, texte);
    m->cmd = type;
    m->cmd_debut = t;
    m->cmd_delai = delai;
    m->cmd_invite = 0;
    m->cmd_info = 0;
    if (type == CMD_CMGL) {
        m->nb_recus = 0;
        m->recu_en_cours = -1;
    }
    char ligne[80];
    const int n = snprintf(ligne, sizeof ligne, "%s\r", texte);
    if (n > 0 && (size_t)n < sizeof ligne) ecrire(m, ligne, (size_t)n);
}

static void retirer_sms(sim_moteur_t *m, int i)
{
    for (int k = i; k + 1 < m->nb_file; ++k) m->file[k] = m->file[k + 1];
    if (m->nb_file > 0) m->nb_file--;
}

/* ------------------------------------------------------------------ */
/* Changements d'etat                                                  */
/* ------------------------------------------------------------------ */

static void changer_carte(sim_moteur_t *m, sim_carte_t c, uint64_t t)
{
    if (c == SIM_CARTE_INCONNUE || c == m->carte) return;
    static const char *const noms[] = {"inconnue", "prete", "code PIN demande", "bloquee (code PUK)", "absente"};
    noter(m, "carte SIM : %s", noms[c]);
    if (c == SIM_CARTE_PIN && !m->pin[0]) noter(m, "ajouter SIM_PIN dans le fichier .env");
    m->carte = c;
    if (c == SIM_CARTE_PRETE) {
        m->proch_init_sms = t;
    } else {
        m->sms_pret = 0;
        m->etape_sms = 0;
    }
}

static void maj_reseau(sim_moteur_t *m, uint64_t t)
{
    const int r = sa_enregistre(m->stat_creg) || sa_enregistre(m->stat_cereg);
    if (r != m->reseau) {
        m->reseau = r;
        noter(m, "reseau mobile : %s", r ? "enregistre" : "non enregistre");
        m->proch_cops = t;  /* operateur a relire */
    }
}

static void perdre_gps(sim_moteur_t *m)
{
    if (m->gps_fix) {
        m->gps_fix = 0;
        noter(m, "GPS : position perdue");
        if (m->r.sur_gps) m->r.sur_gps(m->r.ctx, NULL, 0);
    }
}

static void remettre_a_zero(sim_moteur_t *m, sim_modem_t modem, uint64_t t)
{
    perdre_gps(m);
    m->modem = modem;
    m->carte = SIM_CARTE_INCONNUE;
    m->rssi = -1;
    m->reseau = 0;
    m->operateur[0] = '\0';
    m->techno = -1;
    m->gps_actif = 0;
    m->pin_essaye = 0;
    m->etape_init = m->etape_sms = m->sms_pret = 0;
    m->stat_creg = m->stat_cereg = -1;
    m->ligne_len = 0;
    m->ligne_trop_longue = 0;
    m->lf_vides = 0;
    m->cmd = CMD_AUCUNE;
    m->cmd_invite = 0;
    m->sans_reponse = 0;
    m->nb_recus = 0;
    m->recu_en_cours = -1;
    m->nb_a_supprimer = 0;
    m->proch_gps = m->proch_cmgl = m->proch_csq = m->proch_creg = m->proch_cops = m->proch_cpin = t;
    m->proch_init_sms = t;
    m->proch_gps_on = t + PERIODE_GPS_ON_MS;
    m->dernier_fix = 0;
}

/* ------------------------------------------------------------------ */
/* Fin d'une commande                                                  */
/* ------------------------------------------------------------------ */

static void terminer(sim_moteur_t *m, int succes, uint64_t t)
{
    const int type = m->cmd;
    m->cmd = CMD_AUCUNE;
    m->cmd_invite = 0;

    switch (type) {
    case CMD_INIT:
        if (++m->etape_init >= NB_INIT_MODEM) {
            m->modem = SIM_MODEM_PRET;
            noter(m, "SIM7600 pret");
        }
        break;

    case CMD_INIT_SMS:
        if (!succes && m->etape_sms == 0) {
            m->proch_init_sms = t + REESSAI_INIT_SMS_MS;  /* AT+CMGF indispensable */
            break;
        }
        if (++m->etape_sms >= NB_INIT_SMS) {
            m->sms_pret = 1;
            m->proch_cmgl = t;
            noter(m, "SMS prets");
        }
        break;

    case CMD_CPIN:
        m->proch_cpin = t + (m->carte == SIM_CARTE_PRETE ? PERIODE_CPIN_OK_MS : PERIODE_CPIN_ATTENTE_MS);
        break;

    case CMD_CPIN_CODE:
        m->pin_essaye = 1;
        if (!succes) {
            /* Plus jamais d'essai avec ce code, meme apres un redemarrage du
             * module : 3 codes faux bloquent la carte (code PUK) */
            m->pin_refuse = 1;
            noter(m, "code PIN refuse : verifier SIM_PIN dans le .env puis redemarrer le service");
        } else {
            noter(m, "code PIN accepte");
        }
        m->proch_cpin = t + 2000u;
        break;

    case CMD_CGPSINFO:
        if (!succes) {
            if (m->gps_actif) noter(m, "GPS : ne repond pas");
            m->gps_actif = 0;
            perdre_gps(m);
        }
        break;

    case CMD_CMGL:
        if (succes) {
            for (int i = 0; i < m->nb_recus; ++i) {
                if (m->recus[i].a_lire) {
                    char texte[SIM_TAILLE_TEXTE_RECU];
                    sa_decoder_texte(m->recus[i].texte, texte, sizeof texte);
                    m->sms_recus++;
                    if (m->r.sur_sms) m->r.sur_sms(m->r.ctx, m->recus[i].numero, texte, m->recus[i].horodatage);
                }
                if (m->nb_a_supprimer < SIM_MAX_SUPPRESSIONS) {
                    m->a_supprimer[m->nb_a_supprimer++] = m->recus[i].index;
                }
            }
            /* Liste pleine : il en reste peut-etre, on revient vite */
            if (m->nb_recus == SIM_MAX_RECUS) m->proch_cmgl = t + 1000u;
        }
        m->nb_recus = 0;
        break;

    case CMD_CMGD:
        if (m->nb_a_supprimer > 0) {
            memmove(m->a_supprimer, m->a_supprimer + 1, (size_t)(m->nb_a_supprimer - 1) * sizeof m->a_supprimer[0]);
            m->nb_a_supprimer--;
        }
        break;

    case CMD_CMGS:
        if (m->nb_file > 0) {
            char masque[16];
            sim_masquer_numero(m->file[0].numero, masque, sizeof masque);
            if (succes) {
                m->sms_envoyes++;
                noter(m, "SMS envoye a %s", masque);
                retirer_sms(m, 0);
            } else if (++m->file[0].essais >= ESSAIS_SMS) {
                m->sms_echecs++;
                noter(m, "SMS pour %s abandonne apres %d essais", masque, ESSAIS_SMS);
                retirer_sms(m, 0);
            } else {
                m->file[0].pas_avant = t + REESSAI_SMS_MS;
                noter(m, "echec d'envoi du SMS pour %s, nouvel essai dans %u s", masque, REESSAI_SMS_MS / 1000u);
            }
        }
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Lignes recues                                                       */
/* ------------------------------------------------------------------ */

static int final_erreur(const char *l)
{
    return strcmp(l, "ERROR") == 0 || strncmp(l, "+CME ERROR:", 11) == 0 || strncmp(l, "+CMS ERROR:", 11) == 0;
}

static void ajouter_corps(sim_moteur_t *m, const char *l)
{
    char *texte = m->recus[m->recu_en_cours].texte;
    const size_t pos = strlen(texte);
    snprintf(texte + pos, SIM_TAILLE_TEXTE_RECU - pos, "%s%s", pos ? "\n" : "", l);
}

/* apres_vide : la ligne suit une ligne vide. Le module separe sa reponse
 * finale ("\r\n\r\nOK") du dernier SMS liste par une ligne vide ; dans le
 * texte d'un SMS, "OK", "ERROR" ou "RDY" ne sont que du texte. */
static void traiter_ligne(sim_moteur_t *m, const char *l, int apres_vide, uint64_t t)
{
    m->sans_reponse = 0;  /* le module est vivant */

    /* Corps de SMS : toute ligne apres un en-tete +CMGL, jusqu'au prochain
     * en-tete ou au resultat final precede d'une ligne vide */
    if (m->cmd == CMD_CMGL && m->recu_en_cours >= 0 && m->cmd_info >= 1 && strncmp(l, "+CMGL:", 6) != 0) {
        const int final = apres_vide && (strcmp(l, "OK") == 0 || final_erreur(l));
        if (m->cmd_info == 1 || !final) {
            ajouter_corps(m, l);
            m->cmd_info = 2;
            return;
        }
    }

    if (m->cmd && strcmp(l, m->cmd_texte) == 0) return;  /* echo */

    if (strcmp(l, "RDY") == 0) {
        noter(m, "SIM7600 redemarre : nouvelle configuration");
        remettre_a_zero(m, SIM_MODEM_INIT, t);
        return;
    }
    if (sa_cmti(l)) {
        m->proch_cmgl = t;  /* nouveau SMS : relever tout de suite */
        return;
    }

    changer_carte(m, sa_cpin(l), t);

    int v;
    if (sa_csq(l, &v)) {
        m->rssi = v == 99 ? -1 : v;
    } else if (sa_creg(l, &v)) {
        if (strncmp(l, "+CREG:", 6) == 0) m->stat_creg = v;
        else m->stat_cereg = v;
        maj_reseau(m, t);
    } else if (strncmp(l, "+COPS:", 6) == 0) {
        char nom[32];
        int techno;
        if (sa_cops(l, nom, sizeof nom, &techno)) {
            if (strcmp(nom, m->operateur) != 0 && nom[0]) {
                noter(m, "operateur : %s %s", nom, sa_techno(techno));
            }
            copier(m->operateur, sizeof m->operateur, nom);
            m->techno = techno;
        }
    } else if (strncmp(l, "+CGPSINFO:", 10) == 0) {
        sim_gps_t g;
        const int r = sa_cgpsinfo(l, &g);
        if (r >= 0) m->gps_actif = 1;
        if (r == 1) {
            if (!m->gps_fix) noter(m, "GPS : position trouvee");
            m->gps_fix = 1;
            m->dernier_fix = t;
            if (m->r.sur_gps) m->r.sur_gps(m->r.ctx, &g, 1);
        } else if (r == 0) {
            perdre_gps(m);
        }
    }

    if (strcmp(l, "OK") == 0) {
        if (m->cmd) terminer(m, 1, t);
        return;
    }
    if (final_erreur(l)) {
        if (m->cmd) terminer(m, 0, t);
        return;
    }

    /* En-tete d'un SMS liste */
    if (m->cmd == CMD_CMGL) {
        char statut[24];
        if (strncmp(l, "+CMGL:", 6) == 0) {
            m->recu_en_cours = -1;
            if (m->nb_recus < SIM_MAX_RECUS) {
                const int i = m->nb_recus;
                if (sa_cmgl(l, &m->recus[i].index, statut, sizeof statut, m->recus[i].numero,
                            sizeof m->recus[i].numero, m->recus[i].horodatage, sizeof m->recus[i].horodatage)) {
                    m->recus[i].a_lire = strncmp(statut, "REC", 3) == 0;
                    m->recus[i].texte[0] = '\0';
                    m->nb_recus++;
                    m->recu_en_cours = i;
                    m->cmd_info = 1;  /* la ligne suivante est le corps */
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entrees                                                             */
/* ------------------------------------------------------------------ */

void sim_moteur_init(sim_moteur_t *m, const sim_rappels_t *r)
{
    memset(m, 0, sizeof *m);
    if (r) m->r = *r;
    remettre_a_zero(m, SIM_MODEM_ABSENT, 0);
}

void sim_moteur_definir_pin(sim_moteur_t *m, const char *pin)
{
    /* 4 a 8 chiffres, sinon ignore */
    size_t n = 0;
    while (pin[n] && isdigit((unsigned char)pin[n])) n++;
    if (pin[n] == '\0' && n >= 4 && n <= 8) copier(m->pin, sizeof m->pin, pin);
    else m->pin[0] = '\0';
    m->pin_refuse = 0;
}

void sim_moteur_ouvert(sim_moteur_t *m, uint64_t t)
{
    remettre_a_zero(m, SIM_MODEM_INIT, t);
}

void sim_moteur_ferme(sim_moteur_t *m)
{
    remettre_a_zero(m, SIM_MODEM_ABSENT, 0);
}

int sim_moteur_muet(const sim_moteur_t *m)
{
    return m->modem != SIM_MODEM_ABSENT && m->sans_reponse >= SANS_REPONSE_MAX;
}

void sim_moteur_recevoir(sim_moteur_t *m, const char *data, size_t n, uint64_t t)
{
    for (size_t i = 0; i < n && m->modem != SIM_MODEM_ABSENT; ++i) {
        const unsigned char c = (unsigned char)data[i];

        /* Invite "> " d'AT+CMGS : arrive sans fin de ligne */
        if (c == '>' && m->ligne_len == 0 && m->cmd == CMD_CMGS && m->cmd_invite) {
            m->cmd_invite = 0;
            m->sans_reponse = 0;
            char corps[SIM_TAILLE_TEXTE_ENVOI + 1];
            const int k = snprintf(corps, sizeof corps, "%s\x1A", m->nb_file ? m->file[0].texte : "");
            if (k > 0) ecrire(m, corps, (size_t)k);
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (m->ligne_len > 0) {
                const int apres_vide = m->lf_vides >= 2;
                m->lf_vides = 0;
                if (!m->ligne_trop_longue) {
                    m->ligne[m->ligne_len] = '\0';
                    traiter_ligne(m, m->ligne, apres_vide, t);
                }
            } else if (c == '\n') {
                /* "\r\n" seul : ligne vide. Deux d'affilee juste apres un
                 * en-tete +CMGL : le SMS n'a pas de texte. */
                m->lf_vides++;
                if (m->cmd == CMD_CMGL && m->cmd_info == 1 && m->recu_en_cours >= 0 && m->lf_vides >= 2) {
                    m->cmd_info = 2;
                }
            }
            m->ligne_len = 0;
            m->ligne_trop_longue = 0;
            continue;
        }
        if (c < 0x20) continue;  /* autres caracteres de controle */
        if (c == ' ' && m->ligne_len == 0) continue;  /* espace apres l'invite */
        if (m->ligne_len + 1 < sizeof m->ligne) {
            m->ligne[m->ligne_len++] = (char)c;
        } else {
            m->ligne_trop_longue = 1;
        }
    }
}

/* Choisit la prochaine commande quand le module est libre */
static void planifier(sim_moteur_t *m, uint64_t t)
{
    char c[64];

    if (m->modem == SIM_MODEM_INIT) {
        envoyer(m, CMD_INIT, init_modem[m->etape_init], DELAI_DEFAUT_MS, t);
        return;
    }
    if (t >= m->proch_cpin) {
        m->proch_cpin = t + PERIODE_CPIN_ATTENTE_MS;  /* recale dans terminer() */
        envoyer(m, CMD_CPIN, "AT+CPIN?", DELAI_DEFAUT_MS, t);
        return;
    }
    if (m->carte == SIM_CARTE_PIN && m->pin[0] && !m->pin_essaye && !m->pin_refuse) {
        snprintf(c, sizeof c, "AT+CPIN=\"%s\"", m->pin);
        envoyer(m, CMD_CPIN_CODE, c, DELAI_DEFAUT_MS * 2, t);
        /* Le code ne doit jamais apparaitre dans une trace */
        memset(m->cmd_texte, 0, sizeof m->cmd_texte);
        copier(m->cmd_texte, sizeof m->cmd_texte, "AT+CPIN=****");
        return;
    }
    if (m->carte == SIM_CARTE_PRETE && !m->sms_pret && t >= m->proch_init_sms) {
        envoyer(m, CMD_INIT_SMS, init_sms[m->etape_sms], DELAI_DEFAUT_MS, t);
        return;
    }
    if (m->nb_a_supprimer > 0) {
        snprintf(c, sizeof c, "AT+CMGD=%d", m->a_supprimer[0]);
        envoyer(m, CMD_CMGD, c, DELAI_DEFAUT_MS, t);
        return;
    }
    if (t >= m->proch_gps) {
        m->proch_gps = t + PERIODE_GPS_MS;
        envoyer(m, CMD_CGPSINFO, "AT+CGPSINFO", DELAI_DEFAUT_MS, t);
        return;
    }
    if (t >= m->proch_gps_on && (!m->gps_actif || !m->dernier_fix || ecoule(t, m->dernier_fix) > GPS_SANS_FIX_MS)) {
        m->proch_gps_on = t + PERIODE_GPS_ON_MS;
        envoyer(m, CMD_CGPS_ON, "AT+CGPS=1", DELAI_DEFAUT_MS, t);
        return;
    }
    if (m->nb_file > 0 && m->carte == SIM_CARTE_PRETE && m->reseau && m->sms_pret && t >= m->file[0].pas_avant) {
        snprintf(c, sizeof c, "AT+CMGS=\"%s\"", m->file[0].numero);
        envoyer(m, CMD_CMGS, c, DELAI_CMGS_MS, t);
        m->cmd_invite = 1;
        return;
    }
    if (m->sms_pret && t >= m->proch_cmgl) {
        m->proch_cmgl = t + PERIODE_CMGL_MS;
        envoyer(m, CMD_CMGL, "AT+CMGL=\"ALL\"", DELAI_CMGL_MS, t);
        return;
    }
    if (t >= m->proch_csq) {
        m->proch_csq = t + PERIODE_CSQ_MS;
        envoyer(m, CMD_CSQ, "AT+CSQ", DELAI_DEFAUT_MS, t);
        return;
    }
    if (t >= m->proch_creg) {
        m->proch_creg = t + PERIODE_CREG_MS;
        m->creg_suivant = !m->creg_suivant;
        envoyer(m, CMD_CREG, m->creg_suivant ? "AT+CREG?" : "AT+CEREG?", DELAI_DEFAUT_MS, t);
        return;
    }
    if (t >= m->proch_cops) {
        m->proch_cops = t + PERIODE_COPS_MS;
        envoyer(m, CMD_COPS, "AT+COPS?", DELAI_DEFAUT_MS * 3, t);
        return;
    }
}

void sim_moteur_tick(sim_moteur_t *m, uint64_t t)
{
    if (m->modem == SIM_MODEM_ABSENT) return;

    /* Commande sans reponse */
    if (m->cmd && ecoule(t, m->cmd_debut) >= m->cmd_delai) {
        if (m->cmd == CMD_CMGS && m->cmd_invite) {
            ecrire(m, "\x1B", 1);  /* ECHAP : annule la saisie du SMS */
        }
        m->sans_reponse++;  /* remis a 0 des que le module envoie une ligne */
        noter(m, "pas de reponse a %s", m->cmd_texte);
        /* Liste de SMS coupee (SMS vide suivi de OK) : garder ce qui a ete lu */
        terminer(m, m->cmd == CMD_CMGL && m->nb_recus > 0, t);
    }

    /* SMS trop vieux */
    for (int i = m->nb_file - 1; i >= 0; --i) {
        if (i == 0 && m->cmd == CMD_CMGS) continue;
        if (ecoule(t, m->file[i].cree) > DUREE_VIE_SMS_MS) {
            char masque[16];
            sim_masquer_numero(m->file[i].numero, masque, sizeof masque);
            noter(m, "SMS pour %s abandonne : pas de reseau depuis 15 min", masque);
            m->sms_echecs++;
            retirer_sms(m, i);
        }
    }

    if (!m->cmd && m->modem != SIM_MODEM_ABSENT) planifier(m, t);
}

int sim_moteur_envoyer_sms(sim_moteur_t *m, const char *numero, const char *texte, uint64_t t)
{
    if (!sa_numero_valide(numero) || m->nb_file >= SIM_FILE_ENVOI) return -1;
    char masque[16];
    sim_masquer_numero(numero, masque, sizeof masque);
    copier(m->file[m->nb_file].numero, sizeof m->file[0].numero, numero);
    sa_vers_ascii(texte, m->file[m->nb_file].texte, sizeof m->file[0].texte, SIM_TAILLE_TEXTE_ENVOI - 1);
    m->file[m->nb_file].cree = t;
    m->file[m->nb_file].pas_avant = 0;
    m->file[m->nb_file].essais = 0;
    m->nb_file++;
    noter(m, "SMS en attente pour %s", masque);
    return 0;
}
