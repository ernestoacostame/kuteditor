#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QVector>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <atomic>
#include <thread>

// Declaración previa para no polucionar el header con includes pesados de Windows/COM si no es estrictamente necesario,
// pero incluimos las declaraciones necesarias para punteros COM.
struct IMMDevice;
struct IAudioClient;
struct IAudioRenderClient;
struct IAudioCaptureClient;

class AudioEngine;
class TrackModel;

struct WasapiDevice {
    QString id;                // Device ID (Endpoint ID string)
    QString name;              // Friendly Name
    QString description;       // Description (Manufacturer + Name)
    bool isInput = true;       // True if capture endpoint
    int channelCount = 2;
};

/**
 * Modelo de lista de dispositivos de salida de WASAPI para QML.
 */
class WasapiOutputsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        DeviceIdRole = Qt::UserRole + 1,
        DeviceNameRole,
        DeviceDescriptionRole,
        ChannelCountRole
    };
    explicit WasapiOutputsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDevices(const QVector<WasapiDevice> &outputs);

private:
    QVector<WasapiDevice> m_outputs;
};

/**
 * Manager de audio nativo basado en WASAPI para Windows.
 * Reemplaza a JackManager/CoreAudioManager en Windows manteniendo la misma firma hacia QML.
 */
class WasapiAudioManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY devicesChanged)
    Q_PROPERTY(QString outputDeviceId READ outputDeviceId
               WRITE setOutputDeviceId NOTIFY outputDeviceChanged)
    Q_PROPERTY(bool running READ isActive NOTIFY runningChanged)
public:
    enum Roles {
        DeviceIdRole = Qt::UserRole + 1,
        DeviceNameRole,
        DeviceDescriptionRole,
        IsInputRole,
        ChannelCountRole
    };

    explicit WasapiAudioManager(QObject *parent = nullptr);
    ~WasapiAudioManager() override;

    // Métodos del modelo (Listar dispositivos de entrada)
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Refresca la lista de dispositivos de audio de Windows.
    Q_INVOKABLE void refreshDevices();

    /// Obtiene la cantidad de canales de un dispositivo por su ID.
    Q_INVOKABLE int deviceChannelCount(const QString &deviceId) const;

    int deviceCount() const { return m_inputs.size(); }

    WasapiOutputsModel *outputsModel() { return m_outputsModel; }

    QString outputDeviceId() const { return m_outputDeviceId; }
    void setOutputDeviceId(const QString &deviceId);

    void setAudioEngine(AudioEngine *engine);
    void setTrackModel(TrackModel *model);

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE bool isActive() const { return m_running.load(); }

    /// Conecta la grabación de una pista a un canal de entrada física.
    void connectTrackInput(int trackIndex);
    /// Desconecta la grabación de una pista.
    void disconnectTrackInput(int trackIndex);

    /// Conecta la salida de playback al dispositivo seleccionado.
    void connectPlaybackOutput();
    void disconnectPlaybackOutput();

signals:
    void devicesChanged();
    void outputDeviceChanged();
    void runningChanged();

private slots:
    void onTrackArmed(int trackIndex, bool armed);
    void onTrackDeviceChanged(int trackIndex, const QString &deviceId);
    void onPlaybackStarted();
    void onPlaybackStopped();

private:
    void runAudioThread();
    void processAudioIO();
    void disconnectTrackInputInternal(int trackIndex);

    QVector<WasapiDevice> m_inputs;
    QString m_outputDeviceId;
    WasapiOutputsModel *m_outputsModel = nullptr;

    AudioEngine *m_engine = nullptr;
    TrackModel *m_trackModel = nullptr;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_threadActive{false};
    std::thread m_audioThread;
    mutable QMutex m_mutex;

    // Estructuras de control para pistas grabadoras y sus canales
    struct TrackInputInfo {
        QString deviceId;
        int mode = 0;          // 0 = estéreo, 1 = mono
        int channelIndex = 0;  // Primer canal físico
    };
    QHash<int, TrackInputInfo> m_activeTracks;

    // Estructuras WASAPI internas
    void* m_enumerator = nullptr; // IMMDeviceEnumerator
    
    // Dispositivo de salida e interfaces de playback
    void* m_outDevice = nullptr;  // IMMDevice
    void* m_outClient = nullptr;  // IAudioClient
    void* m_outRender = nullptr;  // IAudioRenderClient
    unsigned int m_outBufferFrames = 0;

    // Conexión para capturadoras activas (agrupadas por dispositivo de captura)
    struct InputConnection {
        void* device = nullptr;      // IMMDevice
        void* client = nullptr;      // IAudioClient
        void* capture = nullptr;     // IAudioCaptureClient
        unsigned int bufferFrames = 0;
        QVector<float> tempBuffer;   // Buffer para acomodar la tasa de muestreo o ruteo
    };
    QHash<QString, InputConnection*> m_activeInputs;

    QString m_lastInputsSig;
    QString m_lastOutputsSig;
};
