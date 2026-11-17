#!/usr/bin/env python3
"""
Point d'entree principal - Monaco Energy Boat Challenge 2026
Lance les threads : BMS, SMS, Clavier/Bouton, Interface Tkinter.
"""

import asyncio
import threading
import os

from interface import interface_bateau
from telemetrie import sms
from telemetrie import appel
from telemetrie import port_serie
from telemetrie import mqtt_publisher
from acquisition import bms_rasp as bms
import config

# Necessaire pour que Tkinter trouve l'ecran sur le Pi
os.environ["DISPLAY"] = ":0.0"


# --- Fonctions de lancement des threads ---

def lancer_taches_bms():
    """
    Wrapper pour lancer les 2 moniteurs BMS dans une boucle asyncio.
    Tourne dans un thread secondaire car Tkinter occupe le thread principal.
    """
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    try:
        loop.run_until_complete(_bms_main())
    except KeyboardInterrupt:
        pass


async def _bms_main():
    """Lance le monitoring des 2 batteries en parallele."""
    interval = config.INTERVALLE_BMS
    await asyncio.gather(
        bms.monitor_bms("BMS_1", config.BMS_CONFIG["BMS_1"], interval),
        bms.monitor_bms("BMS_2", config.BMS_CONFIG["BMS_2"], interval),
    )


def ecouter_clavier():
    """Touche P + Entree pour lancer un appel (sera remplace par bouton physique).
    Touche M + Entree pour envoyer un SMS 'Bien recu' au numero cible (test)."""
    while True:
        touche = input()
        if touche.lower() == "p":
            threading.Thread(target=appel.passer_appel, daemon=True).start()
        elif touche.lower() == "m":
            sms.demander_envoi_sms()


# --- Demarrage ---

if __name__ == "__main__":
    print("Demarrage Global - Monaco 2026")
    interface_bateau.update_messages("Systeme pret. Connexion BMS en cours...")

    # 0. MQTT : connexion au broker (non-bloquant, le systeme tourne meme sans MQTT)
    mqtt_publisher.init()
    # Capture de la console (stdout/stderr) pour diffuser le terminal au dashboard sol
    mqtt_publisher.capturer_console()

    # 1. BMS : toujours lance, independant du SIM7600
    threading.Thread(target=lancer_taches_bms, daemon=True).start()
    print("thread BMS lance")

    # 2. SIM7600 : detection automatique, continue sans si absent
    sim_ok = port_serie.initialiser()
    interface_bateau.update_statut("SIM7600", sim_ok)
    mqtt_publisher.publish_sim_connection(sim_ok)  # statut SIM vers le dashboard sol

    if sim_ok:
        # SMS et clavier ne sont lances que si le modem est present
        threading.Thread(target=sms.lire_sms, daemon=True).start()
        print("thread SMS lance")

        threading.Thread(target=ecouter_clavier, daemon=True).start()
        print("thread clavier lance")
    else:
        print("Threads SMS/Appel non lances (pas de modem)")

    # 3. Interface Tkinter sur le thread principal (obligatoire pour Tkinter)
    try:
        print("thread interface lance (Thread Principal)")
        interface_bateau.init()
    except KeyboardInterrupt:
        print("\nArret.")