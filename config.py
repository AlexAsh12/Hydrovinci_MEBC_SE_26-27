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
BOUTON_1 = 3   # À modifier
BOUTON_2 = 27  # À modifier
BOUTON_3 = 22  # À modifier
BOUTON_4 = 5   # À modifier
BOUTON_5 = 6   # À modifier
LED_1 = 19     # À modifier
LED_2 = 26     # À modifier
LED_3 = 13     # À modifier
LED_4 = 12     # À modifier
LOG_MESSAGES_LIMIT = 20

# --- MQTT ---
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
