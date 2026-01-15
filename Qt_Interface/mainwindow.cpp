#include "mainwindow.h"
#include <QHBoxLayout>
#include <QFont>
#include <QDebug>
#include <QSplitter>
#include <QScrollBar>
#include <QMessageBox>
#include <QGridLayout>
#include <QTableWidget>
#include <QHeaderView>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), mqttClient(new QMqttClient(this)) {

    setupUI();

    // ✅ Set an appropriate initial window size
    resize(1200, 850);  // Adjust width and height as needed
    setMinimumSize(800, 600);  // Prevents the window from being too small
    setWindowTitle("Moniteur de Température Raspberry Pi");

    // Configuration MQTT
    mqttClient->setHostname("test.mosquitto.org");
    mqttClient->setPort(1883);

    // Générer un ID client unique
    QString clientId = "RpiTempMonitor_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    mqttClient->setClientId(clientId);

    connect(mqttClient, &QMqttClient::connected, this, &MainWindow::onConnected);
    connect(mqttClient, &QMqttClient::disconnected, this, &MainWindow::onDisconnected);
    connect(mqttClient, &QMqttClient::messageReceived, this, &MainWindow::onMessageReceived);

    // Timer pour mise à jour du temps écoulé
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::updateElapsedTime);
    updateTimer->start(1000);  // Mise à jour chaque seconde

    // Connecter les événements de commande
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendCommand);
    connect(commandLineEdit, &QLineEdit::returnPressed, this, &MainWindow::sendCommand);
    connect(startButton, &QPushButton::clicked, this, [this](){ sendQuickCommand("START"); });
    connect(stopButton, &QPushButton::clicked, this, [this](){ sendQuickCommand("STOP"); });

    logMessage("Application démarrée. Connexion au broker MQTT...");
    mqttClient->connectToHost();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI() {
    // ✅ Utiliser directement setCentralWidget pour notre widget principal
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    // ✅ Créer un layout principal sans marges pour éliminer l'espace vide
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // ✅ Statut de connexion - plus compact
    statusLabel = new QLabel("⚫ Déconnecté");
    statusLabel->setStyleSheet("color: red; font-weight: bold;");
    statusLabel->setMaximumHeight(30);
    mainLayout->addWidget(statusLabel);

    // ✅ Splitter pour diviser l'écran - occupera tout l'espace disponible
    QSplitter *splitter = new QSplitter(Qt::Horizontal);
    mainLayout->addWidget(splitter, 1); // Le 1 est important: donne tout l'espace disponible

    // ===== Partie gauche - Surveillance des températures =====
    QWidget *leftWidget = new QWidget();
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    // ✅ Dernière lecture avec tous les capteurs
    QGroupBox *readingBox = new QGroupBox("📡 Dernière lecture");
    QVBoxLayout *readingLayout = new QVBoxLayout(readingBox);
    lastUpdateLabel = new QLabel("Dernière mise à jour : --");
    readingLayout->addWidget(lastUpdateLabel);

    // Initialiser les capteurs et leurs widgets
    setupSensorsUI(readingLayout);

    leftLayout->addWidget(readingBox);

    // ✅ Statistiques
    QGroupBox *statsBox = new QGroupBox("📊 Statistiques");
    QVBoxLayout *statsLayout = new QVBoxLayout(statsBox);

    // Utiliser une table pour un affichage plus compact des statistiques
    QTableWidget *statsTable = new QTableWidget(sensorNames.size(), 4);
    statsTable->setHorizontalHeaderLabels({"Capteur", "Min", "Moy", "Max"});
    statsTable->verticalHeader()->setVisible(false);
    statsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // Remplir le tableau avec les informations de chaque capteur
    for (int i = 0; i < sensorNames.size(); i++) {
        const QString &sensorName = sensorNames[i];

        // Colonne Capteur (nom)
        QTableWidgetItem *nameItem = new QTableWidgetItem(sensorName);
        statsTable->setItem(i, 0, nameItem);

        // Les statistiques seront mises à jour plus tard
        SensorStats &stats = sensors[sensorName];

        QLabel *minLabel = new QLabel("-- °C");
        QLabel *avgLabel = new QLabel("-- °C");
        QLabel *maxLabel = new QLabel("-- °C");

        stats.minLabel = minLabel;
        stats.avgLabel = avgLabel;
        stats.maxLabel = maxLabel;

        statsTable->setCellWidget(i, 1, minLabel);
        statsTable->setCellWidget(i, 2, avgLabel);
        statsTable->setCellWidget(i, 3, maxLabel);
    }

    statsLayout->addWidget(statsTable);
    leftLayout->addWidget(statsBox);

    // ✅ Graphique - utiliser stretch factor pour qu'il prenne plus d'espace
    chart = new QChart();
    axisX = new QValueAxis();
    axisY = new QValueAxis();

    // Amélioration du graphique
    chart->setTitle("Évolution des températures");
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);

    // Créer une série pour chaque capteur
    for (int i = 0; i < sensorNames.size(); i++) {
        const QString &sensorName = sensorNames[i];
        QLineSeries *series = new QLineSeries();
        series->setName(sensorName);

        // Attribuer une couleur unique à chaque série
        QPen pen(sensorColors[i]);
        pen.setWidth(2);
        series->setPen(pen);

        chart->addSeries(series);
        sensorSeries[sensorName] = series;
    }

    axisX->setTitleText("Temps");
    axisY->setTitleText("Température (°C)");
    axisY->setRange(10, 40);

    // Ligne horizontale pour le seuil d'alerte
    QLineSeries *thresholdLine = new QLineSeries();
    thresholdLine->setName("Seuil d'alerte");
    QPen thresholdPen(Qt::red);
    thresholdPen.setStyle(Qt::DashLine);
    thresholdPen.setWidth(1);
    thresholdLine->setPen(thresholdPen);
    thresholdLine->append(0, TEMPERATURE_MAX);
    thresholdLine->append(30, TEMPERATURE_MAX);
    chart->addSeries(thresholdLine);

    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);

    // Attacher les axes à toutes les séries
    for (QLineSeries *series : sensorSeries.values()) {
        series->attachAxis(axisX);
        series->attachAxis(axisY);
    }
    thresholdLine->attachAxis(axisX);
    thresholdLine->attachAxis(axisY);

    chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    leftLayout->addWidget(chartView, 1); // Important: stretch factor 1

    // ===== Partie droite - Commandes et journal =====
    QWidget *rightWidget = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    // ✅ Zone de commandes
    QGroupBox *commandGroup = new QGroupBox("🎮 Commandes");
    QVBoxLayout *commandLayout = new QVBoxLayout(commandGroup);

    commandLineEdit = new QLineEdit();
    commandLineEdit->setPlaceholderText("Saisir une commande...");
    commandLayout->addWidget(commandLineEdit);

    QHBoxLayout *buttonLayout = new QHBoxLayout();

    sendButton = new QPushButton("Envoyer");
    startButton = new QPushButton("Démarrer mesure");
    stopButton = new QPushButton("Arrêter mesure");

    buttonLayout->addWidget(sendButton);
    buttonLayout->addWidget(startButton);
    buttonLayout->addWidget(stopButton);

    commandLayout->addLayout(buttonLayout);
    rightLayout->addWidget(commandGroup);

    // ✅ Journal des événements - utiliser stretch factor pour qu'il prenne plus d'espace
    QGroupBox *logGroup = new QGroupBox("📝 Journal des événements");
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);

    logTextEdit = new QTextEdit();
    logTextEdit->setReadOnly(true);

    // Définir une police un peu plus grande pour le journal
    QFont logFont = logTextEdit->font();
    logFont.setPointSize(logFont.pointSize() + 1);
    logTextEdit->setFont(logFont);

    // ✅ Fond blanc pour le journal pour assurer une bonne visibilité
    logTextEdit->setStyleSheet("QTextEdit { background-color: white; color: black; }");

    logLayout->addWidget(logTextEdit);

    rightLayout->addWidget(logGroup, 1); // Important: stretch factor 1

    // Ajouter les widgets à gauche et à droite du splitter
    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);

    // Proportions du splitter
    splitter->setSizes({700, 500});
}

