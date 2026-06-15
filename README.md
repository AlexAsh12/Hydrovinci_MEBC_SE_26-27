# MEBC_SE — Monaco Energy Boat Challenge 2026

Système embarqué de télémétrie et contrôle pour bateau électrique solaire.

## Architecture

```
┌──────────────────────────────────┐         MQTT           ┌─────────────────────────┐
│         Pi Zero (Embarqué)       │    (broker.hivemq.com) │     PC Ground (Sol)     │
│                                  │                        │                         │
│  acquisition/bms_rasp.py         │                        │  ground/src/            │
│    └── Lit 2x BMS XDZN via USB  ├──── publish ──────────►│    transmission.py      │
│    └── Décode les trames         │  mebc_se/bms/...       │    data_logger.py       │
│                                  │                        │    Dashboard Dash/Plotly │
│  telemetrie/mqtt_publisher.py    │                        │    Google Drive upload   │
│    └── Publie les données BMS    │                        │                         │
│                                  │                        │  Accès : localhost:8050  │
│  telemetrie/sms.py               │                        └─────────────────────────┘
│    └── Réception SMS SIM7600     │
│                                  │
│  telemetrie/appel.py             │
│    └── Appel vocal SIM7600       │
│                                  │
│  interface/interface_bateau.py   │
│    └── Affichage Tkinter pilote  │
│                                  │
│  main.py (orchestrateur)         │
└──────────────────────────────────┘
```

## Arborescence

```
MEBC_SE/
├── main.py                  # Point d'entrée — lance tous les threads
├── config.py                # Constantes centralisées (ports, BMS, MQTT, GPIO)
├── requirements.txt         # Dépendances Python (Pi)
│
├── acquisition/             # Lecture des capteurs
│   └── bms_rasp.py          # Monitoring BMS XDZN (2 batteries)
│
├── telemetrie/              # Communication
│   ├── sms.py               # Réception SMS via SIM7600
│   ├── appel.py             # Appel vocal via SIM7600
│   └── mqtt_publisher.py    # Publication MQTT vers le dashboard
│
├── interface/               # Affichage
│   └── interface_bateau.py  # Interface Tkinter pour le pilote
│
├── hardware/                # GPIO
│   └── reboot_button.py     # Bouton de reboot (à intégrer)
│
├── ground/                  # Dashboard PC (code de Debian/Raphaël)
│   ├── Dockerfile
│   ├── docker-compose.yml
│   └── src/
│       ├── dashboard.py     # Point d'entrée dashboard
│       ├── transmission.py  # Client MQTT récepteur
│       ├── data_logger.py   # Logging CSV + Google Drive
│       ├── layout.py        # Interface Dash/Plotly
│       ├── callbacks.py     # Logique du dashboard
│       ├── google_drive.py  # Upload vers Drive
│       └── assets/dashboard.css
│
└── Qt_Interface/            # Interface Qt (obsolète, à supprimer)
```

## Démarrage rapide

### 1. Côté Pi Zero (embarqué)

```bash
# Installer les dépendances
pip install -r requirements.txt

# Lancer le système
python main.py
```

Au démarrage, le système :
1. Détecte le port AT du SIM7600
2. Lance le diagnostic SIM (carte détectée ? réseau ?)
3. Se connecte au broker MQTT (si disponible)
4. Lance les threads : BMS, SMS, clavier, interface

### 2. Côté PC Ground (dashboard)

**Prérequis :** Installer [Docker Desktop](https://www.docker.com/products/docker-desktop/)

```bash
cd ground

# Lancer le dashboard
docker compose up

# Ouvrir dans le navigateur
# http://localhost:8050
```

Le dashboard affiche en temps réel :
- SOC (état de charge)
- Tensions par cellule (15 cellules)
- Courant, puissance, température
- Historique de capacité
- Alarmes

### 3. Google Drive (optionnel)

Pour activer l'enregistrement automatique sur Google Drive :

```bash
cd ground
docker compose --profile auth run --service-ports auth
```

Suivre les instructions à l'écran pour l'authentification OAuth.
Demander l'accès au dossier Drive partagé à Raphaël (Debian).

## Topics MQTT

| Topic | Donnée | Format |
|---|---|---|
| `mebc_se/bms/cells/voltages` | Tensions cellules | JSON list `[3.24, 3.22, ...]` |
| `mebc_se/bms/pack/voltage` | Tension pack | float `48.6` |
| `mebc_se/bms/pack/current` | Courant | float `-32.4` |
| `mebc_se/bms/pack/soc` | État de charge | float `75.0` |
| `mebc_se/bms/pack/capacity` | Capacité totale | float `98.0` |
| `mebc_se/bms/pack/remaining` | Capacité restante | float `73.5` |
| `mebc_se/bms/pack/temperature` | Températures | JSON list `[25.0, 26.1]` |
| `mebc_se/bms/alarms` | Alarmes actives | JSON list `[]` |

Broker : `broker.hivemq.com` (public, port 1883)

## Sécurité

- Le MQTT est un bonus : si le broker est injoignable, le BMS tourne normalement
- Aucun stockage local sur le Pi Zero (tout part en MQTT)
- Les credentials Google Drive ne sont PAS versionnés (`.gitignore`)
- Le broker HiveMQ est public : les données ne sont pas chiffrées
