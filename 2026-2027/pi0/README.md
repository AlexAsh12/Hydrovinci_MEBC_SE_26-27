# pi0/ - Raspberry Pi Zero 2W

La Pi reçoit les données de l'ESP32 par l'UART et les affiche en plein
écran sur le HDMI. Au démarrage, tout se lance seul :

```
alimentation
  -> service tableau-bord : lit l'UART, sert la page sur 127.0.0.1:8080
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

## Dépannage

| Symptôme | Que faire |
|---|---|
| Écran noir | Attendre 10 s (nouvel essai automatique), puis `pgrep -a cage` et `sudo systemctl restart getty@tty1` |
| « Serveur injoignable » | `systemctl status tableau-bord`, puis `journalctl -u tableau-bord -b` |
| « ESP32 perdu » | ESP non alimenté, TX/RX inversés, GND non commun. Vérifier avec `./liaison_esp32 --brut` (service arrêté) |
| « UART indisponible » | `ls -l /dev/serial0` doit pointer vers `ttyAMA0`, sinon relancer l'installation |
| Erreurs UART qui montent | `grep disable-bt /boot/firmware/config.txt` doit trouver `dtoverlay=disable-bt` |
| `liaison_esp32` ne reçoit rien | Le service occupe l'UART : `sudo systemctl stop tableau-bord` |
| `Permission denied` sur `/dev/serial0` | Se déconnecter puis se reconnecter (groupe `dialout`) |
| Une page modifiée ne s'affiche pas | `sudo systemctl restart tableau-bord`, puis `sudo systemctl restart getty@tty1` |
| Tout réparer / réinstaller | `sudo ./kiosque/installer.sh` puis `sudo reboot` (sans risque, peut être relancé) |

## Protéger la carte SD (bateau)

Une coupure d'alimentation brutale peut abîmer la carte SD. Une fois tout
validé, la carte peut passer en lecture seule :

```bash
sudo raspi-config nonint do_overlayfs 0    # activer, puis : sudo reboot
sudo raspi-config nonint do_overlayfs 1    # desactiver (avant une mise a jour), puis : sudo reboot
```
