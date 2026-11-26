#!/bin/bash
# Affiche le tableau de bord en plein ecran sur le HDMI.
# Lance automatiquement a la connexion sur tty1 (voir installer.sh), a la
# place du shell : quand ce script se termine, la session se ferme et la
# connexion automatique le relance. Aucun terminal n'est jamais laisse ouvert
# sur l'ecran du bateau.
# cage = compositeur Wayland minimal qui n'affiche qu'une seule application.

# Un clavier branche ne doit pas pouvoir interrompre le script
trap '' INT QUIT TSTP

URL="http://127.0.0.1:8080/?kiosque"

# Sans ecran, cage et Chromium tournent a vide et prennent environ un coeur
# entier et 150 Mo : on attend qu'un ecran HDMI soit branche. Un ecran qui
# ne se signale pas (pas de detection HDMI) se force dans cmdline.txt avec
# video=HDMI-A-1:D (voir README).
ecran_branche() {
    grep -qx connected /sys/class/drm/card*-HDMI-A-*/status 2>/dev/null
}
if ! ecran_branche; then
    echo "En attente d'un ecran HDMI..."
    until ecran_branche; do sleep 3; done
fi

# Attendre le serveur du tableau de bord (30 s max)
for _ in $(seq 60); do
    (exec 3<>/dev/tcp/127.0.0.1/8080) 2>/dev/null && break
    sleep 0.5
done

NAVIGATEUR=$(command -v chromium || command -v chromium-browser)
if [ -z "$NAVIGATEUR" ]; then
    echo "Chromium introuvable : relancer installer.sh" >&2
    sleep 10
    exit 1
fi

# Profil jetable en memoire vive (/run/user/...) : rien n'est ecrit sur la carte SD
PROFIL=$(mktemp -d "${XDG_RUNTIME_DIR:-/tmp}/kiosque.XXXXXX")
trap 'rm -rf "$PROFIL"' EXIT

cage -d -- "$NAVIGATEUR" \
    --kiosk "$URL" \
    --user-data-dir="$PROFIL" \
    --ozone-platform=wayland \
    --noerrdialogs \
    --disable-infobars \
    --no-first-run \
    --lang=fr \
    --accept-lang=fr-FR,fr \
    --disable-translate \
    --disable-features=Translate,TranslateUI,MediaRouter,OptimizationHints \
    --disable-sync \
    --disable-extensions \
    --disable-background-networking \
    --disable-component-update \
    --disable-default-apps \
    --disable-pinch \
    --overscroll-history-navigation=0 \
    --password-store=basic \
    --check-for-update-interval=31536000 \
    --renderer-process-limit=1 &
CAGE=$!

# Ecran debranche plus de 6 s : on arrete l'affichage (la connexion
# automatique relance ce script, qui attend le prochain ecran)
while kill -0 "$CAGE" 2>/dev/null; do
    sleep 3
    if ! ecran_branche; then
        sleep 3
        ecran_branche || kill "$CAGE" 2>/dev/null
    fi
done
wait "$CAGE" 2>/dev/null

# Navigateur arrete : on laisse souffler puis la connexion automatique relance tout
sleep 2
