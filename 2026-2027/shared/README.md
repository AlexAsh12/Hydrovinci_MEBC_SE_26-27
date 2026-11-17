# shared/ - Ce que les cartes partagent

```
shared/
|-- PROTOCOLE_UART.md            # Specification de la liaison ESP32 <-> Pi (reference)
|-- c/protocole/
    |-- protocole.h / .c         # Implementation C unique (ESP32 ET Pi)
    |-- tests/test_protocole.c   # Tests (executes sur l'ESP32 et sur la Pi)
```

On modifie d'abord la spec, puis `protocole.c/.h`, dans le meme commit.

## Utilisation

**ESP32 (PlatformIO)** : `lib_extra_dirs = ../../shared/c` dans
`platformio.ini`, puis `#include <protocole.h>`.

**Pi** : le `Makefile` de `pi0/` compile `protocole.c` avec le programme.

## Tests

```bash
cd 2026-2027/pi0
make test
```

Sur l'ESP32 : flasher l'environnement PlatformIO `tests_wroom32e`, puis
ouvrir le moniteur serie.
