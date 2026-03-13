import tkinter as tk
from tkinter import font

class InterfaceBateau:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Monitoring Monaco 2026")
        
        # --- ASTUCE TEST : Commente la ligne fullscreen pour ne plus être bloqué ---
        # self.root.attributes('-fullscreen', True) 
        self.root.geometry("1000x700") # Fenêtre large pour tester tranquillement
        
        self.root.configure(bg='#101010')

        # Quitter avec Echap (ESC) - Toujours actif
        self.root.bind("<Escape>", lambda e: self.root.destroy())

        self.title_font = font.Font(family="Helvetica", size=18, weight="bold")
        self.data_font = font.Font(family="Consolas", size=14, weight="bold")

        # Barre du haut fixée pour le bouton
        self.top_bar = tk.Frame(self.root, bg='#101010', height=50)
        self.top_bar.pack(fill="x", side="top")
        
        self.btn_quit = tk.Button(self.top_bar, text=" QUITTER [X] ", command=self.root.destroy, 
                                  bg="#cc0000", fg="white", font=("Arial", 12, "bold"), 
                                  relief="raised", bd=3)
        self.btn_quit.pack(side="right", padx=10, pady=5)

        # Zones d'affichage
        self.lbl_bms = self._setup_section("ÉTAT BATTERIES (V | A | SOC | TEMP)", "#00d4ff")
        self.lbl_msg = self._setup_section("MESSAGES SYSTÈME", "#ffcc00")
        self.lbl_other = self._setup_section("IA & NAVIGATION", "#00ff88")

    def _setup_section(self, title, color):
        frame = tk.LabelFrame(self.root, text=title, bg="#101010", fg=color, font=self.title_font, bd=2)
        frame.pack(fill="both", expand=True, padx=15, pady=8)
        label = tk.Label(frame, text="En attente de connexion...", bg="#101010", fg="white", 
                         font=self.data_font, justify="left", anchor="nw")
        label.pack(fill="both", expand=True, padx=15, pady=10)
        return label

    def update_bms(self, bms1, bms2):
        """Affiche les données des deux BMS"""
        try:
            results = []
            for d in [bms1, bms2]:
                if not d:
                    results.append("BMS : Donnée non disponible")
                    continue
                
                bid = d.get('bms_id', '??')
                v = d.get('voltage', 0.0)
                i = d.get('current_a', 0.0)
                soc = d.get('soc', 0.0)
                temps = d.get('temps', [0, 0])
                
                line = f"[{bid}] {v:5.2f}V | {i:6.2f}A | SOC: {soc:5.1f}% | T: {min(temps)}°C > {max(temps)}°C"
                results.append(line)
            
            self.lbl_bms.config(text="\n\n".join(results))
        except Exception as e:
            self.lbl_bms.config(text=f"Erreur d'affichage : {e}")
        
        self.root.update()

    def update_messages(self, msg):
        self.lbl_msg.config(text=str(msg))
        self.root.update()

# Initialisation
interface = InterfaceBateau()