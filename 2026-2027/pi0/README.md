# pi0/ - Raspberry Pi Zero 2W

## Test de la liaison UART avec l'ESP32

### 1. Cablage (3 fils)

| ESP32-WROOM-32E | Pi Zero 2W |
|---|---|
| GPIO17 (TX2) | GPIO15 / RXD - broche **10** |
| GPIO16 (RX2) | GPIO14 / TXD - broche **8** |
| GND | GND - broche **6** |

TX va toujours vers RX. Ne **jamais** relier le 5 V ou le 3,3 V entre les
deux cartes pour ce test (chacune a sa propre alimentation, seul le GND est commun).

### 2. Configurer l'UART de la Pi (une seule fois)

Par defaut, les broches 8/10 de la Pi Zero 2W sont reliees au "mini-UART",
dont la vitesse varie avec la frequence du processeur. On les passe sur le
vrai UART (PL011) et on libere le port de la console :

```bash
sudo raspi-config
#   3 Interface Options -> I6 Serial Port
#   "login shell over serial ?"      -> No
#   "serial port hardware enabled ?" -> Yes
```

```bash
echo "dtoverlay=disable-bt" | sudo tee -a /boot/firmware/config.txt
sudo reboot
```

(Sur un ancien Raspberry Pi OS, le fichier est `/boot/config.txt`.)

Verification apres le reboot : `/dev/serial0` doit pointer vers `ttyAMA0`.

```bash
ls -l /dev/serial0
```

### 3. Lancer le test

```bash
cd ~/Hydrovinci_MEBC_SE_26-27
git pull
cd 2026-2027/pi0
python3 test_liaison_esp32.py
```

Resultat attendu : `*** LIAISON ESP32 OK ***`, un `HELLO`, puis chaque
seconde une ligne `hb` et une ligne `bms1` (donnees factices), et un `ack`
avec le temps d'aller-retour toutes les 2 s. Statistiques toutes les 10 s.

Options : `--brut` (affiche les octets bruts), `--reboot-esp` (teste la
commande reboot), `--port /dev/ttyAMA0`.

### Depannage

| Symptome | Cause probable |
|---|---|
| `Toujours rien recu...` | TX/RX inverses, GND non commun, ou ESP non flashe |
| `Impossible d'ouvrir /dev/serial0` | UART non active (etape 2) |
| `Permission denied` | `sudo usermod -a -G dialout $USER` puis se reconnecter |
| Beaucoup de trames `erronees` | Mini-UART encore actif (`dtoverlay=disable-bt` manquant) |
| Des `hb` arrivent mais pas d'`ack` | Fil TX Pi -> RX ESP debranche |
| `--brut` affiche des messages de boot | Console serie encore active sur le port (etape 2) |
