#!/usr/bin/env python3
"""
BMS XDZN - Dual Monitoring Câblé
Monaco Energy Boat Challenge 2026
Lecture simultanée sur USB0 et USB3
"""

import serial
import time
import json
import os
import asyncio
from datetime import datetime

# Import de la fonction MQTT depuis transmission.py
from transmission import EnvoyerDonneesMqtt

# --- Configuration ---
BMS_CONFIG = {
    "BMS_1": {"port": "/dev/ttyUSB0", "file": "data_bms_1.json"},
    "BMS_2": {"port": "/dev/ttyUSB3", "file": "data_bms_2.json"}
}
BAUD_RATE = 9600
NOMINAL_CAPACITY_AH = 98.0

def save_to_json(filename, data):
    """Enregistre les données dans le fichier spécifique du BMS"""
    try:
        history = []
        if os.path.exists(filename):
            with open(filename, 'r') as f:
                history = json.load(f)
        
        history.append(data)
        with open(filename, 'w') as f:
            json.dump(history[-1000:], f, indent=4)
    except Exception as e:
        print(f"❌ Erreur JSON ({filename}): {e}")

def decode_xdz_frame(hex_response, bms_name):
    """Décode la trame reçue"""
    try:
        raw = hex_response.replace("~", "").strip()
        if len(raw) < 20: return None

        num_cells = int(raw[16:18], 16)
        ptr = 18
        cells = []
        for _ in range(num_cells):
            cells.append(int(raw[ptr:ptr+4], 16) / 1000.0)
            ptr += 4

        num_temps = int(raw[ptr:ptr+2], 16)
        ptr += 2
        temps = []
        for _ in range(num_temps):
            temps.append((int(raw[ptr:ptr+4], 16) - 2731) / 10.0)
            ptr += 4

        curr_raw = int(raw[ptr:ptr+4], 16)
        current_a = (curr_raw if curr_raw < 32768 else curr_raw - 65536) / 100.0
        ptr += 4
        voltage = int(raw[ptr:ptr+4], 16) / 1000.0
        ptr += 4
        capacity = (int(raw[ptr:ptr+4], 16) * 10) / 1000.0
        soc = (capacity / NOMINAL_CAPACITY_AH) * 100

        return {
            'bms_id': bms_name,
            'cells': cells,
            'temps': temps,
            'voltage': voltage,
            'current_a': current_a,
            'capacity': capacity,
            'soc': round(soc, 1),
            'timestamp': datetime.now().strftime("%H:%M:%S")
        }
    except:
        return None

def display_bms_status(data):
    """Affichage compact pour deux BMS côte à côte ou l'un après l'autre"""
    if not data: return
    
    name = data['bms_id']
    v_min, v_max = min(data['cells']), max(data['cells'])
    delta = (v_max - v_min) * 1000
    
    print(f"[{name}] {data['timestamp']} | {data['voltage']:.1f}V | {data['current_a']:.1f}A | SOC: {data['soc']}% | Delta: {delta:.0f}mV")
    
    # Alertes rapides
    if delta > 100: print(f"  ⚠️ ALERT [{name}]: Déséquilibre important !")
    if v_min < 3.0: print(f"  ⚠️ ALERT [{name}]: Tension basse !")

async def monitor_bms(bms_id, config, interval):
    """Tâche asynchrone pour un seul BMS"""
    print(f"🚀 Démarrage {bms_id} sur {config['port']}")
    
    while True:
        try:
            # On ouvre et ferme le port à chaque lecture pour éviter les blocages
            with serial.Serial(config['port'], BAUD_RATE, timeout=1) as ser:
                ser.write("~01014642E00201FD36\r".encode('ascii'))
                response = ser.read_until(b'\r').decode('ascii', errors='ignore').strip()
                
                if response:
                    data = decode_xdz_frame(response, bms_id)
                    if data:
                        display_bms_status(data)
                        save_to_json(config['file'], data)
                        
                        # Envoi MQTT via transmission.py
                        try:
                            EnvoyerDonneesMqtt(data)
                        except Exception as e:
                            print(f"⚠️ Erreur MQTT [{bms_id}]: {e}")
                else:
                    print(f"❌ {bms_id} ({config['port']}): Pas de réponse")
                    
        except Exception as e:
            print(f"⚠️ Erreur sur {bms_id}: {e}")
        
        await asyncio.sleep(interval)

async def main():
    print("="*60)
    print("MONACO ENERGY BOAT CHALLENGE 2026 - DUAL BMS MONITOR")
    print("="*60)
    
    interval = 2.0
    
    # Lancement des deux monitorings en parallèle
    tasks = [
        monitor_bms("BMS_1", BMS_CONFIG["BMS_1"], interval),
        monitor_bms("BMS_2", BMS_CONFIG["BMS_2"], interval)
    ]
    
    await asyncio.gather(*tasks)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nArrêt du monitoring.")
