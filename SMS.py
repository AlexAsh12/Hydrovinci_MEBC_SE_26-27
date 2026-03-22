import serial
import time
import interface_bateau

# --- CONFIGURATION ---
PORT_SERIE = "/dev/ttyUSB3"
VITESSE = 115200

def lire_sms():
    """Ecoute le port série et affiche les SMS dès qu'ils arrivent"""
    try:
        ser = serial.Serial(PORT_SERIE, VITESSE, timeout=None)
        print("En attente de SMS...")

        # Activer le mode push : le module envoie +CMT dès qu'un SMS arrive
        ser.write("AT+CMGF=1\r\n".encode())  # passe en mode texte
        time.sleep(0.5)
        ser.write("AT+CMGD=1,4\r\n".encode())  # supprime tous les SMS en mémoire
        time.sleep(0.5)
        ser.write("AT+CNMI=2,2,0,0,0\r\n".encode())
        time.sleep(0.5)

        dernier_message = None

        while True:
            ligne = ser.readline().decode(errors='ignore').strip()

            if "+CMT:" in ligne:
                # Extraire l'heure depuis la ligne +CMT
                try:
                    heure = ligne.split(",")[3].replace('"', '').split("+")[0].strip()
                except:
                    heure = "??:??:??"

                # La ligne suivante = contenu du SMS
                raw = ser.readline().strip()
                try:
                    contenu = bytes.fromhex(raw.decode('ascii')).decode('utf-16-be')
                except:
                    contenu = raw.decode('latin-1').strip()


                message = f"[{heure}] {contenu}"

                if message != dernier_message:
                    dernier_message = message
                    print(f"SMS reçu : {message}")
                    interface_bateau.update_messages(message)
                    print("update envoyé vers interface")

                ser.write("AT+CMGD=1,4\r\n".encode())  # supprime le SMS qui vient d'arriver
                ser.flushInput()


    except Exception as e:
        print(f"❌ Erreur SMS : {e}")

