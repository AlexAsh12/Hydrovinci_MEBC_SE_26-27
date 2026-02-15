#!/usr/bin/env python3
"""
BMS XDZN - Monitoring BLE
Monaco Energy Boat Challenge 2026
Registre 0x008C decode avec succes
"""

import asyncio
import time
from datetime import datetime
from bleak import BleakClient, BleakScanner

# Configuration
BMS_MAC = "C0:D6:3C:49:B8:BB"
NOTIFY_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"
WRITE_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"
CHECK_UUID = "0000fffa-0000-1000-8000-00805f9b34fb"

def getCRC(strData):
    """Calcule CRC-16 Modbus"""
    crc = 0xFFFF
    for byte in bytes.fromhex(strData):
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return format(crc, '04x')

def build_command(address, length=0x0000):
    """Construit une commande BLE"""
    cmd = f"7e000103{address:04x}{length:04x}"
    crc = getCRC(cmd)
    return cmd + crc + "0d"

def decode_0x008c(payload):
    """Décode le registre 0x008C - Toutes les données temps réel"""
    try:
        cursor = 0
        
        # Nombre de cellules
        num_cells = payload[cursor]
        cursor += 1
        
        # Cellules (mV)
        cells = []
        for i in range(num_cells):
            cell_mv = int.from_bytes(payload[cursor:cursor+2], 'big')
            cells.append(cell_mv / 1000.0)
            cursor += 2
        
        # Nombre de températures
        num_temps = payload[cursor]
        cursor += 1
        
        # Températures (format RS485: (raw - 2731) / 10)
        temps = []
        for i in range(num_temps):
            temp_raw = int.from_bytes(payload[cursor:cursor+2], 'big')
            temps.append((temp_raw - 2731) / 10.0)
            cursor += 2
        
        # Status flags (2 bytes) - souvent 0x4000
        status_flags = int.from_bytes(payload[cursor:cursor+2], 'big')
        cursor += 2
        
        # Tension totale (cV → V)
        voltage = int.from_bytes(payload[cursor:cursor+2], 'big') / 100.0
        cursor += 2
        
        # Capacité restante (dAh → Ah)
        capacity = int.from_bytes(payload[cursor:cursor+2], 'big') / 10.0
        cursor += 2
        
        # Capacité nominale probable (dAh → Ah) ou autre paramètre
        nominal_capacity = int.from_bytes(payload[cursor:cursor+2], 'big') / 10.0
        cursor += 2
        
        # Cycles ou autre compteur
        cycles = int.from_bytes(payload[cursor:cursor+2], 'big')
        cursor += 2
        
        # Puissance max ou autre paramètre
        power_max = int.from_bytes(payload[cursor:cursor+2], 'big') / 10.0
        cursor += 2
        
        # SOC probable (%)
        soc = int.from_bytes(payload[cursor:cursor+2], 'big') if cursor + 2 <= len(payload) else 0
        cursor += 2
        
        # Données brutes restantes (pour debug)
        remaining = payload[cursor:].hex().upper() if cursor < len(payload) else ""
        
        return {
            'cells': cells,
            'temps': temps,
            'voltage': voltage,
            'capacity': capacity,
            'nominal_capacity': nominal_capacity,
            'cycles': cycles,
            'power_max': power_max,
            'soc': soc,
            'status_flags': status_flags,
            'raw_remaining': remaining
        }
        
    except Exception as e:
        print(f"❌ Erreur décodage: {e}")
        return None

async def read_bms_once():
    """Lit le BMS une fois"""
    devices = await BleakScanner.discover(timeout=5.0)
    
    device = None
    for d in devices:
        if d.address.upper() == BMS_MAC.upper():
            device = d
            break
    
    if not device:
        print("❌ BMS non trouvé")
        return None
    
    try:
        async with BleakClient(device, timeout=30) as client:
            responses = {}
            
            def handler(sender, data):
                responses['last'] = data.hex().upper()
            
            await client.start_notify(NOTIFY_UUID, handler)
            await asyncio.sleep(0.5)
            
            # Authentification
            await client.write_gatt_char(CHECK_UUID, b"HiLink", response=False)
            await asyncio.sleep(1)
            
            # Lire registre 0x008C
            cmd = build_command(0x008C, 0x0000)
            await client.write_gatt_char(WRITE_UUID, bytes.fromhex(cmd), response=False)
            await asyncio.sleep(0.8)
            
            if 'last' in responses:
                resp_bytes = bytes.fromhex(responses['last'])
                data_len = int.from_bytes(resp_bytes[6:8], 'big')
                payload = resp_bytes[8:8+data_len]
                
                return decode_0x008c(payload)
        
    except Exception as e:
        print(f"❌ Erreur BLE: {e}")
        return None

