import tkinter as tk
from tkinter import font

class InterfaceBateau:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Monitoring Monaco 2026")
        self.root.attributes('-fullscreen', True)
        self.root.configure(bg='#101010')

        # Quitter avec Echap ou Bouton
        self.root.bind("<Escape>", lambda e: self.root.destroy())

        self.title_font = font.Font(family="Helvetica", size=18, weight="bold")
        self.data_font = font.Font(family="Courier", size=16, weight="bold") # Police mono pour l'alignement

        # Barre supérieure avec bouton Quitter
        self.top_bar = tk.Frame(self.root, bg='#101010')
        self.top_bar.pack(fill="x", side="top", padx=10, pady=5)
        
        self.btn_quit = tk.Button(self.top_bar, text=" FERMER [X] ", command=self.root.destroy, 
                                  bg="#cc0000", fg="white", font=("Arial", 11, "bold"), relief="flat")
        self.btn_quit.pack(side="right")

        # Zones d'affichage
        self.lbl_bms = self._setup_section("ÉTAT BATTERIES (TEMPS RÉEL)", "#00d4ff")
        self.lbl_msg = self._setup_section("MESSAGES SYSTÈME", "#ffcc00")
        self.lbl_other = self._setup_section("IA & NAVIGATION", "#00ff88")

    def _setup_section(self, title, color):
        frame = tk.LabelFrame(self.root, text=title, bg="#101010", fg=color, font=self.title_font, bd=2)
        frame.pack(fill="both", expand=True, padx=15, pady=8)
        label = tk.Label(frame, text="Attente de données...", bg="#101010", fg="white", 
                         font=self.data_font, justify="left", anchor="nw")
        label.pack(fill="both", expand=True, padx=15, pady=10)
        return label

    def update_bms(self, bms1, bms2):
        """
        Affiche V, A, SOC + Températures Max/Min
        """
        try:
            lines = []
            for data in [bms1, bms2]:
                bid = data.get('bms_id', '??')
                v = data.get('voltage', 0)
                i = data.get('current_a', 0)
                soc = data.get('soc', 0)
                
                # Extraction des températures depuis la liste 'temps'
                temp_list = data.get('temps', [])
                if temp_list:
                    t_max = max(temp_list)
                    t_min = min(temp_list)
                    temp_str = f"T: {t_min}°C > {t_max}°C"
                else:
                    temp_str = "T: N/A"
                
                # Formatage sur deux lignes par BMS pour que ce soit aéré
                line = (f"[{bid}] {v:>5}V | {i:>6}A | SOC: {soc:>5}%\n"
                        f"      {temp_str}")
                lines.append(line)

            full_text = "\n\n".join(lines)
            self.lbl_bms.config(text=full_text)
            
            # --- ALERTES COULEURS ---
            # Rouge si SOC < 15% OU si une cellule dépasse 45°C
            critical_temp = any(max(d.get('temps', [0])) > 45 for d in [bms1, bms2])
            critical_soc = any(d.get('soc', 100) < 15 for d in [bms1, bms2])
            
            if critical_temp or critical_soc:
                self.lbl_bms.config(fg="#ff4444") # Rouge alerte
            else:
                self.lbl_bms.config(fg="white")

        except Exception as e:
            self.lbl_bms.config(text=f"Erreur d'affichage : {e}")
        
        self.root.update()

    def update_messages(self, text):
        self.lbl_msg.config(text=str(text))
        self.root.update()

    def update_other(self, text):
        self.lbl_other.config(text=str(text))
        self.root.update()

interface = InterfaceBateau()