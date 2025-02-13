"""
    Module de gestion des capteurs de température DS18B20.

    Liste des fonctions :
    - LireTempBrut : Lit les données brutes du capteur
    - LireTemperatures : Lit et retourne les températures des capteurs détectés
    - VerifierTemperatures : Vérifie si une température dépasse le seuil maximal
"""

import os
import glob
from datetime import datetime
from config import TEMPERATURE_MAX

class DS18B20Manager:
    """
    Classe pour gérer les capteurs DS18B20 en lisant directement le système de fichiers Linux.
    """

    def __init__(self):
        """
        Initialise les capteurs DS18B20 en activant les modules nécessaires.
        """
        os.system("modprobe w1-gpio")
        os.system("modprobe w1-therm")

        baseDir = "/sys/bus/w1/devices/"
        self.dossiersCapteurs = glob.glob(baseDir + "28*")
        self.nombreCapteurs = len(self.dossiersCapteurs)

        if self.nombreCapteurs == 0:
            print("Aucun capteur DS18B20 détecté. Vérifiez la connexion.")

    def LireTempBrut(self, dossierCapteur):
        """
        Lit les données brutes du fichier système du capteur.

        :param dossierCapteur: Dossier du capteur à lire
        :return: Liste des lignes de données lues
        """
        try:
            with open(dossierCapteur + "/w1_slave", "r") as fichier:
                return fichier.readlines()
        except FileNotFoundError:
            print("Erreur lors de la lecture du capteur. Vérifiez la connexion.")
            return []

    def LireTemperatures(self):
        """
        Lit les températures de tous les capteurs DS18B20.

        :return: Dictionnaire {capteur_id: température en degrés Celsius}
        """
        temperatures = {}

        if not self.dossiersCapteurs:
            print("Aucun capteur détecté lors de la lecture des températures.")
            return temperatures

        nomsCapteurs = {
            "28-5da6491f64ff": "Capteur_Avant",
            "28-9c85491f64ff": "Capteur_Arriere"
        }

        for dossier in self.dossiersCapteurs:
            lignes = self.LireTempBrut(dossier)
            if not lignes or "YES" not in lignes[0]:
                print("Erreur de lecture pour le capteur", dossier)
                continue

            positionEgale = lignes[1].find("t=")
            if positionEgale != -1:
                chaineTemp = lignes[1][positionEgale + 2:]
                try:
                    tempCelsius = float(chaineTemp) / 1000
                    if -50 < tempCelsius < 125:
                        capteurId = dossier.split('/')[-1]
                        nomCapteur = nomsCapteurs.get(capteurId, capteurId)
                        temperatures[nomCapteur] = round(tempCelsius, 2)
                    else:
                        print("Température anormale détectée pour le capteur", dossier)
                except ValueError:
                    print("Erreur lors de la conversion de la température pour le capteur", dossier)
        return temperatures

    def VerifierTemperatures(self):
        """
        Vérifie si une température dépasse le seuil TEMPERATURE_MAX.

        :return: Booléen indiquant si une alerte est déclenchée, dictionnaire des températures et horodatage
        """
        temperatures = self.LireTemperatures()
        alerteDeclenchee = False
        horodatage = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        print("\nLecture des capteurs en cours...")

        if not temperatures:
            print("Aucun capteur disponible")
            return alerteDeclenchee, {}

        for capteur, temperature in temperatures.items():
            print(f"{capteur} : {temperature:.2f} °C")
            if temperature >= TEMPERATURE_MAX:
                print(f"Alerte : Température critique sur {capteur}")
                alerteDeclenchee = True

        return alerteDeclenchee, temperatures, horodatage
