# MEBC_SE - Monaco Energy Boat Challenge 2026

Systeme embarque de telemetrie et controle pour bateau electrique solaire.

Le Pi Zero embarque lit les batteries (BMS), affiche l'etat au pilote (ecran Tkinter),
gere le SIM7600 (SMS / appel) et publie tout via MQTT vers un dashboard au sol.

## Architecture

```
+----------------------------------+         MQTT           +--------------------------+
|         Pi Zero (Embarque)       |    (broker.hivemq.com) |     PC Ground (Sol)      |
|                                  |                        |                          |
|  acquisition/bms_rasp.py         |                        |  ground/src/             |
|    - Lit 2x BMS XDZN via USB     +------- publish ------->|    transmission.py       |
|    - Decode les trames           |  mebc_se/bms/...       |    data_logger.py        |
|                                  |  mebc_se/sim/...       |    Dashboard Dash/Plotly |
|  telemetrie/mqtt_publisher.py    |  mebc_se/log           |    Google Drive upload   |
|    - Publie donnees BMS          |                        |                          |
|    - Publie statut SIM7600       |                        |  Acces : localhost:8050  |
|    - Diffuse la console (logs)   |                        +--------------------------+
|                                  |
|  telemetrie/sms.py / appel.py    |
|    - SMS et appel via SIM7600    |
|  telemetrie/port_serie.py        |
|    - Port serie SIM7600 partage  |
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
|-- config.py               # Constantes centralisees (ports, BMS, MQTT, GPIO)
|
|-- acquisition/
|   |-- bms_rasp.py          # Monitoring BMS XDZN (2 batteries)
|
|-- telemetrie/
|   |-- port_serie.py        # Port serie SIM7600 centralise (+ qualite signal)
|   |-- sms.py               # Reception SMS + envoi "Bien recu" a la demande
|   |-- appel.py             # Appel vocal via SIM7600
|   |-- mqtt_publisher.py    # Publication MQTT (BMS, statut SIM, console)
|
|-- interface/
|   |-- interface_bateau.py  # Interface Tkinter pour le pilote (statuts inclus)
|
|-- hardware/
|   |-- reboot_button.py     # Bouton de reboot (a integrer)
|
|-- setup/                   # Installation et maintenance du Pi
|   |-- setup_pi.sh          # Install auto (apt + service systemd au demarrage)
|   |-- readonly_on.sh       # Active le mode lecture seule (protege la carte SD)
|   |-- readonly_off.sh      # Desactive le mode lecture seule (pour modifier le code)
|   |-- COMMANDES.txt        # Aide-memoire des commandes a copier-coller
|   |-- requirements.txt     # Dependances pip (alternative a apt)
|
|-- docs/
|   |-- autonomie_batterie.md / .html   # Guide complet calcul d'autonomie
|
|-- ground/                  # Dashboard PC au sol (Dash/Plotly, Docker)
|   |-- Dockerfile
|   |-- docker-compose.yml
|   |-- README.md            # Guide detaille du dashboard
|   |-- src/
|       |-- dashboard.py     # Point d'entree du dashboard
|       |-- app.py           # Objet Dash
|       |-- transmission.py  # Client MQTT recepteur
|       |-- data_logger.py   # Logging CSV (toutes les 5 s) + Google Drive
|       |-- layout.py        # Interface Dash/Plotly
|       |-- callbacks.py     # Logique du dashboard
|       |-- google_drive.py  # Upload vers Drive
|       |-- network_monitor.py
|       |-- assets/dashboard.css
|
|-- Qt_Interface/            # Dashboard desktop Qt6 (optionnel, autre poste)
```

## Demarrage rapide

### 1. Cote Pi Zero (embarque)

L'installation se fait avec le script bash (PAS avec pip : Tkinter ne s'installe
que via apt). A lancer une seule fois :

```bash
cd setup
sudo bash setup_pi.sh
```

Le script installe les dependances systeme (python3-tk, python3-serial,
python3-paho-mqtt), donne les droits du port serie, et cree un service systemd
qui lance `main.py` automatiquement au demarrage.

Pour lancer manuellement (debug) :

```bash
python3 main.py
```

Au demarrage, le systeme :
1. Detecte le port AT du SIM7600
2. Lance le diagnostic SIM (carte detectee ? reseau ?)
3. Se connecte au broker MQTT (si disponible)
4. Lance les threads : BMS, SMS, clavier, interface Tkinter

#### (Optionnel) Proteger la carte SD avant la course

Pour eviter toute corruption de la carte si on coupe le courant brutalement
(boite scellee), activer le mode lecture seule une fois le code fige :

