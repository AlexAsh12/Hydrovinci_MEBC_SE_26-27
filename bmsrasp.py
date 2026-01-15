import serial
import time
import sys

# SUR RASPBERRY PI : 
# Si vous utilisez l'adaptateur USB de votre image, le port est généralement /dev/ttyUSB0
PORT_COM = '/dev/ttyUSB0' 
BAUD_RATE = 9600

def decode_xdz_frame(hex_response):
    try:
        # Nettoyage de la trame (on enlève les marqueurs de début/fin si présents)
        raw = hex_response.replace("~", "").strip()
        
        # Vérification minimale de la longueur pour éviter les erreurs d'index
        if len(raw) < 20:
            return

        num_cells = int(raw[16:18], 16)
        
        print(f"\n{'='*45}")
        print(f"      MONITORING BMS XDZ - {time.strftime('%H:%M:%S')}")
        print(f"{'='*45}")

        # 1. Tensions (mV)
        ptr = 18
        print(" Tensions Cellules (mV):")
        for i in range(num_cells):
            v_mv = int(raw[ptr:ptr+4], 16)
            print(f" Cell{i+1:02d}: {v_mv}", end=" | " if (i+1)%4 != 0 else "\n")
            ptr += 4
        if num_cells % 4 != 0: print()

        # 2. Températures (°C)
        num_temp = int(raw[ptr:ptr+2], 16)
        ptr += 2
        
        t_list = []
        for i in range(num_temp):
            val = (int(raw[ptr:ptr+4], 16) - 2731) / 10.0
            t_list.append(val)
            ptr += 4

        if len(t_list) >= 6:
            print("\n Températures (°C):")
            print(f" ENV    : {t_list[5]:.1f}  |  MOS01  : {t_list[4]:.1f}")
            print(f" Tcell01: {t_list[0]:.1f}  |  Tcell02: {t_list[1]:.1f}")
            print(f" Tcell03: {t_list[2]:.1f}  |  Tcell04: {t_list[3]:.1f}")

        # 3. Données Électriques
        curr_raw = int(raw[ptr:ptr+4], 16)
        current_a = (curr_raw if curr_raw < 32768 else curr_raw - 65536) / 100.0
        ptr += 4

        v_afe = int(raw[ptr:ptr+4], 16) / 1000.0
        ptr += 4

        # Capacité et SOC
        rem_cap_mah = int(raw[ptr:ptr+4], 16) * 10 
        full_cap_mah = 98000 
        soc = (rem_cap_mah / full_cap_mah) * 100

        print("-" * 45)
        print(f" Tension AFE : {v_afe:.2f} V")
        print(f" Courant     : {current_a:.2f} A")
        print(f" SOC         : {soc:.1f} %")
        print(f" Capacité    : {rem_cap_mah/1000:.2f} Ah / {full_cap_mah/1000:.1f} Ah")
        print(f"{'='*45}")

    except Exception as e:
        print(f" Erreur de décodage: {e}")

def run_monitor():
    print(f"Lancement du monitoring sur {PORT_COM}...")
    try:
        # Configuration du port série
        with serial.Serial(PORT_COM, BAUD_RATE, timeout=2) as ser:
            # On vide le tampon à l'ouverture
            ser.flushInput()
            
            while True:
                # Envoi de la commande de lecture au BMS
                cmd = "~01014642E00201FD36\r"
                ser.write(cmd.encode('ascii'))
                
                # Lecture de la réponse jusqu'au retour chariot
                response = ser.read_until(b'\r').decode('ascii', errors='ignore').strip()
                
                if response:
                    decode_xdz_frame(response)
                else:
                    print("... en attente de réponse du BMS ...")
                
                time.sleep(2)
                
    except serial.SerialException as e:
        print(f"Erreur port série: {e}")
        print("Vérifiez que l'adaptateur USB est bien branché.")
    except KeyboardInterrupt:
        print("\nArrêt par l'utilisateur.")

if __name__ == "__main__":
    run_monitor()