import serial
import time
import json
import os

PORT_COM = '/dev/ttyUSB0'
BAUD_RATE = 9600
JSON_FILE = 'data_bms.json'

def save_to_json(data):
    """Enregistre les données dans un fichier JSON sous forme de liste d'historique."""
    try:
        # 1. Lire le fichier existant s'il existe
        if os.path.exists(JSON_FILE):
            with open(JSON_FILE, 'r') as f:
                history = json.load(f)
        else:
            history = []

        # 2. Ajouter la nouvelle lecture
        history.append(data)

        # 3. Sauvegarder (on limite à 1000 entrées pour ne pas saturer la carte SD)
        with open(JSON_FILE, 'w') as f:
            json.dump(history[-1000:], f, indent=4)
            
    except Exception as e:
        print(f" Erreur lors de l'enregistrement JSON: {e}")

def decode_xdz_frame(hex_response):
    try:
        raw = hex_response.replace("~", "").strip()
        if len(raw) < 20: return

        num_cells = int(raw[16:18], 16)
        timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
        
        # --- Extraction des données ---
        ptr = 18
        cells_tensions = []
        for i in range(num_cells):
            cells_tensions.append(int(raw[ptr:ptr+4], 16))
            ptr += 4

        num_temp = int(raw[ptr:ptr+2], 16)
        ptr += 2
        t_list = []
        for i in range(num_temp):
            t_list.append((int(raw[ptr:ptr+4], 16) - 2731) / 10.0)
            ptr += 4

        curr_raw = int(raw[ptr:ptr+4], 16)
        current_a = (curr_raw if curr_raw < 32768 else curr_raw - 65536) / 100.0
        ptr += 4
        v_afe = int(raw[ptr:ptr+4], 16) / 1000.0
        ptr += 4
        rem_cap_mah = int(raw[ptr:ptr+4], 16) * 10 
        soc = (rem_cap_mah / 98000) * 100

        # --- Création du dictionnaire pour le JSON ---
        data_to_save = {
            "timestamp": timestamp,
            "tension_totale_v": v_afe,
            "courant_a": current_a,
            "soc_percent": round(soc, 1),
            "capacite_restante_ah": rem_cap_mah / 1000,
            "tensions_cellules_mv": cells_tensions,
            "temperatures_c": {
                "cell_01": t_list[0] if len(t_list)>0 else None,
                "cell_02": t_list[1] if len(t_list)>1 else None,
                "mos": t_list[4] if len(t_list)>4 else None,
                "env": t_list[5] if len(t_list)>5 else None
            }
        }

        # --- Affichage Écran ---
        print(f"\n>>> DONNÉES REÇUES À {timestamp}")
        print(f" SOC: {soc:.1f}% | Tension: {v_afe:.2f}V | Courant: {current_a:.2f}A")
        
        # --- Sauvegarde ---
        save_to_json(data_to_save)
        print(f" Données enregistrées dans {JSON_FILE}")

    except Exception as e:
        print(f" Erreur de décodage: {e}")

def run_monitor():
    print(f"Lancement du monitoring (JSON actif) sur {PORT_COM}...")
    try:
        with serial.Serial(PORT_COM, BAUD_RATE, timeout=2) as ser:
            ser.flushInput()
            while True:
                ser.write("~01014642E00201FD36\r".encode('ascii'))
                response = ser.read_until(b'\r').decode('ascii', errors='ignore').strip()
                if response:
                    decode_xdz_frame(response)
                time.sleep(2)
    except Exception as e:
        print(f"Erreur: {e}")

if __name__ == "__main__":
    run_monitor()