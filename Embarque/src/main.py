import time
import threading
import tkinter as tk
import RPi.GPIO as GPIO

from MEBC_SE.Embarque.src.captation import DS18B20Manager
from MEBC_SE.Embarque.src.transmission import EnvoyerDonneesMqtt, EnvoyerDonneesBackup, connectionStatus
from MEBC_SE.Embarque.src.reception import LancerEcoute, ClignoterLEDAlert, clignotement_alert_actif
from MEBC_SE.Embarque.src.message import LancerEcouteBoutons
from MEBC_SE.Embarque.src.interface_bateau import InterfaceBateau
import MEBC_SE.Embarque.src.config as config  # pour SEUILS_CRITIQUES, LED_ALERT_PIN, etc.

interface = None

def Main():
    def lancer_interface():
        global interface
        root = tk.Tk()
        interface = InterfaceBateau(root)

        # ✅ Démarrer les threads après création de l'interface
        threading.Thread(target=LancerEcoute, args=(interface,), daemon=True).start()
        threading.Thread(target=LancerEcouteBoutons, args=(interface,), daemon=True).start()

        root.mainloop()

    # Lancer l’interface graphique dans un thread séparé
    thread_interface = threading.Thread(target=lancer_interface, daemon=True)
    thread_interface.start()
    time.sleep(2)  # laisser le temps à l'interface de démarrer

    gestionCapteurs = DS18B20Manager()

    # Initialisation des GPIO pour la LED ALERT
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(config.LED_ALERT_PIN, GPIO.OUT)

    while True:
        try:
            if connectionStatus["perdue"] and EnvoyerDonneesBackup():
                continue

            alerte, temperatures, horodatage = gestionCapteurs.VerifierTemperatures()

            # Affichage console debug
            print("Températures détectées :", temperatures)

            # Détection de dépassement de 80% du seuil critique
            alerte_temp = any(
                temp >= 0.8 * config.SEUILS_CRITIQUES.get(nom, config.SEUILS_CRITIQUES["default"])
                for nom, temp in temperatures.items()
            )

            if alerte_temp and not config.alerte_temperature:
                config.alerte_temperature = True
                if not clignotement_alert_actif:
                    threading.Thread(target=ClignoterLEDAlert, daemon=True).start()
            elif not alerte_temp and config.alerte_temperature:
                config.alerte_temperature = False

            if temperatures:
                EnvoyerDonneesMqtt(temperatures, horodatage)

            if interface:
                affichage_capteurs = "\n".join([f"{capteur}: {temp}°C" for capteur, temp in temperatures.items()])
                interface.update_capteurs(affichage_capteurs)

        except Exception as e:
            print("Erreur dans la boucle principale :", e)

        time.sleep(1)

if __name__ == "__main__":
    Main()
