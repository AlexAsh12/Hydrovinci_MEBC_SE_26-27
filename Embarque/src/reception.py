import paho.mqtt.client as mqtt
import threading
import RPi.GPIO as GPIO
import time
import config

MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT = 1883
MQTT_TOPIC_COMMAND = "commande/control"

interface = None
clignotement_actif = False

def ClignoterLED():
    global clignotement_actif
    clignotement_actif = True
    while config.message_recu:
        GPIO.output(config.LED_PIN, GPIO.HIGH)
        time.sleep(0.5)
        if not config.message_recu:
            break
        GPIO.output(config.LED_PIN, GPIO.LOW)
        time.sleep(0.5)
    GPIO.output(config.LED_PIN, GPIO.LOW)
    clignotement_actif = False

def OnMessage(client, userdata, message):
    commande = message.payload.decode("utf-8")
    if interface is not None:
        interface.recevoir_message(f"PC: {commande}")
    if not clignotement_actif:
        config.message_recu = True
        threading.Thread(target=ClignoterLED, daemon=True).start()

def EcouterMessages():
    client = mqtt.Client()  # <--- INSTANCE DÉDIÉE AU RECEPTION SEULEMENT
    client.on_message = OnMessage
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.subscribe(MQTT_TOPIC_COMMAND)
    client.loop_forever()  # NE PAS UTILISER loop_start ici

def LancerEcoute(iface):
    global interface
    interface = iface
    threading.Thread(target=EcouterMessages, daemon=True).start()

# ---------------------
# Clignotement LED Alerte Température
# ---------------------
def ClignoterLEDAlert():
    import config
    global clignotement_alert_actif
    clignotement_alert_actif = True
    while config.alerte_temperature:
        GPIO.output(config.LED_ALERT_PIN, GPIO.HIGH)
        time.sleep(0.5)
        if not config.alerte_temperature:
            break
        GPIO.output(config.LED_ALERT_PIN, GPIO.LOW)
        time.sleep(0.5)
    GPIO.output(config.LED_ALERT_PIN, GPIO.LOW)
    clignotement_alert_actif = False

clignotement_alert_actif = False
