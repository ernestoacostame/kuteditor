#include "CoreAudioManager.h"
#include "AudioEngine.h"
#include "MacPermissions.h"
#include "ui/TrackModel.h"

#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>
#include <cstring>
#include <algorithm>

#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

// ============================================================================
//  Ayudantes de CoreAudio
// ============================================================================

static AudioObjectID getDefaultDevice(bool isInput) {
    AudioObjectPropertyAddress propertyAddress = {
        isInput ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectID deviceID = kAudioObjectUnknown;
    UInt32 dataSize = sizeof(deviceID);
    OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, &deviceID);
    if (status != noErr) return kAudioObjectUnknown;
    return deviceID;
}

static QString getStringProperty(AudioObjectID deviceID, AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress propertyAddress = {
        selector,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFStringRef cfString = nullptr;
    UInt32 dataSize = sizeof(CFStringRef);
    OSStatus status = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, &cfString);
    if (status == noErr && cfString) {
        QString result = QString::fromCFString(cfString);
        CFRelease(cfString);
        return result;
    }
    return QString();
}

static int getChannelCount(AudioObjectID deviceID, bool isInput) {
    AudioObjectPropertyAddress propertyAddress = {
        kAudioDevicePropertyStreamConfiguration,
        isInput ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(deviceID, &propertyAddress, 0, nullptr, &dataSize);
    if (status != noErr || dataSize == 0) return 0;
    
    AudioBufferList *bufferList = (AudioBufferList *)std::malloc(dataSize);
    if (!bufferList) return 0;
    
    status = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, bufferList);
    int totalChannels = 0;
    if (status == noErr) {
        for (UInt32 i = 0; i < bufferList->mNumberBuffers; ++i) {
            totalChannels += bufferList->mBuffers[i].mNumberChannels;
        }
    }
    std::free(bufferList);
    return totalChannels;
}

static AudioObjectID findDeviceByID(const QString &uid, bool isInput) {
    if (uid.isEmpty()) {
        return getDefaultDevice(isInput);
    }
    
    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize);
    if (status != noErr) return kAudioObjectUnknown;
    
    int deviceCount = dataSize / sizeof(AudioObjectID);
    QVector<AudioObjectID> deviceIDs(deviceCount);
    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, deviceIDs.data());
    if (status != noErr) return kAudioObjectUnknown;
    
    for (AudioObjectID id : deviceIDs) {
        if (getStringProperty(id, kAudioDevicePropertyDeviceUID) == uid) {
            // Verificar si el dispositivo tiene canales en el scope indicado
            if (getChannelCount(id, isInput) > 0) {
                return id;
            }
        }
    }
    return kAudioObjectUnknown;
}

// ============================================================================
//  Callbacks de CoreAudio
// ============================================================================

struct CoreAudioManager::InputDeviceConnection {
    CoreAudioManager *manager = nullptr;
    AudioUnit unit = nullptr;
    QString deviceId;
    AudioBufferList *bufferList = nullptr;
    AudioBufferList *resampledBufferList = nullptr;
    int channels = 0;
    UInt32 bufferCapacityBytes = 0;
    UInt32 resampledCapacityBytes = 0;

    // Estado del resampler
    bool needsResampling = false;
    double timeRatio = 1.0;
    double resamplePhase = 0.0;
    QVector<float> lastSamples;
};

