"""
    Module de gestion de l'interface MQTT pour l'envoi de messages.

    Liste des fonctions :
    - EnvoyerMessageMqtt : Publie un message sur un topic MQTT
"""

import paho.mqtt.client as mqtt

# Configuration MQTT
MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT = 1883
MQTT_TOPIC = "commande/control"

# Connexion au broker MQTT
client = mqtt.Client()
client.connect(MQTT_BROKER, MQTT_PORT, 60)

print("\nInterface d'envoi MQTT en ligne de commande")
print("Tapez un message et appuyez sur Entrée pour l'envoyer.")
print("Tapez 'exit' pour quitter.\n")

def EnvoyerMessageMqtt(message):
    """
    Publie un message sur le topic MQTT spécifié.

    :param message: Chaîne de caractères à envoyer
    """
    client.publish(MQTT_TOPIC, message)
    print(f"Message envoyé : {message}\n")

while True:
    message = input("Entrez votre message : ")

    if message.lower() == "exit":
        print("Fermeture de l'interface...")
        break

    EnvoyerMessageMqtt(message)
