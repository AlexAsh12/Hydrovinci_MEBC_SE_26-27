#!/usr/bin/env python3
# transmission.py

import os
import time
import json
from src.config import BACKUP_FILE
import paho.mqtt.client as mqtt


# --- Configuration MQTT ---
MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT   = 1883
MQTT_TOPIC  ="mebc_se/raphael/test"


# --- État de la connexion ---
connectionStatus = {"perdue": False, "connected": False}

# --- Initialisation du client MQTT ---
client = mqtt.Client()

def on_connect(client, userdata, flags, rc):
    """Callback appelé quand la connexion est établie"""
    if rc == 0:
        print("✓ MQTT connecté avec succès")
        connectionStatus["connected"] = True
    else:
        print(f"✗ Échec connexion MQTT, code: {rc}")
        connectionStatus["connected"] = False

def on_publish(client, userdata, mid):
    """Callback appelé quand un message est publié"""
    print(f"✓ Message publié (mid: {mid})")

# Attacher les callbacks
client.on_connect = on_connect
client.on_publish = on_publish

def init_mqtt():
    """
    Tente de se connecter en boucle au broker MQTT.
    Ne bloque pas l'import si le réseau est indisponible au démarrage.
    """
    while True:
        try:
            client.connect(MQTT_BROKER, MQTT_PORT, 60)
            client.loop_start()
            # Attendre que la connexion soit établie
            timeout = 10
            start = time.time()
            while not connectionStatus["connected"] and (time.time() - start) < timeout:
                time.sleep(0.1)
            
            if connectionStatus["connected"]:
                print("MQTT prêt à envoyer")
                break
            else:
                print("Timeout connexion, nouvelle tentative...")
                time.sleep(5)
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
    if not connectionStatus["connected"]:
        print("⚠ Client MQTT non connecté")
        if not connectionStatus["perdue"]:
            SauvegarderDernierEnvoi(data, horodatage)
            connectionStatus["perdue"] = True
        return False
    
    # On construit un payload JSON générique
    payload_dict = {
        "horodatage": horodatage,
        **data,  # injecte toutes les clés/valeurs du dict
    }
    payload = json.dumps(payload_dict, ensure_ascii=False)
    
    print(f"Envoi sur {MQTT_TOPIC}: {payload}")

    try:
        # QoS 1 conseillé pour télémétrie (au moins une fois)
        result = client.publish(
            MQTT_TOPIC,
            payload,
            qos=1,
            retain=True
        )
        
        # Attendre que le message soit vraiment envoyé
        result.wait_for_publish(timeout=5)
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print("✓ Publication réussie")
            connectionStatus["perdue"] = False
            return True
        else:
            print(f"✗ Erreur publication, code: {result.rc}")
            return False

    except Exception as e:
        # On sauvegarde une seule fois par déconnexion
        if not connectionStatus["perdue"]:
            SauvegarderDernierEnvoi(data, horodatage)
            connectionStatus["perdue"] = True
        print(f"✗ Erreur publication MQTT: {e}")
        return False