static OSStatus inputCallback(void *inRefCon,
                              AudioUnitRenderActionFlags *ioActionFlags,
                              const AudioTimeStamp *inTimeStamp,
                              UInt32 /*inBusNumber*/,
                              UInt32 inNumberFrames,
                              AudioBufferList * /*ioData*/)
{
    auto *conn = static_cast<CoreAudioManager::InputDeviceConnection*>(inRefCon);
    if (!conn || !conn->manager || !conn->unit) return noErr;

    const UInt32 requiredSize = inNumberFrames * sizeof(float);

    // Asegurar que el bufferList temporal es lo suficientemente grande
    if (requiredSize > conn->bufferCapacityBytes) {
        conn->bufferCapacityBytes = requiredSize * 2;
        for (int i = 0; i < conn->channels; ++i) {
            conn->bufferList->mBuffers[i].mData = std::realloc(conn->bufferList->mBuffers[i].mData, conn->bufferCapacityBytes);
        }
    }

    // Restablecer la topología del buffer en cada ciclo antes del render
    conn->bufferList->mNumberBuffers = conn->channels;
    for (int i = 0; i < conn->channels; ++i) {
        conn->bufferList->mBuffers[i].mNumberChannels = 1;
        conn->bufferList->mBuffers[i].mDataByteSize = requiredSize;
    }

    // IMPORTANTE: En AUHAL, el bus de entrada de hardware es SIEMPRE el Bus 1.
    // El argumento inBusNumber del callback suele llegar como 0 porque el callback
    // se registró en el scope global (elemento 0). Si se pasa 0 a AudioUnitRender,
    // intentará renderizar desde el bus de salida (que está deshabilitado) y fallará con error.
    OSStatus err = AudioUnitRender(conn->unit, ioActionFlags, inTimeStamp, 1, inNumberFrames, conn->bufferList);
    if (err != noErr) {
        static int errCount = 0;
        errCount++;
        if (errCount <= 100) {
            qDebug() << "[CoreAudio Callback] AudioUnitRender error:" << (int)err;
        }
    } else {
        if (conn->needsResampling && conn->resampledBufferList) {
            uint32_t maxOutputFrames = static_cast<uint32_t>(std::ceil(inNumberFrames / conn->timeRatio)) + 2;
            const UInt32 requiredResampledSize = maxOutputFrames * sizeof(float);
            if (requiredResampledSize > conn->resampledCapacityBytes) {
                conn->resampledCapacityBytes = requiredResampledSize * 2;
                for (int i = 0; i < conn->channels; ++i) {
                    conn->resampledBufferList->mBuffers[i].mData = std::realloc(conn->resampledBufferList->mBuffers[i].mData, conn->resampledCapacityBytes);
                }
            }

            float T[8192 + 1];
            uint32_t N = inNumberFrames;
            if (N > 8192) N = 8192;

            uint32_t outputFrames = 0;
            double pos = conn->resamplePhase;
            double R = conn->timeRatio;

            for (int c = 0; c < conn->channels; ++c) {
                T[0] = conn->lastSamples[c];
                float *inData = static_cast<float*>(conn->bufferList->mBuffers[c].mData);
                std::memcpy(T + 1, inData, N * sizeof(float));

                float *outData = static_cast<float*>(conn->resampledBufferList->mBuffers[c].mData);
                outputFrames = 0;
                pos = conn->resamplePhase;
                while (true) {
                    int idx = static_cast<int>(std::floor(pos));
                    if (idx >= (int)N) break;
                    double frac = pos - idx;
                    outData[outputFrames] = (1.0 - frac) * T[idx] + frac * T[idx + 1];
                    outputFrames++;
                    pos += R;
                }
                conn->lastSamples[c] = T[N];
            }
            conn->resamplePhase = pos - N;

            conn->resampledBufferList->mNumberBuffers = conn->channels;
            for (int c = 0; c < conn->channels; ++c) {
                conn->resampledBufferList->mBuffers[c].mNumberChannels = 1;
                conn->resampledBufferList->mBuffers[c].mDataByteSize = outputFrames * sizeof(float);
            }

            conn->manager->processInput(conn->deviceId, conn->resampledBufferList, outputFrames);
        } else {
            conn->manager->processInput(conn->deviceId, conn->bufferList, inNumberFrames);
        }
    }
    return noErr;
}

static OSStatus outputRenderCallback(void *inRefCon,
                                     AudioUnitRenderActionFlags * /*ioActionFlags*/,
                                     const AudioTimeStamp * /*inTimeStamp*/,
                                     UInt32 /*inBusNumber*/,
                                     UInt32 inNumberFrames,
                                     AudioBufferList *ioData)
{
    auto *manager = static_cast<CoreAudioManager*>(inRefCon);
    if (manager) {
        manager->processOutput(ioData, inNumberFrames);
    }
    return noErr;
}

static OSStatus hardwareListener(AudioObjectID /*inObjectID*/,
                                 UInt32 /*inNumberAddresses*/,
                                 const AudioObjectPropertyAddress * /*inAddresses*/,
                                 void *inClientData)
{
    auto *manager = static_cast<CoreAudioManager*>(inClientData);
    if (manager) {
        QMetaObject::invokeMethod(manager, "refreshDevices", Qt::QueuedConnection);
    }
    return noErr;
}

// ============================================================================
//  CoreAudioOutputsModel
// ============================================================================

CoreAudioOutputsModel::CoreAudioOutputsModel(QObject *parent)
    : QAbstractListModel(parent) {}

int CoreAudioOutputsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_outputs.size();
}

QVariant CoreAudioOutputsModel::data(const QModelIndex &index, int role) const
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

QHash<int, QByteArray> CoreAudioOutputsModel::roleNames() const
{
    return {
        {DeviceIdRole,          "deviceId"},
        {DeviceNameRole,        "deviceName"},
        {DeviceDescriptionRole, "deviceDescription"},
        {ChannelCountRole,      "channelCount"},
    };
}

void CoreAudioOutputsModel::setDevices(const QVector<CoreAudioDevice> &outputs)
{
    beginResetModel();
    m_outputs = outputs;
    endResetModel();
}

// ============================================================================
//  CoreAudioManager
// ============================================================================

CoreAudioManager::CoreAudioManager(QObject *parent)
    : QAbstractListModel(parent)
    , m_outputsModel(new CoreAudioOutputsModel(this))
{
    // Escuchar cambios de dispositivos en el sistema
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &addr, hardwareListener, this);

    AudioObjectPropertyAddress defInAddr = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &defInAddr, hardwareListener, this);

    AudioObjectPropertyAddress defOutAddr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &defOutAddr, hardwareListener, this);
}

CoreAudioManager::~CoreAudioManager()
{
    // Remover listeners
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &addr, hardwareListener, this);

    AudioObjectPropertyAddress defInAddr = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &defInAddr, hardwareListener, this);

    AudioObjectPropertyAddress defOutAddr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &defOutAddr, hardwareListener, this);

    stopAll();
    delete m_outputsModel;
}

