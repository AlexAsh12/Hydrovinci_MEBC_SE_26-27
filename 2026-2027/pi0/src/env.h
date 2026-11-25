/* Lecture du fichier .env (secrets locaux, jamais versionne).
 * Format : une ligne CLE=valeur, valeur eventuellement entre ' ou ".
 * '#' en debut de ligne = commentaire. */
#ifndef ENV_H
#define ENV_H

#include <stddef.h>

/* Cherche CLE dans le fichier. Renvoie 1 et copie la valeur si trouvee,
 * 0 si absente ou fichier illisible (valeur = chaine vide). */
int env_lire(const char *chemin, const char *cle, char *valeur, size_t taille);

/* Meme chose a partir d'un texte deja en memoire (pour les tests). */
int env_chercher(const char *texte, const char *cle, char *valeur, size_t taille);

/* Decoupe une liste "a, b;c d" en elements ("a", "b", "c d") : separateurs
 * ',' et ';', espaces autour retires. Renvoie le nombre d'elements
 * copies (au plus max), chacun tronque a taille_elt - 1 caracteres. */
int env_decouper_liste(const char *liste, char *elements, size_t taille_elt, int max);

#endif /* ENV_H */
