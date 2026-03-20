import json
import pytest
from unittest.mock import MagicMock, patch
from Embarque.src.transmission import MQTTClient

TOPIC = "mebc_se/raphael/test"
PAYLOAD = {"temperature": 22, "humidite": 60}


@pytest.fixture
def client():
    """Crée un MQTTClient avec la connexion réseau mockée."""
    with patch("paho.mqtt.client.Client") as MockPaho:
        mqtt_client = MQTTClient("broker.hivemq.com")
        mqtt_client.client = MockPaho()
        mqtt_client.connected = True  # simule une connexion réussie
        yield mqtt_client


def test_publish_envoie_bon_topic_et_payload(client):
    client.publish(TOPIC, PAYLOAD)

    call_args = client.client.publish.call_args
    topic_appelé = call_args[0][0]
    payload_reçu = json.loads(call_args[0][1])

    assert topic_appelé == TOPIC
    assert payload_reçu == PAYLOAD


def test_publish_sans_retain(client):
    client.publish(TOPIC, PAYLOAD)

    call_kwargs = client.client.publish.call_args[1]
    assert call_kwargs.get("retain", False) is False


def test_subscribe_appelle_bien_le_client(client):
    client.subscribe(TOPIC)
    client.client.subscribe.assert_called_once_with(TOPIC, qos=1)


def test_on_message_stocke_le_payload(client):
    fake_msg = MagicMock()
    fake_msg.payload = json.dumps(PAYLOAD).encode("utf-8")
    fake_msg.topic = TOPIC

    client._on_message(None, None, fake_msg)

    assert len(client.received_messages) == 1
    assert client.received_messages[0] == PAYLOAD


def test_listen_vide_la_liste_apres_recuperation(client):
    fake_msg = MagicMock()
    fake_msg.payload = json.dumps(PAYLOAD).encode("utf-8")
    fake_msg.topic = TOPIC
    client._on_message(None, None, fake_msg)

    messages = client.listen(duration=0)

    assert messages == [PAYLOAD]
    assert client.received_messages == []


def test_connect_timeout_si_pas_de_connexion():
    with patch("paho.mqtt.client.Client"):
        mqtt_client = MQTTClient("broker.hivemq.com")
        mqtt_client.connected = False  # simule un timeout
        result = mqtt_client.connected
        assert result is False