int CoreAudioManager::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_inputs.size();
}

QVariant CoreAudioManager::data(const QModelIndex &index, int role) const
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

QHash<int, QByteArray> CoreAudioManager::roleNames() const
{
    return {
        {DeviceIdRole,          "deviceId"},
        {DeviceNameRole,        "deviceName"},
        {DeviceDescriptionRole, "deviceDescription"},
        {IsInputRole,           "isInput"},
        {ChannelCountRole,      "channelCount"},
    };
}

void CoreAudioManager::refreshDevices()
{
    QVector<CoreAudioDevice> allInputs;
    QVector<CoreAudioDevice> allOutputs;

    // Dispositivo de entrada por defecto
    AudioObjectID defInId = getDefaultDevice(true);
    QString defInName = defInId != kAudioObjectUnknown ? getStringProperty(defInId, kAudioDevicePropertyDeviceNameCFString) : "";
    CoreAudioDevice defIn;
    defIn.id = QString();
    defIn.name = QStringLiteral("default-input");
    defIn.description = defInName.isEmpty() ? QStringLiteral("Entrada por defecto") : QStringLiteral("Entrada por defecto (%1)").arg(defInName);
    defIn.isInput = true;
    defIn.channelCount = defInId != kAudioObjectUnknown ? getChannelCount(defInId, true) : 2;
    for (int c = 0; c < defIn.channelCount; ++c) {
        defIn.portNames.append(QStringLiteral("Canal %1").arg(c + 1));
    }
    allInputs.append(defIn);

    // Dispositivo de salida por defecto
    AudioObjectID defOutId = getDefaultDevice(false);
    QString defOutName = defOutId != kAudioObjectUnknown ? getStringProperty(defOutId, kAudioDevicePropertyDeviceNameCFString) : "";
    CoreAudioDevice defOut;
    defOut.id = QString();
    defOut.name = QStringLiteral("default-output");
    defOut.description = defOutName.isEmpty() ? QStringLiteral("Salida por defecto") : QStringLiteral("Salida por defecto (%1)").arg(defOutName);
    defOut.isInput = false;
    defOut.channelCount = defOutId != kAudioObjectUnknown ? getChannelCount(defOutId, false) : 2;
    for (int c = 0; c < defOut.channelCount; ++c) {
        defOut.portNames.append(QStringLiteral("Canal %1").arg(c + 1));
    }
    allOutputs.append(defOut);

    // Enumerar dispositivos físicos de CoreAudio
    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize);
    if (status == noErr && dataSize > 0) {
        int deviceCount = dataSize / sizeof(AudioObjectID);
        QVector<AudioObjectID> deviceIDs(deviceCount);
        status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, deviceIDs.data());
        if (status == noErr) {
            for (AudioObjectID id : deviceIDs) {
                QString uid = getStringProperty(id, kAudioDevicePropertyDeviceUID);
                QString name = getStringProperty(id, kAudioDevicePropertyDeviceNameCFString);
                QString manufacturer = getStringProperty(id, kAudioDevicePropertyDeviceManufacturerCFString);
                if (uid.isEmpty()) continue;

                int inputChannels = getChannelCount(id, true);
                int outputChannels = getChannelCount(id, false);

                QString descStr = name;
                if (!manufacturer.isEmpty() && !descStr.startsWith(manufacturer)) {
                    descStr = manufacturer + " - " + name;
                }

                if (inputChannels > 0) {
                    CoreAudioDevice dev;
                    dev.id = uid;
                    dev.name = name;
                    dev.description = descStr;
                    dev.isInput = true;
                    dev.channelCount = inputChannels;
                    for (int c = 0; c < inputChannels; ++c) {
                        dev.portNames.append(QStringLiteral("Canal %1").arg(c + 1));
                    }
                    allInputs.append(dev);
                }

                if (outputChannels > 0) {
                    CoreAudioDevice dev;
                    dev.id = uid;
                    dev.name = name;
                    dev.description = descStr;
                    dev.isInput = false;
                    dev.channelCount = outputChannels;
                    for (int c = 0; c < outputChannels; ++c) {
                        dev.portNames.append(QStringLiteral("Canal %1").arg(c + 1));
                    }
                    allOutputs.append(dev);
                }
            }
        }
    }

    // Firma para evitar reinicios redundantes
    auto signature = [](const QVector<CoreAudioDevice> &a) {
        QStringList keys;
        keys.reserve(a.size());
        for (const auto &d : a) {
            keys << d.id + ":" + QString::number(d.channelCount);
        }
        std::sort(keys.begin(), keys.end());
        return keys.join("|");
    };

    const QString sigIn = signature(allInputs);
    const QString sigOut = signature(allOutputs);
    if (sigIn == m_lastInputsSig && sigOut == m_lastOutputsSig) {
        return;
    }
    m_lastInputsSig = sigIn;
    m_lastOutputsSig = sigOut;

    beginResetModel();
    m_inputs = allInputs;
    endResetModel();

    m_outputsModel->setDevices(allOutputs);

    qDebug() << "[CoreAudio] Dispositivos de entrada actualizados (" << allInputs.size() << ")";
    qDebug() << "[CoreAudio] Dispositivos de salida actualizados (" << allOutputs.size() << ")";

    emit devicesChanged();
}

