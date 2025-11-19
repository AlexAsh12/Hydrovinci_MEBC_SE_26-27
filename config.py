# Liste des capteurs : id unique -> nom
CAPTEURS = {
    "28-5da6491f64ff": "CapteurBatterie1",
    "28-9c85491f64ff": "CapteurBatterie2",
    "28-1f8b491f64ff": "CapteurBatterieSE",
    "28-31a6491f64ff": "CapteurMoteur",
    "28-9ba6491f64ff": "CapteurSE"
}

# Seuils critiques par capteur (°C)
SEUILS_CRITIQUES = {
    "CapteurBatterie1": 40,
    "CapteurBatterie2": 38,
    "CapteurBatterieSE": 90,
    "CapteurMoteur": 50,
    "CapteurSE": 35,
    # pour capteurs non nommés : valeur par défaut
    "default": 40
}

API_KEY = ""  # ou une vraie clé si besoin
BACKUP_FILE = "dernier_envoi.json"

# GPIO des boutons physiques
BUTTON_OK = 3
BUTTON_NOK = 27
BUTTON_MAYDAY = 22
BUTTON_PANPAN = 5

# GPIO pour la LED
LED_PIN = 19
LED_ALERT_PIN = 26

# Variables de statut global
message_recu = False
alerte_temperature = False
