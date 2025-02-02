# Programme principal du système de surveillance des températures

# Liste des fonctions :
# - Main : Lance la boucle principale de lecture et transmission des températures

import time
from captation import DS18B20Manager
from transmission import EnvoyerDonneesMqtt


"""
    Fonction principale du programme.
    Elle gère la boucle de lecture des températures et leur envoi via MQTT.
"""
def Main():

    gestionCapteurs = DS18B20Manager()

    while True:
        alerte, temperatures, horodatage = gestionCapteurs.VerifierTemperatures()

        if alerte:
            print("Une ou plusieurs températures critiques ont été détectées !")

        if temperatures:
            EnvoyerDonneesMqtt(temperatures, horodatage)

        time.sleep(10)  # Pause de 10 secondes entre chaque lecture

if __name__ == "__main__":
    Main()