int CoreAudioManager::deviceChannelCount(const QString &deviceId) const
{
    if (deviceId.isEmpty()) {
        AudioObjectID defId = getDefaultDevice(true);
        return defId != kAudioObjectUnknown ? getChannelCount(defId, true) : 2;
    }
    for (const auto &d : m_inputs) {
        if (d.id == deviceId) return d.channelCount;
    }
    return 0;
}

void CoreAudioManager::setOutputDeviceId(const QString &deviceId)
{
    if (m_outputDeviceId == deviceId) return;
    m_outputDeviceId = deviceId;
    emit outputDeviceChanged();
    
    if (m_running.load()) {
        disconnectPlaybackOutput();
        connectPlaybackOutput();
    }
}

void CoreAudioManager::setAudioEngine(AudioEngine *engine)
{
    if (m_engine == engine) return;
    if (m_engine) disconnect(m_engine, nullptr, this, nullptr);
    m_engine = engine;
    if (m_engine) {
        connect(m_engine, &AudioEngine::playbackStarted,
                this, &CoreAudioManager::onPlaybackStarted);
        connect(m_engine, &AudioEngine::playbackStopped,
                this, &CoreAudioManager::onPlaybackStopped);
        connect(m_engine, &AudioEngine::recordingStarted,
                this, &CoreAudioManager::onPlaybackStarted);
        connect(m_engine, &AudioEngine::recordingStopped,
                this, [this](int){ onPlaybackStopped(); });
        connect(m_engine, &QObject::destroyed, this, [this]() {
            m_engine = nullptr;
        });
    }
}

void CoreAudioManager::setTrackModel(TrackModel *model)
{
    if (m_trackModel == model) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = model;
    if (m_trackModel) {
        connect(m_trackModel, &TrackModel::trackArmedChanged,
                this, &CoreAudioManager::onTrackArmed);
        connect(m_trackModel, &TrackModel::trackDeviceChanged,
                this, &CoreAudioManager::onTrackDeviceChanged);
        connect(m_trackModel, &QObject::destroyed, this, [this]() {
            m_trackModel = nullptr;
        });
    }
}

bool CoreAudioManager::start()
{
    if (m_running.load()) return true;

    m_running.store(true);
    emit runningChanged();
    qDebug() << "[CoreAudio] Inicializando backend...";

    refreshDevices();
    connectPlaybackOutput();

    // Reconectar pistas que ya estén armadas
    if (m_trackModel) {
        const QList<int> armed = m_trackModel->armedTrackIndices();
        for (int trackIndex : armed) {
            connectTrackInput(trackIndex);
        }
    }

    qDebug() << "[CoreAudio] Backend activo";
    return true;
}

void CoreAudioManager::stopAll()
{
    if (!m_running.load()) return;

    m_running.store(false);
    emit runningChanged();

    disconnectPlaybackOutput();

    // Detener y liberar dispositivos de entrada
    QList<InputDeviceConnection*> connectionsToDispose;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_activeInputs.begin(); it != m_activeInputs.end(); ++it) {
            InputDeviceConnection *conn = it.value();
            if (conn) {
                connectionsToDispose.append(conn);
            }
        }
        m_activeInputs.clear();
        m_activeTracks.clear();
    }

    for (InputDeviceConnection *conn : connectionsToDispose) {
        disposeInputConnection(conn);
    }

    qDebug() << "[CoreAudio] Backend detenido";
}

void CoreAudioManager::connectTrackInput(int trackIndex)
{
    if (!m_trackModel) return;
    const auto data = m_trackModel->getTrackData(trackIndex);
    if (data.isEmpty()) return;

    QString deviceId = data.value("inputDevice").toString();
    int mode = data.value("inputMode", 0).toInt();
    int channelIndex = data.value("inputChannelIndex", 0).toInt();
    if (channelIndex < 0) channelIndex = 0;

    InputDeviceConnection *connToDispose = nullptr;
    bool needsStart = false;
    {
        QMutexLocker lock(&m_mutex);

        if (m_activeTracks.contains(trackIndex)) {
            const auto &current = m_activeTracks[trackIndex];
            if (current.deviceId == deviceId) {
                // El dispositivo físico es el mismo, solo actualizamos los parámetros de canal/modo
                TrackInputInfo info;
                info.deviceId = deviceId;
                info.mode = mode;
                info.channelIndex = channelIndex;
                m_activeTracks[trackIndex] = info;

                qDebug() << "[CoreAudio] Pista" << trackIndex << "reconfigurada (mismo dispositivo)" << (deviceId.isEmpty() ? "default" : deviceId) << "canal" << channelIndex << (mode == 0 ? "(Estéreo)" : "(Mono)");
                return;
            }
        }

        connToDispose = disconnectTrackInputInternal(trackIndex);

        TrackInputInfo info;
        info.deviceId = deviceId;
        info.mode = mode;
        info.channelIndex = channelIndex;
        m_activeTracks.insert(trackIndex, info);

        if (m_running.load()) {
            needsStart = true;
        }

        qDebug() << "[CoreAudio] Pista" << trackIndex << "conectada al dispositivo" << (deviceId.isEmpty() ? "default" : deviceId) << "canal" << channelIndex << (mode == 0 ? "(Estéreo)" : "(Mono)");
    }

    if (needsStart) {
        startInputDeviceIfNeeded(deviceId);
    }

    if (connToDispose) {
        disposeInputConnection(connToDispose);
    }
}