```bash
sudo bash setup/readonly_on.sh
sudo reboot
```

Pour repasser en modifiable (editer le code, git pull) :

```bash
sudo bash setup/readonly_off.sh
sudo reboot
```

Voir `setup/COMMANDES.txt` pour le detail.

### 2. Cote PC Ground (dashboard)

**Prerequis :** [Docker Desktop](https://www.docker.com/products/docker-desktop/)

```bash
cd ground
docker compose up
# Ouvrir http://localhost:8050
```

Le dashboard affiche en temps reel :
- **Zone Statuts** : BMS_1, BMS_2, SIM7600, MQTT (pastille verte = OK)
- **Terminal Pi** : la console du Pi en direct (erreurs en rouge)
- SOC (etat de charge), tensions par cellule (15 cellules)
- Courant, puissance, temperature, autonomie estimee
- Historique de capacite et alarmes

Voir [ground/README.md](ground/README.md) pour le guide detaille (Docker, Google Drive).

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
| `mebc_se/sim/connection` | Statut SIM7600 (retain) | JSON bool `true` |
| `mebc_se/log` | Console du Pi (ligne par ligne) | texte brut |

Broker : `broker.hivemq.com` (public, port 1883)

## Securite et robustesse

- Le MQTT est un bonus : si le broker est injoignable, le BMS tourne normalement
- Le Pi n'ecrit rien sur la carte SD en fonctionnement (logs en RAM / via MQTT) ;
  le mode lecture seule (`setup/readonly_on.sh`) protege la carte contre la corruption
- Les boucles SMS et BMS survivent aux erreurs (try/except dans la boucle)
- Les credentials Google Drive ne sont PAS versionnes (`.gitignore`)
- Le broker HiveMQ est public : les donnees ne sont pas chiffrees

---

## Guide pas a pas : mise en route complete

### Etape 1 : Cloner le projet

Sur le Pi Zero ET sur le PC au sol :

```bash
git clone https://github.com/Gusleboss01/MEBC_SE.git
cd MEBC_SE
```

### Etape 2 : Configurer le Pi Zero

1. Brancher les 2 BMS en USB sur le Pi (via un hub USB si Pi Zero)
2. Brancher le module SIM7600 en USB sur le Pi
3. Les ports sont detectes automatiquement (SIM7600) ou par identifiant hardware (BMS)
4. Installer et activer le service :

```bash
cd setup
sudo bash setup_pi.sh
```

5. Pour un test manuel, lancer `python3 main.py` et verifier dans le terminal :
   - `Carte SIM detectee` (ou `Pas de reponse` si pas de carte SIM - pas grave)
   - `[MQTT] Connecte au broker` (ou un echec - pas grave non plus)
   - `thread BMS lance`, `thread SMS lance`, `thread interface lance`
   - L'ecran Tkinter s'affiche

### Etape 3 : Installer Docker Desktop sur le PC au sol

1. Aller sur https://www.docker.com/products/docker-desktop/
2. Telecharger et installer (tout par defaut), redemarrer si demande
3. Lancer Docker Desktop et attendre "Docker Desktop is running"

### Etape 4 : Lancer le dashboard

```bash
cd MEBC_SE/ground
docker compose up
```

La premiere fois, Docker telecharge les dependances (2-3 minutes). Quand
`Dash is running on http://0.0.0.0:8050/` apparait, ouvrir un navigateur sur :

```
http://localhost:8050
```

Si le Pi est allume et connecte a internet, les donnees apparaissent en temps reel.

### Etape 5 : Google Drive (optionnel)

Voir [ground/README.md](ground/README.md) pour la sauvegarde automatique des
sessions sur Google Drive.

### Depannage

| Probleme | Solution |
|---|---|
| Le dashboard affiche des donnees mais rien ne bouge | Le Pi n'envoie rien. Verifier qu'il est allume et connecte a internet |
| Les statuts BMS_1 / BMS_2 restent gris | Batteries non detectees. Verifier les branchements USB |
| Le statut SIM7600 reste gris | Modem non detecte, ou le Pi n'a pas encore publie son statut |
| `docker compose up` ne marche pas | Docker Desktop n'est pas lance. Ouvrir l'application Docker Desktop |
| `[MQTT] Impossible de se connecter` sur le Pi | Pas d'internet sur le Pi. Le BMS tourne quand meme, sans dashboard |
| Le dashboard affiche `Deconnecte` | Le sol n'est pas connecte au broker. Verifier sa connexion internet |
| Le terminal Pi du dashboard reste vide | Le Pi n'est pas connecte au MQTT, ou aucune ligne de console depuis la connexion |
