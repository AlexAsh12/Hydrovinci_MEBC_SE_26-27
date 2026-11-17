/*
 * Sondes de temperature DS18B20 sur un bus 1-Wire, en C pur.
 * Les creneaux du bus (reset, ecriture / lecture d'un bit) sont fournis
 * par la plateforme : plateforme_1w_reset() et plateforme_1w_bit().
 *
 * Fonctionnement non bloquant : une mesure de toutes les sondes par seconde.
 * Les sondes sont recherchees au demarrage puis toutes les 10 s (on peut en
 * brancher ou en retirer sans redemarrer).
 */
#ifndef DS18B20_H
#define DS18B20_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS18B20_MAX_SONDES 16
#define DS18B20_INVALIDE INT16_MIN

typedef struct {
    uint8_t rom[8];       /* adresse 1-Wire unique */
    int16_t c_centieme;   /* en 0,01 degC, DS18B20_INVALIDE si lecture ratee */
} ds18b20_sonde_t;

void ds18b20_init(void);
void ds18b20_tick(uint32_t maintenant);

/* Vrai une fois a la fin de chaque cycle de mesure (resultats a jour) */
bool ds18b20_cycle_termine(void);
int ds18b20_nb(void);
const ds18b20_sonde_t *ds18b20_sonde(int i);
uint32_t ds18b20_erreurs(void);

#ifdef __cplusplus
}
#endif

#endif /* DS18B20_H */
