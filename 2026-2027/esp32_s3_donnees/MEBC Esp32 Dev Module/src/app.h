/*
 * Application ESP32 donnees (C pur).
 * La partie materielle (port serie, horloge...) est fournie par main.cpp
 * via les fonctions plateforme_xxx().
 */
#ifndef APP_H
#define APP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_init(void);
void app_octets_recus(const uint8_t *data, size_t len);  /* octets venant de la Pi */
void app_tick(void);                                     /* a appeler en boucle */

/* --- Fournies par la plateforme (main.cpp) --- */
uint32_t plateforme_millis(void);
void plateforme_ecrire(const uint8_t *data, size_t len); /* vers la Pi */
size_t plateforme_place_tx(void);                        /* octets libres en emission */
void plateforme_attendre_fin_emission(void);
void plateforme_changer_vitesse(uint32_t vitesse);
void plateforme_redemarrer(void);
void plateforme_log(const char *format, ...);            /* debug sur l'USB */

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
