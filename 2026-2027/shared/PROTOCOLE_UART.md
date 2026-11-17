# Protocole UART ESP32 <-> Raspberry Pi Zero 2W

Version du protocole : **1**

Ce document est la reference unique. Toute modification du format doit
etre faite ici d'abord, puis dans les deux implementations :

- ESP32 (C++) : [esp/protocole_uart/](esp/protocole_uart/)
- Pi (Python) : [python/protocole_uart.py](python/protocole_uart.py)

## 1. Liaison physique

| Parametre | Valeur |
|---|---|
| Niveau logique | 3,3 V (ESP32 et Pi, pas d'adaptateur) |
| Vitesse | 115200 bauds (pourra monter a 921600 plus tard) |
| Format | 8N1, pas de controle de flux |
| Cablage | TX ESP -> RX Pi (GPIO15), RX ESP <- TX Pi (GPIO14), GND commun |
| Cote Pi | `/dev/serial0` sur le **PL011** (`dtoverlay=disable-bt`, console serie desactivee) |
| Cote ESP32-WROOM-32E (provisoire) | `UART2` : TX = GPIO17, RX = GPIO16 |
| Cote ESP32-S3 (final) | `UART1`, broches a fixer avec le PCB HAT |

L'`UART0` de l'ESP32 reste reserve au flash et aux logs USB pendant le
developpement. Il ne sert jamais a la liaison avec la Pi.

## 2. Format d'une trame

Une trame = **une ligne de texte** :

```
<JSON>*<CRC>\n
```

- `<JSON>` : un objet JSON sur une seule ligne, sans retour a la ligne,
  en ASCII (pas d'accents dans les valeurs texte).
- `*` : separateur.
- `<CRC>` : CRC-16/CCITT-FALSE du `<JSON>` (octets exacts, avant le `*`),
  ecrit en **4 caracteres hexadecimaux majuscules**.
- `\n` : fin de trame (LF seul).

Exemple reel (CRC exact) :

```
{"t":"hb","n":42,"up":125000}*1CDE
```

### CRC-16/CCITT-FALSE

| Parametre | Valeur |
|---|---|
| Polynome | 0x1021 |
| Valeur initiale | 0xFFFF |
| Reflexion | aucune |
| XOR final | 0x0000 |
| Valeur de test | `"123456789"` -> `29B1` |

### Regles de reception

1. On lit octet par octet jusqu'a `\n`. Un `\r` eventuel est ignore.
2. Une ligne de plus de **1024 octets** est jetee (le recepteur attend
   le `\n` suivant pour se resynchroniser).
3. Les octets situes avant le premier `{` de la ligne sont ignores
   (parasites au demarrage). On coupe ensuite sur le **dernier** `*`. Pas de `*`, CRC faux, ou JSON invalide :
   la trame est jetee et un compteur d'erreurs est incremente.
4. Une trame valide sans champ `t` est jetee.
5. Un type `t` inconnu est **ignore sans erreur** (compatibilite avec une
   version plus recente de l'autre carte).

Avantage de ce format : on peut lire la liaison a l'oeil avec
`cat /dev/serial0` ou un moniteur serie, sans outil special.

## 3. Champs communs

Chaque trame contient :

| Champ | Type | Description |
|---|---|---|
| `t` | texte | Type de message (voir sections 4 et 5) |
| `n` | entier | Numero de sequence, 0 a 65535 puis retour a 0. Un compteur par emetteur. Sert a detecter les trames perdues |

Unites : SI, toujours les memes. Tension en V, courant en A (positif =
charge, negatif = decharge), temperature en degC, capacite en Ah, puissance
en W, angles en degres, temps en ms.

Une valeur indisponible (capteur absent) est envoyee a `null`, jamais a 0.

## 4. Messages ESP32 -> Pi

| `t` | Frequence | Role |
|---|---|---|
| `hello` | Au demarrage + en reponse a `info` | Identite de la carte |
| `hb` | 1 Hz | Battement de coeur (la Pi sait que l'ESP est vivant) |
| `bms` | 1 Hz par BMS | Donnees d'un BMS 48 V |
| `temp` | 1 Hz | Sondes 1-Wire DS18B20 |
| `power` | 1 Hz | Mesure INA226 batterie de servitude 12 V |
| `imu` | 5 Hz | Orientation BNO085 |
| `pilot` | A chaque changement + 1 Hz | Boutons et voyants du boitier pilote (via CAN) |
| `alert` | A chaque nouvelle alerte | Alerte (temperature, tension...) |
| `log` | Ponctuel | Ligne de texte de debug de l'ESP |
| `ack` | En reponse a une commande | Accuse de reception |

### `hello`

```json
{"t":"hello","n":0,"proto":1,"fw":"0.1.0","board":"esp32-wroom32e"}
```

`board` vaut `esp32-wroom32e` pendant la phase de developpement, puis
`esp32-s3`. La Pi verifie `proto` : s'il differe du sien, elle l'affiche
en erreur mais continue a decoder ce qu'elle comprend.

### `hb`

```json
{"t":"hb","n":42,"up":125000}
```

| Champ | Description |
|---|---|
| `up` | Temps depuis le demarrage de l'ESP, en ms |

### `bms`

```json
{"t":"bms","n":43,"id":1,"ok":true,"voltage":48.6,"current":-32.4,"soc":75.0,"remaining_ah":73.5,"cells":[3.24,3.22],"temps":[25.0,26.1]}
```

| Champ | Description |
|---|---|
| `id` | 1 ou 2 (BMS_1 adresse 01, BMS_2 adresse 02) |
| `ok` | `false` si le BMS ne repond pas (les autres champs sont alors `null`) |
| `voltage` | Tension du pack (V) |
| `current` | Courant (A, negatif = decharge) |
| `soc` | Etat de charge (%) |
| `remaining_ah` | Capacite restante (Ah) |
| `cells` | Tension de chaque cellule (V), 15 valeurs |
| `temps` | Temperatures internes du BMS (degC) |

Les noms reprennent ceux de 2025-2026 pour garder le dashboard compatible.

### `temp`

```json
{"t":"temp","n":44,"s":[{"id":"28FF641E83160324","c":25.1},{"id":"28FF2A0B83160391","c":null}]}
```

| Champ | Description |
|---|---|
| `s` | Liste des sondes |
| `s[].id` | Adresse ROM 1-Wire unique de la sonde (16 caracteres hexadecimaux) |
| `s[].c` | Temperature (degC), `null` si la lecture a echoue |

L'association adresse -> emplacement (« batterie 12 V », « pack 48 V »...)
est faite cote Pi dans sa configuration, pas dans l'ESP.

### `power`

```json
{"t":"power","n":45,"voltage":12.9,"current":1.8,"power":23.2}
```

### `imu`

```json
{"t":"imu","n":46,"roll":-2.5,"pitch":1.2,"heading":184.0}
```

### `pilot`

```json
{"t":"pilot","n":47,"ok":true,"btn":[0,0,1,0,0],"led":[0,0,0,1]}
```

| Champ | Description |
|---|---|
| `ok` | `false` si le noeud pilote ESP32-C3 ne repond plus sur le CAN |
| `btn` | Etat de chaque bouton (1 = appuye), dans l'ordre de cablage |
| `led` | Etat de chaque voyant (1 = allume) |

### `alert`

```json
{"t":"alert","n":48,"code":"TEMP_HIGH","level":2,"src":"28FF641E83160324","value":62.5,"msg":"Temperature batterie 48 V haute"}
```

| Champ | Description |
|---|---|
| `code` | Code court et stable (`TEMP_HIGH`, `CELL_LOW`, `CELL_DELTA`, `BMS_LOST`, `CAN_LOST`...) |
| `level` | 1 = information, 2 = avertissement, 3 = critique |
| `src` | Origine (id de sonde, `BMS_1`...) |
| `value` | Valeur mesuree qui a declenche l'alerte |
| `msg` | Texte lisible, sans accents |

**Important :** l'ESP32 declenche lui-meme les alertes de securite
(voyant rouge via le CAN). Le message `alert` informe la Pi, il ne lui
delegue pas la decision.

### `log`

```json
{"t":"log","n":49,"lvl":"W","msg":"BMS_2 timeout"}
```

`lvl` : `D` (debug), `I` (info), `W` (warning), `E` (erreur).

### `ack`

```json
{"t":"ack","n":50,"ref":12,"ok":true,"err":null}
```

| Champ | Description |
|---|---|
| `ref` | Numero `n` de la commande a laquelle on repond |
| `ok` | `true` si la commande est executee |
| `err` | Texte d'erreur si `ok` vaut `false`, sinon `null` |

## 5. Messages Pi -> ESP32

| `t` | Role | Reponse |
|---|---|---|
| `ping` | Verifier que l'ESP repond | `ack` |
| `info` | Demander l'identite de la carte | `hello` puis `ack` |
| `cmd` | Executer une commande | `ack` |

```json
{"t":"ping","n":12}
{"t":"cmd","n":13,"name":"set_led","args":{"index":2,"on":true}}
```

Commandes `cmd` prevues (liste a completer) :

| `name` | `args` | Effet |
|---|---|---|
| `set_led` | `index`, `on` | Allume / eteint un voyant du boitier pilote |
| `reboot` | aucun | Redemarre l'ESP32 |

Une commande inconnue renvoie `ack` avec `ok: false` et `err: "unknown command"`.

Si la Pi ne recoit pas d'`ack` sous **500 ms**, elle peut renvoyer la
commande une fois (avec un nouveau `n`).

## 6. Perte de liaison

- **Cote Pi** : aucune trame valide pendant **3 s** -> l'ESP est considere
  perdu (statut rouge a l'ecran et sur le dashboard).
- **Cote ESP32** : la Pi n'est pas obligee de parler. L'ESP envoie ses
  donnees en continu, qu'il y ait quelqu'un a l'ecoute ou non, et
  continue a gerer seul les alertes de securite.

## 7. Evolution du protocole

- Ajouter un **champ** ou un **type de message** : ne change pas la
  version (les anciens recepteurs ignorent ce qu'ils ne connaissent pas).
- Renommer, supprimer ou changer le sens d'un champ : incrementer la
  version (`proto`) ici et dans les deux implementations.
