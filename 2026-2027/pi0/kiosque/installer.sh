#!/bin/bash
# Installe tout ce qu'il faut sur la Pi (Raspberry Pi OS Lite) :
#   - UART materiel pour l'ESP32 (sans console serie, Bluetooth coupe)
#   - compile tableau_bord et le lance au demarrage (service systemd)
#   - affiche la page en plein ecran sur le HDMI des la mise sous tension
#     (connexion automatique sur tty1 + cage + Chromium)
#
# Usage, depuis le depot :
#   cd ~/Hydrovinci_MEBC_SE_26-27/2026-2027/pi0
#   sudo ./kiosque/installer.sh
# Peut etre relance sans risque (met a jour l'installation).
set -euo pipefail

if [ "$(id -u)" -ne 0 ] || [ -z "${SUDO_USER:-}" ] || [ "$SUDO_USER" = root ]; then
    echo "A lancer avec sudo depuis ton utilisateur : sudo ./kiosque/installer.sh" >&2
    exit 1
fi

UTILISATEUR="$SUDO_USER"
MAISON=$(getent passwd "$UTILISATEUR" | cut -d: -f6)
PI0=$(cd "$(dirname "$0")/.." && pwd)
KIOSQUE="$PI0/kiosque"

echo "== UART pour l'ESP32"
raspi-config nonint do_serial_cons 1   # pas de console Linux sur le port serie
raspi-config nonint do_serial_hw 0     # port serie materiel active
CONFIG=/boot/firmware/config.txt
[ -f "$CONFIG" ] || CONFIG=/boot/config.txt
if ! grep -q "^dtoverlay=disable-bt" "$CONFIG"; then
    # Le Bluetooth rend l'UART PL011 (ttyAMA0) aux broches 8 / 10
    printf '\n[all]\ndtoverlay=disable-bt\n' >> "$CONFIG"
fi
systemctl disable hciuart.service 2>/dev/null || true

echo "== Paquets"
apt-get update
if apt-cache show chromium >/dev/null 2>&1; then CHROMIUM=chromium; else CHROMIUM=chromium-browser; fi
apt-get install -y --no-install-recommends build-essential cage "$CHROMIUM" fonts-dejavu-core

echo "== Reglages imposes a Chromium (kiosque)"
# Politique d'entreprise : prioritaire sur tout le reste, aucun bandeau ne
# peut reapparaitre (traduction, mots de passe, plantage precedent...)
mkdir -p /etc/chromium/policies/managed
cat > /etc/chromium/policies/managed/kiosque-hydrovinci.json <<'EOF'
{
  "TranslateEnabled": false,
  "PasswordManagerEnabled": false,
  "AutofillAddressEnabled": false,
  "AutofillCreditCardEnabled": false,
  "BrowserSignin": 0,
  "MetricsReportingEnabled": false,
  "SpellcheckEnabled": false
}
EOF

echo "== Compilation"
sudo -u "$UTILISATEUR" make -C "$PI0" tableau_bord test_protocole
sudo -u "$UTILISATEUR" "$PI0/test_protocole" >/dev/null

echo "== Service tableau-bord"
sed -e "s|@UTILISATEUR@|$UTILISATEUR|g" -e "s|@PI0@|$PI0|g" \
    "$KIOSQUE/tableau-bord.service" > /etc/systemd/system/tableau-bord.service
for groupe in dialout video render input; do
    if getent group "$groupe" >/dev/null; then usermod -a -G "$groupe" "$UTILISATEUR"; fi
done
systemctl daemon-reload
systemctl enable tableau-bord.service
systemctl restart tableau-bord.service

echo "== Affichage au demarrage (connexion auto sur tty1)"
chmod +x "$KIOSQUE/lancer_kiosque.sh"
raspi-config nonint do_boot_behaviour B2
PROFILE="$MAISON/.bash_profile"
touch "$PROFILE"
chown "$UTILISATEUR": "$PROFILE"
# On remplace le bloc s'il existe deja
sed -i '/# >>> kiosque hydrovinci >>>/,/# <<< kiosque hydrovinci <<</d' "$PROFILE"
cat >> "$PROFILE" <<EOF
# >>> kiosque hydrovinci >>>
# bash ignore ~/.profile des que ~/.bash_profile existe : on le recharge
if [ -f "\$HOME/.profile" ]; then . "\$HOME/.profile"; fi
# Sur l'ecran HDMI (tty1) uniquement, pas en SSH. exec : le script remplace
# le shell, aucun terminal ne reste ouvert sur l'ecran.
if [ -z "\${WAYLAND_DISPLAY:-}" ] && [ "\$(tty)" = /dev/tty1 ]; then
    exec "$KIOSQUE/lancer_kiosque.sh"
fi
# <<< kiosque hydrovinci <<<
EOF

if ! grep -q "^dtoverlay=vc4-kms-v3d" "$CONFIG"; then
    echo "ATTENTION : dtoverlay=vc4-kms-v3d absent de $CONFIG (pilote graphique)"
fi

echo
echo "Termine. Redemarrer pour lancer l'affichage : sudo reboot"
echo "Etat du serveur : systemctl status tableau-bord   |   journal : journalctl -u tableau-bord -f"
