#!/usr/bin/env python3
"""
BMS XDZN - Dual Monitoring Câblé
Monaco Energy Boat Challenge 2026
Lecture simultanée sur USB0 et USB3
"""

import serial
import time

import asyncio
from datetime import datetime
from interface import interface_bateau
from telemetrie import mqtt_publisher
import config


# --- Configuration ---
# BMS_CONFIG = {...} -> Centralisé dans config.py
# BAUD_RATE = 9600 -> Centralisé dans config.py
# NOMINAL_CAPACITY_AH = 98.0 -> Centralisé dans config.py
last_reports = {"BMS_1": None, "BMS_2": None}
bms_connected = {"BMS_1": False, "BMS_2": False}





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
        soc = (capacity / config.NOMINAL_CAPACITY_AH) * 100

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

    #print(f"[{name}] {data['timestamp']} | {data['voltage']:.1f}V | {data['current_a']:.1f}A | SOC: {data['soc']}% | Delta: {delta:.0f}mV")

    # Alertes rapides
    if delta > 100: print(f"  ⚠️ ALERT [{name}]: Déséquilibre important !")
    if v_min < 3.0: print(f"  ⚠️ ALERT [{name}]: Tension basse !")

async def monitor_bms(bms_id, bms_conf, interval):
    """Tâche asynchrone pour un seul BMS"""
    print(f"🚀 Démarrage {bms_id} sur {bms_conf['port']}")
    
    # --- NOUVEAU CODE : Variable d'état ---
    erreur_affichee = False

    while True:
        try:
            # On ouvre et ferme le port à chaque lecture pour éviter les blocages
            with serial.Serial(bms_conf['port'], config.BAUD_RATE_BMS, timeout=1) as ser:
                ser.write("~01014642E00201FD36\r".encode('ascii'))
                response = ser.read_until(b'\r').decode('ascii', errors='ignore').strip()

                if response:
                    data = decode_xdz_frame(response, bms_id)
                    if data:
                        display_bms_status(data)
                        last_reports[bms_id] = data

                        interface_bateau.update_bms(
                            last_reports.get("BMS_1"),
                            last_reports.get("BMS_2")
                        )
                        # --- NOUVEAU CODE : Reset de l'état ---
                        if erreur_affichee:
                            print(f"\u2705 {bms_id} reconnecté.")
                            erreur_affichee = False
                        
                        # Signaler la connexion au dashboard
                        if not bms_connected[bms_id]:
                            bms_connected[bms_id] = True
                            try:
                                mqtt_publisher.publish_bms_connection(bms_connected)
                            except Exception:
                                pass

                        # Envoi des données au dashboard via MQTT
                        try:
                            mqtt_publisher.publish_bms(data)
                        except Exception:
                            pass  # Le BMS ne s'arrête JAMAIS à cause du MQTT
                else:
                    # --- MODIFICATION : Filtrage du spam ---
                    if not erreur_affichee:
                        print(f"❌ {bms_id} ({bms_conf['port']}): Pas de réponse")
                        erreur_affichee = True

        except Exception as e:
            # --- MODIFICATION : Filtrage du spam ---
            if not erreur_affichee:
                print(f"\u26a0\ufe0f Erreur sur {bms_id}: {e}")
                erreur_affichee = True
            # Signaler la déconnexion au dashboard
            if bms_connected[bms_id]:
                bms_connected[bms_id] = False
                try:
                    mqtt_publisher.publish_bms_connection(bms_connected)
                except Exception:
                    pass

        await asyncio.sleep(interval)

async def main():
    print("="*60)
    print("MONACO ENERGY BOAT CHALLENGE 2026 - DUAL BMS MONITOR")
    print("="*60)

    interval = config.INTERVALLE_BMS

    # Lancement des deux monitorings en parallèle
    tasks = [
        monitor_bms("BMS_1", config.BMS_CONFIG["BMS_1"], interval),
        monitor_bms("BMS_2", config.BMS_CONFIG["BMS_2"], interval)
    ]

    await asyncio.gather(*tasks)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nArrêt du monitoring.")