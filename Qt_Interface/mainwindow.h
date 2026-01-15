#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtMqtt/QMqttClient>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QLabel>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QDateTime>
#include <QMessageBox>
#include <QMap>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// Définir des niveaux d'alerte pour le journal
enum AlertLevel {
    INFO,       // Information normale (noir)
    WARNING,    // Avertissement (orange)
    CRITICAL    // Alerte critique (rouge vif)
};

// Structure pour stocker les statistiques d'un capteur
struct SensorStats {
    double min = 999;
    double max = -999;
    double sum = 0;
    int count = 0;
    QLabel *minLabel = nullptr;
    QLabel *avgLabel = nullptr;
    QLabel *maxLabel = nullptr;
    QLabel *valueLabel = nullptr;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnected();
    void onDisconnected();
    void onMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
    void onButtonResponseReceived(const QString &response);
    void sendCommand();
    void sendQuickCommand(const QString &command);
    void updateElapsedTime();

private:
    void setupUI();
    void updateChart(const QMap<QString, double> &temperatures);
    void updateStatistics();
    void logMessage(const QString &message, AlertLevel level = INFO);
    void showAlertDialog(const QString &title, const QString &message);
    bool checkEmergencyMessages(const QString &message);
    void setupSensorsUI(QVBoxLayout *layout);

    Ui::MainWindow *ui;
    QMqttClient *mqttClient;

    // ✅ Label pour l'état de connexion et l'horodatage
    QLabel *statusLabel, *lastUpdateLabel;

    // ✅ Map contenant tous les capteurs et leurs statistiques
    QMap<QString, SensorStats> sensors;

    // Liste des noms des capteurs pour l'ordre d'affichage
    QStringList sensorNames = {
        "Capteur_Avant",
        "Capteur_Arriere",
        "Capteur_Moteur",
        "Capteur_Extérieur",
        "Capteur_Intérieur"
    };

    // ✅ Graphique QtCharts
    QChart *chart;
    QChartView *chartView;
    QMap<QString, QLineSeries*> sensorSeries;
    QValueAxis *axisX;
    QValueAxis *axisY;
    int timeCounter = 0;

    // ✅ Commandes MQTT
    QLineEdit *commandLineEdit;
    QPushButton *sendButton;
    QPushButton *startButton;
    QPushButton *stopButton;

    // ✅ Journal des événements
    QTextEdit *logTextEdit;

    // ✅ Horodatage de la dernière mise à jour
    QDateTime lastUpdateTime;
    QTimer *updateTimer;

    // ✅ Constantes
    const double TEMPERATURE_MAX = 28.0;  // Repris de config.py
    const QString MQTT_TOPIC_COMMAND = "commande/control";
    const QString MQTT_TOPIC_RESPONSE = "commande/reponse";

    // 🚨 Liste des messages d'urgence
    QStringList emergencyMessages = {"MAYDAY", "PANPAN"};

    // Couleurs pour les différents capteurs
    QVector<QColor> sensorColors = {
        QColor(0, 0, 255),      // Bleu pour Capteur_Avant
        QColor(255, 0, 0),      // Rouge pour Capteur_Arriere
        QColor(0, 128, 0),      // Vert pour Capteur_Moteur
        QColor(128, 0, 128),    // Violet pour Capteur_Extérieur
        QColor(255, 165, 0)     // Orange pour Capteur_Intérieur
    };
};

#endif // MAINWINDOW_H
