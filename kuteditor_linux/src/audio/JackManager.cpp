#include "JackManager.h"
#include "AudioEngine.h"
#include "ui/TrackModel.h"

#include <QDebug>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <cstring>

#ifdef HAVE_JACK
#include <jack/jack.h>
#endif

// ============================================================================
//  PIMPL
// ============================================================================

struct JackManager::Impl {
    AudioEngine *engine = nullptr;
    JackManager *self = nullptr;

#ifdef HAVE_JACK
    jack_client_t *client = nullptr;
    int sampleRate = 48000;
    int bufferSize = 1024;

    // Puertos de ENTRADA por pista. Cada pista armada crea hasta 2 puertos
    // (mono = 1 puerto duplicado; estéreo = 2 puertos L/R).
    struct TrackPorts {
        jack_port_t *portL = nullptr;
        jack_port_t *portR = nullptr;   // nullptr en modo mono
        QStringList connectedTo;        // nombres de puertos físicos conectados
        double resamplePhase = 0.0;
        float lastL = 0.0f;
        float lastR = 0.0f;
    };
    QHash<int, TrackPorts> trackPorts;

    // Puertos de SALIDA globales para playback.
    jack_port_t *outL = nullptr;
    jack_port_t *outR = nullptr;

    // Estado del resampler de salida (playback)
    double playbackResamplePhase = 0.0;
    float playbackLastL = 0.0f;
    float playbackLastR = 0.0f;
    QVector<float> playback48kBuf;

    // Mutex que protege trackPorts desde el hilo UI. El hilo de audio lee
    // sin bloquear (solo tamaños/pointers estables durante un callback).
    QMutex portsMutex;
#endif
};

// ============================================================================
//  Callbacks JACK
// ============================================================================
#ifdef HAVE_JACK

