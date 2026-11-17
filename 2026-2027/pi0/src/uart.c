/* Port serie Linux (Raspberry Pi) en mode brut. */
#include "uart.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int vitesse_termios(uint32_t vitesse, speed_t *code)
{
    switch (vitesse) {
    case 115200: *code = B115200; return 0;
    case 230400: *code = B230400; return 0;
    case 460800: *code = B460800; return 0;
    case 921600: *code = B921600; return 0;
#ifdef B1000000
    case 1000000: *code = B1000000; return 0;
#endif
#ifdef B1500000
    case 1500000: *code = B1500000; return 0;
#endif
#ifdef B2000000
    case 2000000: *code = B2000000; return 0;
#endif
#ifdef B2500000
    case 2500000: *code = B2500000; return 0;
#endif
#ifdef B3000000
    case 3000000: *code = B3000000; return 0;
#endif
    default: return -1;
    }
}

int uart_changer_vitesse(int fd, uint32_t vitesse)
{
    speed_t code;
    struct termios tio;
    if (vitesse_termios(vitesse, &code) != 0 || tcgetattr(fd, &tio) != 0) {
        return -1;
    }
    cfsetispeed(&tio, code);
    cfsetospeed(&tio, code);
    return tcsetattr(fd, TCSANOW, &tio) == 0 ? 0 : -1;
}

int uart_ouvrir(const char *chemin, uint32_t vitesse)
{
    const int fd = open(chemin, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "Impossible d'ouvrir %s : %s\n", chemin, strerror(errno));
        return -1;
    }
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        fprintf(stderr, "%s n'est pas un port serie : %s\n", chemin, strerror(errno));
        close(fd);
        return -1;
    }
    cfmakeraw(&tio);                       /* 8 bits, pas d'echo, pas de traduction */
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(tcflag_t)(CSTOPB | PARENB | CRTSCTS);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tio) != 0 || uart_changer_vitesse(fd, vitesse) != 0) {
        fprintf(stderr, "Configuration de %s a %u bauds impossible\n", chemin, (unsigned)vitesse);
        close(fd);
        return -1;
    }
    uart_vider_reception(fd);
    return fd;
}

ssize_t uart_lire(int fd, uint8_t *tampon, size_t taille, int timeout_ms)
{
    struct pollfd pfd = {fd, POLLIN, 0};
    const int r = poll(&pfd, 1, timeout_ms);
    if (r < 0) {
        return errno == EINTR ? 0 : -1;
    }
    if (r == 0) {
        return 0;
    }
    const ssize_t n = read(fd, tampon, taille);
    if (n < 0) {
        return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    }
    return n;
}

int uart_ecrire(int fd, const uint8_t *data, size_t len)
{
    size_t envoye = 0;
    while (envoye < len) {
        const ssize_t n = write(fd, data + envoye, len - envoye);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                struct pollfd pfd = {fd, POLLOUT, 0};
                poll(&pfd, 1, 100);
                continue;
            }
            return -1;
        }
        envoye += (size_t)n;
    }
    return 0;
}

void uart_attendre_emission(int fd)
{
    tcdrain(fd);
}

void uart_vider_reception(int fd)
{
    tcflush(fd, TCIFLUSH);
}

void uart_fermer(int fd)
{
    close(fd);
}
