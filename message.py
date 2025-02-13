"""
    Module de gestion des boutons et envoi de messages via MQTT.

    Liste des fonctions :
    - EnvoyerMessage : Envoie un message MQTT
    - BoutonOkPresse : Gère l'appui sur le bouton "OK"
    - BoutonNokPresse : Gère l'appui sur le bouton "NOK"
    - ConfigurerBoutons : Initialise la configuration des boutons GPIO
    - LancerEcouteBoutons : Démarre l'écoute des boutons en continu
"""

import RPi.GPIO as GPIO
import time
import paho.mqtt.client as mqtt

# Configuration du broker MQTT
MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT = 1883
MQTT_TOPIC_MESSAGE = "commande/reponse"

# Définition des broches GPIO pour les boutons
BUTTON_OK = 17
BUTTON_NOK = 27

# Initialisation du client MQTT
client = mqtt.Client()

def EnvoyerMessage(message):
    """
    Envoie un message via MQTT.

    :param message: Chaîne de caractères à envoyer
    """
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.publish(MQTT_TOPIC_MESSAGE, message)
        client.disconnect()
        print(f"Message envoyé : {message}")
    except Exception as e:
        print(f"Erreur d'envoi MQTT : {e}")

def BoutonOkPresse(channel):
    """
    Fonction appelée lors de l'appui sur le bouton "OK".
    """
    print("Bouton OK appuyé, envoi du message...")
    EnvoyerMessage("OK")

def BoutonNokPresse(channel):
    """
    Fonction appelée lors de l'appui sur le bouton "NOK".
    """
    print("Bouton NOK appuyé, envoi du message...")
    EnvoyerMessage("NOK")

def ConfigurerBoutons():
    """
    Configure les GPIO pour détecter les appuis sur les boutons.
    """
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(BUTTON_OK, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    GPIO.setup(BUTTON_NOK, GPIO.IN, pull_up_down=GPIO.PUD_UP)

    GPIO.add_event_detect(BUTTON_OK, GPIO.FALLING, callback=BoutonOkPresse, bouncetime=300)
    GPIO.add_event_detect(BUTTON_NOK, GPIO.FALLING, callback=BoutonNokPresse, bouncetime=300)

def LancerEcouteBoutons():
    """
    Lance l'écoute des boutons en continu.
    """
    ConfigurerBoutons()
    print(f"En attente d'un appui sur les boutons (GPIO {BUTTON_OK} et GPIO {BUTTON_NOK})...")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nArrêt du programme.")
        GPIO.cleanup()