namespace {

// Agrupa un nombre de puerto "Client:port" por la parte antes del ":".
QString clientOf(const QString &portName)
{
    const int colon = portName.indexOf(':');
    return colon > 0 ? portName.left(colon) : portName;
}

int on_process_impl(jack_nframes_t nframes, void *arg)
{
    auto *impl = static_cast<JackManager::Impl*>(arg);
    if (!impl || !impl->engine) return 0;

    struct PendingInput {
        int trackIdx;
        QVector<float> data;
    };
    QVarLengthArray<PendingInput, 8> pendingInputs;
    jack_port_t *snapOutL = nullptr;
    jack_port_t *snapOutR = nullptr;

    {
        QMutexLocker lock(&impl->portsMutex);

        double R_in = double(impl->sampleRate) / 48000.0;
        bool needsResampling = (impl->sampleRate != 48000);

        for (auto it = impl->trackPorts.begin(); it != impl->trackPorts.end(); ++it) {
            auto &tp = it.value();
            if (!tp.portL) continue;

            const float *bufL = static_cast<const float*>(jack_port_get_buffer(tp.portL, nframes));
            const float *bufR = tp.portR
                ? static_cast<const float*>(jack_port_get_buffer(tp.portR, nframes))
                : bufL;

            PendingInput pi;
            pi.trackIdx = it.key();

            if (needsResampling) {
                QVarLengthArray<float, 4096> T_L(nframes + 1);
                QVarLengthArray<float, 4096> T_R(nframes + 1);
                T_L[0] = tp.lastL;
                T_R[0] = tp.lastR;
                std::memcpy(T_L.data() + 1, bufL, nframes * sizeof(float));
                std::memcpy(T_R.data() + 1, bufR, nframes * sizeof(float));

                double pos = tp.resamplePhase;
                while (true) {
                    int idx = static_cast<int>(std::floor(pos));
                    if (idx >= static_cast<int>(nframes)) break;
                    double frac = pos - idx;

                    float l = (1.0f - frac) * T_L[idx] + frac * T_L[idx + 1];
                    float r = (1.0f - frac) * T_R[idx] + frac * T_R[idx + 1];

                    pi.data.append(l);
                    pi.data.append(r);
                    pos += R_in;
                }
                tp.lastL = T_L[nframes];
                tp.lastR = T_R[nframes];
                tp.resamplePhase = pos - nframes;
            } else {
                pi.data.resize(nframes * 2);
                for (jack_nframes_t f = 0; f < nframes; ++f) {
                    pi.data[f * 2 + 0] = bufL[f];
                    pi.data[f * 2 + 1] = bufR[f];
                }
            }
            pendingInputs.append(std::move(pi));
        }

        snapOutL = impl->outL;
        snapOutR = impl->outR;
    }
    // portsMutex is now released — safe to call into engine/trackModel.

    // --- Entradas: pasar datos al engine
    for (const auto &pi : pendingInputs) {
        impl->engine->onAudioInput(pi.trackIdx, pi.data.constData(), pi.data.size() / 2);
    }

    // --- Salidas: pedir al engine que mezcle si está reproduciendo.
    if (snapOutL && snapOutR) {
        float *outL = static_cast<float*>(jack_port_get_buffer(snapOutL, nframes));
        float *outR = static_cast<float*>(jack_port_get_buffer(snapOutR, nframes));

        if (impl->sampleRate != 48000) {
            double R_out = 48000.0 / impl->sampleRate;
            int neededFrames = static_cast<int>(std::ceil(impl->playbackResamplePhase + nframes * R_out)) + 2;

            int currentFrames = impl->playback48kBuf.size() / 2;
            if (currentFrames < neededFrames) {
                int toGenerate = neededFrames - currentFrames;
                // Alinear a múltiplos de 256 por rendimiento
                toGenerate = ((toGenerate + 255) / 256) * 256;

                int oldSize = impl->playback48kBuf.size();
                impl->playback48kBuf.resize((currentFrames + toGenerate) * 2);

                impl->engine->playbackMix(impl->playback48kBuf.data() + oldSize, toGenerate);
            }

            double pos = impl->playbackResamplePhase;
            for (jack_nframes_t f = 0; f < nframes; ++f) {
                int idx = static_cast<int>(std::floor(pos));
                double frac = pos - idx;

                float l0 = impl->playback48kBuf[idx * 2 + 0];
                float r0 = impl->playback48kBuf[idx * 2 + 1];
                float l1 = impl->playback48kBuf[(idx + 1) * 2 + 0];
                float r1 = impl->playback48kBuf[(idx + 1) * 2 + 1];

                outL[f] = (1.0f - frac) * l0 + frac * l1;
                outR[f] = (1.0f - frac) * r0 + frac * r1;

                pos += R_out;
            }

            int consumed = static_cast<int>(std::floor(pos));
            impl->playbackResamplePhase = pos - consumed;

            if (consumed > 0) {
                int remainingFrames = impl->playback48kBuf.size() / 2 - consumed;
                if (remainingFrames > 0) {
                    std::memmove(impl->playback48kBuf.data(),
                                 impl->playback48kBuf.constData() + consumed * 2,
                                 remainingFrames * 2 * sizeof(float));
                }
                impl->playback48kBuf.resize(remainingFrames * 2);
            }
        } else {
            constexpr int MAX_CHUNK = 1024;
            float interleaved[MAX_CHUNK * 2];
            jack_nframes_t done = 0;
            while (done < nframes) {
                const jack_nframes_t chunk = std::min<jack_nframes_t>(MAX_CHUNK, nframes - done);
                impl->engine->playbackMix(interleaved, int(chunk));
                for (jack_nframes_t f = 0; f < chunk; ++f) {
                    outL[done + f] = interleaved[f * 2 + 0];
                    outR[done + f] = interleaved[f * 2 + 1];
                }
                done += chunk;
            }
        }
    }

    return 0;
}

int on_graph_order(void *arg)
{
    auto *impl = static_cast<JackManager::Impl*>(arg);
    if (impl && impl->self) {
        // Se añadieron/removieron puertos: refrescar lista en UI.
        QMetaObject::invokeMethod(impl->self, "refreshDevices", Qt::QueuedConnection);
    }
    return 0;
}

void on_port_registration(jack_port_id_t /*port*/, int /*reg*/, void *arg)
{
    auto *impl = static_cast<JackManager::Impl*>(arg);
    if (impl && impl->self) {
        QMetaObject::invokeMethod(impl->self, "refreshDevices", Qt::QueuedConnection);
    }
}

} // namespace
#endif // HAVE_JACK


// ============================================================================
//  JackOutputsModel
// ============================================================================
JackOutputsModel::JackOutputsModel(QObject *parent)
    : QAbstractListModel(parent) {}

int JackOutputsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_outputs.size();
}

QVariant JackOutputsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_outputs.size())
        return {};
    const auto &d = m_outputs[index.row()];
    switch (role) {
        case DeviceIdRole:          return d.id;
        case DeviceNameRole:        return d.name;
        case Qt::DisplayRole:
        case DeviceDescriptionRole: return d.description;
        case ChannelCountRole:      return d.channelCount;
    }
    return {};
}

QHash<int, QByteArray> JackOutputsModel::roleNames() const
{
    return {
        {DeviceIdRole,          "deviceId"},
        {DeviceNameRole,        "deviceName"},
        {DeviceDescriptionRole, "deviceDescription"},
        {ChannelCountRole,      "channelCount"},
    };
}

void JackOutputsModel::setDevices(const QVector<JackDevice> &outputs)
{
    beginResetModel();
    m_outputs = outputs;
    endResetModel();
}

// ============================================================================
//  JackManager
// ============================================================================
JackManager::JackManager(QObject *parent)
    : QAbstractListModel(parent)
    , m_outputsModel(new JackOutputsModel(this))
    , m_impl(new Impl)
{
    m_impl->self = this;
}

JackManager::~JackManager()
{
    stopAll();
    delete m_impl;
}

int JackManager::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_inputs.size();
}

QVariant JackManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_inputs.size())
        return {};
    const auto &d = m_inputs[index.row()];
    switch (role) {
        case DeviceIdRole:          return d.id;
        case DeviceNameRole:        return d.name;
        case Qt::DisplayRole:       return d.description;
        case DeviceDescriptionRole: return d.description;
        case IsInputRole:           return d.isInput;
        case ChannelCountRole:      return d.channelCount;
    }
    return {};
}

QHash<int, QByteArray> JackManager::roleNames() const
{
    return {
        {DeviceIdRole,          "deviceId"},
        {DeviceNameRole,        "deviceName"},
        {DeviceDescriptionRole, "deviceDescription"},
        {IsInputRole,           "isInput"},
        {ChannelCountRole,      "channelCount"},
    };
}

void JackManager::refreshDevices()
{
    QVector<JackDevice> allInputs;
    QVector<JackDevice> allOutputs;

    // Device "default" con id vacío.
    JackDevice defIn;
    defIn.id = QString();
    defIn.name = QStringLiteral("default-input");
    defIn.description = QStringLiteral("Entrada por defecto (primer cliente físico)");
    defIn.isInput = true;
    defIn.channelCount = 2;
    allInputs.append(defIn);

    JackDevice defOut;
    defOut.id = QString();
    defOut.name = QStringLiteral("default-output");
    defOut.description = QStringLiteral("Salida por defecto (primer cliente físico)");
    defOut.isInput = false;
    defOut.channelCount = 2;
    allOutputs.append(defOut);

#ifdef HAVE_JACK
    if (m_impl && m_impl->client) {
        // Enumerar puertos físicos de captura (= inputs para la app)
        const char **capturePorts = jack_get_ports(
            m_impl->client, nullptr, JACK_DEFAULT_AUDIO_TYPE,
            JackPortIsOutput | JackPortIsPhysical);
        if (capturePorts) {
            QHash<QString, JackDevice> byClient;
            for (const char **p = capturePorts; *p; ++p) {
                const QString portName = QString::fromUtf8(*p);
                const QString client = ::clientOf(portName);
                auto &dev = byClient[client];
                if (dev.id.isEmpty()) {
                    dev.id = client;
                    dev.name = client;
                    dev.description = client;
                    dev.isInput = true;
                }
                dev.portNames.append(portName);
                dev.channelCount = dev.portNames.size();
            }
            jack_free(capturePorts);
            for (const auto &d : byClient) allInputs.append(d);
        }

        // Enumerar puertos físicos de playback (= outputs para la app)
        const char **playbackPorts = jack_get_ports(
            m_impl->client, nullptr, JACK_DEFAULT_AUDIO_TYPE,
            JackPortIsInput | JackPortIsPhysical);
        if (playbackPorts) {
            QHash<QString, JackDevice> byClient;
            for (const char **p = playbackPorts; *p; ++p) {
                const QString portName = QString::fromUtf8(*p);
                const QString client = ::clientOf(portName);
                auto &dev = byClient[client];
                if (dev.id.isEmpty()) {
                    dev.id = client;
                    dev.name = client;
                    dev.description = client;
                    dev.isInput = false;
                }
                dev.portNames.append(portName);
                dev.channelCount = dev.portNames.size();
            }
            jack_free(playbackPorts);
            for (const auto &d : byClient) allOutputs.append(d);
        }
    }
#endif

    // Comparar con el estado previo para evitar resets y logs redundantes.
    // Se producen cuando nosotros mismos (re)registramos puertos de las
    // pistas, porque jack dispara graph_order/port_registration.
    auto signature = [](const QVector<JackDevice> &a) {
        QStringList keys;
        keys.reserve(a.size());
        for (const auto &d : a) {
            keys << d.id + ":" + QString::number(d.channelCount);
        }
        std::sort(keys.begin(), keys.end());
        return keys.join("|");
    };

    const QString sigIn  = signature(allInputs);
    const QString sigOut = signature(allOutputs);
    if (sigIn == m_lastInputsSig && sigOut == m_lastOutputsSig) {
        // Nada cambió: salir silenciosamente.
        return;
    }
    m_lastInputsSig = sigIn;
    m_lastOutputsSig = sigOut;

    beginResetModel();
    m_inputs = allInputs;
    endResetModel();

    m_outputsModel->setDevices(allOutputs);

    // Log (solo cuando realmente cambia algo)
    for (const auto &d : allInputs) {
        qDebug().nospace() << "[JACK] IN  " << d.id
                           << " ch=" << d.channelCount
                           << " ports=" << d.portNames.size();
    }
    for (const auto &d : allOutputs) {
        qDebug().nospace() << "[JACK] OUT " << d.id
                           << " ch=" << d.channelCount
                           << " ports=" << d.portNames.size();
    }

    emit devicesChanged();
}

