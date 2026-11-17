/*
 * Protocole UART binaire ESP32 <-> Raspberry Pi - implementation.
 * Specification : 2026-2027/shared/PROTOCOLE_UART.md
 */
#include "protocole.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* CRC                                                                 */
/* ------------------------------------------------------------------ */

uint16_t proto_crc16(uint16_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* Trames                                                              */
/* ------------------------------------------------------------------ */

size_t proto_encoder_trame(uint8_t *sortie, size_t taille_sortie, uint8_t type,
                           uint16_t seq, const uint8_t *payload, uint16_t len)
{
    const size_t total = PROTO_TAILLE_ENTETE + (size_t)len + PROTO_TAILLE_CRC;
    if (len > PROTO_MAX_PAYLOAD || taille_sortie < total) {
        return 0;
    }
    sortie[0] = PROTO_SOF0;
    sortie[1] = PROTO_SOF1;
    sortie[2] = (uint8_t)(len & 0xFF);
    sortie[3] = (uint8_t)(len >> 8);
    sortie[4] = type;
    sortie[5] = (uint8_t)(seq & 0xFF);
    sortie[6] = (uint8_t)(seq >> 8);
    if (len > 0) {
        memmove(sortie + PROTO_TAILLE_ENTETE, payload, len);
    }
    const uint16_t crc = proto_crc16(0xFFFF, sortie + 2, 5u + len);
    sortie[total - 2] = (uint8_t)(crc & 0xFF);
    sortie[total - 1] = (uint8_t)(crc >> 8);
    return total;
}

void proto_decodeur_init(proto_decodeur_t *d)
{
    d->debut = 0;
    d->fin = 0;
    d->trames_ok = 0;
    d->erreurs_crc = 0;
    d->octets_ignores = 0;
}

size_t proto_decodeur_ajouter(proto_decodeur_t *d, const uint8_t *data, size_t len)
{
    /* Recompacter le tampon si la place manque a la fin */
    if (d->debut > 0 && d->fin + len > sizeof d->tampon) {
        const size_t reste = d->fin - d->debut;
        memmove(d->tampon, d->tampon + d->debut, reste);
        d->debut = 0;
        d->fin = reste;
    }
    size_t place = sizeof d->tampon - d->fin;
    size_t n = len < place ? len : place;
    memcpy(d->tampon + d->fin, data, n);
    d->fin += n;
    return n;
}

bool proto_decodeur_suivante(proto_decodeur_t *d, proto_trame_t *trame)
{
    while (d->fin - d->debut >= 2) {
        const uint8_t *t = d->tampon + d->debut;

        /* 1. Chercher le debut de trame AA 55 */
        if (t[0] != PROTO_SOF0 || t[1] != PROTO_SOF1) {
            d->debut++;
            d->octets_ignores++;
            continue;
        }

        /* 2. Attendre l'en-tete complet */
        const size_t dispo = d->fin - d->debut;
        if (dispo < PROTO_TAILLE_ENTETE) {
            return false;
        }
        const uint16_t len = (uint16_t)(t[2] | (t[3] << 8));
        if (len > PROTO_MAX_PAYLOAD) {
            /* Faux debut de trame : on continue a chercher juste apres */
            d->debut++;
            d->octets_ignores++;
            continue;
        }

        /* 3. Attendre la trame complete */
        const size_t total = PROTO_TAILLE_ENTETE + (size_t)len + PROTO_TAILLE_CRC;
        if (dispo < total) {
            return false;
        }

        /* 4. Verifier le CRC */
        const uint16_t crc_calc = proto_crc16(0xFFFF, t + 2, 5u + len);
        const uint16_t crc_recu = (uint16_t)(t[total - 2] | (t[total - 1] << 8));
        if (crc_calc != crc_recu) {
            /* Trame abimee (ou faux AA 55 dans les donnees) : on se
             * resynchronise a partir de l'octet suivant, sans rien perdre
             * d'une vraie trame qui commencerait plus loin. */
            d->erreurs_crc++;
            d->debut++;
            d->octets_ignores++;
            continue;
        }

        trame->type = t[4];
        trame->seq = (uint16_t)(t[5] | (t[6] << 8));
        trame->len = len;
        trame->payload = t + PROTO_TAILLE_ENTETE;
        d->debut += total;
        d->trames_ok++;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Lecture / ecriture little-endian                                    */
/* ------------------------------------------------------------------ */

void proto_ecrivain_init(proto_ecrivain_t *e, uint8_t *p, size_t taille)
{
    e->p = p;
    e->taille = taille;
    e->pos = 0;
    e->erreur = false;
}

static bool reserver(proto_ecrivain_t *e, size_t n)
{
    if (e->erreur || e->taille - e->pos < n) {
        e->erreur = true;
        return false;
    }
    return true;
}

void proto_e_u8(proto_ecrivain_t *e, uint8_t v)
{
    if (reserver(e, 1)) {
        e->p[e->pos++] = v;
    }
}

void proto_e_u16(proto_ecrivain_t *e, uint16_t v)
{
    if (reserver(e, 2)) {
        e->p[e->pos++] = (uint8_t)(v & 0xFF);
        e->p[e->pos++] = (uint8_t)(v >> 8);
    }
}

void proto_e_i16(proto_ecrivain_t *e, int16_t v)
{
    proto_e_u16(e, (uint16_t)v);
}

void proto_e_u32(proto_ecrivain_t *e, uint32_t v)
{
    if (reserver(e, 4)) {
        for (int i = 0; i < 4; ++i) {
            e->p[e->pos++] = (uint8_t)((v >> (8 * i)) & 0xFF);
        }
    }
}

void proto_e_i32(proto_ecrivain_t *e, int32_t v)
{
    proto_e_u32(e, (uint32_t)v);
}

void proto_e_octets(proto_ecrivain_t *e, const uint8_t *src, size_t n)
{
    if (reserver(e, n)) {
        memcpy(e->p + e->pos, src, n);
        e->pos += n;
    }
}

void proto_lecteur_init(proto_lecteur_t *l, const uint8_t *p, size_t taille)
{
    l->p = p;
    l->taille = taille;
    l->pos = 0;
    l->erreur = false;
}

static bool disponible(proto_lecteur_t *l, size_t n)
{
    if (l->erreur || l->taille - l->pos < n) {
        l->erreur = true;
        return false;
    }
    return true;
}

uint8_t proto_l_u8(proto_lecteur_t *l)
{
    return disponible(l, 1) ? l->p[l->pos++] : 0;
}

uint16_t proto_l_u16(proto_lecteur_t *l)
{
    if (!disponible(l, 2)) {
        return 0;
    }
    const uint16_t v = (uint16_t)(l->p[l->pos] | (l->p[l->pos + 1] << 8));
    l->pos += 2;
    return v;
}

int16_t proto_l_i16(proto_lecteur_t *l)
{
    return (int16_t)proto_l_u16(l);
}

uint32_t proto_l_u32(proto_lecteur_t *l)
{
    if (!disponible(l, 4)) {
        return 0;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= (uint32_t)l->p[l->pos++] << (8 * i);
    }
    return v;
}

int32_t proto_l_i32(proto_lecteur_t *l)
{
    return (int32_t)proto_l_u32(l);
}

void proto_l_octets(proto_lecteur_t *l, uint8_t *dst, size_t n)
{
    if (disponible(l, n)) {
        memcpy(dst, l->p + l->pos, n);
        l->pos += n;
    }
}

/* ------------------------------------------------------------------ */
/* Messages                                                            */
/* ------------------------------------------------------------------ */

#define FIN_ENCODAGE(e) ((e).erreur ? 0 : (e).pos)

size_t proto_encoder_hello(const proto_hello_t *m, uint8_t *p, size_t taille)
{
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u8(&e, m->version);
    proto_e_u8(&e, m->carte);
    proto_e_octets(&e, m->fw, 3);
    return FIN_ENCODAGE(e);
}

bool proto_decoder_hello(const uint8_t *p, size_t len, proto_hello_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    m->version = proto_l_u8(&l);
    m->carte = proto_l_u8(&l);
    proto_l_octets(&l, m->fw, 3);
    return !l.erreur;
}

size_t proto_encoder_hb(const proto_hb_t *m, uint8_t *p, size_t taille)
{
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u32(&e, m->uptime_ms);
    return FIN_ENCODAGE(e);
}

bool proto_decoder_hb(const uint8_t *p, size_t len, proto_hb_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    m->uptime_ms = proto_l_u32(&l);
    return !l.erreur;
}

size_t proto_encoder_bms(const proto_bms_t *m, uint8_t *p, size_t taille)
{
    if (m->nb_cellules > PROTO_MAX_CELLULES || m->nb_temps > PROTO_MAX_TEMPS_BMS) {
        return 0;
    }
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u8(&e, m->id);
    proto_e_u8(&e, m->ok);
    proto_e_u16(&e, m->tension_cv);
    proto_e_i32(&e, m->courant_ca);
    proto_e_u16(&e, m->soc_dixieme);
    proto_e_u16(&e, m->restant_cah);
    proto_e_u8(&e, m->nb_cellules);
    for (uint8_t i = 0; i < m->nb_cellules; ++i) {
        proto_e_u16(&e, m->cellules_mv[i]);
    }
    proto_e_u8(&e, m->nb_temps);
    for (uint8_t i = 0; i < m->nb_temps; ++i) {
        proto_e_i16(&e, m->temps_dixieme[i]);
    }
    return FIN_ENCODAGE(e);
}

bool proto_decoder_bms(const uint8_t *p, size_t len, proto_bms_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    m->id = proto_l_u8(&l);
    m->ok = proto_l_u8(&l);
    m->tension_cv = proto_l_u16(&l);
    m->courant_ca = proto_l_i32(&l);
    m->soc_dixieme = proto_l_u16(&l);
    m->restant_cah = proto_l_u16(&l);
    m->nb_cellules = proto_l_u8(&l);
    if (m->nb_cellules > PROTO_MAX_CELLULES) {
        return false;
    }
    for (uint8_t i = 0; i < m->nb_cellules; ++i) {
        m->cellules_mv[i] = proto_l_u16(&l);
    }
    m->nb_temps = proto_l_u8(&l);
    if (m->nb_temps > PROTO_MAX_TEMPS_BMS) {
        return false;
    }
    for (uint8_t i = 0; i < m->nb_temps; ++i) {
        m->temps_dixieme[i] = proto_l_i16(&l);
    }
    return !l.erreur;
}

size_t proto_encoder_temp(const proto_temp_t *m, uint8_t *p, size_t taille)
{
    if (m->nb > PROTO_MAX_SONDES) {
        return 0;
    }
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u8(&e, m->nb);
    for (uint8_t i = 0; i < m->nb; ++i) {
        proto_e_octets(&e, m->sondes[i].rom, 8);
        proto_e_i16(&e, m->sondes[i].c_centieme);
    }
    return FIN_ENCODAGE(e);
}

bool proto_decoder_temp(const uint8_t *p, size_t len, proto_temp_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    m->nb = proto_l_u8(&l);
    if (m->nb > PROTO_MAX_SONDES) {
        return false;
    }
    for (uint8_t i = 0; i < m->nb; ++i) {
        proto_l_octets(&l, m->sondes[i].rom, 8);
        m->sondes[i].c_centieme = proto_l_i16(&l);
    }
    return !l.erreur;
}

size_t proto_encoder_power(const proto_power_t *m, uint8_t *p, size_t taille)
{
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u16(&e, m->tension_mv);
    proto_e_i32(&e, m->courant_ma);
    proto_e_i32(&e, m->puissance_mw);
    return FIN_ENCODAGE(e);
}

bool proto_decoder_power(const uint8_t *p, size_t len, proto_power_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    m->tension_mv = proto_l_u16(&l);
    m->courant_ma = proto_l_i32(&l);
    m->puissance_mw = proto_l_i32(&l);
    return !l.erreur;
}

size_t proto_encoder_imu(const proto_imu_t *m, uint8_t *p, size_t taille)
{
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_i16(&e, m->roulis_cdeg);
    proto_e_i16(&e, m->tangage_cdeg);
    proto_e_u16(&e, m->cap_cdeg);
    proto_e_u8(&e, m->precision);
    return FIN_ENCODAGE(e);
}

bool proto_decoder_imu(const uint8_t *p, size_t len, proto_imu_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    m->roulis_cdeg = proto_l_i16(&l);
    m->tangage_cdeg = proto_l_i16(&l);
    m->cap_cdeg = proto_l_u16(&l);
    /* Champ ajoute apres coup : absent chez un ancien emetteur */
    m->precision = l.pos < len ? proto_l_u8(&l) : PROTO_IMU_PRECISION_INCONNUE;
    return !l.erreur;
}

size_t proto_encoder_pilot(const proto_pilot_t *m, uint8_t *p, size_t taille)
{
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u8(&e, m->ok);
    proto_e_u8(&e, m->boutons);
    proto_e_u8(&e, m->voyants);
    return FIN_ENCODAGE(e);
}

bool proto_decoder_pilot(const uint8_t *p, size_t len, proto_pilot_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    m->ok = proto_l_u8(&l);
    m->boutons = proto_l_u8(&l);
    m->voyants = proto_l_u8(&l);
    return !l.erreur;
}

size_t proto_encoder_alert(const proto_alert_t *m, uint8_t *p, size_t taille)
{
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u8(&e, m->code);
    proto_e_u8(&e, m->niveau);
    proto_e_u8(&e, m->source);
    proto_e_i32(&e, m->valeur);
    return FIN_ENCODAGE(e);
}

bool proto_decoder_alert(const uint8_t *p, size_t len, proto_alert_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    m->code = proto_l_u8(&l);
    m->niveau = proto_l_u8(&l);
    m->source = proto_l_u8(&l);
    m->valeur = proto_l_i32(&l);
    return !l.erreur;
}

size_t proto_encoder_ack(const proto_ack_t *m, uint8_t *p, size_t taille)
{
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u16(&e, m->ref);
    proto_e_u8(&e, m->ok);
    proto_e_u8(&e, m->erreur);
    return FIN_ENCODAGE(e);
}

bool proto_decoder_ack(const uint8_t *p, size_t len, proto_ack_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    m->ref = proto_l_u16(&l);
    m->ok = proto_l_u8(&l);
    m->erreur = proto_l_u8(&l);
    return !l.erreur;
}

size_t proto_encoder_cmd(const proto_cmd_t *m, uint8_t *p, size_t taille)
{
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u8(&e, m->commande);
    switch (m->commande) {
    case PROTO_CMD_SET_LED:
        proto_e_u8(&e, m->arg.led.index);
        proto_e_u8(&e, m->arg.led.allume);
        break;
    case PROTO_CMD_SET_BAUD:
        proto_e_u32(&e, m->arg.baud.vitesse);
        break;
    case PROTO_CMD_STREAM:
        proto_e_u16(&e, m->arg.stream.taille_payload);
        proto_e_u32(&e, m->arg.stream.duree_ms);
        break;
    default:
        break;
    }
    return FIN_ENCODAGE(e);
}

bool proto_decoder_cmd(const uint8_t *p, size_t len, proto_cmd_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    memset(m, 0, sizeof *m);
    m->commande = proto_l_u8(&l);
    switch (m->commande) {
    case PROTO_CMD_SET_LED:
        m->arg.led.index = proto_l_u8(&l);
        m->arg.led.allume = proto_l_u8(&l);
        break;
    case PROTO_CMD_SET_BAUD:
        m->arg.baud.vitesse = proto_l_u32(&l);
        break;
    case PROTO_CMD_STREAM:
        m->arg.stream.taille_payload = proto_l_u16(&l);
        m->arg.stream.duree_ms = proto_l_u32(&l);
        break;
    default:
        break;  /* commande inconnue : decodee, refusee par l'application */
    }
    return !l.erreur;
}

size_t proto_encoder_stream_fin(const proto_stream_fin_t *m, uint8_t *p, size_t taille)
{
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u32(&e, m->trames);
    proto_e_u32(&e, m->octets);
    return FIN_ENCODAGE(e);
}

bool proto_decoder_stream_fin(const uint8_t *p, size_t len, proto_stream_fin_t *m)
{
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    m->trames = proto_l_u32(&l);
    m->octets = proto_l_u32(&l);
    return !l.erreur;
}

size_t proto_encoder_log(char niveau, const char *texte, uint8_t *p, size_t taille)
{
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u8(&e, (uint8_t)niveau);
    proto_e_octets(&e, (const uint8_t *)texte, strlen(texte));
    return FIN_ENCODAGE(e);
}

bool proto_decoder_log(const uint8_t *p, size_t len, char *niveau, char *texte, size_t taille_texte)
{
    if (len < 1 || taille_texte == 0) {
        return false;
    }
    *niveau = (char)p[0];
    size_t n = len - 1;
    if (n > taille_texte - 1) {
        n = taille_texte - 1;
    }
    memcpy(texte, p + 1, n);
    texte[n] = '\0';
    return true;
}

static uint8_t motif_test(uint32_t compteur, size_t i)
{
    return (uint8_t)((compteur * 31u + (uint32_t)i) & 0xFFu);
}

size_t proto_encoder_test(uint32_t compteur, uint8_t *p, size_t taille)
{
    if (taille < 4) {
        return 0;
    }
    proto_ecrivain_t e;
    proto_ecrivain_init(&e, p, taille);
    proto_e_u32(&e, compteur);
    for (size_t i = 4; i < taille; ++i) {
        p[i] = motif_test(compteur, i);
    }
    return taille;
}

bool proto_verifier_test(const uint8_t *p, size_t len, uint32_t *compteur)
{
    if (len < 4) {
        return false;
    }
    proto_lecteur_t l;
    proto_lecteur_init(&l, p, len);
    *compteur = proto_l_u32(&l);
    for (size_t i = 4; i < len; ++i) {
        if (p[i] != motif_test(*compteur, i)) {
            return false;
        }
    }
    return true;
}

const uint32_t PROTO_VITESSES[] = {
    115200u, 230400u, 460800u, 921600u, 1000000u, 1500000u, 2000000u, 2500000u, 3000000u, 0u
};

bool proto_vitesse_valide(uint32_t vitesse)
{
    for (size_t i = 0; PROTO_VITESSES[i] != 0; ++i) {
        if (PROTO_VITESSES[i] == vitesse) {
            return true;
        }
    }
    return false;
}
