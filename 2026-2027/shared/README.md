# shared/ - Ce que les cartes partagent

Ce dossier contient les definitions communes a plusieurs cartes, pour
qu'elles parlent toutes exactement le meme langage.

```
shared/
|-- PROTOCOLE_UART.md          # Specification de la liaison ESP32 <-> Pi (reference)
|-- esp/protocole_uart/        # Implementation C++ (ESP32, PlatformIO)
|-- python/protocole_uart.py   # Implementation Python (Pi Zero)
|-- python/test_protocole_uart.py
```

## Regle d'or

On modifie **d'abord** la spec `PROTOCOLE_UART.md`, **puis** les deux
implementations, dans le meme commit.

## Utilisation

**ESP32 (PlatformIO)** : dans le `platformio.ini` du projet ESP

```ini
lib_extra_dirs = ../shared/esp
```

puis `#include <protocole_uart.h>`.

**Pi (Python)** : ajouter `shared/python` au chemin d'import, puis
`from protocole_uart import Decodeur, Encodeur`.

## Tests

```bash
cd shared/python
python -m unittest -v test_protocole_uart
```

Valeurs de reference communes aux deux implementations :

| Entree | Resultat |
|---|---|
| CRC de `123456789` | `29B1` |
| Trame de `{"t":"hb","n":42,"up":125000}` | `{"t":"hb","n":42,"up":125000}*1CDE` |
