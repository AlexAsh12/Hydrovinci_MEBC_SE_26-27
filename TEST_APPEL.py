import serial
import time
import interface_bateau
 
# --- CONFIGURATION ---
NUMERO = "+33602185227"
#PORT_SERIE = "/dev/ttyUSB2"
VITESSE = 115200
CSQ_MINIMUM = 5  # Abaissé car module en 4G, CSQ n'est pas représentatif
 
ser = None
 
# --- FONCTIONS SÉRIE ---
def envoyer_commande(cmd, attente=0.5):
    if ser and ser.is_open:
        ser.write((cmd + "\r\n").encode())
        time.sleep(attente)
        if ser.in_waiting:
            reponse = ser.read(ser.in_waiting).decode(errors='ignore').strip()
            print(f"   < {reponse}")
            return reponse
    return ""
 
def lire_tampon():
    if ser and ser.in_waiting:
        return ser.read(ser.in_waiting).decode(errors='ignore')
    return ""
 
def verifier_reseau():
    print("🌍 Vérification réseau...")
 
    # 1. Vérification SIM
    print("🔍 Vérification Carte SIM...")
    rep_sim = envoyer_commande("AT+CPIN?")
    if "READY" in rep_sim:
        print("   ✅ SIM OK.")
    elif "SIM PIN" in rep_sim:
        print("   🔐 SIM VERROUILLÉE (PIN requis)")
        return False
    else:
        print("   ❌ SIM NON DÉTECTÉE")
        return False
 
    # 2. Mode automatique
    envoyer_commande('AT+COPS=0', attente=2)
 
    # 3. Boucle CREG
    print("🔄 Attente enregistrement réseau...")
    for i in range(15):
        reponse = envoyer_commande("AT+CREG?", attente=2)
        if ",1" in reponse or ",5" in reponse:
            print(f"   ✅ RÉSEAU OK (essai {i+1})")
            print("   ⏳ Stabilisation réseau (4s)...")
            time.sleep(4)
            return True
 
        # Après 5 échecs, forcer SFR
        if i == 4:
            print("   ⚡ Forçage réseau SFR (20815)...")
            envoyer_commande('AT+COPS=1,2,"20815"', attente=5)
 
        print(f"   ... En attente ({i+1}/15)")
 
    print("❌ Impossible de s'enregistrer sur le réseau.")
    return False
 
def passer_appel(ser_param, sim_lock):  # ser et sim_lock passés depuis le main
    global ser
    # Lock pour toute la durée de l'appel — bloque les écritures SMS pendant ce temps
    with sim_lock:
        ser = ser_param
        try:
            print("--- Démarrage Système Bateau ---")
            #print(f"Port {PORT_SERIE} ouvert.")
     
            # PURGE OBLIGATOIRE : Élimine les résidus du thread SMS
            if ser.in_waiting:
                ser.reset_input_buffer()

            time.sleep(1)
     
            envoyer_commande("AT")
            envoyer_commande("AT+CMEE=2")
            envoyer_commande("AT+CREG=0")
            envoyer_commande("AT+CGREG=0")
            envoyer_commande("AT+CEREG=0")
     
            if verifier_reseau():
     
                print(f"📞 Lancement appel vers {NUMERO}...")
                interface_bateau.update_messages("Appel en cours...") #message sur l'interface
                ser.write(f"ATD{NUMERO};\r\n".encode())
                time.sleep(2)
     
                rep_appel = lire_tampon().strip()
                if rep_appel:
                    print(f"   < RÉPONSE APPEL : {rep_appel}")
     
                if "NO CARRIER" in rep_appel or "ERROR" in rep_appel:
                    print("❌ APPEL REJETÉ IMMÉDIATEMENT.")
                    print("🔍 Analyse cause (CEER)...")
                    envoyer_commande("AT+CEER")
                    return
     
                est_decroche = False
                print("⏳ En attente de décrochage ou fin d'appel...")
     
                for i in range(300):
                    time.sleep(1)
     
                    ser.write(b"AT+CLCC\r\n")
                    time.sleep(0.5)
                    reponse = lire_tampon()
                    clean = reponse.replace(" ", "").strip()
     
                    if est_decroche and "+CLCC:" not in clean and "OK" in clean:
                        print("📵 L'AUTRE A RACCROCHÉ.")
                        print("🔍 Analyse cause (CEER)...")
                        envoyer_commande("AT+CEER")
                        break
     
                    if "NOCARRIER" in clean or "NO CARRIER" in clean:
                        print("❌ APPEL COUPÉ PAR LE RÉSEAU.")
                        print("🔍 Analyse cause (CEER)...")
                        envoyer_commande("AT+CEER")
                        break
     
                    if "+CLCC:" in clean:
                        parts = clean.split('+CLCC:')[-1].split(',')
                        if len(parts) >= 3:
                            statut = parts[2]
                            if statut == '0' and not est_decroche:
                                print("🟢 L'AUTRE A DÉCROCHÉ !")
                                est_decroche = True
                            elif statut == '3' and not est_decroche:
                                print(f"   🔔 Sonnerie en cours... ({i+1}s)")
     
                else:
                    print("⏱️ Timeout atteint (5 min). Fin forcée.")
     
                print("🏁 Fin de la séquence. Raccrochage...")
                interface_bateau.update_messages("Fin d'appel") #message sur l'interface
                envoyer_commande("AT+CHUP")
     
            else:
                print("🚫 Abandon : Réseau ou signal insuffisant.")
     
        except Exception as e:
            print(f"\n❌ ERREUR SCRIPT : {e}")
     
        finally:
            print("\n--- Nettoyage et Arrêt ---")
            print("Port série rendu au main.")