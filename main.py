# Programme principal du système de surveillance des températures

import time
from captation import DS18B20Manager
from transmission import EnvoyerDonneesMqtt, EnvoyerDonneesBackup, connection_status

def Main():
    """
    Fonction principale du programme.
    """
    gestionCapteurs = DS18B20Manager()

    while True:
        try:
            # S'il y a un backup à envoyer (connexion rétablie)
            if connection_status["perdue"] and EnvoyerDonneesBackup():
                # Si le backup a été envoyé avec succès, on peut continuer
                print("Backup envoyé, reprise des mesures...")
                continue  # On passe au prochain cycle pour faire une nouvelle lecture
            
            # Lecture et envoi des nouvelles températures
            alerte, temperatures, horodatage = gestionCapteurs.VerifierTemperatures()

            if alerte:
                print("Une ou plusieurs températures critiques ont été détectées !")

            if temperatures:
                EnvoyerDonneesMqtt(temperatures, horodatage)

        except Exception as e:
            print(f"Erreur dans la boucle principale : {e}")

        time.sleep(1)

if __name__ == "__main__":
    Main()
