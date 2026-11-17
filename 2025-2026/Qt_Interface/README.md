# Interface Qt - Monitoring MQTT pour MEBC

## 📋 Description

Interface graphique développée en Qt6 pour le monitoring en temps réel des capteurs du bateau via MQTT. L'interface affiche :
- Les températures de 5 capteurs (Avant, Arrière, Moteur, Extérieur, Intérieur)
- Un graphique en temps réel des valeurs
- Des statistiques (min, max, moyenne)
- Un journal des événements avec alertes
- Des commandes pour contrôler le système

---

## 🔧 Prérequis

- **Système d'exploitation** : Windows 10/11, Linux, ou macOS
- **Espace disque** : ~5 GB pour Qt
- **Connexion internet** : Pour télécharger Qt et les dépendances

---

## 📥 Installation de Qt

### Étape 1 : Télécharger Qt

1. Va sur le site officiel : [https://www.qt.io/download-open-source](https://www.qt.io/download-open-source)
2. Clique sur "Download the Qt Online Installer"
3. Télécharge l'installeur correspondant à ton OS :
   - **Windows** : `qt-online-installer-windows-x64.exe`
   - **Linux** : `qt-online-installer-linux-x64.run`
   - **macOS** : `qt-online-installer-macos.dmg`

### Étape 2 : Installer Qt

#### Sur Windows :

1. Lance l'installeur `qt-online-installer-windows-x64.exe`
2. Crée un compte Qt (gratuit) ou connecte-toi
3. Accepte les conditions d'utilisation
4. **Choix des composants** (IMPORTANT) :
   - Sélectionne **Qt 6.8.2** (ou version supérieure)
   - Coche les cases suivantes :
     - ✅ **MinGW 13.1.0 64-bit** (compilateur)
     - ✅ **Qt Charts**
     - ✅ **Qt MQTT**
     - ✅ **CMake** (si pas déjà installé)
     - ✅ **Ninja** (si pas déjà installé)
5. Chemin d'installation recommandé : `C:\Qt\` (par défaut)
6. Lance l'installation (peut prendre 20-30 minutes)

#### Sur Linux (Ubuntu/Debian) :

```bash
# Rendre l'installeur exécutable
chmod +x qt-online-installer-linux-x64.run

# Lancer l'installeur
./qt-online-installer-linux-x64.run

# Suivre les mêmes étapes que Windows pour la sélection des composants
```

**Installation des dépendances système** :
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build
sudo apt install libgl1-mesa-dev libxkbcommon-x11-0
```

#### Sur macOS :

```bash
# Ouvrir le .dmg et suivre les instructions
# Installer Xcode Command Line Tools si nécessaire
xcode-select --install
```

### Étape 3 : Configurer les variables d'environnement (Windows)

1. Ouvre les **Variables d'environnement système**
2. Ajoute au **PATH** :
   ```
   C:\Qt\6.8.2\mingw_64\bin
   C:\Qt\Tools\mingw1310_64\bin
   C:\Qt\Tools\CMake_64\bin
   C:\Qt\Tools\Ninja
   ```

---

## 🚀 Installation et compilation du projet

### Étape 1 : Cloner le repository

```bash
# Navigue dans ton dossier de travail
cd /chemin/vers/ton/dossier

# Clone le repository
git clone https://github.com/Gusleboss01/MEBC_SE.git

# Entre dans le dossier
cd MEBC_SE/Qt_Interface
```

### Étape 2 : Vérifier la configuration

Ouvre le fichier `CMakeLists.txt` et vérifie le chemin de Qt :

```cmake
# Ligne à vérifier/modifier selon ton installation
set(CMAKE_PREFIX_PATH "C:/Qt/6.8.2/mingw_64/lib/cmake")
```

**Exemples de chemins selon l'OS** :
- Windows : `C:/Qt/6.8.2/mingw_64/lib/cmake`
- Linux : `/home/user/Qt/6.8.2/gcc_64/lib/cmake`
- macOS : `/Users/user/Qt/6.8.2/macos/lib/cmake`

### Étape 3 : Compiler le projet

#### Méthode 1 : Avec Qt Creator (recommandé pour débuter)

1. Lance **Qt Creator**
2. `File` → `Open File or Project`
3. Sélectionne `CMakeLists.txt` dans le dossier `Qt_Interface`
4. Choisis le **kit MinGW 64-bit** (ou gcc sur Linux)
5. Clique sur `Configure Project`
6. Clique sur le bouton ▶️ **Run** (ou appuie sur `Ctrl+R`)

#### Méthode 2 : En ligne de commande

**Sur Windows (Git Bash ou PowerShell)** :
```bash
# Crée un dossier de build
mkdir build
cd build

# Configure le projet avec CMake
cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/Qt/6.8.2/mingw_64"

# Compile
cmake --build .

# Lance l'application
./Rpi_Interface.exe
```

**Sur Linux/macOS** :
```bash
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH="/chemin/vers/Qt/6.8.2/gcc_64"
cmake --build .
./Rpi_Interface
```

---

## 🔌 Configuration MQTT

### Étape 1 : Configurer le broker MQTT

Dans le fichier `mainwindow.cpp`, modifie les paramètres de connexion :

```cpp
mqttClient->setHostname("ADRESSE_IP_RASPBERRY");  // Ex: "192.168.1.100"
mqttClient->setPort(1883);                         // Port MQTT par défaut
```

### Étape 2 : Vérifier les topics MQTT

Les topics utilisés par l'interface :
- **Abonnement** : `capteurs/temperatures` (réception des données)
- **Publication** : `commande/control` (envoi des commandes)
- **Réponse** : `commande/reponse` (réception des confirmations)

---

## 📊 Utilisation de l'interface

### Fonctionnalités

1. **Connexion automatique** au démarrage
2. **Graphique temps réel** : affichage des 5 capteurs
3. **Statistiques** : min, max, moyenne pour chaque capteur
4. **Alertes** :
   - ⚠️ Orange : Température > 28°C
   - 🚨 Rouge : Messages d'urgence (MAYDAY, PANPAN)
5. **Commandes** :
   - `START` : Démarrer l'acquisition
   - `STOP` : Arrêter l'acquisition
   - Commandes personnalisées via le champ de texte

### Journal des événements

Les messages sont classés par niveau :
- **Noir** : Information normale
- **Orange** : Avertissement (température élevée)
- **Rouge** : Alerte critique (urgence)

---

## 🛠️ Dépannage

### Erreur : "Qt6 not found"

**Solution** :
1. Vérifie que Qt est bien installé
2. Vérifie le chemin dans `CMakeLists.txt`
3. Utilise le chemin absolu complet

### Erreur : "Cannot find -lQt6::Mqtt"

**Solution** :
```bash
# Réinstalle Qt avec le module MQTT
# Dans Qt Maintenance Tool :
# Ajoute "Qt MQTT" dans les composants Qt 6.8.2
```

### L'application ne se connecte pas au broker MQTT

**Solution** :
1. Vérifie que le Raspberry Pi est allumé et accessible
2. Teste la connexion réseau : `ping ADRESSE_IP_RASPBERRY`
3. Vérifie que le broker Mosquitto tourne sur le Raspberry
4. Vérifie l'adresse IP dans `mainwindow.cpp`

### Erreur de compilation avec MinGW

**Solution** :
```bash
# Nettoie le dossier build
rm -rf build
mkdir build
cd build

# Recompile
cmake .. -G "MinGW Makefiles"
cmake --build .
```

---

## 📁 Structure du projet

```
Qt_Interface/
├── main.cpp              # Point d'entrée de l'application
├── mainwindow.h          # Déclarations de la classe MainWindow
├── mainwindow.cpp        # Implémentation de l'interface
├── mainwindow.ui         # Fichier UI (généré automatiquement)
├── CMakeLists.txt        # Configuration CMake
└── README.md             # Ce fichier
```

---

## 🔄 Mise à jour du projet

```bash
# Récupère les dernières modifications
git pull origin main

# Recompile si nécessaire
cd build
cmake --build .
```

---

## 📝 Pour contribuer

1. Crée une branche pour tes modifications :
   ```bash
   git checkout -b feature/amelioration-interface
   ```

2. Fais tes modifications

3. Commit et push :
   ```bash
   git add .
   git commit -m "Description des modifications"
   git push origin feature/amelioration-interface
   ```

4. Crée une Pull Request sur GitHub

---

## 📞 Support

Pour toute question ou problème :
- Ouvre une **Issue** sur GitHub
- Contacte l'équipe SE du Monaco Energy Boat Challenge

---

## 📚 Ressources utiles

- [Documentation Qt6](https://doc.qt.io/qt-6/)
- [Qt MQTT](https://doc.qt.io/qt-6/qtmqtt-index.html)
- [Qt Charts](https://doc.qt.io/qt-6/qtcharts-index.html)
- [CMake Tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/index.html)
- [MQTT Protocol](https://mqtt.org/)

---

**Version** : 1.0  
**Dernière mise à jour** : Janvier 2026  
**Auteurs** : Équipe SE - MEBC 2026
