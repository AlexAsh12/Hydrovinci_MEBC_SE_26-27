# MEBC_SE



## Structure des fichiers

### 1. `main.py`
- **Rôle : Programme principal.
- **Fonctions principales :**
    - Démarre la lecture des capteurs de température.
    - Lance la réception des commandes MQTT.
    - Gère l'envoi des données et la gestion des boutons physiques.

### 2. `config.py`
- **Rôle :** Définit les constantes et paramètres de configuration du système.
- **Contenu :**
    - Seuil de température maximale autorisée.
    - URL du serveur API.
    - Clé API pour sécuriser les requêtes.

### 3. `captation.py`
- **Rôle :** Gère la lecture des capteurs de température DS18B20.
- **Fonctions principales :**
    - Détecte les capteurs connectés.
    - Lit les températures et détecte les anomalies.
    - Génère des alertes si une température dépasse un seuil défini.

### 4. `transmission.py`
- **Rôle :** Gère l'envoi des données recues des capteurs via MQTT vers le PC distant.
- **Fonctions principales :**
    - Envoi des données de température au serveur MQTT.
    - Sauvegarde locale des données en cas de coupure réseau.
    - Réémission des données sauvegardées une fois la connexion rétablie.
- Pour lire les données cf application 

### 5. `reception.py`
- **Rôle :** Écoute les messages du serveur MQTT envoyées par le pc distant sur le RPI.
- **Fonctions principales :**
    - Abonnement à un topic MQTT.
    - Réception et affichage des commandes reçues.

### 6. `interface_mqtt.py`
- **Rôle :** Interface de communication en ligne de commande pour envoyer des messages via MQTT au RPI. !!! N'est pas utilisé par le main.
- **Fonctions principales :**
    - Permet au pc distant d'envoyer des messages MQTT manuellement.
- Pour executer : aller vers le chemin du fichier et faire python interface_mqtt.py

### 7. `message.py`
- **Rôle :** Gère les boutons physiques connectés au Raspberry Pi et envoie message au pc distant .
- **Fonctions principales :**
    - Détecte l'appui sur les boutons GPIO.
    - Envoie des messages MQTT correspondant à l'appui sur un bouton.
    - Configure les interruptions GPIO pour la gestion des événements.
- Pour ecouter sur le pC distant : mosquitto_sub -h test.mosquitto.org -t "commande/reponse"

---

