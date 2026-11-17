// Toutes les broches de la carte, a un seul endroit.
// Changer de carte = changer d'environnement PlatformIO, pas le code.
#pragma once

#if defined(CARTE_WROOM32E)
// ESP32-WROOM-32E sur DevKit (carte provisoire)
// A eviter : GPIO6-11 (flash), 34-39 (entree seule), 0/2/12/15 (demarrage)
constexpr int PIN_UART_PI_RX = 16;  // UART2 RX  <- TX de la Pi (GPIO14, broche 8)
constexpr int PIN_UART_PI_TX = 17;  // UART2 TX  -> RX de la Pi (GPIO15, broche 10)

#elif defined(CARTE_S3)
// ESP32-S3 DevKitC-1 (carte finale) : a definir avec le routage du PCB HAT
#error "Broches ESP32-S3 pas encore definies : completer include/broches.h"

#else
#error "Aucune carte definie : utiliser un environnement de platformio.ini"
#endif

constexpr unsigned long VITESSE_UART_PI = 115200;
