#!/bin/bash
# ============================================================================
#  readonly_off.sh  -  Monaco Energy Boat Challenge 2026
# ----------------------------------------------------------------------------
#  DESACTIVE le mode lecture seule : retour en lecture/ecriture.
#
#  A lancer quand tu veux modifier le code sur la Pi (editer un fichier,
#  faire un git pull, relancer setup_pi.sh, etc.).
#  Un REDEMARRAGE est necessaire pour appliquer.
# ============================================================================

if [ "$(id -u)" -ne 0 ]; then
    echo "Lance ce script avec sudo : sudo bash $0"
    exit 1
fi

if ! command -v raspi-config >/dev/null 2>&1; then
    echo "Erreur : raspi-config introuvable (es-tu sur un Raspberry Pi OS ?)"
    exit 1
fi

# Convention raspi-config nonint : 1 = desactiver l'overlay
raspi-config nonint do_overlayfs 1

echo "Mode LECTURE/ECRITURE configure (carte SD modifiable)."
echo "Redemarre pour appliquer :  sudo reboot"
