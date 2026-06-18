import tkinter as tk
from tkinter import font

from telemetrie import port_serie

root = None
lbl_bms = None
lbl_msg = None
lbl_other = None
title_font = None
data_font = None

def _setup_section(title, color):
    frame = tk.LabelFrame(root, text=title, bg="#101010", fg=color, font=title_font, bd=2)
    frame.pack(fill="both", expand=True, padx=15, pady=8)
    label = tk.Label(frame, text="En attente de connexion...", bg="#101010", fg="white",
                     font=data_font, justify="left", anchor="nw")
    label.pack(fill="both", expand=True, padx=15, pady=10)
    return label

def init():
    global root, lbl_bms, lbl_msg, lbl_other, title_font, data_font

    root = tk.Tk()
    root.title("Monitoring Monaco 2026")

    # --- ASTUCE TEST : Commente la ligne fullscreen pour ne plus être bloqué ---
    root.attributes('-fullscreen', True)
    root.geometry("1000x700") # Fenêtre large pour tester tranquillement

    root.configure(bg='#101010')

    # Quitter avec Echap (ESC) - Toujours actif
    root.bind("<Escape>", lambda e: root.destroy())

    title_font = font.Font(family="Helvetica", size=18, weight="bold")
    data_font = font.Font(family="Consolas", size=14, weight="bold")

    # Barre du haut fixée pour le bouton
    top_bar = tk.Frame(root, bg='#101010', height=50)
    top_bar.pack(fill="x", side="top")

    btn_quit = tk.Button(top_bar, text=" QUITTER [X] ", command=root.destroy,
                         bg="#cc0000", fg="white", font=("Arial", 12, "bold"),
                         relief="raised", bd=3)
    btn_quit.pack(side="right", padx=10, pady=5)

    # Zones d'affichage
    lbl_bms = _setup_section("ÉTAT BATTERIES (V | A | SOC | TEMP)", "#00d4ff")
    lbl_msg = _setup_section("MESSAGES SYSTÈME", "#ffcc00")
    lbl_other = _setup_section("STATUT ET AUTRES", "#00ff88")

    # Afficher l'etat initial des peripheriques (le SIM7600 est deja detecte a ce stade)
    _rafraichir_statuts()

    root.mainloop()

def update_bms(bms1, bms2):
    def _do():
        try:
            results = []
            for d in [bms1, bms2]:
                if d is None:
                    results.append("> BMS : En attente de connexion...")
                    continue

                bid = d.get('bms_id', '??')
                v = d.get('voltage', 0.0)
                i = d.get('current_a', 0.0)
                soc = d.get('soc', 0.0)
                cells = d.get('cells', [0])
                delta_v = (max(cells) - min(cells)) * 1000

                # Récupération de toutes les sondes
                t_list = d.get('temps', [])
                if len(t_list) >= 6:
                    t_cells_max = max(t_list[0:4])
                    t_ambiant = t_list[4]  # La 5ème valeur (température extérieure/ambiante)
                    t_mosfet = t_list[5]   # La 6ème valeur (électronique)

                    temp_info = f"CellMax: {t_cells_max}°C | AMBIANT: {t_ambiant}°C | MOS: {t_mosfet}°C"
                else:
                    temp_info = f"T: {max(t_list) if t_list else 'N/A'}°C"

                # Affichage final
                line = (f"[{bid}] {v:5.2f}V | {i:6.2f}A | SOC: {soc:5.1f}% | ΔV: {delta_v:3.0f}mV\n"
                        f"      {temp_info}")
                results.append(line)

            lbl_bms.config(text="\n\n".join(results))

            # --- ALERTE COULEUR ---
            # Si l'ambiante ou le MOSFET dépasse 50°C, on passe en rouge
            if any((d.get('temps', [0,0,0,0,0,0])[4] > 50 or d.get('temps', [0,0,0,0,0,0])[5] > 65) for d in [bms1, bms2] if d):
                lbl_bms.config(fg="#ff4444")
            else:
                lbl_bms.config(fg="white")

        except Exception as e:
            print(f"Erreur affichage: {e}")

    if root:
        root.after(0, _do)

messages = []
def update_messages(msg):
    if root:
        messages.append(msg)
        if len(messages) > 20: # Limite stricte en mémoire
            messages.pop(0)
        texte = "\n".join(messages[-13:])
        root.after(0, lambda t=texte: lbl_msg.config(text=t))

# Statuts de connexion des peripheriques
statuts = {"BMS_1": False, "BMS_2": False, "SIM7600": False}

def update_statut(device, connecte):
    """Met a jour le statut d'un peripherique et rafraichit l'affichage."""
    statuts[device] = connecte
    _rafraichir_statuts()

def _rafraichir_statuts():
    """Reconstruit le texte de la section statut a partir du dict."""
    if not root:
        return
    lignes = []
    for device, ok in statuts.items():
        if(device == "SIM7600"):
            if(not ok): lignes.append("[--]  SIM7600 : Non detecte")
            else:
                qualite = port_serie.lire_qualite_signal()
                lignes.append(f"[OK]  SIM7600 : Detecte | Qualité signal : {qualite}")
            continue

        indicateur = "[OK]" if ok else "[--]"
        etat = "Detecte" if ok else "Non detecte"
        lignes.append(f"{indicateur}  {device} : {etat}")
    texte = "\n".join(lignes)
    root.after(0, lambda t=texte: lbl_other.config(text=t))