// Nouvelle méthode pour configurer l'interface des capteurs
void MainWindow::setupSensorsUI(QVBoxLayout *layout) {
    QGridLayout *sensorsLayout = new QGridLayout();
    sensorsLayout->setColumnStretch(1, 1); // Donner plus d'espace à la colonne des valeurs

    // Afficher tous les capteurs dans un GridLayout
    for (int i = 0; i < sensorNames.size(); i++) {
        const QString &sensorName = sensorNames[i];
        QLabel *nameLabel = new QLabel(sensorName + ":");
        QLabel *valueLabel = new QLabel("<b>-- °C</b>");

        // Stocker le label de valeur pour les mises à jour
        SensorStats &stats = sensors[sensorName];
        stats.valueLabel = valueLabel;

        // Appliquer la couleur correspondante au nom du capteur
        QString colorStyle = QString("color: %1;").arg(sensorColors[i].name());
        nameLabel->setStyleSheet("font-weight: bold; " + colorStyle);

        // Ajouter au layout (alternance entre 2 colonnes pour gagner de l'espace)
        int row = i / 2;        // Diviser par 2 pour déterminer la ligne
        int col = (i % 2) * 3;  // Multiplier par 3 pour laisser de l'espace entre les paires

        sensorsLayout->addWidget(nameLabel, row, col);
        sensorsLayout->addWidget(valueLabel, row, col + 1);
    }

    layout->addLayout(sensorsLayout);
}

