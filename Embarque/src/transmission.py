import json
import time
import paho.mqtt.client as mqtt

class MQTTClient:
    def __init__(self, broker: str, port: int = 1883):
        self.broker = broker
        self.port = port
        self.connected = False
        self.received_messages = []

        self.client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        self.connected = rc == 0
        print("✓ Connecté" if self.connected else f"✗ Échec connexion, code: {rc}")

    def _on_message(self, client, userdata, message):
        payload = json.loads(message.payload.decode("utf-8"))
        self.received_messages.append(payload)
        print(f"✓ Message reçu : {payload}")

    def connect(self):
        self.client.connect(self.broker, self.port)
        self.client.loop_start()
        start = time.time()
        while not self.connected and (time.time() - start) < 5:
            time.sleep(0.1)
        return self.connected

    def disconnect(self):
        self.client.loop_stop()
        self.client.disconnect()

    def publish(self, topic: str, data: dict):
        payload = json.dumps(data)
        self.client.publish(topic, payload, qos=1)  # sans retain
        print(f"✓ Envoyé sur '{topic}' : {data}")

    def subscribe(self, topic: str):
        self.client.subscribe(topic, qos=1)
        print(f"✓ Abonné à '{topic}'")

    def listen(self, duration: int = 5) -> list:
        print(f"⏳ Écoute pendant {duration}s...")
        time.sleep(duration)
        messages = self.received_messages.copy()
        self.received_messages.clear()
        return messages


# --- Utilisation ---
TOPIC = "mebc_se/raphael/test"

client = MQTTClient("broker.hivemq.com")
client.connect()

client.subscribe(TOPIC)  # s'abonner AVANT de publier

client.publish(TOPIC, {"temperature": 22, "humidite": 60})

messages = client.listen(duration=5)
print(f"Messages reçus : {messages}")

client.disconnect()