#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QVector>
#include <QHash>
#include <QString>
#include <QStringList>
#include <atomic>

class AudioEngine;
class TrackModel;

/**
 * JackManager: backend de audio basado en JACK (o pipewire-jack, que
 * implementa la misma API). A diferencia de PipeWire nativo, aquí los
 * canales de un dispositivo multichannel se exponen como puertos JACK
 * independientes (p.ej. "RODECaster Duo Pro 1:capture_AUX0" ..
 * "capture_AUX13"), que es la aproximación que usa Reaper.
 *
 * "Device" en nuestra UI = cliente JACK agrupado por nombre antes del
 *                          primer ":" en los puertos físicos.
 * "Channel" = puerto individual dentro de ese cliente.
 *
 * Un único jack_client para toda la app; pistas armadas crean puertos
 * de entrada internos que se conectan a los puertos físicos elegidos.
 */
struct JackDevice {
    QString id;                // nombre del cliente JACK (antes del ":")
    QString name;              // igual que id (sin prettyfication aún)
    QString description;       // igual que id por ahora
    bool isInput = true;       // true si tiene puertos de CAPTURA
    QStringList portNames;     // lista de puertos físicos (nombres completos)
    int channelCount = 0;      // portNames.size()
};

/**
 * Modelo derivado: solo outputs.
 */
class JackOutputsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        DeviceIdRole = Qt::UserRole + 1,
        DeviceNameRole,
        DeviceDescriptionRole,
        ChannelCountRole
    };
    explicit JackOutputsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDevices(const QVector<JackDevice> &outputs);

private:
    QVector<JackDevice> m_outputs;
};

class JackManager : public QAbstractListModel
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

    explicit JackManager(QObject *parent = nullptr);
    ~JackManager() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Re-enumera puertos y vuelve a construir la lista de devices.
    Q_INVOKABLE void refreshDevices();

    /// Canales del device dado (por id). 0 si desconocido.
    Q_INVOKABLE int deviceChannelCount(const QString &deviceId) const;

    int deviceCount() const { return m_inputs.size(); }

    JackOutputsModel *outputsModel() { return m_outputsModel; }

    QString outputDeviceId() const { return m_outputDeviceId; }
    void setOutputDeviceId(const QString &deviceId);

    void setAudioEngine(AudioEngine *engine);
    void setTrackModel(TrackModel *model);

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE bool isActive() const { return m_running.load(); }

    /// Conecta una pista a los puertos físicos según su device/channel/mode.
    /// Crea los jack ports propios si no existen y hace jack_connect.
    void connectTrackInput(int trackIndex);
    /// Desconecta y destruye los puertos internos de una pista.
    void disconnectTrackInput(int trackIndex);

    /// Conecta los puertos de output a los playback del device elegido.
    void connectPlaybackOutput();
    void disconnectPlaybackOutput();

    // Impl interno
    struct Impl;

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
    QVector<JackDevice> m_inputs;
    QString m_outputDeviceId;
    JackOutputsModel *m_outputsModel = nullptr;

    AudioEngine *m_engine = nullptr;
    TrackModel *m_trackModel = nullptr;
    Impl *m_impl = nullptr;

    std::atomic<bool> m_running{false};
    std::atomic<int> m_portNonce{0};

    // Firmas del último refreshDevices para detectar si realmente cambia.
    // JACK dispara graph_order/port_registration también cuando nosotros
    // registramos puertos, provocando bucles de refresh.
    QString m_lastInputsSig;
    QString m_lastOutputsSig;
};