void CoreAudioManager::disconnectTrackInput(int trackIndex)
{
    InputDeviceConnection *connToDispose = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        connToDispose = disconnectTrackInputInternal(trackIndex);
    }

    if (connToDispose) {
        disposeInputConnection(connToDispose);
    }

    if (m_trackModel) {
        m_trackModel->updateTrackLevel(trackIndex, 0.0f, 0.0f);
    }
}

CoreAudioManager::InputDeviceConnection* CoreAudioManager::disconnectTrackInputInternal(int trackIndex)
{
    if (!m_activeTracks.contains(trackIndex)) return nullptr;

    QString deviceId = m_activeTracks[trackIndex].deviceId;
    m_activeTracks.remove(trackIndex);

    InputDeviceConnection *conn = stopInputDeviceIfUnused(deviceId);
    qDebug() << "[CoreAudio] Pista" << trackIndex << "desconectada";
    return conn;
}

void CoreAudioManager::startInputDeviceIfNeeded(const QString &deviceId)
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_activeInputs.contains(deviceId)) return;
    }

    AudioObjectID devId = findDeviceByID(deviceId, true);
    if (devId == kAudioObjectUnknown) {
        devId = getDefaultDevice(true);
        if (devId == kAudioObjectUnknown) {
            qWarning() << "[CoreAudio] No se pudo encontrar un dispositivo de entrada para el ID:" << deviceId;
            return;
        }
    }

    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) {
        qWarning() << "[CoreAudio] No se pudo encontrar el componente AUHAL de CoreAudio";
        return;
    }

    AudioUnit inputUnit = nullptr;
    OSStatus err = AudioComponentInstanceNew(comp, &inputUnit);
    if (err != noErr || !inputUnit) {
        qWarning() << "[CoreAudio] Error al crear la instancia de AudioUnit para la entrada";
        return;
    }

    // Habilitar entrada y deshabilitar salida
    UInt32 enableIO = 1;
    AudioUnitSetProperty(inputUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));
    enableIO = 0;
    AudioUnitSetProperty(inputUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enableIO, sizeof(enableIO));

    // Asignar el dispositivo físico a la unidad
    err = AudioUnitSetProperty(inputUnit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &devId, sizeof(devId));
    if (err != noErr) {
        qWarning() << "[CoreAudio] Error al asignar el dispositivo de entrada al AudioUnit";
        AudioComponentInstanceDispose(inputUnit);
        return;
    }

    int numChannels = getChannelCount(devId, true);
    if (numChannels <= 0) numChannels = 2; // fallback de seguridad

    // Consultar la frecuencia de muestreo nominal nativa del dispositivo físico
    Float64 deviceSampleRate = 48000.0;
    AudioObjectPropertyAddress rateAddr = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 rateSize = sizeof(deviceSampleRate);
    OSStatus rateErr = AudioObjectGetPropertyData(devId, &rateAddr, 0, nullptr, &rateSize, &deviceSampleRate);
    if (rateErr == noErr && deviceSampleRate > 0.0) {
        qDebug() << "[CoreAudio] Frecuencia de muestreo nominal nativa detectada:" << deviceSampleRate << "Hz";
    } else {
        qWarning() << "[CoreAudio] No se pudo obtener la frecuencia de muestreo del dispositivo, usando 48000 Hz";
        deviceSampleRate = 48000.0;
    }

    // Configurar formato de salida del AUHAL a float no entrelazado usando la frecuencia nativa
    AudioStreamBasicDescription inputFormat;
    std::memset(&inputFormat, 0, sizeof(inputFormat));
    inputFormat.mSampleRate = deviceSampleRate;
    inputFormat.mFormatID = kAudioFormatLinearPCM;
    inputFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    inputFormat.mBytesPerPacket = sizeof(float);
    inputFormat.mFramesPerPacket = 1;
    inputFormat.mBytesPerFrame = sizeof(float);
    inputFormat.mChannelsPerFrame = numChannels;
    inputFormat.mBitsPerChannel = 32;

    err = AudioUnitSetProperty(inputUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &inputFormat, sizeof(inputFormat));
    if (err != noErr) {
        qWarning() << "[CoreAudio] Error al configurar el formato del stream del AudioUnit de entrada";
        AudioComponentInstanceDispose(inputUnit);
        return;
    }

    // Inicializar la conexión y preparar el AudioBufferList
    InputDeviceConnection *conn = new InputDeviceConnection();
    conn->manager = this;
    conn->unit = inputUnit;
    conn->deviceId = deviceId;
    conn->channels = numChannels;
    
    // Configurar resampler
    conn->needsResampling = !qFuzzyCompare(deviceSampleRate, 48000.0);
    conn->timeRatio = deviceSampleRate / 48000.0;
    conn->resamplePhase = 0.0;
    conn->lastSamples.fill(0.0f, numChannels);

    conn->bufferCapacityBytes = 4096 * sizeof(float);
    conn->bufferList = (AudioBufferList *)std::malloc(sizeof(AudioBufferList) + (numChannels - 1) * sizeof(AudioBuffer));
    conn->bufferList->mNumberBuffers = numChannels;
    for (int i = 0; i < numChannels; ++i) {
        conn->bufferList->mBuffers[i].mNumberChannels = 1;
        conn->bufferList->mBuffers[i].mDataByteSize = conn->bufferCapacityBytes;
        conn->bufferList->mBuffers[i].mData = std::malloc(conn->bufferCapacityBytes);
    }

    if (conn->needsResampling) {
        conn->resampledCapacityBytes = 4096 * sizeof(float);
        conn->resampledBufferList = (AudioBufferList *)std::malloc(sizeof(AudioBufferList) + (numChannels - 1) * sizeof(AudioBuffer));
        conn->resampledBufferList->mNumberBuffers = numChannels;
        for (int i = 0; i < numChannels; ++i) {
            conn->resampledBufferList->mBuffers[i].mNumberChannels = 1;
            conn->resampledBufferList->mBuffers[i].mDataByteSize = conn->resampledCapacityBytes;
            conn->resampledBufferList->mBuffers[i].mData = std::malloc(conn->resampledCapacityBytes);
        }
    } else {
        conn->resampledCapacityBytes = 0;
        conn->resampledBufferList = nullptr;
    }

    // Configurar callback de entrada
    AURenderCallbackStruct callback;
    callback.inputProc = inputCallback;
    callback.inputProcRefCon = conn;
    err = AudioUnitSetProperty(inputUnit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &callback, sizeof(callback));
    if (err != noErr) {
        qWarning() << "[CoreAudio] Error al registrar callback de entrada";
        for (int i = 0; i < numChannels; ++i) std::free(conn->bufferList->mBuffers[i].mData);
        std::free(conn->bufferList);
        if (conn->resampledBufferList) {
            for (int i = 0; i < numChannels; ++i) std::free(conn->resampledBufferList->mBuffers[i].mData);
            std::free(conn->resampledBufferList);
        }
        delete conn;
        AudioComponentInstanceDispose(inputUnit);
        return;
    }

    err = AudioUnitInitialize(inputUnit);
    if (err != noErr) {
        qWarning() << "[CoreAudio] Error al inicializar AudioUnit de entrada";
        for (int i = 0; i < numChannels; ++i) std::free(conn->bufferList->mBuffers[i].mData);
        std::free(conn->bufferList);
        if (conn->resampledBufferList) {
            for (int i = 0; i < numChannels; ++i) std::free(conn->resampledBufferList->mBuffers[i].mData);
            std::free(conn->resampledBufferList);
        }
        delete conn;
        AudioComponentInstanceDispose(inputUnit);
        return;
    }

    err = AudioOutputUnitStart(inputUnit);
    if (err != noErr) {
        qWarning() << "[CoreAudio] Error al arrancar AudioUnit de entrada";
        AudioUnitUninitialize(inputUnit);
        for (int i = 0; i < numChannels; ++i) std::free(conn->bufferList->mBuffers[i].mData);
        std::free(conn->bufferList);
        if (conn->resampledBufferList) {
            for (int i = 0; i < numChannels; ++i) std::free(conn->resampledBufferList->mBuffers[i].mData);
            std::free(conn->resampledBufferList);
        }
        delete conn;
        AudioComponentInstanceDispose(inputUnit);
        return;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_activeInputs.insert(deviceId, conn);
    }
    qDebug() << "[CoreAudio] Dispositivo de entrada arrancado con éxito:" << (deviceId.isEmpty() ? "default" : deviceId);
}

