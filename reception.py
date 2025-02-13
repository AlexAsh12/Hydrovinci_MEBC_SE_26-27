"""
    Module de réception des messages MQTT.

    Liste des fonctions :
    - OnMessage : Callback exécuté lorsqu'un message est reçu
    - EcouterMessages : Écoute les messages MQTT sur un topic donné
    - LancerEcoute : Démarre l'écoute MQTT dans un thread séparé
"""

import paho.mqtt.client as mqtt
import threading

# Configuration MQTT
MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT = 1883
MQTT_TOPIC_COMMAND = "commande/control"

client = mqtt.Client()

def OnMessage(client, userdata, message):
    """
    Fonction appelée lorsqu'un message est reçu sur le topic MQTT.

    :param client: Instance du client MQTT
    :param userdata: Données utilisateur
    :param message: Message MQTT reçu
    """
    commande = message.payload.decode("utf-8")
    print(f"\nMessage reçu du PC distant : {commande}\n")

def EcouterMessages():
    """
    Démarre l'écoute des messages MQTT et attend les commandes entrantes.
    """
    client.on_message = OnMessage
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.subscribe(MQTT_TOPIC_COMMAND)
    print(f"Abonné au topic '{MQTT_TOPIC_COMMAND}', en attente de commandes...\n")
    client.loop_forever()

def LancerEcoute():
    """
    Lance l'écoute des messages MQTT dans un thread séparé.
    """
    thread = threading.Thread(target=EcouterMessages, daemon=True)
    thread.start()
