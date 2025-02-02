# Module de gestion des capteurs de température DS18B20

# Liste des fonctions :
# - LireTempBrut : Lit les données brutes du capteur
# - LireTemperatures : Lit et retourne les températures des capteurs détectés
# - VerifierTemperatures : Vérifie si une température dépasse le seuil maximal

import os
import glob
from datetime import datetime
from config import TEMPERATURE_MAX

"""
    Classe pour gérer les capteurs DS18B20 en lisant directement le système de fichiers Linux.
"""
class DS18B20Manager:


    """Initialise les capteurs DS18B20 en activant les modules nécessaires."""
    def __init__(self):

        os.system("modprobe w1-gpio")
        os.system("modprobe w1-therm")

        # Récupérer la liste des capteurs détectés
        baseDir = "/sys/bus/w1/devices/"
        self.dossiersCapteurs = glob.glob(baseDir + "28*")
        self.nombreCapteurs = len(self.dossiersCapteurs)

        if self.nombreCapteurs == 0:
            print("Aucun capteur DS18B20 détecté. Vérifiez la connexion.")



    """
    Lit les données brutes du fichier système du capteur.

    :param dossierCapteur: Dossier du capteur à lire
    :return: Liste des lignes de données lues
    """
    def LireTempBrut(self, dossierCapteur):

        try:
            with open(dossierCapteur + "/w1_slave", "r") as fichier:
                return fichier.readlines()
        except FileNotFoundError:
            print("Erreur lors de la lecture du capteur. Vérifiez la connexion.")
            return []


    """
    Lit les températures de tous les capteurs DS18B20.

    :return: Dictionnaire {capteur_id: température en degrés Celsius}
    """
    def LireTemperatures(self):

        temperatures = {}

        if not self.dossiersCapteurs:
            print("Aucun capteur détecté lors de la lecture des températures.")
            return temperatures

        for dossier in self.dossiersCapteurs:
            lignes = self.LireTempBrut(dossier)
            if not lignes or "YES" not in lignes[0]:
                print("Erreur de lecture pour le capteur", dossier)
                continue

            # Extraction de la température en degrés Celsius
            positionEgale = lignes[1].find("t=")
            if positionEgale != -1:
                chaineTemp = lignes[1][positionEgale + 2:]
                try:
                    tempCelsius = float(chaineTemp) / 1000
                    if -50 < tempCelsius < 125:  # Plage normale des capteurs DS18B20
                        temperatures[dossier] = round(tempCelsius, 2)
                    else:
                        print("Température anormale détectée pour le capteur", dossier)
                except ValueError:
                    print("Erreur lors de la conversion de la température pour le capteur", dossier)
        return temperatures



    """
        Vérifie si une température dépasse le seuil TEMPERATURE_MAX.

        :return: Booléen indiquant si une alerte est déclenchée, dictionnaire des températures et horodatage
    """
    def VerifierTemperatures(self):

        temperatures = self.LireTemperatures()
        alerteDeclenchee = False
        horodatage = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print("Heure actuelle :", horodatage)

        if not temperatures:
            print("Aucun capteur disponible pour la lecture des températures.")
            return alerteDeclenchee, {}

        for capteur, temperature in temperatures.items():
            print("Capteur", capteur, ":", temperature, "degrés Celsius")
            if temperature >= TEMPERATURE_MAX:
                print("Alerte ! Température critique atteinte sur le capteur", capteur)
                alerteDeclenchee = True

        return alerteDeclenchee, temperatures, horodatage