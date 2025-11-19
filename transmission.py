#!/usr/bin/env python3
# transmission.py

import os
import time
import json
import paho.mqtt.client as mqtt
from config import BACKUP_FILE

# --- Configuration MQTT ---
MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT   = 1883
MQTT_TOPIC  = "capteur/temperature"

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


def SauvegarderDernierEnvoi(temperatures, horodatage):
    """
    Sauvegarde les données en local si la connexion MQTT vient à tomber.
    """
    with open(BACKUP_FILE, "w") as fichier:
        json.dump({"horodatage": horodatage, "temperatures": temperatures}, fichier)


def EnvoyerDonneesBackup():
    """
    Tente d'envoyer en MQTT les données sauvegardées localement.
    Si l'envoi réussit, supprime le fichier de backup.
    """
    if not os.path.exists(BACKUP_FILE):
        return False

    with open(BACKUP_FILE, "r") as fichier:
        donnees = json.load(fichier)

    if EnvoyerDonneesMqtt(donnees["temperatures"], donnees["horodatage"]):
        os.remove(BACKUP_FILE)
        return True

    return False


def EnvoyerDonneesMqtt(temperatures, horodatage):
    """
    Envoie les données sur le topic MQTT.
    En cas d'échec, sauvegarde une fois les données et bascule en mode 'perdue'.
    """
    payload = json.dumps({
        "horodatage": horodatage,
        "temperatures": temperatures
    })

    try:
        client.publish(MQTT_TOPIC, payload)
        connectionStatus["perdue"] = False
        return True

    except Exception as e:
        # On sauvegarde une seule fois par déconnexion
        if not connectionStatus["perdue"]:
            SauvegarderDernierEnvoi(temperatures, horodatage)
            connectionStatus["perdue"] = True
        print(f"Erreur publication MQTT: {e}")
        return False
