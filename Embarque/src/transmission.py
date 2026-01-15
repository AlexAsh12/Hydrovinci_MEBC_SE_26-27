#!/usr/bin/env python3
# transmission.py

import os
import time
import json
from config import BACKUP_FILE
import paho.mqtt.client as mqtt


# --- Configuration MQTT ---
MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT   = 1883
MQTT_TOPIC  ="mebc_se/raphael/test"


# --- État de la connexion ---
connectionStatus = {"perdue": False}

# --- Initialisation du client MQTT ---
client = mqtt.Client()

def init_mqtt():
    """
    Tente de se connecter en boucle au broker MQTT.
    Ne bloque pas l'import si le réseau est indisponible au démarrage.
    """
    while True:
        try:
            client.connect(MQTT_BROKER, MQTT_PORT, 60)
            client.loop_start()
            print("MQTT connecté")
            break
        except Exception as e:
            print(f"Échec connexion MQTT: {e}, nouvelle tentative dans 5s")
            time.sleep(5)

# Lancer la connexion dès l'import du module
init_mqtt()


def SauvegarderDernierEnvoi(data: dict, horodatage: str) -> None:
    """
    Sauvegarde les données (dictionnaire générique) en local
    si la connexion MQTT vient à tomber.
    """
    backup_payload = {
        "horodatage": horodatage,
        "data": data
    }

    with open(BACKUP_FILE, "w") as fichier:
        json.dump(backup_payload, fichier, ensure_ascii=False, indent=2)



def EnvoyerDonneesMqtt(data: dict, horodatage: str) -> bool:
    """
    Envoie un dictionnaire (générique) sur le topic MQTT.
    En cas d'échec, sauvegarde une fois les données et bascule en mode 'perdue'.

    data: dict contenant n'importe quelles mesures (températures, vitesse, etc.)
    horodatage: timestamp (string)
    """
    # On construit un payload JSON générique
    payload_dict = {
        "horodatage": horodatage,
        **data,  # injecte toutes les clés/valeurs du dict
    }
    payload = json.dumps(payload_dict, ensure_ascii=False)

    try:
        # QoS 1 conseillé pour télémétrie (au moins une fois)
        client.publish(
    MQTT_TOPIC,
    payload,
    qos=1,
    retain=True
)

        connectionStatus["perdue"] = False
        return True

    except Exception as e:
        # On sauvegarde une seule fois par déconnexion
        if not connectionStatus["perdue"]:
            SauvegarderDernierEnvoi(data, horodatage)  # adapte cette fonction aussi
            connectionStatus["perdue"] = True
        print(f"Erreur publication MQTT: {e}")
        return False
