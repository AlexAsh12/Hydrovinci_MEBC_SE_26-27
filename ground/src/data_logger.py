import csv
import os
import threading
import time
import datetime
from transmission import get_state

try:
    from google_drive import upload_file as _upload_file
    _drive_available = True
except Exception as e:
    print(f"[logger] Google Drive non disponible : {e}")
    _drive_available = False

LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "sessions")
UPLOAD_INTERVAL = 300   # synchro Drive toutes les 5 min
LOG_INTERVAL    = 5     # une ligne toutes les 5 secondes

CELL_HEADERS = [f"c{i+1}" for i in range(15)]
HEADERS = [
    "timestamp", "soc", "pack_voltage", "current",
    "remaining_ah", "capacity_ah", "speed_knots", "temp_max",
    *CELL_HEADERS
]


def _current_filepath():
    os.makedirs(LOG_DIR, exist_ok=True)
    date = datetime.date.today().strftime("%Y-%m-%d")
    return os.path.join(LOG_DIR, f"session_{date}.csv")


def _log_loop():
    while True:
        try:
            s    = get_state()

            # last_update est le timestamp de la derniere donnee MQTT recue du Pi.
            # Il vaut None tant que le Pi n'a rien envoye.
            # Sans ce check, le logger ecrirait les valeurs par defaut (soc=50, voltage=48...)
            # en boucle meme quand le Pi est eteint, ce qui pollue le CSV.
            if s["last_update"] is None:
                time.sleep(LOG_INTERVAL)
                continue

            path = _current_filepath()

            # cr├®e le fichier avec headers s'il n'existe pas
            write_header = not os.path.exists(path)

            with open(path, "a", newline="") as f:
                writer = csv.DictWriter(f, fieldnames=HEADERS)
                if write_header:
                    writer.writeheader()

                temps = s["temperature"]
                tmax  = max(temps) if isinstance(temps, list) else temps
                cells = s["cell_voltages"]

                row = {
                    "timestamp":    datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                    "soc":          s["soc"],
                    "pack_voltage": s["pack_voltage"],
                    "current":      s["current"],
                    "remaining_ah": s["remaining_ah"],
                    "capacity_ah":  s["capacity_ah"],
                    "speed_knots":  s["speed_knots"],
                    "temp_max":     tmax,
                    **{f"c{i+1}": cells[i] for i in range(min(len(cells), 15))},
                }
                writer.writerow(row)
        except Exception as e:
            print(f"[logger] Erreur dans _log_loop : {e}")

        time.sleep(LOG_INTERVAL)


def _upload_loop():
    """Synchro p├®riodique vers Drive ÔÇö ind├®pendante du logging."""
    if not _drive_available:
        print("[logger] Upload Drive d├®sactiv├® (module indisponible).")
        return
    while True:
        time.sleep(UPLOAD_INTERVAL)
        path = _current_filepath()
        if os.path.exists(path):
            _upload_file(path)
            print(f"[logger] Upload├® : {path}")


# d├®marre les deux threads au chargement
threading.Thread(target=_log_loop,    daemon=True).start()
threading.Thread(target=_upload_loop, daemon=True).start()
