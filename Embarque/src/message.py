import RPi.GPIO as GPIO
import time
import paho.mqtt.client as mqtt
import config

MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT = 1883
MQTT_TOPIC_MESSAGE = "commande/reponse"

client = mqtt.Client()
client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_start()  # ENVOI = loop_start

interface = None

def EnvoyerMessage(message):
    try:
        client.publish(MQTT_TOPIC_MESSAGE, message)
        if interface:
            interface.recevoir_message(f"RPI: {message}")
        config.message_recu = False
        GPIO.output(config.LED_PIN, GPIO.LOW)
    except:
        pass

def BoutonPresse(channel):
    message = {
        config.BUTTON_OK: "OK",
        config.BUTTON_NOK: "NOK",
        config.BUTTON_MAYDAY: "MAYDAY",
        config.BUTTON_PANPAN: "PANPAN"
    }.get(channel, "UNKNOWN")
    EnvoyerMessage(message)

def ConfigurerBoutons():
    GPIO.setmode(GPIO.BCM)
    for bouton in [config.BUTTON_OK, config.BUTTON_NOK, config.BUTTON_MAYDAY, config.BUTTON_PANPAN]:
        GPIO.setup(bouton, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        GPIO.add_event_detect(bouton, GPIO.FALLING, callback=BoutonPresse, bouncetime=300)
    GPIO.setup(config.LED_PIN, GPIO.OUT)

def LancerEcouteBoutons(iface):
    global interface
    interface = iface
    ConfigurerBoutons()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        GPIO.cleanup()
