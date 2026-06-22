#!/usr/bin/env python3
"""
Reception des SMS via le module SIM7600.
Utilise le polling : interroge la carte SIM toutes les 4 secondes
pour verifier si un nouveau message est arrive.
"""

import time
import config
from interface import interface_bateau
from telemetrie import port_serie


# --- Envoi d'un SMS a la demande du pilote (declenche par un bouton poussoir) ---
# Le bouton appelle demander_envoi_sms(); le SMS part au prochain tour de la
# boucle de lire_sms() (donc dans le sim_lock, sans perturber lecture/appel).
_sms_a_envoyer = False
_sms_contenu = "Bien recu"


def demander_envoi_sms(texte="Bien recu"):
    """Marque un SMS a envoyer au numero cible (config.NUMERO_DEST).
    Appele par le bouton pilote. L'envoi a lieu au prochain tour de lire_sms()."""
    global _sms_a_envoyer, _sms_contenu
    _sms_contenu = texte
    _sms_a_envoyer = True


def _envoyer_sms(numero, texte):
    """Envoie un SMS en mode texte via envoyer_commande. A appeler en tenant sim_lock.
    Le mode texte (AT+CMGF=1) est deja active par la boucle lire_sms()."""
    # 1) Destinataire : le modem repond par un prompt ">"
    port_serie.envoyer_commande(f'AT+CMGS="{numero}"')
    # 2) Corps du message + Ctrl-Z (0x1A) = envoi. attente plus longue (envoi reseau).
    reponse = port_serie.envoyer_commande(texte + "\x1a", attente=3)
    print(f"Envoi SMS -> {numero} : {reponse}")


def lire_sms():
    """
    Boucle principale de lecture des SMS.
    Fonctionne en mode polling securise :
    1. Initialise le modem en mode texte
    2. Toutes les 4 secondes, demande la liste des SMS stockes sur la SIM
    3. Decode et affiche les nouveaux messages sur l'interface
    4. Purge les SMS lus pour ne pas les relire
    """
    global _sms_a_envoyer
    print("En attente de SMS (Mode Polling Securise v2)...")

    dernier_message = None

    while True:
        try:
            # Reinitialisation AT a chaque tour : reconfigure le modem automatiquement
            # s'il a reset. Commandes idempotentes, cout batterie negligeable (pas de radio).
            # NOTE: pourra etre sorti de la boucle plus tard si besoin (ex: reinit seulement sur echec).
            with port_serie.sim_lock:

                # Forcer le stockage et la lecture sur la carte SIM (pas la memoire interne)
                port_serie.ser.write("AT+CPMS=\"SM\",\"SM\",\"SM\"\r\n".encode())
                time.sleep(0.5)
                port_serie.ser.reset_input_buffer()

                # Activer le mode texte (par opposition au mode PDU binaire)
                port_serie.ser.write("AT+CMGF=1\r\n".encode())
                time.sleep(0.5)
                port_serie.ser.reset_input_buffer()

                # Mode silencieux : le modem stocke les SMS sans envoyer de notification
                # Evite de perturber les appels en cours
                port_serie.ser.write("AT+CNMI=2,0,0,0,0\r\n".encode())
                time.sleep(0.5)
                port_serie.ser.reset_input_buffer()

            # Pause entre chaque verification. 4 secondes = bon compromis
            # entre reactivite et charge CPU/port serie
            time.sleep(4)

            lignes_cmgl = []

            # Lecture de tous les SMS stockes sur la SIM
            with port_serie.sim_lock:
                port_serie.ser.reset_input_buffer()
                port_serie.ser.write("AT+CMGL=\"ALL\"\r\n".encode())

                # Lecture avec timeout : on attend max 3 secondes la reponse complete
                debut = time.time()
                old_timeout = port_serie.ser.timeout
                port_serie.ser.timeout = 0.5

                try:
                    while time.time() - debut < 3:
                        ligne_brute = port_serie.ser.readline().decode(errors='ignore').strip()
                        if ligne_brute:
                            lignes_cmgl.append(ligne_brute)
                        # "OK" ou "ERROR" = le modem a fini d'envoyer
                        if ligne_brute == "OK" or ligne_brute == "ERROR":
                            break
                finally:
                    port_serie.ser.timeout = old_timeout

            interface_bateau.update_statut("SIM7600", True)  # Le modem est present, meme si aucun SMS n'est recu
            
            a_lu_des_sms = False

            # Traitement ligne par ligne de la reponse AT+CMGL
            for i, ligne in enumerate(lignes_cmgl):
                if "+CMGL:" in ligne:
                    a_lu_des_sms = True

                    # Extraction de l'heure d'envoi du SMS
                    try:
                        elements = ligne.split(",")
                        heure_brute = elements[-1]
                        heure = heure_brute.replace('"', '').split("+")[0].strip()
                    except:
                        heure = "??:??:??"

                    # La ligne suivante contient le corps du SMS
                    if i + 1 < len(lignes_cmgl):
                        raw = lignes_cmgl[i+1]

                        # Ignorer les lignes qui ne sont pas du contenu SMS
                        if raw == "OK" or "+CMGL:" in raw:
                            continue

                        # Tentative de decodage UCS-2 (certains operateurs encodent en hex)
                        try:
                            contenu = bytes.fromhex(raw).decode('utf-16-be')
                        except:
                            contenu = raw

                        message = f"[{heure}] {contenu}"

                        # Afficher uniquement les nouveaux messages (evite les doublons)
                        if message != dernier_message:
                            dernier_message = message
                            print(f"SMS recu : {message}")
                            interface_bateau.update_messages(message)

            # Purge de la SIM apres lecture pour ne pas relire les memes SMS
            if a_lu_des_sms:
                with port_serie.sim_lock:
                    port_serie.ser.write("AT+CMGD=1,4\r\n".encode())
                    time.sleep(0.5)
                    port_serie.ser.reset_input_buffer()

            # Envoi d'un SMS a la demande du pilote (bouton). Verifie a chaque tour.
            # Si l'envoi rate, pas grave : pas de retry (drapeau remis a False avant).
            if _sms_a_envoyer:
                _sms_a_envoyer = False
                with port_serie.sim_lock:
                    _envoyer_sms(config.NUMERO_DEST, _sms_contenu)

        except Exception as e:
            print(f"Erreur SMS : {e}")