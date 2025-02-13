"""
    Module de transmission des données via MQTT.

    Liste des fonctions :
    - SauvegarderDernierEnvoi : Sauvegarde les données en local en cas de coupure réseau
    - EnvoyerDonneesBackup : Envoie les données sauvegardées si la connexion est rétablie
    - EnvoyerDonneesMqtt : Envoie les données de température via MQTT
"""

import paho.mqtt.client as mqtt
import json
import os
from config import API_KEY

# Configuration MQTT
MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT = 1883
MQTT_TOPIC = "capteur/temperature"
BACKUP_FILE = "dernier_envoi.json"

# Variables globales
global connectionStatus
connectionStatus = {"perdue": False}

client = mqtt.Client()

def SauvegarderDernierEnvoi(temperatures, horodatage):
    """
    Sauvegarde localement les dernières données envoyées.

    :param temperatures: Dictionnaire des températures des capteurs
    :param horodatage: Horodatage du relevé
    """
    donnees = {
        "horodatage": horodatage,
        "temperatures": temperatures
    }
    with open(BACKUP_FILE, "w") as fichier:
        json.dump(donnees, fichier, indent=4)
        print("\nDonnées sauvegardées dans le backup")

def EnvoyerDonneesBackup():
    """
    Tente d'envoyer les dernières données sauvegardées.

    :return: True si l'envoi a réussi, False sinon
    """
    if not os.path.exists(BACKUP_FILE):
        return False

    try:
        with open(BACKUP_FILE, "r") as fichier:
            donnees = json.load(fichier)

        print("\nDonnées de backup à envoyer...")

        if EnvoyerDonneesMqtt(donnees["temperatures"], donnees["horodatage"]):
            os.remove(BACKUP_FILE)
            print("Backup envoyé avec succès")
            return True

    except Exception as e:
        print(f"Échec de l'envoi du backup : {e}")

    return False

def EnvoyerDonneesMqtt(temperatures, horodatage):
    """
    Envoie les données de température à un serveur MQTT.

    :param temperatures: Dictionnaire des températures des capteurs
    :param horodatage: Horodatage du relevé
    :return: True si l'envoi a réussi, False sinon
    """
    payload = {
        "horodatage": horodatage,
        "temperatures": temperatures
    }

    jsonPayload = json.dumps(payload, indent=4)

    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.publish(MQTT_TOPIC, jsonPayload)
        client.disconnect()
        print("Transmission MQTT réussie")
        connectionStatus["perdue"] = False
        return True

    except Exception as e:
        print(f"Erreur de transmission MQTT : {e}")

        if not connectionStatus["perdue"]:
            SauvegarderDernierEnvoi(temperatures, horodatage)
            connectionStatus["perdue"] = True

        return False
