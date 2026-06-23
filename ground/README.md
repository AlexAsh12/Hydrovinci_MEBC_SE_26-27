# MEBC Ground Dashboard

Interface web de supervision du BMS (Battery Management System) du bateau MEBC.
Affiche en temps reel l'etat de la batterie, les tensions des cellules, la puissance
electrique, la navigation, les statuts (BMS / SIM7600 / MQTT) et la console du Pi.

---

## Prerequis

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) installe et demarre
- (Google Drive, optionnel) le fichier `client.json` fourni par Raphael
- (Google Drive, optionnel) votre adresse Gmail communiquee a Raphael

---

## Installation (premiere fois)

### 1. Recuperer les fichiers

Cloner le depot et se placer dans le dossier `ground/`.

### 2. Construire l'image Docker

```bash
docker compose build
```

### 3. (Optionnel) S'authentifier sur Google Drive

Seulement si vous voulez la synchro automatique des CSV sur le Drive de l'asso.

> **Important** : avant cette etape, envoyez votre adresse Gmail a Raphael afin
> qu'il vous ajoute comme utilisateur de test sur l'application OAuth. Sans cela,
> Google refuse la connexion.

Placer le `client.json` fourni dans un dossier `credentials/` :

```
ground/
|-- credentials/
|   |-- client.json     <- fourni par Raphael
|-- src/
|-- Dockerfile
|-- docker-compose.yml
```

Puis lancer l'authentification (une seule fois par machine) :

```bash
docker compose --profile auth run --service-ports auth
```

Un lien s'affiche dans le terminal. Le copier dans le navigateur, se connecter avec
le compte Google autorise, accepter l'acces : la page se ferme. Le fichier
`credentials/token.json` est cree.

Sans Google Drive, le dashboard fonctionne quand meme : les CSV restent en local
dans `data/sessions/`.

---

## Utilisation quotidienne

### Demarrer le dashboard

```bash
docker compose up -d
```

Ouvrir ensuite [http://localhost:8050](http://localhost:8050).

### Arreter le dashboard

```bash
docker compose down
```

### Voir les logs du conteneur

```bash
docker compose logs -f dashboard
```

---

## Ce que montre le dashboard

- **Cartes principales** : SOC, tension/courant/puissance, vitesse, autonomie, temperature, alarmes
- **Tension par cellule** : barres des 15 cellules + repartition + historique de capacite
- **Zone Statuts** : BMS_1, BMS_2, SIM7600, MQTT (pastille verte = connecte, grise = non)
- **Terminal Pi** : la console du Pi en direct (les erreurs apparaissent en rouge,
  la ligne la plus recente en haut)

---

## Structure des donnees

Les donnees sont sauvegardees localement dans le dossier `data/` :

```
data/
|-- sessions/
|   |-- session_AAAA-MM-JJ.csv   <- une ligne toutes les 5 secondes
|-- state.json                   <- dernier etat BMS connu
|-- history.json                 <- historique capacite residuelle
```

Si Google Drive est configure, les CSV sont aussi synchronises automatiquement
toutes les 5 minutes.

---

## Comment ca fonctionne

```
Pi Zero (embarque)
    |  publie sur MQTT (broker.hivemq.com)
    v
Ground Dashboard (Docker)
    |-- transmission.py   - recoit les donnees MQTT et maintient l'etat en memoire
    |-- data_logger.py    - enregistre une ligne CSV toutes les 5 s + synchro Drive
    |-- callbacks.py      - Dash rafraichit l'interface toutes les secondes
    |-- layout.py         - structure de la page (cartes, statuts, terminal)
    |-- google_drive.py   - upload le CSV du jour vers le Drive partage
```

Le dashboard est une application [Dash](https://dash.plotly.com/) (Python) qui
s'actualise toutes les **1 seconde**. Les donnees transitent par MQTT sur le broker
public HiveMQ (`broker.hivemq.com:1883`). Point d'entree : `src/dashboard.py`.

---

## Problemes frequents

**Le dashboard affiche "Deconnecte"**
-> Le sol n'est pas connecte au broker MQTT. Verifier la connexion internet du PC.

**Le terminal Pi reste vide**
-> Le Pi n'est pas connecte au MQTT, ou il n'a rien affiche depuis la connexion.

**`[drive] Erreur upload : invalid_grant`**
-> Le token Google a expire. Relancer l'authentification :

```bash
rm credentials/token.json
docker compose --profile auth run --service-ports auth
```

**Le port 8050 est deja utilise**
-> Changer le port dans `docker-compose.yml` : remplacer `"8050:8050"` par
`"8051:8050"`, puis acceder a [http://localhost:8051](http://localhost:8051).

---

## Mettre a jour (apres tout changement de code)

> **Important** : le code source est COPIE dans l'image Docker au moment du `build`.
> Un `git pull` seul ne suffit PAS : `docker compose up` relance l'ANCIENNE image
> et donc l'ancien code. Apres chaque modification du code (ou `git pull`), il faut
> TOUJOURS reconstruire l'image avec `--build`, sinon les changements n'apparaissent pas.

```bash
git pull                       # recuperer le nouveau code
docker compose up -d --build   # reconstruire l'image PUIS relancer
```

Pour verifier que l'image a bien ete reconstruite :

```bash
docker images mebc-ground-dashboard
```

La colonne `CREATED` doit afficher "seconds/minutes ago" (pas "7 days ago").

---

## Contact

Pour tout probleme ou pour etre ajoute aux utilisateurs Google Drive :
**raphaelraphaelmarques@gmail.com**
