import serial
import time
import interface_bateau
import config

# --- CONFIGURATION ---
# PORT_SERIE = "/dev/ttyUSB3" -> Géré dynamiquement
# VITESSE = 115200 -> Centralisé dans config.py

def envoyer_commande(cmd, ser, attente=0.5):
    if ser and ser.is_open:
        ser.write((cmd + "\r\n").encode())
        time.sleep(attente)
        if ser.in_waiting:
            reponse = ser.read(ser.in_waiting).decode(errors='ignore').strip()
            print(f"   < {reponse}")
            return reponse
    return ""


def lire_sms(ser, sim_lock):  # ser et sim_lock passés depuis le main
    """Ecoute le port série et affiche les SMS dès qu'ils arrivent"""
    try:
        print("En attente de SMS (Mode Polling Sécurisé v2)...")

        # Initialisation AT — lock uniquement pour les écritures
        with sim_lock:

            envoyer_commande("AT", ser)

            # 1. Forcer explicitement le stockage et la lecture sur la carte SIM
            ser.write("AT+CPMS=\"SM\",\"SM\",\"SM\"\r\n".encode())
            time.sleep(0.5)
            ser.reset_input_buffer()

            # 2. Activer le mode texte
            ser.write("AT+CMGF=1\r\n".encode())  
            time.sleep(0.5)
            ser.reset_input_buffer()
            
            # 3. Le modem stocke en silence (ne perturbe pas les appels)
            ser.write("AT+CNMI=2,0,0,0,0\r\n".encode())
            time.sleep(0.5)
            ser.reset_input_buffer()
            
            # (Note: Suppression totale du CMGD initial pour conserver les SMS reçus hors-ligne)

        dernier_message = None

        while True:
            # Respiration. Laisse le CPU libre pour les appels.
            time.sleep(4) 
            
            lignes_cmgl = []
            
            # --- Zone critique verrouillée pour la lecture intégrale ---
            with sim_lock:
                ser.reset_input_buffer()
                ser.write("AT+CMGL=\"ALL\"\r\n".encode())
                
                # Attente dynamique : on lit jusqu'à recevoir le signal de fin "OK" (max 3 sec)
                debut = time.time()
                old_timeout = ser.timeout
                ser.timeout = 0.5 
                
                try:
                    while time.time() - debut < 3:
                        ligne_brute = ser.readline().decode(errors='ignore').strip()
                        if ligne_brute:
                            lignes_cmgl.append(ligne_brute)
                        if ligne_brute == "OK" or ligne_brute == "ERROR":
                            break # Le modem a fini d'envoyer toute sa mémoire
                finally:
                    ser.timeout = old_timeout
            # -----------------------------------------------------------

            a_lu_des_sms = False

            # Traitement des lignes récupérées
            for i, ligne in enumerate(lignes_cmgl):
                if "+CMGL:" in ligne:
                    a_lu_des_sms = True
                    # Extraction robuste de l'heure
                    try:
                        # On récupère le bloc de temps à la fin de la ligne
                        elements = ligne.split(",")
                        heure_brute = elements[-1] 
                        heure = heure_brute.replace('"', '').split("+")[0].strip()
                    except:
                        heure = "??:??:??"

                    # La ligne suivante = contenu du SMS
                    if i + 1 < len(lignes_cmgl):
                        raw = lignes_cmgl[i+1]
                        
                        if raw == "OK" or "+CMGL:" in raw: 
                            continue # Ignorer les faux positifs
                        
                        # Décodage
                        try:
                            contenu = bytes.fromhex(raw).decode('utf-16-be')
                        except:
                            contenu = raw

                        message = f"[{heure}] {contenu}"

                        if message != dernier_message:
                            dernier_message = message
                            print(f"SMS reçu : {message}")
                            interface_bateau.update_messages(message)
                            print("update envoyé vers interface")

            # Purge de la carte SIM uniquement SI on a trouvé et lu des messages
            if a_lu_des_sms:
                with sim_lock:
                    ser.write("AT+CMGD=1,4\r\n".encode())  # supprime les SMS lus
                    time.sleep(0.5) # Le modem a besoin de temps pour effacer la SIM
                    ser.reset_input_buffer()

    except Exception as e:
        print(f"❌ Erreur SMS : {e}")