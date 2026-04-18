#!/usr/bin/env python3
NUMERO_DEST = "+33602185227"
VITESSE_MODEM = 115200
BMS_CONFIG = {
    "BMS_1": {
        "port": "/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_B0044HGC-if00-port0", 
        "file": "data_bms_1.json",
        "label": "Batterie Principale"
    },
    "BMS_2": {
        "port": "/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_0001-if00-port0", 
        "file": "data_bms_2.json",
        "label": "Batterie Auxiliaire"
    }
}
BAUD_RATE_BMS = 9600
NOMINAL_CAPACITY_AH = 98.0
INTERVALLE_BMS = 2.0
SEUILS_CRITIQUES = {
    "CapteurBatterie1": 40,
    "CapteurBatterie2": 38,
    "CapteurBatterieSE": 90,
    "CapteurSE": 35,
    "default": 40
}
BOUTON_1 = 3
BOUTON_2 = 27
BOUTON_3 = 22
BOUTON_4 = 5
BOUTON_5 = 6
LED_1 = 19
LED_2 = 26
LED_3 = 13
LED_4 = 12
BACKUP_FILE = "dernier_envoi.json"
LOG_MESSAGES_LIMIT = 20
