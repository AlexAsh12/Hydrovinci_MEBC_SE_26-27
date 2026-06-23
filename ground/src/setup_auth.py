"""
Authentification Google Drive — à lancer une seule fois par machine.
Voir README.md pour les instructions complètes.
"""
import os
import sys
from google_auth_oauthlib.flow import InstalledAppFlow

_SRC_DIR    = os.path.dirname(os.path.abspath(__file__))
_ROOT_DIR   = os.path.dirname(_SRC_DIR)
CLIENT_FILE = os.path.join(_ROOT_DIR, "credentials", "client.json")
TOKEN_FILE  = os.path.join(_ROOT_DIR, "credentials", "token.json")
SCOPES      = ["https://www.googleapis.com/auth/drive.file"]


def main():
    if not os.path.exists(CLIENT_FILE):
        print("✗  credentials/client.json introuvable.")
        print("   Demandez le fichier client.json à Raphaël et placez-le dans le dossier credentials/")
        sys.exit(1)

    print("=" * 60)
    print("  Authentification Google Drive — MEBC Ground Dashboard")
    print("=" * 60)
    print()
    print("1. Un lien va s'afficher ci-dessous.")
    print("2. Copiez-le et ouvrez-le dans votre navigateur.")
    print("3. Connectez-vous avec votre compte Google et autorisez l'accès.")
    print("4. Votre navigateur sera redirigé vers http://localhost:8080")
    print("   → la fenêtre se fermera automatiquement, c'est normal.")
    print()

    flow = InstalledAppFlow.from_client_secrets_file(CLIENT_FILE, SCOPES)

    # host='0.0.0.0' est obligatoire en Docker pour que le port-forwarding
    # fonctionne (le navigateur de l'hôte doit atteindre le serveur du conteneur)
    creds = flow.run_local_server(host="0.0.0.0", port=8080, open_browser=False)

    os.makedirs(os.path.dirname(TOKEN_FILE), exist_ok=True)
    with open(TOKEN_FILE, "w") as f:
        f.write(creds.to_json())

    print()
    print("✓  Authentification réussie ! token.json sauvegardé dans credentials/")
    print()
    print("Vous pouvez maintenant lancer le dashboard :")
    print("   docker compose up -d")
    print()


if __name__ == "__main__":
    main()
