/*
 * Tests du protocole UART binaire (C99 portable).
 *
 * - Sur la Pi / un PC : make test (dans 2026-2027/pi0)
 * - Sur l'ESP32      : environnement PlatformIO "tests_wroom32e"
 *
 * test_protocole_executer() renvoie le nombre d'echecs (0 = tout est bon).
 */
#include <stdio.h>
#include <string.h>

#include "../protocole.h"

static int nb_verifs;
static int nb_echecs;

#define VERIF(cond)                                                    \
    do {                                                               \
        nb_verifs++;                                                   \
        if (!(cond)) {                                                 \
            nb_echecs++;                                               \
            printf("  ECHEC ligne %d : %s\n", __LINE__, #cond);        \
        }                                                              \
    } while (0)

/* Generateur pseudo-aleatoire deterministe (memes resultats partout) */
static uint32_t graine = 12345u;
static uint32_t aleatoire(void)
{
    graine = graine * 1103515245u + 12345u;
    return graine >> 8;
}

/* Encode une trame avec un payload deja prepare */
static size_t trame(uint8_t *sortie, uint8_t type, uint16_t seq, const uint8_t *p, size_t len)
{
    return proto_encoder_trame(sortie, PROTO_MAX_TRAME, type, seq, p, (uint16_t)len);
}

/* Decode tout `flux` d'un coup et range les trames dans `out` */
static int decoder_flux(proto_decodeur_t *d, const uint8_t *flux, size_t n,
                        proto_trame_t *out, int max)
{
    int nb = 0;
    size_t pos = 0;
    while (pos < n) {
        pos += proto_decodeur_ajouter(d, flux + pos, n - pos);
        proto_trame_t t;
        while (proto_decodeur_suivante(d, &t)) {
            if (nb < max) {
                out[nb] = t;
            }
            nb++;
        }
    }
    return nb;
}

static void test_crc(void)
{
    VERIF(proto_crc16(0xFFFF, (const uint8_t *)"123456789", 9) == 0x29B1);
    VERIF(proto_crc16(0xFFFF, NULL, 0) == 0xFFFF);
    /* Calcul incremental = calcul en une fois */
    uint16_t c = proto_crc16(0xFFFF, (const uint8_t *)"1234", 4);
    VERIF(proto_crc16(c, (const uint8_t *)"56789", 5) == 0x29B1);
}

static void test_format_trame(void)
{
    uint8_t p[8], t[PROTO_MAX_TRAME];
    proto_hb_t hb = {125000u};
    size_t lp = proto_encoder_hb(&hb, p, sizeof p);
    VERIF(lp == 4);
    size_t n = trame(t, PROTO_T_HB, 42, p, lp);
    VERIF(n == 13);
    VERIF(t[0] == 0xAA && t[1] == 0x55);
    VERIF(t[2] == 4 && t[3] == 0);          /* LEN */
    VERIF(t[4] == PROTO_T_HB);
    VERIF(t[5] == 42 && t[6] == 0);         /* SEQ */
    VERIF(t[7] == 0x48 && t[8] == 0xE8 && t[9] == 0x01 && t[10] == 0x00); /* 125000 LE */
    uint16_t crc = proto_crc16(0xFFFF, t + 2, 9);
    VERIF(t[11] == (crc & 0xFF) && t[12] == (crc >> 8));

    /* Trop gros ou sortie trop petite */
    static uint8_t gros[PROTO_MAX_PAYLOAD + 1];
    VERIF(proto_encoder_trame(t, sizeof t, PROTO_T_TEST, 0, gros, PROTO_MAX_PAYLOAD + 1) == 0);
    VERIF(proto_encoder_trame(t, 12, PROTO_T_HB, 0, p, 4) == 0);
    /* Payload vide */
    VERIF(trame(t, PROTO_T_PING, 7, NULL, 0) == 9);
}

static void test_messages(void)
{
    uint8_t p[PROTO_MAX_PAYLOAD];
    size_t n;

    proto_hello_t h = {PROTO_VERSION, PROTO_CARTE_WROOM32E, {1, 2, 3}}, h2;
    n = proto_encoder_hello(&h, p, sizeof p);
    VERIF(n == 5 && proto_decoder_hello(p, n, &h2));
    VERIF(h2.version == 2 && h2.carte == 1 && h2.fw[0] == 1 && h2.fw[2] == 3);

    proto_bms_t b, b2;
    memset(&b, 0, sizeof b);
    b.id = 2; b.ok = 1; b.tension_cv = 4860; b.courant_ca = -3240;
    b.soc_dixieme = 750; b.restant_cah = 7350; b.nb_cellules = 15; b.nb_temps = 2;
    for (int i = 0; i < 15; ++i) b.cellules_mv[i] = (uint16_t)(3200 + i);
    b.temps_dixieme[0] = 250; b.temps_dixieme[1] = -55;
    n = proto_encoder_bms(&b, p, sizeof p);
    VERIF(n == 13 + 30 + 1 + 4);
    VERIF(proto_decoder_bms(p, n, &b2));
    VERIF(b2.id == 2 && b2.courant_ca == -3240 && b2.nb_cellules == 15);
    VERIF(b2.cellules_mv[14] == 3214 && b2.temps_dixieme[1] == -55);
    VERIF(!proto_decoder_bms(p, n - 1, &b2));      /* payload tronque */
    b.nb_cellules = PROTO_MAX_CELLULES + 1;
    VERIF(proto_encoder_bms(&b, p, sizeof p) == 0); /* trop de cellules */
    VERIF(proto_encoder_bms(&b2, p, 10) == 0);      /* sortie trop petite */

    proto_temp_t te, te2;
    memset(&te, 0, sizeof te);
    te.nb = 2;
    te.sondes[0].rom[0] = 0x28; te.sondes[0].c_centieme = 2510;
    te.sondes[1].rom[7] = 0x91; te.sondes[1].c_centieme = PROTO_TEMP_INVALIDE;
    n = proto_encoder_temp(&te, p, sizeof p);
    VERIF(n == 1 + 2 * 10 && proto_decoder_temp(p, n, &te2));
    VERIF(te2.sondes[0].rom[0] == 0x28 && te2.sondes[1].rom[7] == 0x91);
    VERIF(te2.sondes[1].c_centieme == PROTO_TEMP_INVALIDE);

    proto_power_t pw = {12900, -1800, -23220}, pw2;
    n = proto_encoder_power(&pw, p, sizeof p);
    VERIF(n == 10 && proto_decoder_power(p, n, &pw2));
    VERIF(pw2.tension_mv == 12900 && pw2.courant_ma == -1800 && pw2.puissance_mw == -23220);

    proto_imu_t im = {-250, 120, 35999, 2, 450}, im2;
    n = proto_encoder_imu(&im, p, sizeof p);
    VERIF(n == 9 && proto_decoder_imu(p, n, &im2));
    VERIF(im2.roulis_cdeg == -250 && im2.cap_cdeg == 35999 && im2.precision == 2 &&
          im2.incertitude_cdeg == 450);
    /* anciens formats : sans l'incertitude, puis sans la precision */
    VERIF(proto_decoder_imu(p, 7, &im2) && im2.precision == 2 &&
          im2.incertitude_cdeg == PROTO_IMU_INCERTITUDE_INCONNUE);
    VERIF(proto_decoder_imu(p, 6, &im2) && im2.precision == PROTO_IMU_PRECISION_INCONNUE &&
          im2.incertitude_cdeg == PROTO_IMU_INCERTITUDE_INCONNUE);

    proto_mouvement_t mv = {-981, 12, 30000, -1234, 56, -32000}, mv2;
    n = proto_encoder_mouvement(&mv, p, sizeof p);
    VERIF(n == 12 && proto_decoder_mouvement(p, n, &mv2));
    VERIF(memcmp(&mv, &mv2, sizeof mv) == 0);
    VERIF(!proto_decoder_mouvement(p, 11, &mv2));

    proto_pilot_t pi = {1, 0x04, 0x08}, pi2;
    n = proto_encoder_pilot(&pi, p, sizeof p);
    VERIF(n == 3 && proto_decoder_pilot(p, n, &pi2) && pi2.boutons == 0x04);

    proto_alert_t al = {PROTO_ALERTE_TEMP_HAUTE, 3, 0x10, 6250}, al2;
    n = proto_encoder_alert(&al, p, sizeof p);
    VERIF(n == 7 && proto_decoder_alert(p, n, &al2) && al2.valeur == 6250 && al2.source == 0x10);

    proto_ack_t ak = {65535, 0, PROTO_ERR_CMD_INCONNUE}, ak2;
    n = proto_encoder_ack(&ak, p, sizeof p);
    VERIF(n == 4 && proto_decoder_ack(p, n, &ak2) && ak2.ref == 65535 && ak2.erreur == 1);

    proto_cmd_t c, c2;
    memset(&c, 0, sizeof c);
    c.commande = PROTO_CMD_SET_BAUD; c.arg.baud.vitesse = 3000000u;
    n = proto_encoder_cmd(&c, p, sizeof p);
    VERIF(n == 5 && proto_decoder_cmd(p, n, &c2) && c2.arg.baud.vitesse == 3000000u);
    c.commande = PROTO_CMD_STREAM; c.arg.stream.taille_payload = 240; c.arg.stream.duree_ms = 3000;
    n = proto_encoder_cmd(&c, p, sizeof p);
    VERIF(n == 7 && proto_decoder_cmd(p, n, &c2));
    VERIF(c2.arg.stream.taille_payload == 240 && c2.arg.stream.duree_ms == 3000);
    c.commande = PROTO_CMD_SET_LED; c.arg.led.index = 2; c.arg.led.allume = 1;
    n = proto_encoder_cmd(&c, p, sizeof p);
    VERIF(n == 3 && proto_decoder_cmd(p, n, &c2) && c2.arg.led.index == 2);
    VERIF(!proto_decoder_cmd(p, 2, &c2));          /* argument manquant */
    c.commande = PROTO_CMD_REBOOT;
    n = proto_encoder_cmd(&c, p, sizeof p);
    VERIF(n == 1 && proto_decoder_cmd(p, n, &c2) && c2.commande == PROTO_CMD_REBOOT);

    proto_stream_fin_t sf = {123456u, 7890123u}, sf2;
    n = proto_encoder_stream_fin(&sf, p, sizeof p);
    VERIF(n == 8 && proto_decoder_stream_fin(p, n, &sf2) && sf2.octets == 7890123u);

    char niv, txt[8];
    n = proto_encoder_log('W', "BMS_2 timeout", p, sizeof p);
    VERIF(n == 14 && proto_decoder_log(p, n, &niv, txt, sizeof txt));
    VERIF(niv == 'W' && strcmp(txt, "BMS_2 t") == 0);  /* tronque proprement */

    uint32_t cpt;
    n = proto_encoder_test(77u, p, 240);
    VERIF(n == 240 && proto_verifier_test(p, n, &cpt) && cpt == 77u);
    p[100] ^= 0x01;
    VERIF(!proto_verifier_test(p, n, &cpt));
    VERIF(proto_encoder_test(1u, p, 3) == 0);
}

static void test_decodeur_simple(void)
{
    uint8_t t[PROTO_MAX_TRAME];
    proto_hb_t hb = {5000u};
    uint8_t p[4];
    size_t n = trame(t, PROTO_T_HB, 9, p, proto_encoder_hb(&hb, p, sizeof p));

    /* Octet par octet */
    proto_decodeur_t d;
    proto_decodeur_init(&d);
    int recues = 0;
    proto_trame_t tr;
    for (size_t i = 0; i < n; ++i) {
        proto_decodeur_ajouter(&d, t + i, 1);
        while (proto_decodeur_suivante(&d, &tr)) {
            recues++;
            proto_hb_t r;
            VERIF(tr.type == PROTO_T_HB && tr.seq == 9);
            VERIF(proto_decoder_hb(tr.payload, tr.len, &r) && r.uptime_ms == 5000u);
        }
    }
    VERIF(recues == 1 && d.erreurs_crc == 0 && d.octets_ignores == 0);
}

static void test_decodeur_robustesse(void)
{
    static uint8_t flux[4096];
    proto_trame_t out[16];
    proto_decodeur_t d;
    size_t n = 0, k;

    /* 1. Parasites avant et entre les trames (y compris de faux AA 55) */
    const uint8_t parasites[] = {0x00, 0xFF, 0xAA, 0x13, 0xAA, 0x55, 0xFF, 0xFF};
    memcpy(flux + n, parasites, sizeof parasites); n += sizeof parasites;
    n += trame(flux + n, PROTO_T_PING, 1, NULL, 0);
    memcpy(flux + n, parasites, sizeof parasites); n += sizeof parasites;
    n += trame(flux + n, PROTO_T_PING, 2, NULL, 0);
    proto_decodeur_init(&d);
    VERIF(decoder_flux(&d, flux, n, out, 16) == 2);
    VERIF(out[0].seq == 1 && out[1].seq == 2);

    /* 2. Octet corrompu : la trame est rejetee, la suivante est recue */
    n = 0;
    k = trame(flux + n, PROTO_T_PING, 1, NULL, 0); flux[n + 4] ^= 0x40; n += k;
    n += trame(flux + n, PROTO_T_PING, 2, NULL, 0);
    proto_decodeur_init(&d);
    VERIF(decoder_flux(&d, flux, n, out, 16) == 1);
    VERIF(out[0].seq == 2 && d.erreurs_crc >= 1);

    /* 3. Trame tronquee (octets perdus) suivie d'une trame complete :
     *    la trame complete ne doit PAS etre avalee par la tronquee. */
    uint8_t p[200];
    n = 0;
    k = trame(flux + n, PROTO_T_TEST, 1, p, proto_encoder_test(1, p, 200));
    n += k - 50;                           /* on "perd" les 50 derniers octets */
    n += trame(flux + n, PROTO_T_TEST, 2, p, proto_encoder_test(2, p, 200));
    n += trame(flux + n, PROTO_T_TEST, 3, p, proto_encoder_test(3, p, 200));
    proto_decodeur_init(&d);
    int nb = decoder_flux(&d, flux, n, out, 16);
    VERIF(nb == 2);
    VERIF(nb == 2 && out[0].seq == 2 && out[1].seq == 3);

    /* 4. AA 55 a l'interieur des donnees : pas de probleme */
    uint8_t pl[6] = {0xAA, 0x55, 0xAA, 0x55, 0x00, 0x00};
    n = trame(flux, PROTO_T_LOG, 5, pl, sizeof pl);
    proto_decodeur_init(&d);
    VERIF(decoder_flux(&d, flux, n, out, 16) == 1 && out[0].len == 6 && out[0].payload[2] == 0xAA);

    /* 5. LEN absurde dans un faux en-tete */
    const uint8_t faux[] = {0xAA, 0x55, 0xFF, 0xFF, 0x01, 0x00, 0x00};
    memcpy(flux, faux, sizeof faux); n = sizeof faux;
    n += trame(flux + n, PROTO_T_PING, 4, NULL, 0);
    proto_decodeur_init(&d);
    VERIF(decoder_flux(&d, flux, n, out, 16) == 1 && out[0].seq == 4);

    /* 6. Trame vide (PING) puis trame maximale */
    static uint8_t gros[PROTO_MAX_PAYLOAD];
    static uint8_t flux2[PROTO_MAX_TRAME * 2];
    n = trame(flux2, PROTO_T_PING, 6, NULL, 0);
    n += trame(flux2 + n, PROTO_T_TEST, 7, gros, proto_encoder_test(7, gros, PROTO_MAX_PAYLOAD));
    proto_decodeur_init(&d);
    nb = decoder_flux(&d, flux2, n, out, 16);
    VERIF(nb == 2 && out[1].len == PROTO_MAX_PAYLOAD);
}

/* Flux continu de 2000 trames de tailles variees, livre en morceaux de
 * tailles aleatoires, avec quelques octets corrompus : on doit recevoir
 * exactement toutes les trames saines, dans l'ordre. */
static void test_decodeur_flux_long(void)
{
    static uint8_t p[PROTO_MAX_PAYLOAD];
    static uint8_t t[PROTO_MAX_TRAME];
    proto_decodeur_t d;
    proto_decodeur_init(&d);

    uint32_t attendu = 0, recues = 0, corrompues = 0, erreurs_contenu = 0, ordre = 0;
    uint32_t dernier = 0;
    int premier = 1;

    for (uint32_t i = 0; i < 2000; ++i) {
        size_t taille = 4 + aleatoire() % 600;
        size_t n = trame(t, PROTO_T_TEST, (uint16_t)i, p, proto_encoder_test(i, p, taille));
        if (i % 97 == 50) {
            t[7 + aleatoire() % taille] ^= 0x20;  /* corruption dans le payload */
            corrompues++;
        } else {
            attendu++;
        }
        size_t pos = 0;
        while (pos < n) {
            size_t morceau = 1 + aleatoire() % 300;
            if (morceau > n - pos) morceau = n - pos;
            size_t acceptes = 0;
            while (acceptes < morceau) {
                acceptes += proto_decodeur_ajouter(&d, t + pos + acceptes, morceau - acceptes);
                proto_trame_t tr;
                while (proto_decodeur_suivante(&d, &tr)) {
                    uint32_t cpt;
                    if (!proto_verifier_test(tr.payload, tr.len, &cpt)) {
                        erreurs_contenu++;
                    }
                    if (!premier && cpt <= dernier) {
                        ordre++;
                    }
                    dernier = cpt;
                    premier = 0;
                    recues++;
                }
            }
            pos += morceau;
        }
    }
    VERIF(recues == attendu);
    VERIF(erreurs_contenu == 0);
    VERIF(ordre == 0);
    VERIF(d.erreurs_crc >= corrompues);
    if (recues != attendu) {
        printf("  (recues %lu / attendues %lu)\n", (unsigned long)recues, (unsigned long)attendu);
    }
}

static void test_vitesses(void)
{
    VERIF(proto_vitesse_valide(115200u));
    VERIF(proto_vitesse_valide(3000000u));
    VERIF(!proto_vitesse_valide(9600u));
    VERIF(!proto_vitesse_valide(0u));
}

int test_protocole_executer(void)
{
    nb_verifs = 0;
    nb_echecs = 0;
    graine = 12345u;
    printf("Tests du protocole UART v%d\n", PROTO_VERSION);
    printf("- CRC\n");              test_crc();
    printf("- format des trames\n"); test_format_trame();
    printf("- messages\n");          test_messages();
    printf("- decodeur simple\n");   test_decodeur_simple();
    printf("- robustesse\n");        test_decodeur_robustesse();
    printf("- flux long\n");         test_decodeur_flux_long();
    printf("- vitesses\n");          test_vitesses();
    printf("RESULTAT : %d verifications, %d echec(s)\n", nb_verifs, nb_echecs);
    return nb_echecs;
}