def display_data(data):
    """Affiche les données de façon lisible"""
    if not data:
        return
    
    timestamp = datetime.now().strftime("%H:%M:%S")
    
    print(f"\n{'='*80}")
    print(f"BMS XDZN - {timestamp}")
    print(f"{'='*80}\n")
    
    # Cellules
    print(f"CELLULES ({len(data['cells'])}S):")
    for i, v in enumerate(data['cells'], 1):
        status = ""
        if v < 3.0:
            status = "[BASSE]"
        elif v > 4.2:
            status = "[HAUTE]"
        else:
            status = "[OK]"
        
        print(f"  Cell {i:2d}: {v:.3f}V  {status}")
    
    # Stats cellules
    v_min = min(data['cells'])
    v_max = max(data['cells'])
    v_avg = sum(data['cells']) / len(data['cells'])
    delta = (v_max - v_min) * 1000
    
    print(f"\n  {'-'*40}")
    print(f"  Min:      {v_min:.3f}V  (Cell #{data['cells'].index(v_min)+1})")
    print(f"  Max:      {v_max:.3f}V  (Cell #{data['cells'].index(v_max)+1})")
    print(f"  Moyenne:  {v_avg:.3f}V")
    print(f"  Delta:    {delta:.0f}mV", end="")
    
    if delta > 100:
        print(" [DESEQUILIBRE]")
    elif delta > 50:
        print(" [ATTENTION]")
    else:
        print(" [OK]")
    
    # Températures
    print(f"\nTEMPERATURES ({len(data['temps'])} sondes):")
    for i, t in enumerate(data['temps'], 1):
        status = ""
        if t < 0 or t > 60:
            status = "[ALERTE]"
        else:
            status = "[OK]"
        print(f"  Sonde {i}: {t:5.1f}C  {status}")
    
    # Système principal
    print(f"\nSYSTEME:")
    print(f"  Tension totale:    {data['voltage']:.2f}V")
    print(f"  Capacite restante: {data['capacity']:.2f}Ah")
    print(f"  SOC (estime):      {data['soc']}%")
    
    # Informations complémentaires
    print(f"\nINFORMATIONS:")
    print(f"  Capacite nominale: {data['nominal_capacity']:.1f}Ah")
    print(f"  Cycles:            {data['cycles']}")
    print(f"  Puissance max:     {data['power_max']:.1f}W")
    
    # Calculs dérivés
    print(f"\nCALCULES:")
    
    # SOC calculé depuis capacité (si nominale est connue)
    if data['nominal_capacity'] > 0:
        soc_calc = (data['capacity'] / data['nominal_capacity']) * 100
        print(f"  SOC (cap/nom):     {soc_calc:.1f}%")
    
    # État de santé (SOH)
    balance = 100 - min(delta / 2, 100)
    print(f"  Equilibrage:       {balance:.0f}%")
    
    # Alertes
    alerts = []
    if delta > 100:
        alerts.append("ALERTE: Desequilibre cellules > 100mV")
    if any(t > 50 for t in data['temps']):
        alerts.append("ALERTE: Temperature elevee")
    if any(t < 0 for t in data['temps']):
        alerts.append("ALERTE: Temperature negative")
    if v_min < 3.0:
        alerts.append("ALERTE: Cellule sous-dechargee")
    if v_max > 4.2:
        alerts.append("ALERTE: Cellule sur-chargee")
    
    if alerts:
        print(f"\nALERTES:")
        for alert in alerts:
            print(f"  {alert}")
    else:
        print(f"\nAucune alerte")
    
    print(f"\n{'='*80}")

async def monitor_continuous(interval):
    """Monitoring continu"""
    print("Demarrage du monitoring BMS XDZN")
    print(f"MAC: {BMS_MAC}")
    print(f"Intervalle: {interval}s\n")
    
    errors = 0
    
    while True:
        try:
            data = await read_bms_once()
            
            if data:
                display_data(data)
                errors = 0
            else:
                errors += 1
                print(f"Echec lecture ({errors})")
                
                if errors >= 3:
                    print("\nTrop d'erreurs consecutives. Arret.")
                    break
            
            await asyncio.sleep(interval)
            
        except KeyboardInterrupt:
            print("\n\nArret monitoring")
            break
        except Exception as e:
            print(f"\nErreur: {e}")
            errors += 1
            await asyncio.sleep(interval)

async def read_once():
    """Lecture unique"""
    print("Lecture BMS...\n")
    data = await read_bms_once()
    
    if data:
        display_data(data)
    else:
        print("Echec lecture BMS")

if __name__ == "__main__":
    print("="*80)
    print("BMS XDZN - Monitoring BLE")
    print("Monaco Energy Boat Challenge 2026")
    print("="*80 + "\n")
    
    mode = input("Mode (1=Lecture unique, 2=Monitoring continu): ").strip()
    
    if mode == "2":
        try:
            interval = float(input("Intervalle en secondes (defaut 2s): ").strip() or "2")
            if interval <= 0:
                interval = 2
        except ValueError:
            interval = 2
        
        print()
        asyncio.run(monitor_continuous(interval))
    else:
        asyncio.run(read_once())