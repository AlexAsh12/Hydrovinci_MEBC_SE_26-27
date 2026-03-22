#!/usr/bin/env python3
import asyncio
import threading
import interface_bateau
import SMS
import time
# On importe la fonction monitor_bms et la config depuis ton fichier
# (Assure-toi que le nom ici correspond au nom de ton fichier sans le .py)
import bms_rasp as bms

async def main():
    print("Démarrage Global - Monaco 2026")
    interface_bateau.update_messages("Système prêt. Connexion BMS en cours...")

    interval = 1.0

    # On crée les tâches en utilisant les fonctions de TON fichier
    tasks = [
        bms.monitor_bms("BMS_1", bms.BMS_CONFIG["BMS_1"], interval),
        bms.monitor_bms("BMS_2", bms.BMS_CONFIG["BMS_2"], interval)
    ]

    # Lancement simultané
    await asyncio.gather(*tasks)

if __name__ == "__main__":
    threading.Thread(target=interface_bateau.init).start()
    print("thread interface lancé")
    threading.Thread(target=SMS.lire_sms, daemon=True).start()
    print("thread SMS lancé")

    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nArrêt.")