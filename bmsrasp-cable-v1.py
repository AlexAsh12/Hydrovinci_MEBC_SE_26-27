#!/usr/bin/env python3
"""
BMS XDZN - Monitoring Câblé (RS485/UART)
Monaco Energy Boat Challenge 2026
Adaptation directe du code BLE vers Série
"""
import transmission
import serial
import time
import json
import os
from datetime import datetime

# --- Configuration ---
PORT_COM = '/dev/ttyUSB0'  # À adapter (ex: '/dev/ttyAMA0' sur RPi ou 'COM3' sur Windows)
BAUD_RATE = 9600
JSON_FILE = 'data_bms.json'
NOMINAL_CAPACITY_AH = 98.0  # Capacité pour le calcul SOC (si non fournie par le BMS)

def save_to_json(data):
    """Enregistre les données dans un historique JSON (max 1000 entrées)"""
    try:
        if os.path.exists(JSON_FILE):
            with open(JSON_FILE, 'r') as f:
                history = json.load(f)
        else:
            history = []

        history.append(data)
        with open(JSON_FILE, 'w') as f:
            json.dump(history[-1000:], f, indent=4)
    except Exception as e:
        print(f"❌ Erreur JSON: {e}")

def decode_xdz_frame(hex_response):
    """
    Décode la trame série pour extraire un dictionnaire compatible 
    avec la fonction display_data d'origine.
    """
    try:
        raw = hex_response.replace("~", "").strip()
        if len(raw) < 20: 
            return None

        # Extraction selon le protocole série XDZN
        num_cells = int(raw[16:18], 16)
        
        # Cellules (mV -> V)
        ptr = 18
        cells = []
        for i in range(num_cells):
            cells.append(int(raw[ptr:ptr+4], 16) / 1000.0)
            ptr += 4

        # Températures
        num_temps = int(raw[ptr:ptr+2], 16)
        ptr += 2
        temps = []
        for i in range(num_temps):
            temps.append((int(raw[ptr:ptr+4], 16) - 2731) / 10.0)
            ptr += 4

        # Courant (Signé)
        curr_raw = int(raw[ptr:ptr+4], 16)
        current_a = (curr_raw if curr_raw < 32768 else curr_raw - 65536) / 100.0
        ptr += 4
        
        # Tension totale
        voltage = int(raw[ptr:ptr+4], 16) / 1000.0
        ptr += 4
        
        # Capacité restante (mAh -> Ah)
        cap_mah = int(raw[ptr:ptr+4], 16) * 10 
        capacity = cap_mah / 1000.0
        
        # SOC
        soc = (capacity / NOMINAL_CAPACITY_AH) * 100

        return {
            'cells': cells,
            'temps': temps,
            'voltage': voltage,
            'current_a': current_a,
            'capacity': capacity,
            'nominal_capacity': NOMINAL_CAPACITY_AH,
            'cycles': 0,        # Non présent dans cette trame courte
            'power_max': 0,     # Non présent
            'soc': round(soc, 1),
            'timestamp': datetime.now().strftime("%H:%M:%S")
        }
    except Exception as e:
        print(f"❌ Erreur décodage: {e}")
        return None

def display_data(data):
    """
    FONCTION ORIGINALE : Affiche les données de façon lisible avec analyse
    """
    if not data: return
    
    print(f"\n{'='*80}")
    print(f"BMS XDZN - {data['timestamp']} (LIAISON CÂBLE)")
    print(f"{'='*80}\n")
    
    # Cellules
    print(f"CELLULES ({len(data['cells'])}S):")
    for i, v in enumerate(data['cells'], 1):
        status = "[BASSE]" if v < 3.0 else "[HAUTE]" if v > 4.2 else "[OK]"
        print(f"  Cell {i:2d}: {v:.3f}V  {status}")
    
    # Stats cellules
    v_min, v_max = min(data['cells']), max(data['cells'])
    v_avg = sum(data['cells']) / len(data['cells'])
    delta = (v_max - v_min) * 1000
    
    print(f"\n  {'-'*40}")
    print(f"  Min:      {v_min:.3f}V  (Cell #{data['cells'].index(v_min)+1})")
    print(f"  Max:      {v_max:.3f}V  (Cell #{data['cells'].index(v_max)+1})")
    print(f"  Moyenne:  {v_avg:.3f}V")
    print(f"  Delta:    {delta:.0f}mV", end="")
    
    if delta > 100: print(" [DESEQUILIBRE]")
    elif delta > 50: print(" [ATTENTION]")
    else: print(" [OK]")
    
    # Températures
    print(f"\nTEMPERATURES ({len(data['temps'])} sondes):")
    for i, t in enumerate(data['temps'], 1):
        status = "[ALERTE]" if (t < 0 or t > 60) else "[OK]"
        print(f"  Sonde {i}: {t:5.1f}C  {status}")
    
    # Système
    print(f"\nSYSTEME:")
    print(f"  Tension totale:    {data['voltage']:.2f}V")
    print(f"  Courant actuel:    {data.get('current_a', 0):.2f}A")
    print(f"  Capacite restante: {data['capacity']:.2f}Ah")
    print(f"  SOC (calculé):     {data['soc']}%")
    
    # Alertes
    alerts = []
    if delta > 100: alerts.append("ALERTE: Desequilibre cellules > 100mV")
    if any(t > 50 for t in data['temps']): alerts.append("ALERTE: Temperature elevee")
    if v_min < 3.0: alerts.append("ALERTE: Cellule sous-dechargee")
    
    if alerts:
        print(f"\nALERTES:")
        for alert in alerts: print(f"  ⚠️ {alert}")
    
    print(f"\n{'='*80}")

async def run_monitor(interval):
    """Boucle principale de monitoring via Serial"""
    print(f"Démarrage Monitoring sur {PORT_COM}...")
    errors = 0
    
    try:
        # Initialisation du port série
        with serial.Serial(PORT_COM, BAUD_RATE, timeout=2) as ser:
            ser.flushInput()
            
            while True:
                # Commande de lecture
                ser.write("~01014642E00201FD36\r".encode('ascii'))
                
                # Lecture
                response = ser.read_until(b'\r').decode('ascii', errors='ignore').strip()
                
                if response:
                    data = decode_xdz_frame(response)
                    if data:
                        display_data(data)
                        save_to_json(data)

                        # --- Envoi MQTT via transmission.py ---
                        try:
                            transmission.EnvoyerDonneesMqtt(data)
                        except Exception as e:
                            print(f"⚠️ Erreur MQTT: {e}")

                        errors = 0
                    else:
                        errors += 1
                else:
                    errors += 1
                    print(f"Echec lecture ({errors}/3)")

                if errors >= 3:
                    print("\nBMS injoignable par câble. Vérifiez les branchements.")
                    break
                
                time.sleep(interval)
                
    except KeyboardInterrupt:
        print("\nArrêt demandé.")
    except Exception as e:
        print(f"Erreur série: {e}")

if __name__ == "__main__":
    import asyncio
    
    print("="*80)
    print("BMS XDZN - MONITORING CÂBLE")
    print("Monaco Energy Boat Challenge 2026")
    print("="*80 + "\n")
    
    try:
        sec = float(input("Intervalle (sec, défaut 2s): ") or "2")
    except:
        sec = 2

    asyncio.run(run_monitor(sec))
