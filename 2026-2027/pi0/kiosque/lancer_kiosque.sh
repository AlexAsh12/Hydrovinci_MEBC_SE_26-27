#!/bin/bash
# Affiche le tableau de bord en plein ecran sur le HDMI.
# Lance automatiquement a la connexion sur tty1 (voir installer.sh).
# cage = compositeur Wayland minimal qui n'affiche qu'une seule application.

URL="http://127.0.0.1:8080/?kiosque"

# Attendre le serveur du tableau de bord (30 s max)
for _ in $(seq 60); do
    (exec 3<>/dev/tcp/127.0.0.1/8080) 2>/dev/null && break
    sleep 0.5
done

NAVIGATEUR=$(command -v chromium || command -v chromium-browser)
if [ -z "$NAVIGATEUR" ]; then
    echo "Chromium introuvable : relancer installer.sh" >&2
    exit 1
fi

# Profil jetable : rien n'est ecrit sur la carte SD d'un demarrage a l'autre
PROFIL=$(mktemp -d /tmp/kiosque.XXXXXX)
trap 'rm -rf "$PROFIL"' EXIT

cage -d -- "$NAVIGATEUR" \
    --kiosk "$URL" \
    --user-data-dir="$PROFIL" \
    --ozone-platform=wayland \
    --noerrdialogs \
    --disable-infobars \
    --no-first-run \
    --disable-translate \
    --disable-features=Translate,MediaRouter,OptimizationHints \
    --disable-sync \
    --disable-extensions \
    --disable-background-networking \
    --disable-component-update \
    --disable-default-apps \
    --disable-pinch \
    --overscroll-history-navigation=0 \
    --password-store=basic \
    --check-for-update-interval=31536000 \
    --renderer-process-limit=1
