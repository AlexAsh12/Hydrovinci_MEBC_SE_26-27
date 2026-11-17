// Point d'entree Arduino : uniquement le lien avec le materiel.
// Toute la logique est en C dans app.c.

#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <stdarg.h>

#include "app.h"
#include "broches.h"
#include <protocole.h>

static HardwareSerial& portPi = Serial2;

extern "C" {

uint32_t plateforme_millis(void) { return millis(); }

void plateforme_ecrire(const uint8_t* data, size_t len) { portPi.write(data, len); }

size_t plateforme_place_tx(void) { return static_cast<size_t>(portPi.availableForWrite()); }

void plateforme_attendre_fin_emission(void) { portPi.flush(); }

void plateforme_changer_vitesse(uint32_t vitesse) { portPi.updateBaudRate(vitesse); }

void plateforme_redemarrer(void) {
    delay(20);
    ESP.restart();
}

void plateforme_log(const char* format, ...) {
    char ligne[160];
    va_list args;
    va_start(args, format);
    vsnprintf(ligne, sizeof ligne, format, args);
    va_end(args);
    Serial.print(ligne);
}

bool plateforme_imu_i2c_ecrire(uint8_t adresse, const uint8_t* data, size_t len) {
    Wire.beginTransmission(adresse);
    Wire.write(data, len);
    return Wire.endTransmission() == 0;
}

bool plateforme_imu_i2c_lire(uint8_t adresse, uint8_t* data, size_t len) {
    if (Wire.requestFrom(adresse, len) != len) {
        while (Wire.available()) Wire.read();
        return false;
    }
    for (size_t i = 0; i < len; ++i) data[i] = static_cast<uint8_t>(Wire.read());
    return true;
}

bool plateforme_imu_int_actif(void) { return digitalRead(PIN_IMU_INT) == LOW; }

void plateforme_imu_rst(int niveau) { digitalWrite(PIN_IMU_RST, niveau ? HIGH : LOW); }

// 1-Wire : broche en drain ouvert (1 = ligne relachee, tiree au 3,3 V par
// la resistance). Les creneaux de quelques us sont faits interruptions
// coupees pour que le timing soit exact.
static portMUX_TYPE verrou1w = portMUX_INITIALIZER_UNLOCKED;
static const gpio_num_t BROCHE_1W = static_cast<gpio_num_t>(PIN_ONEWIRE);

bool plateforme_1w_reset(void) {
    gpio_set_level(BROCHE_1W, 0);
    esp_rom_delay_us(480);
    portENTER_CRITICAL(&verrou1w);
    gpio_set_level(BROCHE_1W, 1);
    esp_rom_delay_us(70);
    const bool presence = gpio_get_level(BROCHE_1W) == 0;
    portEXIT_CRITICAL(&verrou1w);
    esp_rom_delay_us(410);
    return presence;
}

int plateforme_1w_bit(int bit) {
    int lu = 0;
    portENTER_CRITICAL(&verrou1w);
    gpio_set_level(BROCHE_1W, 0);
    if (bit) {
        esp_rom_delay_us(6);
        gpio_set_level(BROCHE_1W, 1);
        esp_rom_delay_us(9);
        lu = gpio_get_level(BROCHE_1W);
        portEXIT_CRITICAL(&verrou1w);
        esp_rom_delay_us(55);
    } else {
        esp_rom_delay_us(60);
        gpio_set_level(BROCHE_1W, 1);
        portEXIT_CRITICAL(&verrou1w);
        esp_rom_delay_us(10);
    }
    return lu;
}

}  // extern "C"

void setup() {
    Serial.begin(115200);  // USB : logs de debug
    // Tampons agrandis pour le haut debit (a regler AVANT begin)
    portPi.setRxBufferSize(4096);
    portPi.setTxBufferSize(8192);
    portPi.begin(PROTO_VITESSE_DEFAUT, SERIAL_8N1, PIN_UART_PI_RX, PIN_UART_PI_TX);

    pinMode(PIN_IMU_INT, INPUT_PULLUP);
    pinMode(PIN_IMU_RST, OUTPUT);
    digitalWrite(PIN_IMU_RST, HIGH);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    Wire.setTimeOut(20);

    gpio_reset_pin(BROCHE_1W);
    gpio_set_direction(BROCHE_1W, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(BROCHE_1W, GPIO_PULLUP_ONLY);  // en plus de la 4,7 kOhm
    gpio_set_level(BROCHE_1W, 1);
    delay(100);
    app_init();
}

void loop() {
    uint8_t tampon[256];
    int dispo;
    while ((dispo = portPi.available()) > 0) {
        const size_t n = portPi.read(tampon, dispo < static_cast<int>(sizeof tampon) ? dispo : sizeof tampon);
        app_octets_recus(tampon, n);
    }
    app_tick();
}