int JackManager::deviceChannelCount(const QString &deviceId) const
{
    if (deviceId.isEmpty()) return 2;
    for (const auto &d : m_inputs) {
        if (d.id == deviceId) return d.channelCount;
    }
    return 0;
}

void JackManager::setOutputDeviceId(const QString &deviceId)
{
    if (m_outputDeviceId == deviceId) return;
    m_outputDeviceId = deviceId;
    emit outputDeviceChanged();
    // Si hay playback activo, reconectar los puertos de salida.
    if (m_engine && m_engine->isOutputActive()) {
        disconnectPlaybackOutput();
        connectPlaybackOutput();
    }
}

void JackManager::setAudioEngine(AudioEngine *engine)
{
    if (m_engine == engine) return;
    if (m_engine) disconnect(m_engine, nullptr, this, nullptr);
    m_engine = engine;
    if (m_impl) m_impl->engine = engine;
    if (m_engine) {
        connect(m_engine, &AudioEngine::playbackStarted,
                this, &JackManager::onPlaybackStarted);
        connect(m_engine, &AudioEngine::playbackStopped,
                this, &JackManager::onPlaybackStopped);
        // Monitoring: conectar salidas durante recording para escuchar
        // las pistas no armadas mientras se graba.
        connect(m_engine, &AudioEngine::recordingStarted,
                this, &JackManager::onPlaybackStarted);
        connect(m_engine, &AudioEngine::recordingStopped,
                this, [this](int){ onPlaybackStopped(); });
        connect(m_engine, &QObject::destroyed, this, [this]() {
            m_engine = nullptr;
            if (m_impl) m_impl->engine = nullptr;
        });
    }
}

void JackManager::setTrackModel(TrackModel *model)
{
    if (m_trackModel == model) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = model;
    if (m_trackModel) {
        connect(m_trackModel, &TrackModel::trackArmedChanged,
                this, &JackManager::onTrackArmed);
        connect(m_trackModel, &TrackModel::trackDeviceChanged,
                this, &JackManager::onTrackDeviceChanged);
        connect(m_trackModel, &QObject::destroyed, this, [this]() {
            m_trackModel = nullptr;
        });
    }
}

