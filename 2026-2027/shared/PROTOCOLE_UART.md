# Protocole UART ESP32 <-> Raspberry Pi Zero 2W

Version du protocole : **2** (binaire)

Ce document est la reference unique. Toute modification du format se fait
ici d'abord, puis dans l'implementation C commune
[c/protocole/](c/protocole/), compilee telle quelle sur l'ESP32 et sur la Pi.

## 1. Liaison physique

| Parametre | Valeur |
|---|---|
| Niveau logique | 3,3 V (ESP32 et Pi, pas d'adaptateur) |
| Vitesse au demarrage | **921600 bauds** (`PROTO_VITESSE_DEFAUT`), soit 92 ko/s |
| Vitesses possibles | 115200, 230400, 460800, 921600, 1M, 1,5M, 2M, 2,5M, 3M bauds |
| Format | 8N1, pas de controle de flux |
| Cablage | TX ESP -> RX Pi (GPIO15, broche 10), RX ESP <- TX Pi (GPIO14, broche 8), GND commun |
| Cote Pi | `/dev/serial0` sur le PL011 (`dtoverlay=disable-bt`, console serie desactivee) |
| Cote ESP32-WROOM-32E (provisoire) | `UART2` : TX = GPIO17, RX = GPIO16 |
| Cote ESP32-S3 (final) | a fixer avec le PCB HAT |

L'`UART0` de l'ESP32 reste reserve au flash et aux logs USB.

La vitesse se change en cours de route avec la commande `SET_BAUD` (section 6).

## 2. Format d'une trame

```
+------+------+--------+------+--------+-------------+--------+
| 0xAA | 0x55 |  LEN   | TYPE |  SEQ   |   PAYLOAD   |  CRC   |
|  1   |  1   |   2    |  1   |   2    |  LEN octets |   2    |
+------+------+--------+------+--------+-------------+--------+
```

| Champ | Taille | Description |
|---|---|---|
| SOF | 2 | Debut de trame : `AA 55` |
| LEN | 2 | Taille du payload, 0 a 1024 |
| TYPE | 1 | Type de message (section 4) |
| SEQ | 2 | Numero de sequence, +1 a chaque trame, un compteur par emetteur (0 apres 65535) |
| PAYLOAD | LEN | Contenu, selon le type |
| CRC | 2 | CRC-16/CCITT-FALSE calcule sur LEN, TYPE, SEQ et PAYLOAD |

- Tous les entiers sont en **little-endian**.
- Surcout : 9 octets par trame.
- CRC-16/CCITT-FALSE : polynome 0x1021, valeur initiale 0xFFFF, sans
  reflexion, sans XOR final. Valeur de test : `"123456789"` -> `0x29B1`.

### Regles de reception

1. Chercher `AA 55`. Les octets avant sont ignores.
2. Si LEN > 1024 : faux debut de trame, reprendre la recherche a l'octet suivant.
3. Attendre la trame complete (`9 + LEN` octets).
4. CRC faux : la trame est rejetee et la recherche reprend **a l'octet
   suivant le AA** (pas apres la trame), pour ne jamais avaler une vraie
   trame qui suivrait une trame tronquee.
5. Un TYPE inconnu est ignore sans erreur (compatibilite).
6. Des octets en trop a la fin d'un payload sont ignores (on peut ajouter
   des champs a la fin d'un message sans casser les anciens recepteurs).

## 3. Unites

Valeurs entieres a virgule fixe : pas de flottants sur le fil.

| Grandeur | Unite |
|---|---|
| Tension pack BMS | 0,01 V |
| Tension cellule | mV |
| Courant BMS | 0,01 A (negatif = decharge) |
| Etat de charge | 0,1 % |
| Capacite | 0,01 Ah |
| Temperature BMS | 0,1 degC |
| Temperature sonde 1-Wire | 0,01 degC |
| Tension / courant / puissance INA226 | mV / mA / mW |
| Angles IMU | 0,01 degre |
| Temps | ms |

Temperature indisponible : `INT16_MIN` (-32768).

## 4. Messages ESP32 -> Pi

| TYPE | Nom | Frequence | Payload |
|---|---|---|---|
| 0x01 | HELLO | Au demarrage + reponse a INFO | `u8 version, u8 carte, u8 fw[3]` |
| 0x02 | HB | 1 Hz | `u32 uptime_ms` |
| 0x10 | BMS | 1 Hz par BMS | voir ci-dessous |
| 0x11 | TEMP | 1 Hz | `u8 nb` puis nb x (`u8 rom[8], i16 temp`) |
| 0x12 | POWER | 1 Hz | `u16 tension_mv, i32 courant_ma, i32 puissance_mw` |
| 0x13 | IMU | 5 Hz | `i16 roulis, i16 tangage, u16 cap` (0 a 35999) |
| 0x14 | PILOT | Changement + 1 Hz | `u8 ok, u8 boutons, u8 voyants` (bit i = element i) |
| 0x20 | ALERT | A chaque alerte | `u8 code, u8 niveau, u8 source, i32 valeur` |
| 0x21 | LOG | Ponctuel | `u8 niveau` ('D','I','W','E') puis texte ASCII sans `\0` |
| 0x30 | ACK | Reponse a une commande | `u16 ref, u8 ok, u8 erreur` |
| 0x70 | TEST | Flux de mesure | `u32 compteur` puis remplissage (section 6) |
| 0x71 | STREAM_FIN | Fin du flux | `u32 trames, u32 octets` |

**HELLO** : `carte` = 1 pour ESP32-WROOM-32E, 2 pour ESP32-S3.

**BMS** (28 + 2 x cellules + 2 x temperatures octets) :

| Champ | Type |
|---|---|
| id (1 ou 2) | u8 |
| ok (0 = BMS muet) | u8 |
| tension pack | u16 |
| courant | i32 |
| SOC | u16 |
| capacite restante | u16 |
| nb_cellules (max 24) | u8 |
| cellules | nb_cellules x u16 |
| nb_temps (max 8) | u8 |
| temperatures | nb_temps x i16 |

**TEMP** : `rom` = adresse 1-Wire unique de la sonde. L'association
adresse -> emplacement est faite cote Pi.

**ALERT** : codes 1 temperature haute (valeur en 0,01 degC), 2 cellule
basse (mV), 3 desequilibre (mV), 4 BMS perdu, 5 CAN perdu. Niveau 1 info,
2 avertissement, 3 critique. Source 0 aucune, 1 BMS_1, 2 BMS_2, 0x10+i sonde i.
L'ESP32 declenche lui-meme les alertes de securite ; ce message informe la Pi.

**ACK** : `ref` = SEQ de la commande. Erreurs : 0 aucune, 1 commande
inconnue, 2 argument invalide, 3 non disponible.

## 5. Messages Pi -> ESP32

| TYPE | Nom | Payload | Reponse |
|---|---|---|---|
| 0x80 | PING | vide | ACK |
| 0x81 | INFO | vide | HELLO puis ACK |
| 0x82 | CMD | `u8 commande` + arguments | ACK |

| Commande | Arguments | Effet |
|---|---|---|
| 1 REBOOT | aucun | ACK puis redemarrage |
| 2 SET_LED | `u8 index, u8 allume` | Voyant du boitier pilote |
| 3 SET_BAUD | `u32 vitesse` | Change la vitesse UART (section 6) |
| 4 STREAM | `u16 taille_payload, u32 duree_ms` | Flux de test (section 6) |

## 6. Vitesse et mesure de debit

### Changement de vitesse (SET_BAUD)

1. La Pi envoie `CMD SET_BAUD` a la vitesse actuelle.
2. L'ESP repond `ACK` a la vitesse actuelle, attend la fin de l'emission,
   puis bascule.
3. La Pi bascule a son tour, puis envoie un `PING` a la nouvelle vitesse.
4. Securites cote ESP32 :
   - aucune trame valide de la Pi dans les **3 s** apres le changement :
     retour a la vitesse par defaut (921600 bauds) ;
   - a une vitesse autre que celle par defaut, Pi silencieuse plus de
     **10 s** : retour a la vitesse par defaut.

### Flux de test (STREAM)

L'ESP envoie des trames TEST aussi vite que la liaison le permet pendant
`duree_ms` (30 s max), puis une trame STREAM_FIN avec le nombre de trames
et d'octets envoyes.

Contenu d'une trame TEST : `u32 compteur` (0, 1, 2...), puis les octets
`i = 4 .. taille-1` valent `(compteur x 31 + i) & 0xFF`. La Pi verifie
chaque octet : une trame peut avoir un CRC correct ET un contenu faux
seulement en cas de bug.

La Pi compare les compteurs recus a STREAM_FIN pour compter les trames perdues.

## 7. Perte de liaison

- Cote Pi : aucune trame valide pendant **3 s** -> ESP32 perdu.
- Cote ESP32 : la Pi n'est pas obligee de parler ; l'ESP envoie ses
  donnees en continu et gere seul les alertes de securite.

## 8. Evolution

- Nouveau champ **a la fin** d'un message, ou nouveau TYPE : meme version.
- Champ renomme, deplace, supprime ou change d'unite : incrementer
  `PROTO_VERSION` (dans `protocole.h`) et ce document.
