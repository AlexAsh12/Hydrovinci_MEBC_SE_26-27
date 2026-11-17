#!/usr/bin/env python3
"""
Test de la liaison UART Pi <-> ESP32.
Protocole : 2026-2027/shared/PROTOCOLE_UART.md

Sur la Pi :
    python3 test_liaison_esp32.py              # port /dev/serial0 par defaut
    python3 test_liaison_esp32.py --port /dev/ttyAMA0
    python3 test_liaison_esp32.py --reboot-esp # teste aussi la commande reboot

Affiche tout ce que l'ESP32 envoie, envoie un "ping" toutes les 2 s et
mesure le temps de reponse, compte les trames perdues et detecte la perte
de liaison (aucune trame valide pendant 3 s). Ctrl+C pour arreter.
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "shared", "python"))

from protocole_uart import TIMEOUT_LIAISON_S, VERSION_PROTOCOLE, Decodeur, Encodeur  # noqa: E402

try:
    import serial
except ImportError:
    sys.exit("pyserial manquant : sudo apt install python3-serial")

INTERVALLE_PING_S = 2.0
INTERVALLE_STATS_S = 10.0


def heure():
    return time.strftime("%H:%M:%S")


def resume(msg):
    """Une ligne lisible par type de message."""
    t = msg["t"]
    if t == "hello":
        return f"HELLO carte={msg.get('board')} fw={msg.get('fw')} proto={msg.get('proto')}"
    if t == "hb":
        return f"hb    uptime ESP = {msg.get('up', 0) / 1000:.1f} s"
    if t == "bms":
        cells = msg.get("cells") or []
        return (f"bms{msg.get('id')}  {msg.get('voltage')} V  {msg.get('current')} A  "
                f"SOC {msg.get('soc')} %  {len(cells)} cellules")
    if t == "ack":
        etat = "OK" if msg.get("ok") else f"ERREUR ({msg.get('err')})"
        return f"ack   commande n={msg.get('ref')} : {etat}"
    return f"{t:5} {msg}"


class Stats:
    def __init__(self):
        self.recus = 0
        self.perdus = 0
        self.dernier_n = None
        self.latences_ms = []

    def compter(self, msg):
        n = msg.get("n")
        if isinstance(n, int):
            if self.dernier_n is not None:
                attendu = (self.dernier_n + 1) % 65536
                if n != attendu:
                    # n == 0 apres un grand saut = l'ESP a redemarre, pas une perte
                    if n != 0:
                        self.perdus += (n - attendu) % 65536
            self.dernier_n = n
        self.recus += 1


def main():
    parser = argparse.ArgumentParser(description="Test de la liaison UART avec l'ESP32")
    parser.add_argument("--port", default="/dev/serial0")
    parser.add_argument("--vitesse", type=int, default=115200)
    parser.add_argument("--reboot-esp", action="store_true", help="envoie la commande reboot apres 5 s")
    parser.add_argument("--brut", action="store_true", help="affiche aussi les octets bruts recus")
    args = parser.parse_args()

    try:
        port = serial.serial_for_url(args.port, baudrate=args.vitesse, timeout=0.05)
    except serial.SerialException as e:
        sys.exit(f"Impossible d'ouvrir {args.port} : {e}")

    print(f"[{heure()}] Ecoute sur {args.port} @ {args.vitesse} bauds (Ctrl+C pour arreter)")

    dec = Decodeur()
    enc = Encodeur()
    stats = Stats()
    pings_en_attente = {}  # n de la commande -> instant d'envoi
    debut = time.monotonic()
    dernier_avertissement = debut
    derniere_trame = None
    liaison_ok = False
    prochain_ping = debut
    prochaines_stats = debut + INTERVALLE_STATS_S
    reboot_envoye = False

    def envoyer(message):
        port.write(enc.encoder(message))
        return enc.dernier_n

    envoyer({"t": "info"})

    try:
        while True:
            maintenant = time.monotonic()

            donnees = port.read(512)
            if args.brut and donnees:
                print(f"[{heure()}] brut : {donnees!r}")

            for msg in dec.ajouter(donnees):
                derniere_trame = maintenant
                if not liaison_ok:
                    liaison_ok = True
                    print(f"[{heure()}] *** LIAISON ESP32 OK ***")
                stats.compter(msg)

                if msg["t"] == "hello" and msg.get("proto") != VERSION_PROTOCOLE:
                    print(f"[{heure()}] ATTENTION : protocole ESP v{msg.get('proto')}, "
                          f"Pi v{VERSION_PROTOCOLE}")
                if msg["t"] == "ack" and msg.get("ref") in pings_en_attente:
                    latence = (maintenant - pings_en_attente.pop(msg["ref"])) * 1000
                    stats.latences_ms.append(latence)
                    print(f"[{heure()}] {resume(msg)}  (aller-retour {latence:.0f} ms)")
                else:
                    print(f"[{heure()}] {resume(msg)}")

            # Perte de liaison
            if liaison_ok and maintenant - derniere_trame > TIMEOUT_LIAISON_S:
                liaison_ok = False
                print(f"[{heure()}] *** ESP32 PERDU : aucune trame valide depuis "
                      f"{TIMEOUT_LIAISON_S:.0f} s ***")
            if derniere_trame is None and maintenant - dernier_avertissement > TIMEOUT_LIAISON_S:
                print(f"[{heure()}] Toujours rien recu... verifier le cablage "
                      f"(TX ESP -> RX Pi), le GND commun et le port")
                dernier_avertissement = maintenant  # message repete toutes les 3 s

            # Ping periodique
            if maintenant >= prochain_ping:
                prochain_ping = maintenant + INTERVALLE_PING_S
                n = envoyer({"t": "ping"})
                pings_en_attente[n] = maintenant
                # Oublier les pings sans reponse depuis longtemps
                for vieux in [k for k, v in pings_en_attente.items() if maintenant - v > 5]:
                    pings_en_attente.pop(vieux)

            if args.reboot_esp and not reboot_envoye and maintenant - debut > 5 and liaison_ok:
                reboot_envoye = True
                print(f"[{heure()}] Envoi de la commande reboot a l'ESP32")
                envoyer({"t": "cmd", "name": "reboot", "args": {}})

            # Statistiques
            if maintenant >= prochaines_stats:
                prochaines_stats = maintenant + INTERVALLE_STATS_S
                lat = stats.latences_ms
                lat_txt = (f"aller-retour moy {sum(lat) / len(lat):.0f} ms, max {max(lat):.0f} ms"
                           if lat else "aucune reponse aux pings")
                print(f"[{heure()}] --- STATS : {stats.recus} trames recues, {stats.perdus} perdues, "
                      f"{dec.trames_erreur} erronees | {lat_txt} ---")
    except KeyboardInterrupt:
        print(f"\n[{heure()}] Arret. {stats.recus} trames recues, {stats.perdus} perdues, "
              f"{dec.trames_erreur} erronees.")
    except serial.SerialException as e:
        print(f"[{heure()}] Port {args.port} inutilisable : {e}")
    finally:
        port.close()


if __name__ == "__main__":
    main()