CoreAudioManager::InputDeviceConnection* CoreAudioManager::stopInputDeviceIfUnused(const QString &deviceId)
{
    bool isUsed = false;
    for (const auto &info : m_activeTracks) {
        if (info.deviceId == deviceId) {
            isUsed = true;
            break;
        }
    }

    if (!isUsed && m_activeInputs.contains(deviceId)) {
        return m_activeInputs.take(deviceId);
    }
    return nullptr;
}

void CoreAudioManager::disposeInputConnection(InputDeviceConnection *conn)
{
    if (!conn) return;

    AudioOutputUnitStop(conn->unit);
    AudioUnitUninitialize(conn->unit);
    AudioComponentInstanceDispose(conn->unit);
    for (int i = 0; i < conn->channels; ++i) {
        std::free(conn->bufferList->mBuffers[i].mData);
    }
    std::free(conn->bufferList);
    if (conn->resampledBufferList) {
        for (int i = 0; i < conn->channels; ++i) {
            std::free(conn->resampledBufferList->mBuffers[i].mData);
        }
        std::free(conn->resampledBufferList);
    }
    const QString devId = conn->deviceId;
    delete conn;
    qDebug() << "[CoreAudio] Dispositivo de entrada detenido por falta de uso:" << (devId.isEmpty() ? "default" : devId);
}

