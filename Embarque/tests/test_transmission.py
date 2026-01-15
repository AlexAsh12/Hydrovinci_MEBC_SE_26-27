import json
import importlib
import types
import pytest

MODULE_PATH = "transmission"

PAYLOAD = {
    "Speed_kmh": 50,
    "Acceleration_ms2": 1.2,
    "Battery_State_%": 80,
    "Battery_Voltage_V": 48,
    "Battery_Temperature_C": 25,
    "Humidity_%": 60,
    "Wind_Speed_ms": 3,
    "Vehicle_Weight_kg": 1200,
}


@pytest.fixture
def transmission(monkeypatch, tmp_path):
    """
    Charge le module transmission en neutralisant init_mqtt()
    et en redirigeant BACKUP_FILE vers un fichier temporaire.
    """
    transmission = importlib.import_module(MODULE_PATH)

    # ⛔ Désactive toute connexion réseau
    monkeypatch.setattr(transmission, "init_mqtt", lambda: None, raising=True)

    # 📁 Backup dans un dossier temporaire
    backup_file = tmp_path / "backup.json"
    monkeypatch.setattr(transmission, "BACKUP_FILE", str(backup_file), raising=False)

    # 🔄 Recharge le module proprement
    transmission = importlib.reload(transmission)

    # 🔁 Reset état connexion
    transmission.connectionStatus["perdue"] = False

    return transmission


def test_envoyer_donnees_mqtt_success(monkeypatch, transmission):
    """
    Cas nominal : MQTT fonctionne → publish OK, pas de backup
    """
    published = {}

    class FakeClient:
        def publish(self, topic, payload, qos=0, **kwargs):
            published["topic"] = topic
            published["payload"] = payload
            published["qos"] = qos
            published.update(kwargs)  # ex: retain=True
            return types.SimpleNamespace()

    monkeypatch.setattr(transmission, "client", FakeClient(), raising=True)

    ts = "2026-01-15 12:34:56"
    ok = transmission.EnvoyerDonneesMqtt(PAYLOAD, ts)

    assert ok is True
    assert transmission.connectionStatus["perdue"] is False
    assert published["topic"] == transmission.MQTT_TOPIC
    assert published["qos"] == 1
    # Si ton code publie avec retain=True, tu peux décommenter :
    # assert published.get("retain") is True

    payload_dict = json.loads(published["payload"])
    assert payload_dict["horodatage"] == ts

    for key, value in PAYLOAD.items():
        assert payload_dict[key] == value


def test_envoyer_donnees_mqtt_failure_creates_backup(monkeypatch, transmission):
    """
    Cas erreur MQTT : exception → backup créé
    """
    class FakeClient:
        def publish(self, topic, payload, qos=0, **kwargs):
            raise RuntimeError("MQTT down")

    monkeypatch.setattr(transmission, "client", FakeClient(), raising=True)

    ts = "2026-01-15 12:00:00"
    ok = transmission.EnvoyerDonneesMqtt(PAYLOAD, ts)

    assert ok is False
    assert transmission.connectionStatus["perdue"] is True

    with open(transmission.BACKUP_FILE, "r", encoding="utf-8") as f:
        backup = json.load(f)

    assert backup["horodatage"] == ts
    assert backup["data"] == PAYLOAD


def test_no_multiple_backup_when_already_perdue(monkeypatch, transmission):
    """
    Si déjà en mode 'perdue', on ne resauvegarde pas à chaque échec
    """
    transmission.connectionStatus["perdue"] = True
    calls = {"count": 0}

    def fake_save(data, ts):
        calls["count"] += 1

    monkeypatch.setattr(transmission, "SauvegarderDernierEnvoi", fake_save, raising=True)

    class FakeClient:
        def publish(self, topic, payload, qos=0, **kwargs):
            raise RuntimeError("MQTT down")

    monkeypatch.setattr(transmission, "client", FakeClient(), raising=True)

    ok = transmission.EnvoyerDonneesMqtt(PAYLOAD, "2026-01-15 12:01:00")

    assert ok is False
    assert calls["count"] == 0


def test_sauvegarder_dernier_envoi_format(transmission):
    """
    Test direct de SauvegarderDernierEnvoi : structure JSON correcte
    """
    ts = "2026-01-15 10:00:00"
    transmission.SauvegarderDernierEnvoi(PAYLOAD, ts)

    with open(transmission.BACKUP_FILE, "r", encoding="utf-8") as f:
        backup = json.load(f)

    assert backup == {"horodatage": ts, "data": PAYLOAD}
