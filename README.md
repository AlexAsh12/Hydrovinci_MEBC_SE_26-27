# MEBC_SE - Monaco Energy Boat Challenge 2026

Systeme embarque de telemetrie et controle pour bateau electrique solaire.

## Architecture

```
+----------------------------------+         MQTT           +-------------------------+
|         Pi Zero (Embarque)       |    (broker.hivemq.com) |     PC Ground (Sol)     |
|                                  |                        |                         |
|  acquisition/bms_rasp.py         |                        |  ground/src/            |
|    - Lit 2x BMS XDZN via USB    +------- publish ------->|    transmission.py      |
|    - Decode les trames           |  mebc_se/bms/...       |    data_logger.py       |
|                                  |                        |    Dashboard Dash/Plotly |
|  telemetrie/mqtt_publisher.py    |                        |    Google Drive upload   |
|    - Publie les donnees BMS      |                        |                         |
|                                  |                        |  Acces : localhost:8050  |
|  telemetrie/sms.py               |                        +-------------------------+
|    - Reception SMS SIM7600       |
|                                  |
|  telemetrie/appel.py             |
|    - Appel vocal SIM7600         |
|                                  |
|  interface/interface_bateau.py   |
|    - Affichage Tkinter pilote    |
|                                  |
|  main.py (orchestrateur)         |
+----------------------------------+
```

## Arborescence

```
MEBC_SE/
|-- main.py                  # Point d'entree - lance tous les threads
|-- config.py                # Constantes centralisees (ports, BMS, MQTT, GPIO)
|-- requirements.txt         # Dependances Python (Pi)
|
|-- acquisition/             # Lecture des capteurs
|   |-- bms_rasp.py          # Monitoring BMS XDZN (2 batteries)
|
|-- telemetrie/              # Communication
|   |-- sms.py               # Reception SMS via SIM7600
|   |-- appel.py             # Appel vocal via SIM7600
|   |-- mqtt_publisher.py    # Publication MQTT vers le dashboard
|
|-- interface/               # Affichage
|   |-- interface_bateau.py  # Interface Tkinter pour le pilote
|
|-- hardware/                # GPIO
|   |-- reboot_button.py     # Bouton de reboot (a integrer)
|
|-- ground/                  # Dashboard PC (code de Raphael)
|   |-- Dockerfile
|   |-- docker-compose.yml
|   |-- src/
|       |-- dashboard.py     # Point d'entree dashboard
|       |-- transmission.py  # Client MQTT recepteur
|       |-- data_logger.py   # Logging CSV + Google Drive
|       |-- layout.py        # Interface Dash/Plotly
|       |-- callbacks.py     # Logique du dashboard
|       |-- google_drive.py  # Upload vers Drive
|       |-- assets/dashboard.css
```

## Demarrage rapide

### 1. Cote Pi Zero (embarque)

```bash
# Installer les dependances
pip install -r requirements.txt

# Lancer le systeme
python main.py
```

Au demarrage, le systeme :
1. Detecte le port AT du SIM7600
2. Lance le diagnostic SIM (carte detectee ? reseau ?)
3. Se connecte au broker MQTT (si disponible)
4. Lance les threads : BMS, SMS, clavier, interface

### 2. Cote PC Ground (dashboard)

