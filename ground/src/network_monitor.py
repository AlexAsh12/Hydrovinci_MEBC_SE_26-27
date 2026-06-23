import threading
import time
import socket
import statistics

# ─── État partagé ─────────────────────────────────────────────────────────────
_lock = threading.Lock()

net_state = {
    "status":   "checking",   # "ok" | "unstable" | "offline" | "checking"
    "latency_ms": None,        # dernière latence en ms
    "latency_avg": None,       # moyenne glissante
    "loss_pct":  0,            # % de paquets perdus (fenêtre glissante)
    "last_check": None,
}

_latency_history = []   # fenêtre des 10 dernières mesures
WINDOW = 10
CHECK_INTERVAL = 3      # secondes entre chaque sonde

# Hôtes à sonder (on essaie dans l'ordre)
PROBE_HOSTS = [
    ("8.8.8.8", 53),        # Google DNS
    ("1.1.1.1", 53),        # Cloudflare DNS
    ("9.9.9.9", 53),        # Quad9
]


def get_net_state():
    with _lock:
        return dict(net_state)


def _probe() -> float | None:
    """Tente une connexion TCP sur les hôtes de sonde.
    Retourne la latence en ms, ou None si échec."""
    for host, port in PROBE_HOSTS:
        try:
            start = time.perf_counter()
            sock = socket.create_connection((host, port), timeout=2)
            sock.close()
            return (time.perf_counter() - start) * 1000
        except OSError:
            continue
    return None


def _classify(latencies: list, loss_pct: float) -> str:
    if loss_pct >= 50:
        return "offline"
    if loss_pct >= 20:
        return "unstable"
    if not latencies:
        return "offline"
    avg = statistics.mean(latencies)
    jitter = statistics.stdev(latencies) if len(latencies) > 1 else 0
    if avg > 300 or jitter > 80:
        return "unstable"
    return "ok"


def _monitor_loop():
    import datetime
    while True:
        latency = _probe()

        with _lock:
            _latency_history.append(latency)
            if len(_latency_history) > WINDOW:
                _latency_history.pop(0)

            valid = [l for l in _latency_history if l is not None]
            lost  = [l for l in _latency_history if l is None]
            loss_pct = (len(lost) / len(_latency_history)) * 100 if _latency_history else 100

            net_state["latency_ms"]  = round(latency, 1) if latency else None
            net_state["latency_avg"] = round(statistics.mean(valid), 1) if valid else None
            net_state["loss_pct"]    = round(loss_pct)
            net_state["status"]      = _classify(valid, loss_pct)
            net_state["last_check"]  = datetime.datetime.now().strftime("%H:%M:%S")

        time.sleep(CHECK_INTERVAL)


# Lance le thread au chargement du module
_thread = threading.Thread(target=_monitor_loop, daemon=True)
_thread.start()
