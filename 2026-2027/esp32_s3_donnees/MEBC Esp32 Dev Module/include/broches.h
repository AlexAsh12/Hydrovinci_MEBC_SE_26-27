// Toutes les broches de la carte, a un seul endroit.
// Changer de carte = changer d'environnement PlatformIO, pas le code.
#pragma once

#if defined(CARTE_WROOM32E)
// ESP32-WROOM-32E sur DevKit (carte provisoire)
// A eviter : GPIO6-11 (flash), 34-39 (entree seule), 0/2/12/15 (demarrage)
constexpr int PIN_UART_PI_RX = 16;  // UART2 RX  <- TX de la Pi (GPIO14, broche 8)
constexpr int PIN_UART_PI_TX = 17;  // UART2 TX  -> RX de la Pi (GPIO15, broche 10)

// Bus I2C (IMU BNO085 en 0x4A, plus tard INA226)
constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;
constexpr int PIN_IMU_INT = 4;   // BNO085 INT : a 0 quand une mesure est prete
constexpr int PIN_IMU_RST = 23;  // BNO085 RST : a 0 = reset

// Bus 1-Wire des sondes DS18B20 (resistance de 4,7 kOhm vers le 3,3 V)
constexpr int PIN_ONEWIRE = 27;

#elif defined(CARTE_S3)
// ESP32-S3 DevKitC-1 (carte finale). Numeros = GPIO sérigraphies sur la carte.
// A eviter : 0, 3, 45, 46 (demarrage), 19/20 (USB natif), 26-32 (flash),
// 33-37 (PSRAM des modules R8), 43/44 (logs USB par le port "UART").
constexpr int PIN_UART_PI_RX = 18;  // UART RX  <- TX de la Pi (GPIO14, broche 8)
constexpr int PIN_UART_PI_TX = 17;  // UART TX  -> RX de la Pi (GPIO15, broche 10)

// Bus I2C (IMU BNO085 en 0x4A, plus tard INA226) : broches I2C par defaut du S3
constexpr int PIN_I2C_SDA = 8;
constexpr int PIN_I2C_SCL = 9;
constexpr int PIN_IMU_INT = 4;   // BNO085 INT : a 0 quand une mesure est prete
constexpr int PIN_IMU_RST = 5;   // BNO085 RST : a 0 = reset

// Bus 1-Wire des sondes DS18B20 (resistance de 4,7 kOhm vers le 3,3 V)
constexpr int PIN_ONEWIRE = 7;

#else
#error "Aucune carte definie : utiliser un environnement de platformio.ini"
#endif

// La vitesse de la liaison Pi est definie par le protocole (PROTO_VITESSE_DEFAUT).
