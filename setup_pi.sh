#!/bin/bash
# Script d'installation automatique pour le Raspberry Pi (Monaco 2026)

echo "=== DEBUT DE L'INSTALLATION MEBC ==="

# 1. Detection automatique de l'utilisateur et du dossier
# $SUDO_USER recupere le vrai utilisateur (ex: edouard) meme si lance avec sudo
USER_NAME=${SUDO_USER:-$USER}
PROJECT_DIR=$(pwd)

echo "Utilisateur detecte : $USER_NAME"
echo "Dossier du projet : $PROJECT_DIR"

# 2. Installation des dependances systeme (Interface graphique et ports serie)
echo "--- Installation des paquets systeme via apt-get ---"
sudo apt-get update
# On installe les librairies Python via apt pour eviter les problemes PEP 668 sur le Pi
sudo apt-get install -y python3-tk python3-serial python3-paho-mqtt

# 3. Droits d'acces au port serie (essentiel sous Linux)
echo "--- Configuration des droits serie ---"
sudo usermod -a -G dialout $USER_NAME

# 4. Configuration du lancement automatique (Systemd)
echo "--- Configuration du service de demarrage automatique ---"

cat <<EOF | sudo tee /etc/systemd/system/mebc.service > /dev/null
[Unit]
Description=Monaco Energy Boat Challenge - Telemetrie
After=network.target
Wants=graphical.target

[Service]
User=$USER_NAME
WorkingDirectory=$PROJECT_DIR
ExecStart=/usr/bin/python3 $PROJECT_DIR/main.py
Restart=always
RestartSec=5
# Assigne l'affichage a l'ecran principal pour Tkinter
Environment=DISPLAY=:0.0

[Install]
WantedBy=graphical.target
EOF

# 5. Activation du service
echo "--- Activation du service ---"
sudo systemctl daemon-reload
sudo systemctl enable mebc.service
sudo systemctl restart mebc.service

echo "=== INSTALLATION TERMINEE ==="
echo "Le programme tourne desormais en arriere-plan et se lancera au demarrage."
echo "Pour voir les logs : journalctl -u mebc.service -f"
echo "Pour l'arreter   : sudo systemctl stop mebc.service"