bool JackManager::start()
{
    if (m_running.load()) return true;

#ifdef HAVE_JACK
    jack_status_t status = jack_status_t(0);
    m_impl->client = jack_client_open("podcast-editor",
                                      JackNoStartServer, &status);
    if (!m_impl->client) {
        qWarning() << "[JACK] jack_client_open falló, status=" << int(status);
        return false;
    }

    m_impl->sampleRate = int(jack_get_sample_rate(m_impl->client));
    m_impl->bufferSize = int(jack_get_buffer_size(m_impl->client));
    qDebug() << "[JACK] Cliente creado. SR=" << m_impl->sampleRate
             << "buf=" << m_impl->bufferSize;

    // El engine asume 48000; si JACK corre a otra tasa, avisamos.
    if (m_impl->sampleRate != AudioEngine::SAMPLE_RATE) {
        qWarning() << "[JACK] SR del servidor =" << m_impl->sampleRate
                   << "pero engine usa" << AudioEngine::SAMPLE_RATE
                   << "— habrá desajuste de velocidad";
    }

    // Callback principal
    jack_set_process_callback(m_impl->client, ::on_process_impl, m_impl);
    jack_set_graph_order_callback(m_impl->client, ::on_graph_order, m_impl);
    jack_set_port_registration_callback(m_impl->client, ::on_port_registration, m_impl);

    // Puertos de salida globales
    m_impl->outL = jack_port_register(m_impl->client, "out_L",
                                      JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsOutput, 0);
    m_impl->outR = jack_port_register(m_impl->client, "out_R",
                                      JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsOutput, 0);

    if (jack_activate(m_impl->client) != 0) {
        qWarning() << "[JACK] jack_activate falló";
        jack_client_close(m_impl->client);
        m_impl->client = nullptr;
        return false;
    }

    m_impl->playbackResamplePhase = 0.0;
    m_impl->playbackLastL = 0.0f;
    m_impl->playbackLastR = 0.0f;
    m_impl->playback48kBuf.clear();

    m_running.store(true);
    emit runningChanged();
    qDebug() << "[JACK] Activo";

    // Primera enumeración de devices.
    refreshDevices();
    return true;
#else
    qWarning() << "[JACK] Compilado sin soporte JACK.";
    return false;
#endif
}

void JackManager::stopAll()
{
#ifdef HAVE_JACK
    if (m_impl && m_impl->client) {
        // Desactivar el cliente primero para asegurar que el hilo de callback
        // de audio (on_process_impl) se detenga y no intente procesar puertos
        // que estamos a punto de desregistrar.
        jack_deactivate(m_impl->client);

        // Desconectar puertos de pistas.
        {
            QMutexLocker lock(&m_impl->portsMutex);
            for (auto it = m_impl->trackPorts.begin(); it != m_impl->trackPorts.end(); ++it) {
                if (it.value().portL) jack_port_unregister(m_impl->client, it.value().portL);
                if (it.value().portR) jack_port_unregister(m_impl->client, it.value().portR);
            }
            m_impl->trackPorts.clear();
        }

        if (m_impl->outL) { jack_port_unregister(m_impl->client, m_impl->outL); m_impl->outL = nullptr; }
        if (m_impl->outR) { jack_port_unregister(m_impl->client, m_impl->outR); m_impl->outR = nullptr; }

        jack_client_close(m_impl->client);
        m_impl->client = nullptr;
    }
#endif
    m_running.store(false);
    emit runningChanged();
    qDebug() << "[JACK] Detenido";
}

