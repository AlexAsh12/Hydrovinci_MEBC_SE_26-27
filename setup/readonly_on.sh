#!/bin/bash
# ============================================================================
#  readonly_on.sh  -  Monaco Energy Boat Challenge 2026
# ----------------------------------------------------------------------------
#  ACTIVE le mode lecture seule (overlay filesystem) du Raspberry Pi.
#
#  Effet : plus AUCUNE ecriture sur la carte SD. Tout (journal, swap, fichiers
#  temporaires) part en RAM et est perdu au reboot. La carte ne peut donc plus
#  etre corrompue par une coupure de courant brutale.
#
#  A lancer AVANT la course, une fois que le code ne bouge plus.
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

# Convention raspi-config nonint : 0 = activer l'overlay
raspi-config nonint do_overlayfs 0

echo "Mode LECTURE SEULE configure (carte SD protegee)."
echo "Redemarre pour appliquer :  sudo reboot"
