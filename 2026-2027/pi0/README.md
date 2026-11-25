# pi0/ - Raspberry Pi Zero 2W

La Pi reçoit les données de l'ESP32 par l'UART et celles du module 4G/GPS
SIM7600 par l'USB, et les affiche en plein écran sur le HDMI. Au
démarrage, tout se lance seul :

```
alimentation
  -> service tableau-bord : lit l'UART et le SIM7600, sert la page sur 127.0.0.1:8080
  -> connexion automatique sur l'écran -> kiosque/lancer_kiosque.sh
       -> Chromium plein écran (cage) sur la page
```

## Se connecter à la Pi

| | |
|---|---|
| Nom de la Pi | `Hydro-MEBC` |
| Utilisateur | `hydro` |
| Dépôt | `~/Hydrovinci_MEBC_SE_26-27` (branche `saison-2026-2027`) |

Depuis un PC sur le même réseau Wi-Fi :

```bash
ssh hydro@Hydro-MEBC.local
```

Si `Hydro-MEBC.local` n'est pas trouvé, utiliser l'adresse IP de la Pi
(page de la box ou du partage de connexion, ou `hostname -I` sur la Pi).

Câblage de la liaison :

| ESP32-WROOM-32E (sérigraphie DevKit) | Pi Zero 2W |
|---|---|
| `17` / TX2 | broche **10** (GPIO15 / RXD) |
| `16` / RX2 | broche **8** (GPIO14 / TXD) |
| GND | broche **6** (GND) |

## Mettre à jour

```bash
cd ~/Hydrovinci_MEBC_SE_26-27 && git pull
cd 2026-2027/pi0 && make && make test
sudo systemctl restart tableau-bord
sudo systemctl restart getty@tty1    # recharge l'affichage
```

## Lancer les programmes à la main

Le service occupe l'UART : l'arrêter d'abord, le relancer après.

```bash
cd ~/Hydrovinci_MEBC_SE_26-27/2026-2027/pi0
sudo systemctl stop tableau-bord
```

| Commande | Rôle |
|---|---|
| `./tableau_bord` | Serveur du tableau de bord (Ctrl+C pour arrêter) |
| `./tableau_bord --ecoute 0.0.0.0` | Idem, page visible depuis un PC : `http://Hydro-MEBC.local:8080` |
| `./tableau_bord --sans-sim` | Sans le module 4G/GPS |
| `./tableau_bord --sim-port /dev/ttyUSB2` | Impose le port AT du SIM7600 (sinon trouvé tout seul) |
| `./liaison_esp32` | Moniteur brut : tout ce que l'ESP envoie, en texte |
| `./liaison_esp32 --banc` | Mesure du débit à chaque vitesse UART |
| `./liaison_esp32 --brut` | Octets bruts reçus (diagnostic du câblage) |
| `./tableau_bord --aide`, `./liaison_esp32 --aide` | Toutes les options |

```bash
sudo systemctl start tableau-bord    # retour au fonctionnement normal
```

`--ecoute 0.0.0.0` ouvre la page à tout le réseau : à n'utiliser que pour
un test, le service, lui, reste limité à la Pi.

## Écran

| Commande | Rôle |
|---|---|
| `sudo systemctl restart getty@tty1` | Relance l'affichage (recharge la page) |
| `sudo systemctl stop getty@tty1` | Coupe l'affichage |
| `pgrep -a cage` | Vérifie que l'affichage tourne |

## Service

| Commande | Rôle |
|---|---|
| `systemctl status tableau-bord` | État du serveur |
| `journalctl -u tableau-bord -f` | Journal en direct (ESP présent / perdu, écrans) |
| `journalctl -u tableau-bord -b` | Journal depuis le dernier démarrage |
| `sudo systemctl restart tableau-bord` | Redémarre le serveur |

## Noms des sondes de température

Sans configuration, une sonde apparaît sous son adresse (ex.
`28DC9C9F110000AB`).

```bash
cd ~/Hydrovinci_MEBC_SE_26-27/2026-2027/pi0
cp sondes.conf.exemple sondes.conf   # la premiere fois
nano sondes.conf                     # une ligne : adresse puis nom
sudo systemctl restart tableau-bord
```

`sondes.conf` n'est pas versionné : un `git pull` ne l'écrase pas.

## Module 4G / GPS (SIM7600G-H 4G HAT (B))

Branchement : section 4 de
[CABLAGE.md](../hardware/schema_electrique/CABLAGE.md) (HAT vissé sous la
Pi, liaison USB par les pogo pins, aucun fil).

Le tableau de bord trouve le module tout seul, à chaque branchement.
L'ESP32 et le module sont indépendants : si l'un est absent ou débranché,
l'autre continue de s'afficher.

