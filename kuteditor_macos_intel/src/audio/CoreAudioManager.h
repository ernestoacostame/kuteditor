#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QVector>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <atomic>

class AudioEngine;
class TrackModel;

struct CoreAudioDevice {
    QString id;                // UID del dispositivo CoreAudio
    QString name;              // Nombre del dispositivo
    QString description;       // Descripción (Fabricante + Nombre)
    bool isInput = true;       // True si es de captura (entrada)
    QStringList portNames;     // Nombres de los canales físicos
    int channelCount = 0;      // Cantidad de canales
};

/**
 * Modelo de lista de dispositivos de salida de CoreAudio para QML.
 */
class CoreAudioOutputsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        DeviceIdRole = Qt::UserRole + 1,
        DeviceNameRole,
        DeviceDescriptionRole,
        ChannelCountRole
    };
    explicit CoreAudioOutputsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDevices(const QVector<CoreAudioDevice> &outputs);

private:
    QVector<CoreAudioDevice> m_outputs;
};

/**
 * Manager de audio basado en CoreAudio para macOS.
 * Reemplaza a JackManager manteniendo la misma interfaz de QML y C++.
 */
class CoreAudioManager : public QAbstractListModel
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

    explicit CoreAudioManager(QObject *parent = nullptr);
    ~CoreAudioManager() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Re-enumera los dispositivos CoreAudio del sistema y actualiza los modelos.
    Q_INVOKABLE void refreshDevices();

    /// Devuelve la cantidad de canales del dispositivo indicado.
    Q_INVOKABLE int deviceChannelCount(const QString &deviceId) const;

    int deviceCount() const { return m_inputs.size(); }

    CoreAudioOutputsModel *outputsModel() { return m_outputsModel; }

    QString outputDeviceId() const { return m_outputDeviceId; }
    void setOutputDeviceId(const QString &deviceId);

    void setAudioEngine(AudioEngine *engine);
    void setTrackModel(TrackModel *model);

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE bool isActive() const { return m_running.load(); }

    /// Conecta los canales de entrada físicos de un dispositivo a una pista.
    void connectTrackInput(int trackIndex);
    /// Desconecta el canal de entrada de una pista.
    void disconnectTrackInput(int trackIndex);

    /// Conecta la salida de playback al dispositivo de salida elegido.
    void connectPlaybackOutput();
    void disconnectPlaybackOutput();

    // Estructuras de implementación interna
    struct InputDeviceConnection;

    // Procesa buffers desde los callbacks del hilo de tiempo real de CoreAudio
    void processInput(const QString &deviceId, void *ioData, uint32_t frames);
    void processOutput(void *ioData, uint32_t frames);

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
    InputDeviceConnection* disconnectTrackInputInternal(int trackIndex);
    void startInputDeviceIfNeeded(const QString &deviceId);
    InputDeviceConnection* stopInputDeviceIfUnused(const QString &deviceId);
    void disposeInputConnection(InputDeviceConnection *conn);

    QVector<CoreAudioDevice> m_inputs;
    QString m_outputDeviceId;
    CoreAudioOutputsModel *m_outputsModel = nullptr;

    AudioEngine *m_engine = nullptr;
    TrackModel *m_trackModel = nullptr;

    std::atomic<bool> m_running{false};
    mutable QMutex m_mutex;

    // Estado del hardware CoreAudio
    void *m_outputUnit = nullptr; // AudioUnit de salida

    // Estructura para registrar qué pista está escuchando de qué dispositivo/canales
    struct TrackInputInfo {
        QString deviceId;
        int mode = 0; // 0 = stereo, 1 = mono
        int channelIndex = 0;
    };
    QHash<int, TrackInputInfo> m_activeTracks;
    QHash<QString, InputDeviceConnection*> m_activeInputs; // conexiones de entrada activas

    QString m_lastInputsSig;
    QString m_lastOutputsSig;
};
