#!/usr/bin/env python3
"""
Gestion des appels vocaux via le module SIM7600.
Sequence complete : verification reseau -> lancement appel -> suivi en temps reel -> raccrochage.
"""

import time
from interface import interface_bateau
from telemetrie import port_serie
import config

# Seuil minimum de qualite de signal pour tenter un appel.
# Abaisse car le module SIM7600 en 4G retourne souvent un CSQ bas
# meme avec une connexion fonctionnelle.
CSQ_MINIMUM = 5


def verifier_reseau():
    """
    Verifie que le module est enregistre sur le reseau mobile.
    Etapes :
    1. Verification de la presence de la carte SIM (AT+CPIN)
    2. Passage en mode operateur automatique (AT+COPS=0)
    3. Attente de l'enregistrement reseau (AT+CREG) avec 15 tentatives
    4. Si 5 echecs consecutifs, forçage sur SFR (code 20815)
    Retourne True si le reseau est pret, False sinon.
    """
    print("Verification reseau...")

    # Etape 1 : la carte SIM est-elle presente et deverrouillee ?
    rep_sim = port_serie.envoyer_commande("AT+CPIN?")
    if "READY" in rep_sim:
        print("   SIM OK.")
    elif "SIM PIN" in rep_sim:
        print("   SIM VERROUILLEE (PIN requis)")
        return False
    else:
        print("   SIM NON DETECTEE")
        return False

    # Etape 2 : laisser le module choisir l'operateur automatiquement
    port_serie.envoyer_commande('AT+COPS=0', attente=2)

    # Etape 3 : attendre que le module s'enregistre sur le reseau
    # CREG retourne +CREG: 0,X ou X=1 (enregistre) ou X=5 (roaming)
    print("Attente enregistrement reseau...")
    for i in range(15):
        reponse = port_serie.envoyer_commande("AT+CREG?", attente=2)
        if ",1" in reponse or ",5" in reponse:
            print(f"   RESEAU OK (essai {i+1})")
            # Pause de stabilisation avant de lancer l'appel
            print("   Stabilisation reseau (4s)...")
            time.sleep(4)
            return True

        # Apres 5 echecs, on force manuellement le reseau SFR
        if i == 4:
            print("   Forcage reseau SFR (20815)...")
            port_serie.envoyer_commande('AT+COPS=1,2,"20815"', attente=5)

        print(f"   ... En attente ({i+1}/15)")

    print("Impossible de s'enregistrer sur le reseau.")
    return False


def passer_appel():
    """
    Lance un appel vocal vers le numero defini dans config.py.
    Sequence :
    1. Verrouille le port serie (bloque les SMS pendant l'appel)
    2. Purge le buffer serie (residus du thread SMS)
    3. Configure le modem (desactive les notifications non-sollicitees)
    4. Verifie le reseau
    5. Lance l'appel avec ATD
    6. Boucle de suivi : verifie toutes les secondes si l'appel est actif
    7. Detecte le decrochage, le raccrochage ou la coupure reseau
    8. Raccroche proprement avec AT+CHUP
    """
    # Verrouillage du port pour toute la duree de l'appel
    with port_serie.sim_lock:
        try:
            print("--- Demarrage sequence appel ---")

            # Purge du buffer : elimine les residus laisses par le thread SMS
            if port_serie.ser.in_waiting:
                port_serie.ser.reset_input_buffer()

            time.sleep(1)

            # Configuration initiale du modem
            port_serie.envoyer_commande("AT")            # Ping
            port_serie.envoyer_commande("AT+CMEE=2")     # Erreurs detaillees en texte
            port_serie.envoyer_commande("AT+CREG=0")     # Desactive les notifs CREG
            port_serie.envoyer_commande("AT+CGREG=0")    # Desactive les notifs GPRS
            port_serie.envoyer_commande("AT+CEREG=0")    # Desactive les notifs LTE

            if verifier_reseau():

                # Lancement de l'appel avec la commande ATD (le ; final = appel vocal)
                print(f"Lancement appel vers {config.NUMERO_DEST}...")
                interface_bateau.update_messages("Appel en cours...")
                port_serie.ser.write(f"ATD{config.NUMERO_DEST};\r\n".encode())
                time.sleep(2)

                # Lecture de la reponse immediate (OK, NO CARRIER, ERROR)
                rep_appel = port_serie.lire_tampon().strip()
                if rep_appel:
                    print(f"   < REPONSE APPEL : {rep_appel}")

                # Rejet immediat de l'appel
                if "NO CARRIER" in rep_appel or "ERROR" in rep_appel:
                    print("APPEL REJETE IMMEDIATEMENT.")
                    print("Analyse cause (CEER)...")
                    port_serie.envoyer_commande("AT+CEER")
                    return

                est_decroche = False
                print("En attente de decrochage ou fin d'appel...")

                # Boucle de suivi : interroge AT+CLCC chaque seconde pendant max 5 min
                # CLCC retourne la liste des appels actifs avec leur statut :
                # statut 0 = actif (decroche), statut 3 = sonnerie
                for i in range(300):
                    time.sleep(1)

                    port_serie.ser.write(b"AT+CLCC\r\n")
                    time.sleep(0.5)
                    reponse = port_serie.lire_tampon()
                    clean = reponse.replace(" ", "").strip()

                    # Cas 1 : on etait decroche et plus d'appel actif = l'autre a raccroche
                    if est_decroche and "+CLCC:" not in clean and "OK" in clean:
                        print("L'AUTRE A RACCROCHE.")
                        port_serie.envoyer_commande("AT+CEER")
                        break

                    # Cas 2 : coupure reseau
                    if "NOCARRIER" in clean or "NO CARRIER" in clean:
                        print("APPEL COUPE PAR LE RESEAU.")
                        port_serie.envoyer_commande("AT+CEER")
                        break

                    # Cas 3 : analyse du statut CLCC
                    if "+CLCC:" in clean:
                        parts = clean.split('+CLCC:')[-1].split(',')
                        if len(parts) >= 3:
                            statut = parts[2]
                            if statut == '0' and not est_decroche:
                                print("L'AUTRE A DECROCHE !")
                                est_decroche = True
                            elif statut == '3' and not est_decroche:
                                print(f"   Sonnerie en cours... ({i+1}s)")

                else:
                    # La boucle for s'est terminee sans break = timeout 5 min
                    print("Timeout atteint (5 min). Fin forcee.")

            # Raccrochage propre dans tous les cas
            print("Fin de la sequence. Raccrochage...")
            interface_bateau.update_messages("Fin d'appel")
            port_serie.envoyer_commande("AT+CHUP")

        except Exception as e:
            print(f"ERREUR SCRIPT : {e}")

        finally:
            print("--- Nettoyage et Arret ---")
            print("Port serie rendu au main.")