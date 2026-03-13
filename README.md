# 🚤 Monitoring Monaco Energy Boat Challenge 2026

Ce projet assure l'acquisition, le stockage et l'affichage en temps réel des données des batteries (BMS). L'objectif est d'offrir une **latence zéro** au pilote tout en sécurisant l'historique de navigation.

---

### 1. Le "Pont" Logiciel (Import Direct)
Contrairement à une architecture classique où l'interface lirait des fichiers sur le disque (lent), nous avons créé un **lien direct en mémoire vive (RAM)**.

* **Fonctionnement :** Dès le lancement de `main_provisoire.py`, l'objet `interface` est importé et reste résident en mémoire.
* **Avantage :** Cela crée un canal de communication direct : le script de lecture n'a pas besoin de "chercher" l'affichage, il **pousse** la donnée dedans dès qu'elle est décodée.

---

### 2. Traitement Parallèle : Sécurité + Vitesse
Dans le script `bms_rasp.py`, chaque réception de donnée déclenche une **double action immédiate** :

1. **Action A (Stockage) :** Écriture dans le fichier `.json` pour conserver une "boîte noire" de la course.
2. **Action B (Affichage) :** Appel de la fonction `interface.update_bms()`.

**Efficacité :** Le programme n'attend pas la fin de l'écriture sur le disque (opération lente) pour mettre à jour l'écran. La donnée passe de l'USB à l'affichage en quelques millisecondes.

---

### 3. Gestion Asynchrone (Le Multi-tâches)
L'utilisation de la bibliothèque `asyncio` est le cœur de la réactivité :

* **Non-bloquant :** Si un BMS met du temps à répondre, le processeur ne reste pas figé. Il continue de traiter l'autre BMS et de rafraîchir l'interface.
* **Fluidité :** L'interface reste interactive (le bouton "QUITTER" ou la touche "Echap" répondent instantanément).

---

### 4. Synthèse des flux de données

| Étape | Action | Vitesse |
| :--- | :--- | :--- |
| **1. Réception** | Le BMS envoie les octets bruts via USB | Ultra-rapide |
| **2. Décodage** | Traduction en Volts, Ampères et Températures | Instantané |
| **3. Dispatch** | Envoi simultané vers le JSON et l'Interface | < 10ms |
| **4. Affichage** | Rafraîchissement du tableau de bord (Tkinter) | Temps réel |

---

**Note technique :** Shout out to Albert le GOAT et Emile (un peu moins).
