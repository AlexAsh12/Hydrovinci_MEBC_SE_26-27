#!/usr/bin/env python3
"""
Gestionnaire centralise du port serie SIM7600.
Utilise par sms.py, appel.py et main.py.

Le SIM7600 communique via des commandes AT sur un port USB serie.
Ce module fournit :
- SafeSerial : classe corrigee pour eviter les crashs DTR/RTS sur Pi
- Detection automatique du port AT au demarrage
- Fonctions partagees : envoyer_commande, lire_tampon
- Lock pour empecher SMS et Appel d'ecrire en meme temps
"""

import serial
import time
import glob
import threading
import config


class SafeSerial(serial.Serial):
    """
    Herite de serial.Serial.
    Neutralise les signaux DTR/RTS que le port virtuel du SIM7600
    ne supporte pas (provoque un crash sur Pi 5 et Pi Zero).
    """
    def _update_dtr_state(self):
        pass

    def _update_rts_state(self):
        pass


# --- Etat global du port serie ---
# ser = None signifie que le SIM7600 n'est pas branche.
# Les BMS utilisent leurs propres ports serie, ils ne sont pas affectes.
ser = None

# Lock partage entre SMS et Appel pour eviter les conflits d'ecriture.
sim_lock = threading.Lock()


def detecter_port_at():
    """
    Balaye tous les ports /dev/ttyUSB* pour trouver celui qui repond a "AT".
    Retourne le chemin du port (ex: "/dev/ttyUSB3") ou None si aucun ne repond.
    """
    candidats = sorted(glob.glob('/dev/ttyUSB*'))
    print(f"Audit materiel des ports : {candidats}")

    for port in candidats:
        try:
            test = SafeSerial(port, baudrate=int(config.VITESSE_MODEM), timeout=1)
            test.reset_input_buffer()

            # Reveil materiel : certains modules ont besoin de 2 AT consecutifs
            test.write(b"AT\r\n")
            time.sleep(0.2)
            test.write(b"AT\r\n")
            time.sleep(0.2)

            reponse = test.read_all().decode(errors='ignore').strip()
            test.close()

            if "OK" in reponse:
                print(f"Interface AT validee sur : {port}")
                return port

        except Exception as e:
            print(f"Rejet de {port} : {e}")

    print("Aucun port AT ne repond. SIM7600 non detecte.")
    return None


def initialiser():
    """
    Detecte le port AT, ouvre la connexion serie et lance le diagnostic.
    Retourne True si le SIM7600 est pret, False sinon.
    Le programme continue dans les deux cas.
    """
    global ser

    port = detecter_port_at()
    if port is None:
        print("SIM7600 non detecte. Le systeme continue sans SMS/Appel.")
        return False

    ser = SafeSerial(port, baudrate=config.VITESSE_MODEM, timeout=1)
    diagnostic_sim()
    return True


def envoyer_commande(cmd, attente=0.5):
    """
    Envoie une commande AT sur le port serie et retourne la reponse.
    Si le port n'est pas ouvert, retourne une chaine vide sans planter.
    """
    if ser and ser.is_open:
        ser.write((cmd + "\r\n").encode())
        time.sleep(attente)
        if ser.in_waiting:
            reponse = ser.read(ser.in_waiting).decode(errors='ignore').strip()
            print(f"   < {reponse}")
            return reponse
    return ""


def lire_tampon():
    """
    Lit tout le contenu en attente dans le buffer du port serie.
    Utilise apres une commande pour recuperer les reponses asynchrones.
    """
    if ser and ser.in_waiting:
        return ser.read(ser.in_waiting).decode(errors='ignore')
    return ""


def diagnostic_sim():
    """
    Diagnostic rapide au demarrage : verifie la carte SIM, le signal et le reseau.
    Purement informatif, ne bloque jamais le programme.
    """
    print("=" * 40)
    print("DIAGNOSTIC SIM7600")
    print("=" * 40)
    try:
        ser.reset_input_buffer()

        # 1. Carte SIM presente ?
        ser.write(b"AT+CPIN?\r\n")
        time.sleep(0.5)
        rep = ser.read(ser.in_waiting).decode(errors='ignore').strip() if ser.in_waiting else ""
        if "READY" in rep:
            print("Carte SIM detectee")
        else:
            print(f"SIM : {rep if rep else 'Pas de reponse'}")
            print("=" * 40)
            return

        # 2. Qualite du signal (AT+CSQ)
        ser.write(b"AT+CSQ\r\n")
        time.sleep(0.5)
        rep = ser.read(ser.in_waiting).decode(errors='ignore').strip() if ser.in_waiting else ""
        print(f"Signal : {rep}")

        # 3. Enregistrement sur le reseau mobile (AT+CREG)
        ser.write(b"AT+CREG?\r\n")
        time.sleep(0.5)
        rep = ser.read(ser.in_waiting).decode(errors='ignore').strip() if ser.in_waiting else ""
        if ",1" in rep or ",5" in rep:
            print("Enregistre sur le reseau")
        else:
            print(f"Reseau : {rep if rep else 'Non enregistre'}")

        # 4. Operateur actif (AT+COPS)
        ser.write(b"AT+COPS?\r\n")
        time.sleep(0.5)
        rep = ser.read(ser.in_waiting).decode(errors='ignore').strip() if ser.in_waiting else ""
        print(f"Operateur : {rep}")

    except Exception as e:
        print(f"Diagnostic impossible : {e}")
    print("=" * 40)
