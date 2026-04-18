#!/usr/bin/env python3
import asyncio
import threading
import serial
import interface_bateau
import SMS
import TEST_APPEL
import time
import bms_rasp as bms
import os
import glob #Pour la recherche du port
import config

os.environ["DISPLAY"] = ":0.0"

class SafeSerial(serial.Serial):   #Classe surchargée : correctif pour pyserial
    """
    Hérite de la classe standard serial.Serial.
    Neutralise les appels matériels DTR/RTS que le port virtuel du SIM7600 ne supporte pas.
    """
    def _update_dtr_state(self):
        # Le mot-clé 'pass' ordonne à Python de ne rien faire.
        # Cela empêche l'exécution de l'appel système (fcntl.ioctl) qui fait crasher le Pi 5.
        pass

    def _update_rts_state(self):
        pass

def detecter_port_at():
    """
    Balaye dynamiquement tous les ports USB pour identifier l'interface AT active.
    Élimine le point de défaillance lié au glissement d'index USB au démarrage.
    """
    # 1. Lister tous les périphériques USB série connectés
    candidats = sorted(glob.glob('/dev/ttyUSB*'))
    print(f"Audit matériel des ports : {candidats}")
    
    for port in candidats:
        try:
            # 2. Test du port avec la classe sécurisée et un timeout strict
            # On force le cast en int(config.VITESSE_MODEM) au cas où
            ser = SafeSerial(port, baudrate=int(config.VITESSE_MODEM), timeout=1)
            ser.reset_input_buffer()
            
            # --- RÉVEIL MATÉRIEL ---
            ser.write(b"AT\r\n")
            time.sleep(0.2)
            
            # 3. Envoi de la requête ping (AT)
            ser.write(b"AT\r\n")
            time.sleep(0.2)
            
            # 4. Lecture de l'accusé de réception
            reponse = ser.read_all().decode(errors='ignore').strip()
            ser.close()
            
            # 5. Validation par l'évidence
            if "OK" in reponse:
                print(f"✅ Interface AT validée sur : {port}")
                return port # Retourne la chaîne de caractères (ex: "/dev/ttyUSB3")
                
        except Exception as e:
            # On affiche l'erreur réelle pour comprendre pourquoi il rejette le port
            print(f"⚠️ Rejet de {port} : {e}")
            pass
            
    print("❌ ERREUR CRITIQUE : Aucun port AT ne répond au ping matériel.")
    return None


PORT_ACTIF = detecter_port_at()

if PORT_ACTIF is None:
    # Arrêt immédiat si le matériel n'est pas prêt (Fail-fast)
    print("Arrêt de la séquence : Modem introuvable.")
    exit(1)

# 2. Ouverture sécurisée et non-bloquante du port validé
#Port série partagé entre SMS et appel
ser = SafeSerial(PORT_ACTIF, baudrate=config.VITESSE_MODEM, timeout=1)


# Lock pour éviter que SMS et appel utilisent le port en même temps
sim_lock = threading.Lock()


# --- 2. FONCTION D'AUDIT (Le protocole Zero Trust) ---

def ecouter_clavier():
    """Appuyer sur P + Entrée pour lancer un appel (sera remplacé par bouton physique)"""
    while True:
        touche = input()
        if touche.lower() == "p":
            threading.Thread(target=TEST_APPEL.passer_appel, args=(ser, sim_lock), daemon=True).start()

async def main():
    print("Démarrage Global - Monaco 2026")
    interface_bateau.update_messages("Système prêt. Connexion BMS en cours...")

    interval = config.INTERVALLE_BMS

    # On crée les tâches en utilisant les fonctions de TON fichier
    tasks = [
        bms.monitor_bms("BMS_1", config.BMS_CONFIG["BMS_1"], interval),
        bms.monitor_bms("BMS_2", config.BMS_CONFIG["BMS_2"], interval)
    ]

    # Lancement simultané
    await asyncio.gather(*tasks)

def lancer_taches_bms():
    """
    Fonction wrapper asynchrone ajoutée pour l'architecture Pi Zero.
    Nécessaire car asyncio requiert la création explicite d'une boucle d'événements 
    lorsqu'il s'exécute dans un thread secondaire.
    """
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    try:
        loop.run_until_complete(main())
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    # La lecture des BMS est déportée dans un thread secondaire
    threading.Thread(target=lancer_taches_bms, daemon=True).start()
    print("thread BMS lancé")

    threading.Thread(target=SMS.lire_sms, args=(ser, sim_lock), daemon=True).start()
    print("thread SMS lancé")
    
    # Touche P + Entrée pour lancer un appel (sera remplacé par bouton physique)
    threading.Thread(target=ecouter_clavier, daemon=True).start()
    print("thread clavier lancé")

    try:
        # L'interface graphique Tkinter est exécutée de force sur le Thread Principal
        print("thread interface lancé (Thread Principal)")
        interface_bateau.init()
    except KeyboardInterrupt:
        print("\nArrêt.")