from datetime import datetime
import transmission

print("send_one.py started")
print("Using broker:", transmission.MQTT_BROKER, transmission.MQTT_PORT)
print("Using topic :", transmission.MQTT_TOPIC)

data = {"Speed_kmh": 42, "Humidity_%": 55}
ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

ok = transmission.EnvoyerDonneesMqtt(data, ts)
print("publish ok =", ok)