void CoreAudioManager::connectPlaybackOutput()
{
    if (!m_running.load()) return;
    
    QMutexLocker lock(&m_mutex);
    if (m_outputUnit) return; // Ya está conectado

    AudioObjectID devId = findDeviceByID(m_outputDeviceId, false);
    if (devId == kAudioObjectUnknown) {
        devId = getDefaultDevice(false);
        if (devId == kAudioObjectUnknown) {
            qWarning() << "[CoreAudio] No se pudo encontrar un dispositivo de salida";
            return;
        }
    }

    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) {
        qWarning() << "[CoreAudio] No se pudo encontrar el componente AUHAL de CoreAudio";
        return;
    }

    OSStatus err = AudioComponentInstanceNew(comp, (AudioUnit*)&m_outputUnit);
    if (err != noErr || !m_outputUnit) {
        qWarning() << "[CoreAudio] Error al crear la instancia de AudioUnit para la salida";
        return;
    }

    // Habilitar salida y deshabilitar entrada
    UInt32 enableIO = 1;
    AudioUnitSetProperty((AudioUnit)m_outputUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enableIO, sizeof(enableIO));
    enableIO = 0;
    AudioUnitSetProperty((AudioUnit)m_outputUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));

    // Asignar dispositivo físico de salida
    err = AudioUnitSetProperty((AudioUnit)m_outputUnit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &devId, sizeof(devId));
    if (err != noErr) {
        qWarning() << "[CoreAudio] Error al asignar el dispositivo de salida al AudioUnit";
        AudioComponentInstanceDispose((AudioUnit)m_outputUnit);
        m_outputUnit = nullptr;
        return;
    }

    int numChannels = getChannelCount(devId, false);
    if (numChannels <= 0) numChannels = 2; // fallback de seguridad

    // Configurar formato: float entrelazado usando la frecuencia de 48000Hz y los canales nativos del dispositivo
    AudioStreamBasicDescription outputFormat;
    std::memset(&outputFormat, 0, sizeof(outputFormat));
    outputFormat.mSampleRate = 48000.0;
    outputFormat.mFormatID = kAudioFormatLinearPCM;
    outputFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    outputFormat.mBytesPerPacket = numChannels * sizeof(float);
    outputFormat.mFramesPerPacket = 1;
    outputFormat.mBytesPerFrame = numChannels * sizeof(float);
    outputFormat.mChannelsPerFrame = numChannels;
    outputFormat.mBitsPerChannel = 32;

    err = AudioUnitSetProperty((AudioUnit)m_outputUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &outputFormat, sizeof(outputFormat));
    if (err != noErr) {
        qWarning() << "[CoreAudio] Error al configurar el formato del AudioUnit de salida";
        AudioComponentInstanceDispose((AudioUnit)m_outputUnit);
        m_outputUnit = nullptr;
        return;
    }

    // Configurar callback de render
    AURenderCallbackStruct callback;
    callback.inputProc = outputRenderCallback;
    callback.inputProcRefCon = this;
    err = AudioUnitSetProperty((AudioUnit)m_outputUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Global, 0, &callback, sizeof(callback));
    if (err != noErr) {
        qWarning() << "[CoreAudio] Error al configurar el callback de salida";
        AudioComponentInstanceDispose((AudioUnit)m_outputUnit);
        m_outputUnit = nullptr;
        return;
    }

    err = AudioUnitInitialize((AudioUnit)m_outputUnit);
    if (err != noErr) {
        qWarning() << "[CoreAudio] Error al inicializar el AudioUnit de salida";
        AudioComponentInstanceDispose((AudioUnit)m_outputUnit);
        m_outputUnit = nullptr;
        return;
    }

    err = AudioOutputUnitStart((AudioUnit)m_outputUnit);
    if (err != noErr) {
        qWarning() << "[CoreAudio] Error al arrancar el AudioUnit de salida";
        AudioUnitUninitialize((AudioUnit)m_outputUnit);
        AudioComponentInstanceDispose((AudioUnit)m_outputUnit);
        m_outputUnit = nullptr;
        return;
    }

    qDebug() << "[CoreAudio] Dispositivo de salida arrancado con éxito:" << (m_outputDeviceId.isEmpty() ? "default" : m_outputDeviceId);
}

void CoreAudioManager::disconnectPlaybackOutput()
{
    AudioUnit unitToDispose = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        if (m_outputUnit) {
            unitToDispose = (AudioUnit)m_outputUnit;
            m_outputUnit = nullptr;
        }
    }
    if (unitToDispose) {
        AudioOutputUnitStop(unitToDispose);
        AudioUnitUninitialize(unitToDispose);
        AudioComponentInstanceDispose(unitToDispose);
        qDebug() << "[CoreAudio] Dispositivo de salida detenido";
    }
}