**Prerequis :** Installer [Docker Desktop](https://www.docker.com/products/docker-desktop/)

```bash
cd ground

# Lancer le dashboard
docker compose up

# Ouvrir dans le navigateur
# http://localhost:8050
```

Le dashboard affiche en temps reel :
- Indicateurs BMS_1 / BMS_2 (vert = detecte, rouge = non detecte)
- SOC (etat de charge)
- Tensions par cellule (15 cellules)
- Courant, puissance, temperature
- Historique de capacite
- Alarmes

### 3. Google Drive (optionnel)

Pour activer l'enregistrement automatique sur Google Drive :

```bash
cd ground
docker compose --profile auth run --service-ports auth
```

Suivre les instructions a l'ecran pour l'authentification OAuth.
Demander l'acces au dossier Drive partage a Raphael (Debian).

Si Google Drive n'est pas configure, le dashboard fonctionne quand meme. Les CSV sont enregistres en local dans `ground/data/sessions/`.

## Topics MQTT

| Topic | Donnee | Format |
|---|---|---|
| `mebc_se/bms/cells/voltages` | Tensions cellules | JSON list `[3.24, 3.22, ...]` |
| `mebc_se/bms/pack/voltage` | Tension pack | float `48.6` |
| `mebc_se/bms/pack/current` | Courant | float `-32.4` |
| `mebc_se/bms/pack/soc` | Etat de charge | float `75.0` |
| `mebc_se/bms/pack/capacity` | Capacite totale | float `98.0` |
| `mebc_se/bms/pack/remaining` | Capacite restante | float `73.5` |
| `mebc_se/bms/pack/temperature` | Temperatures | JSON list `[25.0, 26.1]` |
| `mebc_se/bms/alarms` | Alarmes actives | JSON list `[]` |
| `mebc_se/bms/connection` | Etat connexion BMS | JSON `{"BMS_1": true, "BMS_2": false}` |

Broker : `broker.hivemq.com` (public, port 1883)

## Securite

- Le MQTT est un bonus : si le broker est injoignable, le BMS tourne normalement
- Aucun stockage local sur le Pi Zero (tout part en MQTT)
- Les credentials Google Drive ne sont PAS versionnes (`.gitignore`)
- Le broker HiveMQ est public : les donnees ne sont pas chiffrees

---

## Guide pas a pas : mise en route complete

### Etape 1 : Cloner le projet

Sur le Pi Zero ET sur le PC au sol, ouvrir un terminal :

```bash
git clone https://github.com/Gusleboss01/MEBC_SE.git
cd MEBC_SE
```

### Etape 2 : Configurer le Pi Zero

1. Brancher les 2 BMS en USB sur le Pi (via un hub USB si Pi Zero)
2. Brancher le module SIM7600 en USB sur le Pi
3. Les ports sont detectes automatiquement (SIM7600) ou par identifiant hardware (BMS)
4. Installer les dependances Python :

```bash
pip install -r requirements.txt
```

5. Lancer le systeme :

```bash
python main.py
```

6. Verifier dans le terminal :
   - `Carte SIM detectee` (ou `Pas de reponse` si pas de carte SIM - c'est pas grave)
   - `[MQTT] Connecte au broker` (ou `Impossible de se connecter` - c'est pas grave non plus)
   - `thread BMS lance`
   - `thread SMS lance`
   - `thread interface lance (Thread Principal)`
   - L'ecran Tkinter s'affiche

### Etape 3 : Installer Docker Desktop sur le PC au sol

1. Aller sur https://www.docker.com/products/docker-desktop/
2. Cliquer sur "Download for Windows" (ou Mac/Linux selon votre PC)
3. Lancer l'installateur et suivre les etapes (tout par defaut)
4. Redemarrer le PC si demande
5. Lancer Docker Desktop (icone baleine sur le bureau)
6. Attendre que le message "Docker Desktop is running" apparaisse

### Etape 4 : Lancer le dashboard

1. Ouvrir PowerShell (ou Terminal)
2. Se placer dans le dossier du projet :

```bash
cd C:\chemin\vers\MEBC_SE\ground
```

3. Lancer le dashboard :

```bash
docker compose up
```

4. La premiere fois, Docker telecharge les dependances (ca prend 2-3 minutes)
5. Quand vous voyez `Dash is running on http://0.0.0.0:8050/` : c'est pret
6. Ouvrir un navigateur (Chrome, Firefox...) et aller sur :

```
http://localhost:8050
```

7. Le dashboard s'affiche. Si le Pi est allume et connecte a internet, les donnees apparaissent en temps reel.

### Etape 5 : Google Drive (optionnel)

Seulement si vous voulez que les donnees de course soient sauvegardees automatiquement sur le Drive de l'asso.

1. Demander a Raphael :
   - L'acces en ecriture au dossier Google Drive partage
   - Le fichier `client.json`

2. Creer le dossier `ground/credentials/` et y mettre le fichier `client.json`

3. Lancer l'authentification (une seule fois) :

```bash
cd C:\chemin\vers\MEBC_SE\ground
docker compose --profile auth run --service-ports auth
```

4. Un lien s'affiche dans le terminal. Le copier et l'ouvrir dans le navigateur.
5. Se connecter avec le compte Google qui a acces au Drive partage.
6. Autoriser l'acces. La page se ferme automatiquement.
7. Le terminal affiche `Authentification reussie`. C'est fait.

A partir de maintenant, chaque session de course sera automatiquement sauvegardee dans le dossier Drive partage sous forme de fichier CSV.

### Depannage

| Probleme | Solution |
|---|---|
| Le dashboard affiche des donnees mais rien ne bouge | Le Pi n'envoie pas de donnees. Verifier qu'il est allume et connecte a internet |
| Les points BMS_1 / BMS_2 sont rouges | Les batteries ne sont pas detectees. Verifier les branchements USB |
| `docker compose up` ne marche pas | Docker Desktop n'est peut-etre pas lance. Ouvrir l'application Docker Desktop |
| `[MQTT] Impossible de se connecter` sur le Pi | Pas d'internet sur le Pi. Le BMS tourne quand meme, mais le dashboard ne recoit rien |
| Le dashboard affiche `Deconnecte` | Le Pi n'est pas connecte au broker MQTT. Verifier la connexion internet du Pi |
