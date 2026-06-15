# MEBC Ground Dashboard

Interface web de supervision du BMS (Battery Management System) du voilier MEBC.
Affiche en temps r├®el l'├®tat de la batterie, les tensions des cellules, la puissance ├®lectrique et la navigation.

---

## Pr├®requis

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) install├® et d├®marr├®
- Le fichier `client.json` fourni par Rapha├½l
- Votre adresse Gmail communiqu├®e ├á Rapha├½l (voir section Google Drive ci-dessous)

---

## Installation (premi├¿re fois)

### 1. R├®cup├®rer les fichiers

T├®l├®chargez ou clonez ce dossier `Ground/` sur votre machine.

### 2. Placer le fichier client.json

Cr├®ez un dossier `credentials/` ├á la racine du projet et placez-y le fichier `client.json` fourni par Rapha├½l :

```
Ground/
Ôö£ÔöÇÔöÇ credentials/
Ôöé   ÔööÔöÇÔöÇ client.json     ÔåÉ fichier fourni par Rapha├½l
Ôö£ÔöÇÔöÇ src/
Ôö£ÔöÇÔöÇ Dockerfile
ÔööÔöÇÔöÇ docker-compose.yml
```

### 3. Construire l'image Docker

```bash
docker compose build
```

### 4. S'authentifier sur Google Drive (obligatoire pour la synchro des donn├®es)

> **Important** : avant de faire cette ├®tape, envoyez votre adresse Gmail ├á Rapha├½l
> afin qu'il vous ajoute en tant qu'utilisateur de test sur l'application OAuth.
> Sans cela, Google refusera la connexion.

Une fois que Rapha├½l vous a confirm├® l'ajout, lancez :

```bash
docker compose --profile auth run --service-ports auth
```

Un lien s'affiche dans le terminal. Copiez-le et ouvrez-le dans votre navigateur.
Connectez-vous avec votre compte Google ÔåÆ autorisez l'acc├¿s ÔåÆ la page se ferme automatiquement.

Le fichier `credentials/token.json` est cr├®├®. **Cette ├®tape n'est ├á faire qu'une seule fois par machine.**

---

## Utilisation quotidienne

### D├®marrer le dashboard

```bash
docker compose up -d
```

Ouvrez ensuite [http://localhost:8050](http://localhost:8050) dans votre navigateur.

### Arr├¬ter le dashboard

```bash
docker compose down
```

### Voir les logs en direct

```bash
docker compose logs -f dashboard
```

---

## Structure des donn├®es

Les donn├®es sont sauvegard├®es localement dans le dossier `data/` :

```
data/
Ôö£ÔöÇÔöÇ sessions/
Ôöé   ÔööÔöÇÔöÇ session_YYYY-MM-DD.csv   ÔåÉ une ligne par seconde
Ôö£ÔöÇÔöÇ state.json                   ÔåÉ dernier ├®tat BMS connu
ÔööÔöÇÔöÇ history.json                 ÔåÉ historique capacit├® r├®siduelle
```

Les fichiers CSV sont aussi synchronis├®s automatiquement sur Google Drive toutes les 5 minutes
(si `credentials/token.json` est pr├®sent).

---

## Comment ├ºa fonctionne

```
BMS (embarqu├®)
    Ôöé  publie sur MQTT (broker.hivemq.com)
    Ôû╝
Ground Dashboard (Docker)
    Ôö£ÔöÇÔöÇ transmission.py   ÔÇö re├ºoit les donn├®es MQTT et maintient l'├®tat en m├®moire
    Ôö£ÔöÇÔöÇ data_logger.py    ÔÇö enregistre une ligne CSV par seconde + synchro Drive
    Ôö£ÔöÇÔöÇ callbacks.py      ÔÇö Dash rafra├«chit l'interface toutes les secondes
    ÔööÔöÇÔöÇ google_drive.py   ÔÇö upload le CSV du jour vers le Drive partag├®
```

Le dashboard est une application [Dash](https://dash.plotly.com/) (Python) qui s'actualise toutes les **1 seconde**.
Les donn├®es transitent par MQTT sur le broker public HiveMQ (`broker.hivemq.com:1883`).

---

## Probl├¿mes fr├®quents

**Le dashboard affiche "D├®connect├®"**
ÔåÆ Le BMS n'envoie pas de donn├®es MQTT. V├®rifiez que l'embarqu├® est allum├® et connect├® ├á internet.

**`[drive] Erreur upload : invalid_grant`**
ÔåÆ Votre token Google a expir├®. Relancez l'authentification :

```bash
rm credentials/token.json
docker compose --profile auth run --service-ports auth
```

**Le port 8050 est d├®j├á utilis├®**
ÔåÆ Changez le port dans `docker-compose.yml` : remplacez `"8050:8050"` par ex. `"8051:8050"`,
puis acc├®dez au dashboard sur [http://localhost:8051](http://localhost:8051).

---

## Mettre ├á jour

```bash
docker compose down
git pull
docker compose build
docker compose up -d
```

---

## Contact

Pour tout probl├¿me ou pour ├¬tre ajout├® aux utilisateurs Google Drive : **raphaelraphaelmarques@gmail.com**