void CoreAudioManager::processInput(const QString &deviceId, void *ioData, uint32_t frames)
{
    if (!m_engine || !m_trackModel) return;

    AudioBufferList *bufferList = static_cast<AudioBufferList*>(ioData);
    
    // Snapshot the routing info under the lock, then release before calling
    // into the engine.  This avoids holding m_mutex while onAudioInput()
    // acquires TrackModel::m_bufMutex (which would invert the lock order
    // with connectTrackInput on the GUI thread).
    struct PendingInput {
        int trackIdx;
        int channelIndex;
        int mode;
    };
    QVarLengthArray<PendingInput, 8> pending;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_activeTracks.constBegin(); it != m_activeTracks.constEnd(); ++it) {
            const auto &info = it.value();
            if (info.deviceId != deviceId) continue;
            pending.append({it.key(), info.channelIndex, info.mode});
        }
    }
    // m_mutex is now released — safe to call into engine/trackModel.

    constexpr int MAX_CHUNK = 1024;
    float interleaved[MAX_CHUNK * 2];

    for (const auto &p : pending) {
        int chIdx = p.channelIndex;
        if (chIdx < 0 || chIdx >= (int)bufferList->mNumberBuffers) {
            chIdx = 0;
        }

        const float *bufL = nullptr;
        const float *bufR = nullptr;

        if (chIdx >= 0 && chIdx < (int)bufferList->mNumberBuffers) {
            bufL = static_cast<const float*>(bufferList->mBuffers[chIdx].mData);
        }

        if (p.mode == 0) { // Estéreo
            int chR = chIdx + 1;
            if (chR >= 0 && chR < (int)bufferList->mNumberBuffers) {
                bufR = static_cast<const float*>(bufferList->mBuffers[chR].mData);
            } else {
                bufR = bufL;
            }
        } else { // Mono
            bufR = bufL;
        }

        if (!bufL) continue;

        uint32_t done = 0;
        while (done < frames) {
            uint32_t chunk = std::min<uint32_t>(MAX_CHUNK, frames - done);
            for (uint32_t f = 0; f < chunk; ++f) {
                interleaved[f*2 + 0] = bufL[done + f];
                interleaved[f*2 + 1] = bufR ? bufR[done + f] : bufL[done + f];
            }
            m_engine->onAudioInput(p.trackIdx, interleaved, int(chunk));
            done += chunk;
        }
    }
}

void CoreAudioManager::processOutput(void *ioData, uint32_t frames)
{
    AudioBufferList *bufferList = static_cast<AudioBufferList*>(ioData);
    if (!bufferList || bufferList->mNumberBuffers == 0) return;

    float *outBuf = static_cast<float*>(bufferList->mBuffers[0].mData);
    if (!outBuf) return;

    const int outChannels = bufferList->mBuffers[0].mNumberChannels;

    if (m_engine && m_engine->isOutputActive()) {
        constexpr int MAX_CHUNK = 1024;
        float interleaved[MAX_CHUNK * 2];

        uint32_t done = 0;
        while (done < frames) {
            uint32_t chunk = std::min<uint32_t>(MAX_CHUNK, frames - done);
            m_engine->playbackMix(interleaved, int(chunk));
            
            if (outChannels == 2) {
                std::memcpy(outBuf + done * 2, interleaved, chunk * 2 * sizeof(float));
            } else if (outChannels == 1) {
                for (uint32_t f = 0; f < chunk; ++f) {
                    outBuf[done + f] = (interleaved[f * 2 + 0] + interleaved[f * 2 + 1]) * 0.5f;
                }
            } else {
                for (uint32_t f = 0; f < chunk; ++f) {
                    outBuf[(done + f) * outChannels + 0] = interleaved[f * 2 + 0]; // L
                    outBuf[(done + f) * outChannels + 1] = interleaved[f * 2 + 1]; // R
                    for (int c = 2; c < outChannels; ++c) {
                        outBuf[(done + f) * outChannels + c] = 0.0f; // Silenciar el resto
                    }
                }
            }
            done += chunk;
        }
    } else {
        std::memset(outBuf, 0, frames * outChannels * sizeof(float));
    }
}

void CoreAudioManager::onTrackArmed(int trackIndex, bool armed)
{
    if (armed) {
        MacPermissions::requestMicrophoneAccess();
        if (m_trackModel) {
            const auto data = m_trackModel->getTrackData(trackIndex);
            const QString devId = data.value("inputDevice").toString();
            if (devId.isEmpty() && !m_inputs.isEmpty()) {
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

    if (m_engine) {
        m_engine->checkMonitoring();
    }
}

void CoreAudioManager::onTrackDeviceChanged(int trackIndex, const QString & /*deviceId*/)
{
    if (!m_trackModel) return;
    const auto armed = m_trackModel->armedTrackIndices();
    if (armed.contains(trackIndex)) {
        connectTrackInput(trackIndex);
    }
}

void CoreAudioManager::onPlaybackStarted() { connectPlaybackOutput(); }
void CoreAudioManager::onPlaybackStopped() { disconnectPlaybackOutput(); }
