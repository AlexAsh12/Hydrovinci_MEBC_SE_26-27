# Module de transmission des données via MQTT

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
global connection_status
connection_status = {"perdue": False}

client = mqtt.Client()

def SauvegarderDernierEnvoi(temperatures, horodatage):
    """
    Sauvegarde localement les dernières données envoyées.
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
    """
    if not os.path.exists(BACKUP_FILE):
        return False
        
    try:
        with open(BACKUP_FILE, "r") as fichier:
            donnees = json.load(fichier)
        
        print("\n" + "="*60)
        print("                   DONNÉES DE BACKUP")
        print(f"                   {donnees['horodatage']}")
        print("-"*60)
        
        for capteur, temperature in donnees["temperatures"].items():
            print(f"► {capteur:15} : {temperature:5.2f} °C")
        
        print("="*60)

        # Envoyer les données de backup
        if EnvoyerDonneesMqtt(donnees["temperatures"], donnees["horodatage"]):
            os.remove(BACKUP_FILE)
            print("Backup envoyé avec succès")
            return True
            
    except Exception as e:
        print(f"! Échec de l'envoi du backup : {e}")
    
    return False

def EnvoyerDonneesMqtt(temperatures, horodatage):
    """
    Envoie les données de température à un serveur MQTT.
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
        print("\n" + "-"*60)
        print("✓ Transmission MQTT réussie")
        print("-"*60)
        connection_status["perdue"] = False
        return True
        
    except Exception as e:
        print("-"*60)
        print(f"! ERREUR de transmission MQTT : {e}")
        print("-"*60)
        
        if not connection_status["perdue"]:
            # Sauvegarder uniquement lors de la première déconnexion
            SauvegarderDernierEnvoi(temperatures, horodatage)
            connection_status["perdue"] = True
            
        return False
