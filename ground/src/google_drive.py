import os
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload
from google.oauth2.credentials import Credentials
from google.auth.transport.requests import Request

_SRC_DIR    = os.path.dirname(os.path.abspath(__file__))
_ROOT_DIR   = os.path.dirname(_SRC_DIR)
CLIENT_FILE = os.path.join(_ROOT_DIR, "credentials", "client.json")
TOKEN_FILE  = os.path.join(_ROOT_DIR, "credentials", "token.json")
FOLDER_ID   = "1I4q2WttF_B5utXc-vIzHyF_Qjiv_AMlk"
SCOPES      = ["https://www.googleapis.com/auth/drive.file"]


def _get_service():
    if not os.path.exists(TOKEN_FILE):
        raise RuntimeError(
            "credentials/token.json introuvable ÔÇö "
            "lancez 'docker compose --profile auth run --service-ports auth' pour vous authentifier"
        )

    creds = Credentials.from_authorized_user_file(TOKEN_FILE, SCOPES)

    if not creds.valid:
        if creds.expired and creds.refresh_token:
            creds.refresh(Request())
            with open(TOKEN_FILE, "w") as f:
                f.write(creds.to_json())
        else:
            raise RuntimeError(
                "Token Google expir├® et non renouvelable ÔÇö "
                "relancez l'authentification avec 'docker compose --profile auth run --service-ports auth'"
            )

    return build("drive", "v3", credentials=creds)


def upload_file(local_path: str):
    """Upload ou met ├á jour un fichier CSV sur Drive."""
    filename = os.path.basename(local_path)

    try:
        service = _get_service()

        response = service.files().list(
            q=f"name='{filename}' and '{FOLDER_ID}' in parents and trashed=false",
            fields="files(id, name)",
        ).execute()

        existing = response.get("files", [])
        media    = MediaFileUpload(local_path, mimetype="text/csv", resumable=True)

        if existing:
            file_id = existing[0]["id"]
            service.files().update(
                fileId=file_id,
                media_body=media,
            ).execute()
            print(f"[drive] Mis ├á jour : {filename}")
        else:
            metadata = {
                "name":    filename,
                "parents": [FOLDER_ID],
            }
            service.files().create(
                body=metadata,
                media_body=media,
                fields="id",
            ).execute()
            print(f"[drive] Cr├®├® : {filename}")

    except Exception as e:
        print(f"[drive] Erreur upload : {e}")
