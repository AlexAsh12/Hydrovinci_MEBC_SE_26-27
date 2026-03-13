import json
import time
import os
from interface_bateau import interface

def get_latest_data(filename):
    """Lit un fichier JSON et retourne le dernier objet de la liste"""
    try:
        if os.path.exists(filename):
            with open(filename, 'r') as f:
                data_list = json.load(f)
                if data_list:
                    return data_list[-1]  # On prend le dernier
    except Exception as e:
        print(f"Erreur lecture {filename}: {e}")
    return None

def main():
    print("Démarrage du Monitoring Monaco 2026...")
    
    # Message de bienvenue sur l'interface
    interface.update_messages("Système initialisé. Lecture des fichiers JSON en cours...")

    try:
        while True:
            # 1. Récupération des données depuis les fichiers
            d1 = get_latest_data('data_bms_1.json')
            d2 = get_latest_data('data_bms_2.json')

            # 2. Mise à jour de l'interface si on a des données
            if d1 and d2:
                interface.update_bms(d1, d2)
            else:
                interface.update_messages("⚠️ En attente de données des fichiers JSON...")

            # 3. On peut aussi update d'autres infos (IA / Messages)
            # interface.update_other("Vitesse: 12 noeuds | Cap: 180°")

            # Temporisation pour ne pas surcharger le CPU
            time.sleep(0.5) 
            
    except KeyboardInterrupt:
        print("Arrêt du programme.")

if __name__ == "__main__":
    main()