void JackManager::connectTrackInput(int trackIndex)
{
#ifdef HAVE_JACK
    if (!m_impl || !m_impl->client || !m_trackModel) return;
    const auto data = m_trackModel->getTrackData(trackIndex);
    if (data.isEmpty()) return;
    const QString deviceId = data.value("inputDevice").toString();
    const int mode = data.value("inputMode", 0).toInt();
    int channelIndex = data.value("inputChannelIndex", 0).toInt();
    if (channelIndex < 0) channelIndex = 0;

    // Descubrir puertos físicos del device seleccionado.
    // Si deviceId está vacío, usamos los primeros disponibles de cualquier cliente físico.
    QStringList physPorts;
    if (!deviceId.isEmpty()) {
        for (const auto &d : m_inputs) {
            if (d.id == deviceId) { physPorts = d.portNames; break; }
        }
    }

    // Si no se encontró el dispositivo o no tiene puertos asignados, o si deviceId está vacío,
    // usamos la primera opción disponible por defecto en el sistema.
    if (physPorts.isEmpty()) {
        if (!deviceId.isEmpty()) {
            qWarning() << "[JACK] No se encontró el dispositivo" << deviceId << "o no tiene puertos físicos asignados. Usando dispositivo por defecto.";
        }
        const char **all = jack_get_ports(m_impl->client, nullptr,
                                          JACK_DEFAULT_AUDIO_TYPE,
                                          JackPortIsOutput | JackPortIsPhysical);
        if (all) {
            for (const char **p = all; *p; ++p) physPorts << QString::fromUtf8(*p);
            jack_free(all);
        }
    }

    if (physPorts.isEmpty()) {
        qWarning() << "[JACK] No hay puertos físicos de entrada disponibles en el sistema.";
        return;
    }

    // Desconectar/reconectar si ya existía.
    disconnectTrackInput(trackIndex);

    // Usar un contador creciente para los nombres de puerto: jack_port_unregister
    // libera el nombre de forma diferida (tras el siguiente ciclo de audio), así
    // que un re-registro rápido con el mismo nombre falla. Con un contador
    // nunca colisionamos.
    const int nonce = m_portNonce.fetch_add(1) + 1;
    const QByteArray nameL = QStringLiteral("track_%1_L_%2").arg(trackIndex).arg(nonce).toUtf8();
    const QByteArray nameR = QStringLiteral("track_%1_R_%2").arg(trackIndex).arg(nonce).toUtf8();

    Impl::TrackPorts tp;
    tp.portL = jack_port_register(m_impl->client, nameL.constData(),
                                  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    if (mode == 0) { // estéreo
        tp.portR = jack_port_register(m_impl->client, nameR.constData(),
                                      JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    }

    if (!tp.portL) {
        qWarning() << "[JACK] jack_port_register falló pista" << trackIndex;
        return;
    }

    // Conectar: elegir puertos físicos según channelIndex y modo.
    const int chL = qBound(0, channelIndex, physPorts.size() - 1);
    QString srcL = physPorts[chL];
    const QString destL = QString::fromUtf8(jack_port_name(tp.portL));
    const int rcL = jack_connect(m_impl->client, srcL.toUtf8().constData(),
                                                 destL.toUtf8().constData());
    if (rcL != 0 && rcL != EEXIST) {
        qWarning() << "[JACK] jack_connect L falló:" << rcL
                   << srcL << "->" << destL;
    }
    tp.connectedTo.append(srcL);

    if (mode == 0 && tp.portR) {
        const int chR = qBound(0, channelIndex + 1, physPorts.size() - 1);
        QString srcR = physPorts[chR];
        const QString destR = QString::fromUtf8(jack_port_name(tp.portR));
        const int rcR = jack_connect(m_impl->client, srcR.toUtf8().constData(),
                                                     destR.toUtf8().constData());
        if (rcR != 0 && rcR != EEXIST) {
            qWarning() << "[JACK] jack_connect R falló:" << rcR
                       << srcR << "->" << destR;
        }
        tp.connectedTo.append(srcR);
    }

    {
        QMutexLocker lock(&m_impl->portsMutex);
        m_impl->trackPorts.insert(trackIndex, tp);
    }
    qDebug() << "[JACK] Pista" << trackIndex
             << (mode == 0 ? "estéreo" : "mono")
             << "conectada a" << tp.connectedTo;
#else
    Q_UNUSED(trackIndex);
#endif
}

void JackManager::disconnectTrackInput(int trackIndex)
{
#ifdef HAVE_JACK
    if (!m_impl || !m_impl->client) return;
    Impl::TrackPorts tp;
    {
        QMutexLocker lock(&m_impl->portsMutex);
        auto it = m_impl->trackPorts.find(trackIndex);
        if (it == m_impl->trackPorts.end()) return;
        tp = it.value();
        m_impl->trackPorts.erase(it);
    }
    if (tp.portL) jack_port_unregister(m_impl->client, tp.portL);
    if (tp.portR) jack_port_unregister(m_impl->client, tp.portR);

    // Al desarmar, el VU de esta pista debe volver a 0 (si no, se queda
    // el último valor congelado).
    if (m_trackModel) m_trackModel->updateTrackLevel(trackIndex, 0.0f, 0.0f);

    qDebug() << "[JACK] Pista" << trackIndex << "desconectada";
#else
    Q_UNUSED(trackIndex);
#endif
}

void JackManager::connectPlaybackOutput()
{
#ifdef HAVE_JACK
    if (!m_impl || !m_impl->client || !m_impl->outL || !m_impl->outR) return;

    QStringList targetPorts;
    if (!m_outputDeviceId.isEmpty()) {
        // Buscar los puertos del cliente seleccionado.
        const char **pb = jack_get_ports(m_impl->client,
                                         (m_outputDeviceId + ":").toUtf8().constData(),
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsInput | JackPortIsPhysical);
        if (pb) {
            for (const char **p = pb; *p && targetPorts.size() < 2; ++p)
                targetPorts << QString::fromUtf8(*p);
            jack_free(pb);
        }
    }

    // Si no se encontró el dispositivo de salida guardado o estaba vacío, usar el de por defecto en el sistema
    if (targetPorts.isEmpty()) {
        if (!m_outputDeviceId.isEmpty()) {
            qWarning() << "[JACK] No se encontró el dispositivo de salida" << m_outputDeviceId << "usando el dispositivo por defecto.";
        }
        // Buscar los dos primeros puertos físicos playback.
        const char **pb = jack_get_ports(m_impl->client, nullptr,
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsInput | JackPortIsPhysical);
        if (pb) {
            for (const char **p = pb; *p && targetPorts.size() < 2; ++p)
                targetPorts << QString::fromUtf8(*p);
            jack_free(pb);
        }
    }

    if (targetPorts.size() < 1) {
        qWarning() << "[JACK] No hay puertos playback físicos disponibles";
        return;
    }

    const QString srcL = QString::fromUtf8(jack_port_name(m_impl->outL));
    const QString srcR = QString::fromUtf8(jack_port_name(m_impl->outR));

    jack_connect(m_impl->client, srcL.toUtf8().constData(),
                                 targetPorts[0].toUtf8().constData());
    if (targetPorts.size() >= 2) {
        jack_connect(m_impl->client, srcR.toUtf8().constData(),
                                     targetPorts[1].toUtf8().constData());
    } else {
        // Mono playback: duplicar L en el único destino disponible
        jack_connect(m_impl->client, srcR.toUtf8().constData(),
                                     targetPorts[0].toUtf8().constData());
    }
    qDebug() << "[JACK] Playback conectado a" << targetPorts;
#endif
}

void JackManager::disconnectPlaybackOutput()
{
#ifdef HAVE_JACK
    if (!m_impl || !m_impl->client) return;
    if (m_impl->outL) jack_port_disconnect(m_impl->client, m_impl->outL);
    if (m_impl->outR) jack_port_disconnect(m_impl->client, m_impl->outR);
    qDebug() << "[JACK] Playback desconectado";
#endif
}

void JackManager::onTrackArmed(int trackIndex, bool armed)
{
    if (armed) {
        // Si la pista no tiene device asignado, asignarle el primero
        // disponible para que la UI (ComboBox) lo refleje.
        if (m_trackModel) {
            const auto data = m_trackModel->getTrackData(trackIndex);
            const QString devId = data.value("inputDevice").toString();
            if (devId.isEmpty() && !m_inputs.isEmpty()) {
                // Saltar la entrada "" (sin device) si existe; buscar el
                // primer device "real" (con puertos físicos y nombre).
                for (const auto &d : m_inputs) {
                    if (!d.id.isEmpty() && !d.portNames.isEmpty()) {
                        m_trackModel->updateInputDevice(trackIndex, d.id, d.name);
                        break;
                    }
                }
            }
        }
        connectTrackInput(trackIndex);
    } else {
        disconnectTrackInput(trackIndex);
    }

    // Iniciar/parar el timer de UI para monitorización VU en modo Idle.
    if (m_engine)
        m_engine->checkMonitoring();
}

void JackManager::onTrackDeviceChanged(int trackIndex, const QString & /*deviceId*/)
{
    if (!m_trackModel) return;
    const auto armed = m_trackModel->armedTrackIndices();
    if (armed.contains(trackIndex)) {
        connectTrackInput(trackIndex); // reconecta con el nuevo device/channel
    }
}

void JackManager::onPlaybackStarted() { connectPlaybackOutput(); }
void JackManager::onPlaybackStopped() { disconnectPlaybackOutput(); }