void MainWindow::onConnected() {
    qDebug() << "Connecté au broker MQTT !";
    statusLabel->setText("🟢 Connecté");
    statusLabel->setStyleSheet("color: green; font-weight: bold;");

    // S'abonner aux topics
    mqttClient->subscribe(QMqttTopicFilter("capteur/temperature"));
    mqttClient->subscribe(QMqttTopicFilter(MQTT_TOPIC_RESPONSE));

    logMessage("Connecté au broker MQTT");
}

void MainWindow::onDisconnected() {
    statusLabel->setText("⚫ Déconnecté");
    statusLabel->setStyleSheet("color: red; font-weight: bold;");
    logMessage("Déconnecté du broker MQTT", WARNING);
}

void MainWindow::onMessageReceived(const QByteArray &message, const QMqttTopicName &topic) {
    qDebug() << "Message reçu sur" << topic.name() << ":" << message;

    // Vérifier si c'est un message d'urgence
    QString messageStr = QString::fromUtf8(message);
    if (checkEmergencyMessages(messageStr)) {
        showAlertDialog("MESSAGE D'URGENCE", "Message d'urgence reçu: " + messageStr);
        logMessage("⚠️ MESSAGE D'URGENCE: " + messageStr, CRITICAL);
        return;
    }

    if (topic.name() == "capteur/temperature") {
        QJsonDocument doc = QJsonDocument::fromJson(message);
        if (!doc.isObject()) return;

        QJsonObject json = doc.object();
        if (!json.contains("horodatage") || !json.contains("temperatures")) return;

        QString timestamp = json["horodatage"].toString();
        QJsonObject temperatureObjects = json["temperatures"].toObject();

        // Extraire toutes les températures dans une map
        QMap<QString, double> temperatures;
        bool tempAlert = false;

        // Traiter chaque capteur présent dans le message
        for (const QString &sensorName : sensorNames) {
            if (temperatureObjects.contains(sensorName)) {
                double temp = temperatureObjects[sensorName].toDouble();
                temperatures[sensorName] = temp;

                // Mise à jour de l'affichage avec code couleur
                QString style = "<b>%1 °C</b>";

                if (temp >= TEMPERATURE_MAX) {
                    style = "<b><font color='red'>%1 °C</font></b>";
                    logMessage("⚠️ ALERTE: " + sensorName + " au-dessus du seuil (" + QString::number(temp) + " °C)", CRITICAL);
                    tempAlert = true;
                }

                if (sensors.contains(sensorName) && sensors[sensorName].valueLabel) {
                    sensors[sensorName].valueLabel->setText(QString(style).arg(temp));
                }

                // Mettre à jour les statistiques du capteur
                SensorStats &stats = sensors[sensorName];

                if (stats.count == 0) {
                    stats.min = stats.max = temp;
                }

                stats.min = qMin(stats.min, temp);
                stats.max = qMax(stats.max, temp);
                stats.sum += temp;
                stats.count++;
            }
        }

        // Afficher une alerte en cas de dépassement de seuil
        if (tempAlert) {
            QString alertMessage = "Température critique détectée!\n\n";

            for (const QString &sensorName : temperatures.keys()) {
                double temp = temperatures[sensorName];
                if (temp >= TEMPERATURE_MAX) {
                    alertMessage += sensorName + ": " + QString::number(temp) + " °C\n";
                }
            }

            showAlertDialog("ALERTE TEMPÉRATURE", alertMessage);
        }

        lastUpdateLabel->setText("Dernière mise à jour : " + timestamp);
        lastUpdateTime = QDateTime::currentDateTime();

        updateStatistics();
        updateChart(temperatures);

        // Niveau d'alerte pour le message de log
        AlertLevel logLevel = INFO;
        for (double temp : temperatures.values()) {
            if (temp >= TEMPERATURE_MAX) {
                logLevel = CRITICAL;
                break;
            } else if (temp >= TEMPERATURE_MAX - 2) {
                logLevel = WARNING;
            }
        }

        // Construire un message de log avec toutes les températures
        QString tempStr;
        for (auto it = temperatures.constBegin(); it != temperatures.constEnd(); ++it) {
            if (!tempStr.isEmpty()) tempStr += ", ";
            tempStr += it.key() + "=" + QString::number(it.value()) + "°C";
        }

        logMessage("Mise à jour des températures: " + tempStr, logLevel);
    }
    else if (topic.name() == MQTT_TOPIC_RESPONSE) {
        QString response = QString::fromUtf8(message);
        onButtonResponseReceived(response);
    }
}

