# Module de transmission des données via MQTT

# Liste des fonctions :
# - EnvoyerDonneesMqtt : Envoie les températures au serveur MQTT
# - SauvegarderDonneesEchouees : Sauvegarde localement les données en cas d'échec

import paho.mqtt.client as mqtt
import json
import os
from config import API_KEY

# Configuration MQTT
MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT = 1883
MQTT_TOPIC = "capteur/temperature"

client = mqtt.Client()

"""
    Envoie les données de température à un serveur MQTT.

    :param temperatures: Dictionnaire contenant les températures des capteurs
    :param horodatage: Timestamp correspondant à la lecture des températures
"""
def EnvoyerDonneesMqtt(temperatures, horodatage):

    payload = {
        "horodatage": horodatage,
        "temperatures": {capteur: round(temp) for capteur, temp in temperatures.items()},
    }

    jsonPayload = json.dumps(payload, indent=4)

    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.publish(MQTT_TOPIC, jsonPayload)
        client.disconnect()
        print("Données envoyées avec succès via MQTT !")
    except Exception as e:
        print(f"Erreur lors de l'envoi MQTT : {e}")
        SauvegarderDonneesEchouees(temperatures, horodatage)



"""
    Sauvegarde localement les données si l'envoi échoue.

    :param temperatures: Dictionnaire des températures à sauvegarder
    :param horodatage: Timestamp des données sauvegardées
"""
def SauvegarderDonneesEchouees(temperatures, horodatage):

    donnees = {"horodatage": horodatage, "temperatures": temperatures}
    with open("failed_transmissions.json", "a") as fichier:
        json.dump(donnees, fichier)
        fichier.write("\n")