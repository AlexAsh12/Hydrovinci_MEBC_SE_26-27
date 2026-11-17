/* Port serie Linux (Raspberry Pi) en mode brut, 8N1, sans controle de flux. */
#ifndef UART_H
#define UART_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Ouvre le port. Renvoie le descripteur, ou -1 (message sur stderr). */
int uart_ouvrir(const char *chemin, uint32_t vitesse);
/* Change la vitesse. Renvoie 0 si OK, -1 si la vitesse n'est pas geree. */
int uart_changer_vitesse(int fd, uint32_t vitesse);
/* Attend au plus timeout_ms puis lit ce qui est disponible.
 * Renvoie le nombre d'octets lus, 0 si rien, -1 si erreur. */
ssize_t uart_lire(int fd, uint8_t *tampon, size_t taille, int timeout_ms);
/* Ecrit tout le tampon. Renvoie 0 si OK, -1 si erreur. */
int uart_ecrire(int fd, const uint8_t *data, size_t len);
/* Attend que tout soit parti sur le fil. */
void uart_attendre_emission(int fd);
/* Jette tout ce qui a ete recu et pas encore lu. */
void uart_vider_reception(int fd);
void uart_fermer(int fd);

#endif /* UART_H */
