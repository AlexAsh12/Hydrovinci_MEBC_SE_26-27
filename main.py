"""
    Module principal du programme.

    Liste des fonctions :
    - Main : Gère l'envoi des données, la réception des commandes et l'écoute des boutons.
"""

import time
import threading
from captation import DS18B20Manager
from transmission import EnvoyerDonneesMqtt, EnvoyerDonneesBackup, connectionStatus
from reception import LancerEcoute
from message import LancerEcouteBoutons

def Main():
    """
    Fonction principale du programme.
    Gère la lecture des capteurs, l'envoi des données et l'écoute des commandes.
    """
    gestionCapteurs = DS18B20Manager()

    # Lancer l'écoute des commandes MQTT dans un thread
    threadReception = threading.Thread(target=LancerEcoute, daemon=True)
    threadReception.start()

    # Lancer l'écoute des boutons dans un thread
    threadBoutons = threading.Thread(target=LancerEcouteBoutons, daemon=True)
    threadBoutons.start()

    print("Le système est prêt : envoi des données, écoute des commandes et détection des boutons en cours...\n")

    while True:
        try:
            # Vérifier si un backup de données doit être envoyé
            if connectionStatus["perdue"] and EnvoyerDonneesBackup():
                print("Backup envoyé, reprise des mesures...")
                continue  # Passer au prochain cycle de lecture

            # Lire les températures et les envoyer
            alerte, temperatures, horodatage = gestionCapteurs.VerifierTemperatures()

            if alerte:
                print("Alerte : Température critique détectée !")

            if temperatures:
                EnvoyerDonneesMqtt(temperatures, horodatage)

        except Exception as e:
            print(f"Erreur dans la boucle principale : {e}")

        time.sleep(1)  # Attente d'une seconde avant la prochaine lecture

if __name__ == "__main__":
    Main()
