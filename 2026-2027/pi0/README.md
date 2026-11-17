# pi0/ - Raspberry Pi Zero 2W

## Liaison UART avec l'ESP32

### Cablage

| ESP32-WROOM-32E (serigraphie DevKit) | Pi Zero 2W |
|---|---|
| `17` / TX2 | broche **10** (GPIO15 / RXD) |
| `16` / RX2 | broche **8** (GPIO14 / TXD) |
| GND | broche **6** (GND) |

### Configuration de la Pi (une seule fois)

```bash
sudo raspi-config
#   3 Interface Options -> I6 Serial Port
#   "login shell over serial ?"      -> No
#   "serial port hardware enabled ?" -> Yes
```

```bash
echo "dtoverlay=disable-bt" | sudo tee -a /boot/firmware/config.txt
sudo apt install -y build-essential
sudo reboot
```

Verification : `ls -l /dev/serial0` doit pointer vers `ttyAMA0`.

### Compiler

```bash
cd ~/Hydrovinci_MEBC_SE_26-27/2026-2027/pi0
make test        # tests du protocole
make             # programme liaison_esp32
```

### Lancer

```bash
./liaison_esp32            # moniteur : trames de l'ESP32, ping, statistiques
./liaison_esp32 --banc     # mesure du debit a chaque vitesse UART
./liaison_esp32 --aide     # toutes les options
```

### Depannage

| Symptome | Cause probable |
|---|---|
| `Toujours rien recu...` | TX/RX inverses, GND non commun, ESP non flashe |
| `Impossible d'ouvrir /dev/serial0` | UART non active |
| `Permission denied` | `sudo usermod -a -G dialout $USER` puis se reconnecter |
| Beaucoup de trames rejetees (CRC) | Mini-UART encore actif (`dtoverlay=disable-bt` manquant) |
| `--brut` affiche du texte de boot Linux | Console serie encore active sur le port |