void MainWindow::onButtonResponseReceived(const QString &response) {
    // Vérifier si c'est un message d'urgence
    if (checkEmergencyMessages(response)) {
        showAlertDialog("MESSAGE D'URGENCE", "Message d'urgence reçu: " + response);
        logMessage("⚠️ MESSAGE D'URGENCE: " + response, CRITICAL);
    } else {
        logMessage("Réponse du bouton reçue: " + response);
    }
}

bool MainWindow::checkEmergencyMessages(const QString &message) {
    // Vérifier si le message contient un terme d'urgence
    QString messageUpper = message.toUpper().trimmed();
    for (const QString &term : emergencyMessages) {
        if (messageUpper.contains(term)) {
            return true;
        }
    }
    return false;
}

void MainWindow::showAlertDialog(const QString &title, const QString &message) {
    // Afficher une boîte de dialogue d'alerte non modale
    QMessageBox *alert = new QMessageBox(this);
    alert->setWindowTitle(title);
    alert->setText(message);
    alert->setIcon(QMessageBox::Critical);
    alert->setStandardButtons(QMessageBox::Ok);

    // Rendre la boîte de dialogue plus visible et attrayante
    QPalette palette = alert->palette();
    palette.setColor(QPalette::Window, QColor(255, 200, 200));
    palette.setColor(QPalette::WindowText, Qt::red);
    alert->setPalette(palette);

    // Style personnalisé pour l'alerte
    alert->setStyleSheet(
        "QMessageBox {"
        "  background-color: #FFEEEE;"
        "  border: 2px solid red;"
        "}"
        "QMessageBox QLabel {"
        "  color: red;"
        "  font-weight: bold;"
        "  font-size: 12pt;"
        "}"
        "QMessageBox QPushButton {"
        "  background-color: #FF5555;"
        "  color: white;"
        "  padding: 5px 15px;"
        "  border-radius: 3px;"
        "  font-weight: bold;"
        "}"
        );

    // Afficher en non-modal pour ne pas bloquer l'application
    alert->setModal(false);
    alert->show();

    // Se détruire automatiquement après la fermeture
    connect(alert, &QMessageBox::finished, alert, &QMessageBox::deleteLater);
}

void MainWindow::updateChart(const QMap<QString, double> &temperatures) {
    // Ajouter les données aux séries
    for (const QString &sensorName : temperatures.keys()) {
        double temperature = temperatures[sensorName];

        if (sensorSeries.contains(sensorName)) {
            sensorSeries[sensorName]->append(timeCounter, temperature);

            // Limiter à 30 points par série
            if (sensorSeries[sensorName]->count() > 30) {
                sensorSeries[sensorName]->remove(0);
            }
        }
    }

    timeCounter++;

    // Ajuster l'axe X
    axisX->setRange(qMax(0, timeCounter - 30), timeCounter);

    // Trouver les min/max pour l'axe Y
    double minY = 100.0;
    double maxY = 0.0;

    for (const SensorStats &stats : sensors) {
        if (stats.count > 0) {
            minY = qMin(minY, stats.min);
            maxY = qMax(maxY, stats.max);
        }
    }

    // Ajouter une marge
    minY = qMax(0.0, minY - 2);
    maxY = maxY + 2;

    // S'assurer que le seuil d'alerte est visible
    if (TEMPERATURE_MAX > minY && TEMPERATURE_MAX < maxY) {
        // Le seuil est déjà dans la plage
    } else if (TEMPERATURE_MAX >= maxY) {
        maxY = TEMPERATURE_MAX + 2;
    }

    axisY->setRange(minY, qMax(TEMPERATURE_MAX + 2, maxY));
}

