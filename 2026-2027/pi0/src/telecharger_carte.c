/*
 * Telecharge les tuiles Plan IGN des zones de carte/zones.conf, pour la carte
 * de l'ecran (sans Internet sur l'eau). A lancer avec Internet (Wi-Fi), sur
 * la Pi ou un PC Linux, apres avoir ajoute une zone :
 *
 *   ./telecharger_carte                 toutes les zones (tuiles manquantes seulement)
 *   ./telecharger_carte --zone monaco   une seule zone
 *   ./telecharger_carte --compter       nombre de tuiles, sans rien telecharger
 *
 * Utilise la commande curl. Si pngquant est installe (sudo apt install
 * pngquant), chaque tuile est reduite a 64 couleurs : environ 3 fois plus
 * legere, sans difference visible.
 *
 * Source : Geoplateforme IGN (Plan IGN v2), donnees ouvertes. Une requete
 * toutes les 150 ms au plus, pour ne pas surcharger le service.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "carte_zones.h"

#define MAX_RECTS 64
#define TAILLE_MAX_CONF 65536
#define PAUSE_MS 150

static const char *const URL_IGN =
    "https://data.geopf.fr/wmts?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0"
    "&LAYER=GEOGRAPHICALGRIDSYSTEMS.PLANIGNV2&STYLE=normal&TILEMATRIXSET=PM"
    "&TILEMATRIX=%d&TILEROW=%d&TILECOL=%d&FORMAT=image/png";

/* Lance une commande (sans shell) et attend sa fin. 0 si elle a reussi. */
static int executer(char *const argv[], int silencieux)
{
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (silencieux) {
            const int nul = open("/dev/null", O_WRONLY);
            if (nul >= 0) {
                dup2(nul, STDOUT_FILENO);
                dup2(nul, STDERR_FILENO);
                close(nul);
            }
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int statut;
    while (waitpid(pid, &statut, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(statut) ? WEXITSTATUS(statut) : -1;
}

static int existe(const char *chemin, long *taille)
{
    struct stat st;
    if (stat(chemin, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) return 0;
    if (taille) *taille = (long)st.st_size;
    return 1;
}

/* mkdir -p du dossier parent du fichier */
static void creer_dossiers(const char *fichier)
{
    char d[512];
    snprintf(d, sizeof d, "%s", fichier);
    for (char *p = d + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(d, 0755);
            *p = '/';
        }
    }
}

static int est_png(const char *chemin)
{
    static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    unsigned char debut[8];
    FILE *f = fopen(chemin, "rb");
    if (!f) return 0;
    const size_t n = fread(debut, 1, sizeof debut, f);
    fclose(f);
    return n == sizeof debut && memcmp(debut, signature, sizeof debut) == 0;
}

static void pause_ms(int ms)
{
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {
    }
}

int main(int argc, char **argv)
{
    const char *dossier = "carte";
    const char *zone = NULL;
    int compter = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--zone") && i + 1 < argc) {
            zone = argv[++i];
        } else if (!strcmp(argv[i], "--dossier") && i + 1 < argc) {
            dossier = argv[++i];
        } else if (!strcmp(argv[i], "--compter")) {
            compter = 1;
        } else {
            printf("Usage : %s [--zone NOM] [--dossier carte] [--compter]\n", argv[0]);
            return strcmp(argv[i], "--aide") == 0 ? 0 : 2;
        }
    }

    char chemin[512];
    snprintf(chemin, sizeof chemin, "%s/zones.conf", dossier);
    static char texte[TAILLE_MAX_CONF + 1];
    FILE *f = fopen(chemin, "r");
    if (!f) {
        fprintf(stderr, "Impossible de lire %s : %s\n", chemin, strerror(errno));
        return 1;
    }
    texte[fread(texte, 1, TAILLE_MAX_CONF, f)] = '\0';
    fclose(f);

    cz_rect_t rects[MAX_RECTS];
    int invalides = 0;
    const int nb = cz_lire(texte, rects, MAX_RECTS, &invalides);
    if (invalides) fprintf(stderr, "%s : %d ligne(s) invalide(s) ignoree(s)\n", chemin, invalides);

    char *const test_pngquant[] = {"pngquant", "--version", NULL};
    const int pngquant = !compter && executer(test_pngquant, 1) == 0;
    if (!compter && !pngquant) printf("pngquant absent : tuiles gardees telles quelles (plus lourdes)\n");

    long nouvelles = 0, deja = 0, erreurs = 0, octets = 0;
    for (int r = 0; r < nb; ++r) {
        if (zone && strcmp(zone, rects[r].nom) != 0) continue;
        long dans_rect = 0;
        for (int z = rects[r].zmin; z <= rects[r].zmax; ++z) {
            int x0, y0, x1, y1;
            cz_etendue(&rects[r], z, &x0, &y0, &x1, &y1);
            for (int x = x0; x <= x1; ++x) {
                for (int y = y0; y <= y1; ++y) {
                    dans_rect++;
                    char fichier[512], temporaire[512], url[512];
                    snprintf(fichier, sizeof fichier, "%s/tuiles/%d/%d/%d.png", dossier, z, x, y);
                    long taille = 0;
                    if (existe(fichier, &taille)) {
                        deja++;
                        octets += taille;
                        continue;
                    }
                    if (compter) {
                        nouvelles++;
                        continue;
                    }
                    creer_dossiers(fichier);
                    /* Nom jamais servi par tableau_bord (pas que des chiffres) */
                    snprintf(temporaire, sizeof temporaire, "%s/tuiles/%d/%d/%d.tmp.png", dossier, z, x, y);
                    snprintf(url, sizeof url, URL_IGN, z, y, x);
                    char *const curl[] = {"curl", "-sSf", "--max-time", "20", "-A", "Hydrovinci-tableau-de-bord",
                                          "-o", temporaire, url, NULL};
                    if (executer(curl, 0) != 0 || !est_png(temporaire)) {
                        fprintf(stderr, "echec tuile %d/%d/%d\n", z, x, y);
                        unlink(temporaire);
                        erreurs++;
                    } else {
                        if (pngquant) {
                            /* Remplace le fichier sur place (--ext .png) */
                            char *const reduire[] = {"pngquant", "--force", "--skip-if-larger", "--ext", ".png",
                                                     "64",       "--",      temporaire,         NULL};
                            executer(reduire, 1);  /* en cas d'echec, la tuile d'origine reste */
                        }
                        if (rename(temporaire, fichier) == 0 && existe(fichier, &taille)) {
                            nouvelles++;
                            octets += taille;
                        } else {
                            unlink(temporaire);
                            erreurs++;
                        }
                    }
                    pause_ms(PAUSE_MS);
                }
            }
        }
        printf("%-12s %.4f..%.4f / %.4f..%.4f  zoom %d-%d : %ld tuiles\n", rects[r].nom, rects[r].sud, rects[r].nord,
               rects[r].ouest, rects[r].est, rects[r].zmin, rects[r].zmax, dans_rect);
    }

    if (compter) {
        printf("%ld tuile(s) a telecharger, %ld deja presente(s)\n", nouvelles, deja);
    } else {
        printf("%ld telechargee(s), %ld deja presente(s), %ld echec(s) ; %ld ko au total\n", nouvelles, deja, erreurs,
               octets / 1024);
        if (nouvelles) printf("Puis : sudo systemctl restart tableau-bord (et git add carte/ pour partager)\n");
    }
    return erreurs ? 1 : 0;
}
