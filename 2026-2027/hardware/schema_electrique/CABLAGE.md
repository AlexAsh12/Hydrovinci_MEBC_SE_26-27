# Tableau de câblage - saison 2026-2027

Montage actuel (ESP32-WROOM-32E provisoire). Une ligne = un fil.
Étiqueter chaque fil **à ses deux bouts** avec son repère (colonne `Repère`).

Côté ESP32 : numéros **sérigraphiés sur la DevKit** (= numéros de GPIO),
pas les numéros de broche de la datasheet. Côté Pi : numéro de **broche
physique** du connecteur 40 broches (1 à 40), GPIO entre parenthèses.

Référence dans le code : `esp32_s3_donnees/MEBC Esp32 Dev Module/include/broches.h`.

## 1. Liaison ESP32 - Raspberry Pi Zero 2W (UART)

| Repère | Fonction | ESP32 | Vers | Couleur | Remarque |
|---|---|---|---|---|---|
| U1 | UART ESP → Pi | `17` (TX2) | Pi broche **10** (GPIO15, RXD) | | TX d'un côté → RX de l'autre |
| U2 | UART Pi → ESP | `16` (RX2) | Pi broche **8** (GPIO14, TXD) | | |
| U3 | Masse commune | `GND` | Pi broche **6** (GND) | | Obligatoire, sinon aucune communication |

921600 bauds, niveaux 3,3 V des deux côtés (pas d'adaptateur).

## 2. IMU 9 axes - Adafruit BNO085 (I²C, adresse 0x4A)

| Repère | Fonction | ESP32 | BNO085 | Couleur | Remarque |
|---|---|---|---|---|---|
| I1 | Alimentation | `3V3` | `VIN` | | Partagé avec la sonde (S1) |
| I2 | Masse | `GND` | `GND` | | |
| I3 | Données I²C | `21` | `SDA` | | Bus I²C (l'INA226 s'y ajoutera) |
| I4 | Horloge I²C | `22` | `SCL` | | |
| I5 | Mesure prête | `4` | `INT` | | |
| I6 | Remise à zéro | `23` | `RST` | | |

À laisser **débranchés** sur le BNO085 : `DI`, `CS`, `P0`, `P1`, `3Vo`.
- `P0` / `P1` choisissent le mode : branchés, le capteur quitte l'I²C.
- `3Vo` est une **sortie** 3,3 V : ne jamais la relier au 3V3 de l'ESP.

## 3. Sondes de température DS18B20 (bus 1-Wire)

| Repère | Fonction | ESP32 | Sonde | Couleur | Remarque |
|---|---|---|---|---|---|
| S1 | Alimentation | `3V3` | fil **rouge** | rouge | |
| S2 | Masse | `GND` | fil **noir** | noir | |
| S3 | Données | `27` | fil **jaune** | jaune | |
| R1 | Rappel du bus | `27` | `3V3` | - | **Résistance 4,7 kΩ** entre le jaune et le 3V3 |

Plusieurs sondes : tous les rouges ensemble, tous les noirs ensemble, tous
les jaunes ensemble sur `27`, **une seule** résistance R1 pour tout le bus.

Sondes branchées :

| Adresse (affichée à l'écran) | Emplacement |
|---|---|
| `28DC9C9F110000AB` | |

## 4. Alimentation, écran, USB

| Repère | Fonction | De | Vers | Remarque |
|---|---|---|---|---|
| A1 | Alimentation Pi | Alimentation 5 V | Pi, micro-USB **PWR IN** | |
| A2 | Alimentation ESP32 | | ESP32, USB | À compléter pour le bateau (aujourd'hui : USB du PC) |
| E1 | Écran | Pi, **mini-HDMI** | Écran HDMI | Adaptateur mini-HDMI → HDMI |
| P1 | Programmation | ESP32, USB | PC | Flash et journaux (pilote CP210x, COM3) |

## 5. Broches ESP32 occupées et à éviter

| GPIO | Utilisation |
|---|---|
| 4 | BNO085 INT |
| 16, 17 | UART vers la Pi |
| 21, 22 | Bus I²C |
| 23 | BNO085 RST |
| 27 | Bus 1-Wire |
| 1, 3 | Réservées : USB (flash et journaux) |
| 6 à 11 | Interdites : mémoire flash |
| 0, 2, 12, 15 | À éviter : rôle au démarrage |
| 34 à 39 | Entrée seulement |

## 6. Prévu, pas encore câblé

- BMS 1 et 2 (RS485)
- INA226 (énergie), sur le bus I²C (SDA `21` / SCL `22`), adresse 0x40
- Bus CAN
- ESP32-C3 du boîtier pilote
- Module 4G SIM7600 sur la Pi
- Passage à l'ESP32-S3 : toutes les broches ESP32 de ce tableau changeront

## 7. Contrôle avant mise sous tension

- [ ] GND commun entre ESP32, Pi et capteurs (U3, I2, S2)
- [ ] U1 / U2 croisés : TX → RX
- [ ] BNO085 : rien sur `P0`, `P1`, `3Vo`
- [ ] Résistance 4,7 kΩ présente sur le bus 1-Wire (R1)
- [ ] Sondes et résistance R1 sur le **3V3**, pas sur le 5 V : sinon le fil
      de données envoie du 5 V dans l'ESP32, qui ne le supporte pas
