#!/usr/bin/env python3
"""
Publisher MQTT — Côté Embarqué (Pi Zero)
Envoie les données BMS au dashboard via le broker HiveMQ.

RÈGLE DE SÉCURITÉ : Si le MQTT est mort, le BMS continue normalement.
Aucune exception de ce module ne doit remonter au code appelant.
"""

import sys
import json
import threading
import paho.mqtt.client as mqtt
import config

from interface import interface_bateau

# --- Topics MQTT (doivent correspondre à ceux du dashboard Ground) ---
TOPICS = {
    "cell_voltages": "mebc_se/bms/cells/voltages",
    "pack_voltage":  "mebc_se/bms/pack/voltage",
    "current":       "mebc_se/bms/pack/current",
    "soc":           "mebc_se/bms/pack/soc",
    "capacity_ah":   "mebc_se/bms/pack/capacity",
    "remaining_ah":  "mebc_se/bms/pack/remaining",
    "temperature":   "mebc_se/bms/pack/temperature",
    "alarms":        "mebc_se/bms/alarms",
    "bms_connection":"mebc_se/bms/connection",
    "sim_connection":"mebc_se/sim/connection",
    "log":           "mebc_se/log",
}

# --- Client MQTT global ---
_client = None
_connected = False
# Dernier statut SIM7600 connu (republie a chaque (re)connexion au broker).
_sim_status = False


def _on_connect(client, userdata, flags, rc, properties=None):
    global _connected
    _connected = (rc == 0)
    if rc == 0:
        print("[MQTT] Connecté au broker")
        interface_bateau.update_statut("MQTT", True)
        # Republier le statut SIM7600 (il a pu etre defini avant la connexion au broker)
        publish_sim_connection(_sim_status)
    else:
        print(f"[MQTT] Echec connexion, code: {rc}")
        interface_bateau.update_statut("MQTT", False)


def _on_disconnect(client, userdata, flags, rc, properties=None):
    global _connected
    _connected = False
    interface_bateau.update_statut("MQTT", False)
    print("[MQTT] Deconnecte du broker (reconnexion automatique)")


def init():
    """Initialise le client MQTT en arriere-plan. Ne bloque jamais."""
    global _client
    try:
        _client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        _client.on_connect = _on_connect
        _client.on_disconnect = _on_disconnect
        # connect_async + loop_start : la connexion se fait en arriere-plan.
        # Si le broker est injoignable, ca ne bloque pas le demarrage.
        _client.connect_async(config.MQTT_BROKER, config.MQTT_PORT, keepalive=60)
        _client.loop_start()
        print(f"[MQTT] Connexion en cours vers {config.MQTT_BROKER}:{config.MQTT_PORT}")
    except Exception as e:
        print(f"[MQTT] Impossible d'initialiser : {e} — le BMS tourne sans MQTT")
        _client = None


def publish_bms(data):
    """
    Publie les données d'un BMS sur les topics MQTT.
    Ne lève JAMAIS d'exception — le BMS ne doit pas s'arrêter à cause du MQTT.
    """
    if _client is None or not _connected:
        return

    try:
        bms_id = data.get("bms_id", "BMS_1")
        
        # Publier chaque donnée sur son topic
        _client.publish(TOPICS["cell_voltages"], json.dumps(data["cells"]), qos=1)
        _client.publish(TOPICS["pack_voltage"],  str(data["voltage"]),      qos=1)
        _client.publish(TOPICS["current"],       str(data["current_a"]),    qos=1)
        _client.publish(TOPICS["soc"],           str(data["soc"]),          qos=1)
        _client.publish(TOPICS["remaining_ah"],  str(data["capacity"]),     qos=1)
        _client.publish(TOPICS["capacity_ah"],   str(config.NOMINAL_CAPACITY_AH), qos=1)
        _client.publish(TOPICS["temperature"],   json.dumps(data["temps"]), qos=1)

        # Générer les alarmes à partir des données BMS
        alarms = []
        cells = data["cells"]
        if cells:
            delta = (max(cells) - min(cells)) * 1000
            if delta > 100:
                alarms.append(f"Desequilibre {bms_id}: {delta:.0f}mV")
            if min(cells) < 3.0:
                alarms.append(f"Tension basse {bms_id}: {min(cells):.3f}V")
        _client.publish(TOPICS["alarms"], json.dumps(alarms), qos=1)

    except Exception as e:
        print(f"[MQTT] Erreur publication {data.get('bms_id', '?')}: {e}")


def publish_bms_connection(bms_status):
    """
    Publie l'état de connexion de chaque BMS.
    bms_status = {"BMS_1": True, "BMS_2": False}
    Ne lève JAMAIS d'exception.
    """
    if _client is None or not _connected:
        return

    try:
        _client.publish(TOPICS["bms_connection"], json.dumps(bms_status), qos=1)
    except Exception:
        pass


def publish_sim_connection(connecte):
    """
    Publie l'état de connexion du SIM7600 (message retenu = retain).
    Memorise la valeur pour la republier a chaque reconnexion au broker.
    """
    global _sim_status
    _sim_status = bool(connecte)
    if _client is None or not _connected:
        return
    try:
        _client.publish(TOPICS["sim_connection"], json.dumps(_sim_status), qos=1, retain=True)
    except Exception:
        pass


def publish_log(ligne):
    """
    Publie une ligne de console (stdout/stderr du Pi) vers le dashboard.
    NE DOIT JAMAIS appeler print() : ce serait une recursion infinie via la capture console.
    """
    if _client is None or not _connected:
        return
    try:
        _client.publish(TOPICS["log"], ligne, qos=0)
    except Exception:
        pass


# --- Capture de la console (stdout + stderr) pour la diffuser au dashboard ---
_tee_lock = threading.Lock()


class _ConsoleTee:
    """
    Duplique un flux (stdout ou stderr) : ecrit en local (console/journal)
    ET publie chaque ligne complete via MQTT. Capture donc les print() et les
    tracebacks d'erreurs (qui partent sur stderr).
    """
    def __init__(self, original):
        self._original = original
        self._buffer = ""

    def write(self, text):
        # 1) Affichage local inchange (console / journal systemd)
        self._original.write(text)
        # 2) Extraction des lignes completes (sous verrou : plusieurs threads ecrivent)
        lignes = []
        with _tee_lock:
            self._buffer += text
            while "\n" in self._buffer:
                ligne, self._buffer = self._buffer.split("\n", 1)
                if ligne.strip():
                    lignes.append(ligne)
        # 3) Publication hors verrou (publish_log ne print jamais -> pas de recursion)
        for ligne in lignes:
            publish_log(ligne)

    def flush(self):
        self._original.flush()

    def isatty(self):
        return False


def capturer_console():
    """Redirige stdout et stderr pour diffuser la console du Pi vers le dashboard."""
    sys.stdout = _ConsoleTee(sys.stdout)
    sys.stderr = _ConsoleTee(sys.stderr)
