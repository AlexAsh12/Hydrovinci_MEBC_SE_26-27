import json
import time
import threading
import statistics
import datetime
from pathlib import Path
import paho.mqtt.client as mqtt

# ÔöÇÔöÇÔöÇ Persistence paths ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
_BASE_DIR   = Path(__file__).resolve().parent.parent
_DATA_DIR   = _BASE_DIR / "data"
_DATA_DIR.mkdir(exist_ok=True)
_STATE_FILE   = _DATA_DIR / "state.json"
_HISTORY_FILE = _DATA_DIR / "history.json"

# ÔöÇÔöÇÔöÇ Topics ÔÇö adapte ├á ta nomenclature ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
TOPICS = {
    "cell_voltages":  "mebc_se/bms/cells/voltages",   # JSON list [3.24, 3.22, ...]
    "pack_voltage":   "mebc_se/bms/pack/voltage",      # float "48.6"
    "current":        "mebc_se/bms/pack/current",      # float "-32.4"
    "soc":            "mebc_se/bms/pack/soc",          # float "75.0"
    "capacity_ah":    "mebc_se/bms/pack/capacity",     # float "150.0"
    "remaining_ah":   "mebc_se/bms/pack/remaining",    # float "112.5"
    "temperature":    "mebc_se/bms/pack/temperature",  # float ou list
    "speed_knots":    "mebc_se/bms/nav/speed",         # float "6.2"
    "alarms":         "mebc_se/bms/alarms",            # JSON list []
    "bms_connection": "mebc_se/bms/connection",        # JSON {"BMS_1": true, ...}
    "sim_connection": "mebc_se/sim/connection",        # JSON bool
    "log":            "mebc_se/log",                   # texte brut (ligne de console Pi)
}

# ÔöÇÔöÇÔöÇ ├ëtat partag├® ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
_lock = threading.Lock()

bms_state = {
    "cell_voltages": [3.20] * 15,
    "pack_voltage":  48.0,
    "current":       0.0,
    "soc":           50.0,
    "capacity_ah":   150.0,
    "remaining_ah":  75.0,
    "temperature":   [25.0],
    "speed_knots":   0.0,
    "alarms":        [],
    "connected":     False,
    "last_update":   None,
    "bms_connection": {"BMS_1": False, "BMS_2": False},
    "sim_connection": False,
}

capacity_history = []
MAX_HISTORY = 120

# Lignes de console recues du Pi (terminal temps reel du dashboard)
log_lines = []
MAX_LOG = 300


# ÔöÇÔöÇÔöÇ Persistence helpers ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

def _save_state():
    try:
        with open(_STATE_FILE, "w", encoding="utf-8") as f:
            json.dump(bms_state, f, separators=(",", ":"))
    except Exception as e:
        print(f"[persistence] Cannot save state.json: {e}")


def _save_history():
    try:
        with open(_HISTORY_FILE, "w", encoding="utf-8") as f:
            json.dump(capacity_history, f, separators=(",", ":"))
    except Exception as e:
        print(f"[persistence] Cannot save history.json: {e}")


def _load_persisted():
    """Called once at module load to restore the last saved state."""
    if _STATE_FILE.exists():
        try:
            saved = json.loads(_STATE_FILE.read_text(encoding="utf-8"))
            for k, v in saved.items():
                if k in bms_state and k != "connected":
                    bms_state[k] = v
            print(f"[persistence] state.json loaded from {_STATE_FILE}")
        except Exception as e:
            print(f"[persistence] Cannot load state.json: {e}")

    if _HISTORY_FILE.exists():
        try:
            saved_hist = json.loads(_HISTORY_FILE.read_text(encoding="utf-8"))
            capacity_history.extend(saved_hist[-MAX_HISTORY:])
            print(f"[persistence] history.json loaded ({len(capacity_history)} entries)")
        except Exception as e:
            print(f"[persistence] Cannot load history.json: {e}")


_load_persisted()


def get_state():
    with _lock:
        return dict(bms_state)

def get_history():
    with _lock:
        return list(capacity_history)

def get_logs():
    with _lock:
        return list(log_lines)


class MQTTClient:
    def __init__(self, broker: str, port: int = 1883):
        self.broker = broker
        self.port = port

        self.client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        self.client.on_connect    = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message    = self._on_message

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        with _lock:
            bms_state["connected"] = (rc == 0)
        if rc == 0:
            for topic in TOPICS.values():
                client.subscribe(topic, qos=1)
            print(f"Ô£ô Connect├® ├á {self.broker}, abonn├® ├á {len(TOPICS)} topics")
        else:
            print(f"Ô£ù ├ëchec connexion, code: {rc}")

    def _on_disconnect(self, client, userdata, flags, rc, properties=None):
        with _lock:
            bms_state["connected"] = False
        print("Ô£ù D├®connect├® du broker")

    def _on_message(self, client, userdata, message):
        topic = message.topic
        raw   = message.payload.decode("utf-8").strip()

        # Flux de logs : lignes de console brutes (pas du JSON), on les accumule
        if topic == TOPICS["log"]:
            with _lock:
                log_lines.append(raw)
                if len(log_lines) > MAX_LOG:
                    del log_lines[:len(log_lines) - MAX_LOG]
            return

        topic_to_key = {v: k for k, v in TOPICS.items()}
        key = topic_to_key.get(topic)
        if key is None:
            return

        try:
            value = json.loads(raw)
        except json.JSONDecodeError:
            try:
                value = float(raw)
            except ValueError:
                return

        history_updated = False
        with _lock:
            bms_state[key] = value
            bms_state["last_update"] = datetime.datetime.now().strftime("%H:%M:%S")

            if key == "remaining_ah":
                try:
                    ah_val = float(value)
                except (TypeError, ValueError):
                    ah_val = None
                if ah_val is not None:
                    capacity_history.append({
                        "t":  datetime.datetime.now().strftime("%H:%M"),
                        "ah": ah_val,
                    })
                if len(capacity_history) > MAX_HISTORY:
                    capacity_history.pop(0)
                history_updated = True

        _save_state()
        if history_updated:
            _save_history()

        print(f"Ô£ô [{key}] {value}")

    def publish(self, topic: str, data: dict):
        payload = json.dumps(data)
        self.client.publish(topic, payload, qos=1)
        print(f"Ô£ô Envoy├® sur '{topic}' : {data}")

    def connect_background(self):
        """Connexion non-bloquante ÔÇö tourne en thread daemon."""
        try:
            self.client.connect(self.broker, self.port, keepalive=60)
            self.client.loop_start()
        except Exception as e:
            print(f"[MQTT] Connexion impossible : {e} ÔÇö donn├®es de d├®mo actives")


# ÔöÇÔöÇÔöÇ Instance globale lanc├®e au chargement du module ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
mqtt_client = MQTTClient("broker.hivemq.com")
mqtt_client.connect_background()