void MainWindow::updateStatistics() {
    // Mettre à jour les statistiques pour chaque capteur
    for (auto it = sensors.begin(); it != sensors.end(); ++it) {
        const QString &sensorName = it.key();
        SensorStats &stats = it.value();

        if (stats.count > 0) {
            if (stats.minLabel) stats.minLabel->setText(QString::number(stats.min, 'f', 1) + " °C");
            if (stats.avgLabel) stats.avgLabel->setText(QString::number(stats.sum / stats.count, 'f', 1) + " °C");
            if (stats.maxLabel) stats.maxLabel->setText(QString::number(stats.max, 'f', 1) + " °C");
        } else {
            if (stats.minLabel) stats.minLabel->setText("-- °C");
            if (stats.avgLabel) stats.avgLabel->setText("-- °C");
            if (stats.maxLabel) stats.maxLabel->setText("-- °C");
        }
    }
}

void MainWindow::sendCommand() {
    QString command = commandLineEdit->text().trimmed();
    if (command.isEmpty()) return;

    // Vérifier si c'est un message d'urgence
    if (checkEmergencyMessages(command)) {
        showAlertDialog("ENVOI MESSAGE D'URGENCE", "Vous êtes sur le point d'envoyer un message d'urgence: " + command);
        logMessage("⚠️ ENVOI MESSAGE D'URGENCE: " + command, CRITICAL);
    }

    if (mqttClient->state() == QMqttClient::Connected) {
        mqttClient->publish(QMqttTopicName(MQTT_TOPIC_COMMAND), command.toUtf8());
        logMessage("Commande envoyée: " + command);
        commandLineEdit->clear();
    } else {
        logMessage("Erreur: Impossible d'envoyer la commande, non connecté au broker", WARNING);
    }
}

void MainWindow::sendQuickCommand(const QString &command) {
    if (mqttClient->state() == QMqttClient::Connected) {
        mqttClient->publish(QMqttTopicName(MQTT_TOPIC_COMMAND), command.toUtf8());
        logMessage("Commande rapide envoyée: " + command);
    } else {
        logMessage("Erreur: Impossible d'envoyer la commande rapide, non connecté au broker", WARNING);
    }
}

void MainWindow::updateElapsedTime() {
    if (lastUpdateTime.isValid()) {
        qint64 elapsedSeconds = lastUpdateTime.secsTo(QDateTime::currentDateTime());
        int hours = elapsedSeconds / 3600;
        int minutes = (elapsedSeconds % 3600) / 60;
        int seconds = elapsedSeconds % 60;

        QString elapsedStr = QString("(%1:%2:%3 écoulées)")
                                 .arg(hours, 2, 10, QChar('0'))
                                 .arg(minutes, 2, 10, QChar('0'))
                                 .arg(seconds, 2, 10, QChar('0'));

        // Extraire la partie timestamp sans le temps écoulé
        QString timestampPart = lastUpdateLabel->text().split('(').first().trimmed();
        lastUpdateLabel->setText(timestampPart + " " + elapsedStr);
    }
}

void MainWindow::logMessage(const QString &message, AlertLevel level) {
    QString timestamp = QTime::currentTime().toString("hh:mm:ss");

    // Formater le message selon le niveau d'alerte
    QString formattedMessage;

    switch (level) {
    case INFO:
        // Message normal en noir sur fond blanc
        formattedMessage = QString("<span style='color:black;'>[%1] %2</span>").arg(timestamp).arg(message);
        break;
    case WARNING:
        // Avertissement en orange vif sur fond blanc
        formattedMessage = QString("<span style='color:#FF6600; font-weight:bold;'>[%1] %2</span>").arg(timestamp).arg(message);
        break;
    case CRITICAL:
        // Alerte critique en rouge vif avec fond jaune très visible
        formattedMessage = QString("<span style='background-color:#FFFF00; color:#FF0000; font-weight:bold; font-size:120%; padding: 2px;'>[%1] %2</span>").arg(timestamp).arg(message);
        break;
    }

    logTextEdit->append(formattedMessage);

    // Défiler vers le bas
    QScrollBar *scrollBar = logTextEdit->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}