| Fonction | Fonctionnement |
|---|---|
| Vitesse | Filtre de Kalman : vitesse GPS (1 Hz) + accélération avant de l'IMU (5 Hz). Le filtre apprend le biais de l'accéléromètre. GPS perdu : accéléromètre seul quelques secondes, puis « vitesse inconnue ». Pas d'IMU : vitesse GPS lissée |
| Carte | Trace du bateau (un point tous les 3 m), position, cap, nord en haut. Fond facultatif : `carte.conf` |
| SMS reçus | Seuls les numéros de `SIM_NUMEROS` (`.env`) sont écoutés. Une commande reçoit une réponse, tout autre texte s'affiche 30 s en bandeau |
| SMS envoyés | File d'attente : partent quand carte SIM et réseau sont là, abandonnés après 15 min |

Commandes SMS (majuscules ou minuscules) :

| SMS | Réponse |
|---|---|
| `POSITION` ou `POS` | Heure, coordonnées, vitesse, cap, lien Google Maps |
| `VITESSE` | Vitesse et cap |
| `ETAT` | Batteries, puissance, ESP, GPS, réseau |
| `AIDE` | Liste des commandes |

Réglages, dans le `.env` à la racine du dépôt (modèle : `.env.example`) :

```bash
nano ~/Hydrovinci_MEBC_SE_26-27/.env
#   SIM_NUMEROS='+33612345678,+33698765432'
#   SIM_PIN=         (vide si la carte SIM n'a pas de code)
sudo systemctl restart tableau-bord
```

Fond de carte (facultatif) :

```bash
cd ~/Hydrovinci_MEBC_SE_26-27/2026-2027/pi0
cp carte.conf.exemple carte.conf     # puis suivre les explications du fichier
sudo systemctl restart tableau-bord
```

Vérifier : `journalctl -u tableau-bord -f` doit afficher
`SIM7600 : port de commandes ...`, `SIM7600 pret`, `carte SIM : prete`,
puis `GPS : position trouvee` (dehors, environ 1 minute).

## Dépannage

| Symptôme | Que faire |
|---|---|
| Écran noir | Attendre 10 s (nouvel essai automatique), puis `pgrep -a cage` et `sudo systemctl restart getty@tty1` |
| « Serveur injoignable » | `systemctl status tableau-bord`, puis `journalctl -u tableau-bord -b` |
| « ESP32 perdu » | ESP non alimenté, TX/RX inversés, GND non commun. Vérifier avec `./liaison_esp32 --brut` (service arrêté) |
| « UART indisponible » | `ls -l /dev/serial0` doit pointer vers `ttyAMA0`, sinon relancer l'installation |
| Erreurs UART qui montent | `grep disable-bt /boot/firmware/config.txt` doit trouver `dtoverlay=disable-bt` |
| `liaison_esp32` ne reçoit rien | Le service occupe l'UART : `sudo systemctl stop tableau-bord` |
| Vitesse toujours « GPS seul » | L'ESP n'envoie pas l'IMU (panneau Mouvement grisé) : la vitesse reste juste, mais lissée |
| `Permission denied` sur `/dev/serial0` | Se déconnecter puis se reconnecter (groupe `dialout`) |
| Une page modifiée ne s'affiche pas | `sudo systemctl restart tableau-bord`, puis `sudo systemctl restart getty@tty1` |
| « 4G absent » | `ls /dev/serial/by-id/` doit montrer des `usb-SimTech...`. Sinon : voyant PWR du HAT, pogo pins sur leurs pastilles, `dmesg \| tail` |
| « Pas de carte SIM » / « SIM : code PIN » | Carte insérée hors tension ? Code dans `SIM_PIN` du `.env` |
| « Pas de réseau » | Antenne LTE sur `Main`, forfait actif. Le voyant NET doit clignoter 1 fois par seconde |
| « Recherche GPS… » qui dure | Antenne GPS sur `GNSS`, à l'extérieur, ciel dégagé (1 min environ) |
| SMS reçus sans effet | Numéro absent de `SIM_NUMEROS` : `journalctl -u tableau-bord \| grep SMS` |
| Tout réparer / réinstaller | `sudo ./kiosque/installer.sh` puis `sudo reboot` (sans risque, peut être relancé) |

## Protéger la carte SD (bateau)

Une coupure d'alimentation brutale peut abîmer la carte SD. Une fois tout
validé, la carte peut passer en lecture seule :

```bash
sudo raspi-config nonint do_overlayfs 0    # activer, puis : sudo reboot
sudo raspi-config nonint do_overlayfs 1    # desactiver (avant une mise a jour), puis : sudo reboot
```
