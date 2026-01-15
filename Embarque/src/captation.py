import os
import glob
from datetime import datetime
from config import CAPTEURS, SEUILS_CRITIQUES

class DS18B20Manager:
    """Gère les capteurs DS18B20, cache la liste pour performance."""
    def __init__(self):
        os.system("modprobe w1-gpio")
        os.system("modprobe w1-therm")
        self.baseDir = "/sys/bus/w1/devices/"
        self.dossiersCapteurs = glob.glob(self.baseDir + "28*")

    def LireTempBrut(self, dossierCapteur):
        try:
            with open(dossierCapteur + "/w1_slave", "r") as fichier:
                return fichier.readlines()
        except:
            return []

    def LireTemperatures(self):
        """Renvoie un dict {nom_capteur: temp}"""
        temperatures = {}
        for dossier in self.dossiersCapteurs:
            lignes = self.LireTempBrut(dossier)
            if not lignes or "YES" not in lignes[0]:
                continue
            temp_pos = lignes[1].find("t=")
            if temp_pos != -1:
                try:
                    temp_celsius = float(lignes[1][temp_pos + 2:]) / 1000
                    if -50 < temp_celsius < 125:
                        capteur_id = dossier.split('/')[-1]
                        nom = CAPTEURS.get(capteur_id, capteur_id)
                        temperatures[nom] = round(temp_celsius, 2)
                except:
                    continue
        return temperatures

    def VerifierTemperatures(self):
        temperatures = self.LireTemperatures()
        # On cherche pour CHAQUE capteur si la température dépasse SON seuil critique
        alerte = any(
            temp >= SEUILS_CRITIQUES.get(nom, SEUILS_CRITIQUES.get("default", 40))
            for nom, temp in temperatures.items()
        )
        horodatage = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        return alerte, temperatures, horodatage
