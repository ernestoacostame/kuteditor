#include "WasapiAudioManager.h"
#include "AudioEngine.h"
#include "ui/TrackModel.h"

#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>
#include <algorithm>
#include <chrono>

// Windows COM & Audio API Includes
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h> // Define los GUIDs en esta unidad de traducción
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>

// Macros para liberar COM de forma segura
template <class T>
void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

// ============================================================================
//  WasapiOutputsModel
// ============================================================================
WasapiOutputsModel::WasapiOutputsModel(QObject *parent)
    : QAbstractListModel(parent) {}

int WasapiOutputsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_outputs.size();
}

QVariant WasapiOutputsModel::data(const QModelIndex &index, int role) const
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

QHash<int, QByteArray> WasapiOutputsModel::roleNames() const
{
    return {
        {DeviceIdRole,          "deviceId"},
        {DeviceNameRole,        "deviceName"},
        {DeviceDescriptionRole, "deviceDescription"},
        {ChannelCountRole,      "channelCount"},
    };
}

void WasapiOutputsModel::setDevices(const QVector<WasapiDevice> &outputs)
{
    beginResetModel();
    m_outputs = outputs;
    endResetModel();
}


// ============================================================================
//  WasapiAudioManager
// ============================================================================
WasapiAudioManager::WasapiAudioManager(QObject *parent)
    : QAbstractListModel(parent)
    , m_outputsModel(new WasapiOutputsModel(this))
{
    // Inicializar COM para el hilo de la UI
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        qWarning() << "[WASAPI] CoInitializeEx falló en el constructor:" << hr;
    }

    // Crear el enumerador global de dispositivos
    IMMDeviceEnumerator *pEnumerator = nullptr;
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumerator, (void**)&pEnumerator);
    if (SUCCEEDED(hr)) {
        m_enumerator = pEnumerator;
    } else {
        qCritical() << "[WASAPI] No se pudo crear IMMDeviceEnumerator:" << hr;
    }
}

WasapiAudioManager::~WasapiAudioManager()
{
    stopAll();
    
    IMMDeviceEnumerator *pEnumerator = static_cast<IMMDeviceEnumerator*>(m_enumerator);
    SafeRelease(&pEnumerator);
    m_enumerator = nullptr;

    CoUninitialize();
}

int WasapiAudioManager::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_inputs.size();
}

QVariant WasapiAudioManager::data(const QModelIndex &index, int role) const
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

QHash<int, QByteArray> WasapiAudioManager::roleNames() const
{
    return {
        {DeviceIdRole,          "deviceId"},
        {DeviceNameRole,        "deviceName"},
        {DeviceDescriptionRole, "deviceDescription"},
        {IsInputRole,           "isInput"},
        {ChannelCountRole,      "channelCount"},
    };
}

void WasapiAudioManager::refreshDevices()
{
    QMutexLocker lock(&m_mutex);

    QVector<WasapiDevice> allInputs;
    QVector<WasapiDevice> allOutputs;

    // Dispositivo por defecto
    WasapiDevice defIn;
    defIn.id = QString();
    defIn.name = QStringLiteral("default-input");
    defIn.description = QStringLiteral("Entrada por defecto de Windows");
    defIn.isInput = true;
    defIn.channelCount = 2;
    allInputs.append(defIn);

    WasapiDevice defOut;
    defOut.id = QString();
    defOut.name = QStringLiteral("default-output");
    defOut.description = QStringLiteral("Salida por defecto de Windows");
    defOut.isInput = false;
    defOut.channelCount = 2;
    allOutputs.append(defOut);

    IMMDeviceEnumerator *pEnumerator = static_cast<IMMDeviceEnumerator*>(m_enumerator);
    if (!pEnumerator) return;

    // 1. Enumerar entradas (captura)
    IMMDeviceCollection *pInputsCollection = nullptr;
    HRESULT hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pInputsCollection);
    if (SUCCEEDED(hr)) {
        UINT count = 0;
        pInputsCollection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice *pDevice = nullptr;
            if (SUCCEEDED(pInputsCollection->Item(i, &pDevice))) {
                LPWSTR wstrId = nullptr;
                pDevice->GetId(&wstrId);
                QString id = QString::fromWCharArray(wstrId);
                CoTaskMemFree(wstrId);

                // Obtener nombre amigable
                IPropertyStore *pProps = nullptr;
                QString name = id;
                if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps))) {
                    PROPVARIANT varName;
                    PropVariantInit(&varName);
                    if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName))) {
                        name = QString::fromWCharArray(varName.pwszVal);
                        PropVariantClear(&varName);
                    }
                    SafeRelease(&pProps);
                }

                // Intentar leer cantidad de canales del MixFormat nativo
                int channels = 2;
                IAudioClient *pClient = nullptr;
                if (SUCCEEDED(pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&pClient))) {
                    WAVEFORMATEX *pwfx = nullptr;
                    if (SUCCEEDED(pClient->GetMixFormat(&pwfx))) {
                        channels = pwfx->nChannels;
                        CoTaskMemFree(pwfx);
                    }
                    SafeRelease(&pClient);
                }

                WasapiDevice dev;
                dev.id = id;
                dev.name = name;
                dev.description = name;
                dev.isInput = true;
                dev.channelCount = channels;
                allInputs.append(dev);

                SafeRelease(&pDevice);
            }
        }
        SafeRelease(&pInputsCollection);
    }

    // 2. Enumerar salidas (render)
    IMMDeviceCollection *pOutputsCollection = nullptr;
    hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pOutputsCollection);
    if (SUCCEEDED(hr)) {
        UINT count = 0;
        pOutputsCollection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice *pDevice = nullptr;
            if (SUCCEEDED(pOutputsCollection->Item(i, &pDevice))) {
                LPWSTR wstrId = nullptr;
                pDevice->GetId(&wstrId);
                QString id = QString::fromWCharArray(wstrId);
                CoTaskMemFree(wstrId);

                IPropertyStore *pProps = nullptr;
                QString name = id;
                if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps))) {
                    PROPVARIANT varName;
                    PropVariantInit(&varName);
                    if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName))) {
                        name = QString::fromWCharArray(varName.pwszVal);
                        PropVariantClear(&varName);
                    }
                    SafeRelease(&pProps);
                }

                int channels = 2;
                IAudioClient *pClient = nullptr;
                if (SUCCEEDED(pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&pClient))) {
                    WAVEFORMATEX *pwfx = nullptr;
                    if (SUCCEEDED(pClient->GetMixFormat(&pwfx))) {
                        channels = pwfx->nChannels;
                        CoTaskMemFree(pwfx);
                    }
                    SafeRelease(&pClient);
                }

                WasapiDevice dev;
                dev.id = id;
                dev.name = name;
                dev.description = name;
                dev.isInput = false;
                dev.channelCount = channels;
                allOutputs.append(dev);

                SafeRelease(&pDevice);
            }
        }
        SafeRelease(&pOutputsCollection);
    }

    // Comprobar firmas para evitar refrescos cíclicos en QML
    auto signature = [](const QVector<WasapiDevice> &a) {
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
        return;
    }
    m_lastInputsSig = sigIn;
    m_lastOutputsSig = sigOut;

    beginResetModel();
    m_inputs = allInputs;
    endResetModel();

    m_outputsModel->setDevices(allOutputs);

    qDebug() << "[WASAPI] Refrescar dispositivos completado. Entradas:" << m_inputs.size() << "Salidas:" << allOutputs.size();
    emit devicesChanged();
}

int WasapiAudioManager::deviceChannelCount(const QString &deviceId) const
{
    if (deviceId.isEmpty()) return 2;
    QMutexLocker lock(&m_mutex);
    for (const auto &d : m_inputs) {
        if (d.id == deviceId) return d.channelCount;
    }
    return 0;
}

void WasapiAudioManager::setOutputDeviceId(const QString &deviceId)
{
    if (m_outputDeviceId == deviceId) return;
    m_outputDeviceId = deviceId;
    emit outputDeviceChanged();

    if (m_running.load()) {
        disconnectPlaybackOutput();
        connectPlaybackOutput();
    }
}

void WasapiAudioManager::setAudioEngine(AudioEngine *engine)
{
    if (m_engine == engine) return;
    if (m_engine) disconnect(m_engine, nullptr, this, nullptr);
    m_engine = engine;
    if (m_engine) {
        connect(m_engine, &AudioEngine::playbackStarted, this, &WasapiAudioManager::onPlaybackStarted);
        connect(m_engine, &AudioEngine::playbackStopped, this, &WasapiAudioManager::onPlaybackStopped);
        connect(m_engine, &AudioEngine::recordingStarted, this, &WasapiAudioManager::onPlaybackStarted);
        connect(m_engine, &AudioEngine::recordingStopped, this, [this](int){ onPlaybackStopped(); });
        connect(m_engine, &QObject::destroyed, this, [this]() { m_engine = nullptr; });
    }
}

void WasapiAudioManager::setTrackModel(TrackModel *model)
{
    if (m_trackModel == model) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = model;
    if (m_trackModel) {
        connect(m_trackModel, &TrackModel::trackArmedChanged, this, &WasapiAudioManager::onTrackArmed);
        connect(m_trackModel, &TrackModel::trackDeviceChanged, this, &WasapiAudioManager::onTrackDeviceChanged);
        connect(m_trackModel, &QObject::destroyed, this, [this]() { m_trackModel = nullptr; });
    }
}

bool WasapiAudioManager::start()
{
    if (m_running.load()) return true;

    // Asegurarse de enumerar dispositivos si m_inputs está vacío
    if (m_inputs.size() <= 1) {
        refreshDevices();
    }

    // Iniciar con la salida de playback
    connectPlaybackOutput();

    // Iniciar grabación para pistas ya armadas
    if (m_trackModel) {
        for (int i = 0; i < m_trackModel->rowCount(); ++i) {
            if (m_trackModel->getTrackArmed(i)) {
                connectTrackInput(i);
            }
        }
    }

    m_threadActive.store(true);
    m_audioThread = std::thread(&WasapiAudioManager::runAudioThread, this);

    m_running.store(true);
    emit runningChanged();
    qDebug() << "[WASAPI] Motor iniciado";
    return true;
}

void WasapiAudioManager::stopAll()
{
    if (!m_running.load()) return;

    m_threadActive.store(false);
    if (m_audioThread.joinable()) {
        m_audioThread.join();
    }

    disconnectPlaybackOutput();

    // Detener todas las capturadoras
    QMutexLocker lock(&m_mutex);
    for (auto it = m_activeInputs.begin(); it != m_activeInputs.end(); ++it) {
        InputConnection *conn = it.value();
        if (conn) {
            IAudioClient *pClient = static_cast<IAudioClient*>(conn->client);
            if (pClient) pClient->Stop();
            
            IMMDevice *pDevice = static_cast<IMMDevice*>(conn->device);
            IAudioCaptureClient *pCapture = static_cast<IAudioCaptureClient*>(conn->capture);
            SafeRelease(&pCapture);
            SafeRelease(&pClient);
            SafeRelease(&pDevice);
            delete conn;
        }
    }
    m_activeInputs.clear();

    m_running.store(false);
    emit runningChanged();
    qDebug() << "[WASAPI] Motor detenido";
}

void WasapiAudioManager::connectTrackInput(int trackIndex)
{
    QMutexLocker lock(&m_mutex);
    if (!m_trackModel) return;

    const auto data = m_trackModel->getTrackData(trackIndex);
    if (data.isEmpty()) return;

    QString deviceId = data.value("inputDevice").toString();
    const int mode = data.value("inputMode", 0).toInt(); // 0 = estéreo, 1 = mono
    int channelIndex = data.value("inputChannelIndex", 0).toInt();

    // Si deviceId está vacío, asignar el primero real
    if (deviceId.isEmpty() && m_inputs.size() > 1) {
        deviceId = m_inputs[1].id;
    }

    disconnectTrackInputInternal(trackIndex);

    TrackInputInfo info;
    info.deviceId = deviceId;
    info.mode = mode;
    info.channelIndex = channelIndex;
    m_activeTracks.insert(trackIndex, info);

    // Crear o reutilizar la conexión para este dispositivo
    if (!m_activeInputs.contains(deviceId)) {
        IMMDeviceEnumerator *pEnumerator = static_cast<IMMDeviceEnumerator*>(m_enumerator);
        if (!pEnumerator) return;

        IMMDevice *pDevice = nullptr;
        HRESULT hr = S_OK;

        if (deviceId.isEmpty()) {
            hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
        } else {
            std::wstring wId = deviceId.toStdWString();
            hr = pEnumerator->GetDevice(wId.c_str(), &pDevice);
            if (FAILED(hr)) {
                qWarning() << "[WASAPI] No se pudo obtener dispositivo de captura guardado" << deviceId << "usando el de por defecto.";
                hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
            }
        }

        if (FAILED(hr)) {
            qWarning() << "[WASAPI] No se pudo obtener dispositivo de captura:" << hr << deviceId;
            return;
        }

        IAudioClient *pClient = nullptr;
        hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&pClient);
        if (FAILED(hr)) {
            qWarning() << "[WASAPI] No se pudo activar IAudioClient de captura:" << hr;
            SafeRelease(&pDevice);
            return;
        }

        // Definir formato de mezcla deseado (IEEE float 48kHz estéreo, compatible con AudioEngine)
        WAVEFORMATEXTENSIBLE wfx;
        ZeroMemory(&wfx, sizeof(WAVEFORMATEXTENSIBLE));
        wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wfx.Format.nChannels = 2;
        wfx.Format.nSamplesPerSec = AudioEngine::SAMPLE_RATE;
        wfx.Format.wBitsPerSample = 32;
        wfx.Format.nBlockAlign = wfx.Format.nChannels * wfx.Format.wBitsPerSample / 8;
        wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
        wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        wfx.Samples.wValidBitsPerSample = 32;
        wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

        // 100ms de buffer
        REFERENCE_TIME bufferDuration = 1000000; 
        // Inicializar usando el convertidor de formatos automático nativo de Windows (AUTOCONVERTPCM)
        hr = pClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                 bufferDuration, 0, (WAVEFORMATEX*)&wfx, nullptr);

        if (FAILED(hr)) {
            qWarning() << "[WASAPI] Inicialización de captura falló:" << hr;
            SafeRelease(&pClient);
            SafeRelease(&pDevice);
            return;
        }

        UINT32 bufferFrames = 0;
        pClient->GetBufferSize(&bufferFrames);

        IAudioCaptureClient *pCapture = nullptr;
        hr = pClient->GetService(IID_IAudioCaptureClient, (void**)&pCapture);
        if (FAILED(hr)) {
            qWarning() << "[WASAPI] Error al obtener el servicio IAudioCaptureClient:" << hr;
            SafeRelease(&pClient);
            SafeRelease(&pDevice);
            return;
        }

        hr = pClient->Start();
        if (FAILED(hr)) {
            qWarning() << "[WASAPI] No se pudo iniciar el cliente de captura:" << hr;
            SafeRelease(&pCapture);
            SafeRelease(&pClient);
            SafeRelease(&pDevice);
            return;
        }

        InputConnection *conn = new InputConnection();
        conn->device = pDevice;
        conn->client = pClient;
        conn->capture = pCapture;
        conn->bufferFrames = bufferFrames;
        conn->tempBuffer.resize(1024 * 2); // Buffer inicial de reusabilidad

        m_activeInputs.insert(deviceId, conn);
        qDebug() << "[WASAPI] Conexión de captura iniciada para" << deviceId;
    }
}

void WasapiAudioManager::disconnectTrackInput(int trackIndex)
{
    QMutexLocker lock(&m_mutex);
    disconnectTrackInputInternal(trackIndex);
}

void WasapiAudioManager::disconnectTrackInputInternal(int trackIndex)
{
    if (!m_activeTracks.contains(trackIndex)) return;

    QString devId = m_activeTracks[trackIndex].deviceId;
    m_activeTracks.remove(trackIndex);

    // Si ningún otra pista activa usa este dispositivo, cerrarlo
    bool devInUse = false;
    for (auto it = m_activeTracks.constBegin(); it != m_activeTracks.constEnd(); ++it) {
        if (it.value().deviceId == devId) {
            devInUse = true;
            break;
        }
    }

    if (!devInUse && m_activeInputs.contains(devId)) {
        InputConnection *conn = m_activeInputs[devId];
        m_activeInputs.remove(devId);

        if (conn) {
            IAudioClient *pClient = static_cast<IAudioClient*>(conn->client);
            if (pClient) pClient->Stop();

            IAudioCaptureClient *pCapture = static_cast<IAudioCaptureClient*>(conn->capture);
            IMMDevice *pDevice = static_cast<IMMDevice*>(conn->device);
            SafeRelease(&pCapture);
            SafeRelease(&pClient);
            SafeRelease(&pDevice);
            delete conn;
        }
        qDebug() << "[WASAPI] Conexión de captura liberada para" << devId;
    }

    if (m_trackModel) {
        m_trackModel->updateTrackLevel(trackIndex, 0.0f, 0.0f);
    }
}

void WasapiAudioManager::connectPlaybackOutput()
{
    QMutexLocker lock(&m_mutex);

    IMMDeviceEnumerator *pEnumerator = static_cast<IMMDeviceEnumerator*>(m_enumerator);
    if (!pEnumerator) return;

    IMMDevice *pDevice = nullptr;
    HRESULT hr = S_OK;

    if (m_outputDeviceId.isEmpty()) {
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    } else {
        std::wstring wId = m_outputDeviceId.toStdWString();
        hr = pEnumerator->GetDevice(wId.c_str(), &pDevice);
        if (FAILED(hr)) {
            qWarning() << "[WASAPI] No se pudo obtener dispositivo de salida guardado" << m_outputDeviceId << "usando el de por defecto.";
            hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
        }
    }

    if (FAILED(hr)) {
        qWarning() << "[WASAPI] No se pudo obtener dispositivo de salida:" << hr;
        return;
    }

    IAudioClient *pClient = nullptr;
    hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&pClient);
    if (FAILED(hr)) {
        qWarning() << "[WASAPI] No se pudo activar IAudioClient de salida:" << hr;
        SafeRelease(&pDevice);
        return;
    }

    // 48kHz Float Stereo
    WAVEFORMATEXTENSIBLE wfx;
    ZeroMemory(&wfx, sizeof(WAVEFORMATEXTENSIBLE));
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = 2;
    wfx.Format.nSamplesPerSec = AudioEngine::SAMPLE_RATE;
    wfx.Format.wBitsPerSample = 32;
    wfx.Format.nBlockAlign = wfx.Format.nChannels * wfx.Format.wBitsPerSample / 8;
    wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
    wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    wfx.Samples.wValidBitsPerSample = 32;
    wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

    // Inicializar con unos 60ms de latencia buffer compartida
    REFERENCE_TIME bufferDuration = 600000;
    // Inicializar usando el convertidor de formatos automático nativo de Windows (AUTOCONVERTPCM)
    hr = pClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                             bufferDuration, 0, (WAVEFORMATEX*)&wfx, nullptr);

    if (FAILED(hr)) {
        qWarning() << "[WASAPI] Inicialización de salida falló:" << hr;
        SafeRelease(&pClient);
        SafeRelease(&pDevice);
        return;
    }

    UINT32 bufferFrames = 0;
    pClient->GetBufferSize(&bufferFrames);

    IAudioRenderClient *pRender = nullptr;
    hr = pClient->GetService(IID_IAudioRenderClient, (void**)&pRender);
    if (FAILED(hr)) {
        qWarning() << "[WASAPI] Falló GetService IAudioRenderClient:" << hr;
        SafeRelease(&pClient);
        SafeRelease(&pDevice);
        return;
    }

    hr = pClient->Start();
    if (FAILED(hr)) {
        qWarning() << "[WASAPI] Falló Start en salida:" << hr;
        SafeRelease(&pRender);
        SafeRelease(&pClient);
        SafeRelease(&pDevice);
        return;
    }

    m_outDevice = pDevice;
    m_outClient = pClient;
    m_outRender = pRender;
    m_outBufferFrames = bufferFrames;

    qDebug() << "[WASAPI] Playback configurado exitosamente. Buffer frames:" << bufferFrames;
}

void WasapiAudioManager::disconnectPlaybackOutput()
{
    QMutexLocker lock(&m_mutex);

    IAudioClient *pClient = static_cast<IAudioClient*>(m_outClient);
    if (pClient) pClient->Stop();

    IAudioRenderClient *pRender = static_cast<IAudioRenderClient*>(m_outRender);
    IMMDevice *pDevice = static_cast<IMMDevice*>(m_outDevice);

    SafeRelease(&pRender);
    SafeRelease(&pClient);
    SafeRelease(&pDevice);

    m_outRender = nullptr;
    m_outClient = nullptr;
    m_outDevice = nullptr;
    m_outBufferFrames = 0;

    qDebug() << "[WASAPI] Playback desconectado";
}

void WasapiAudioManager::runAudioThread()
{
    // COM en el hilo secundario
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Ajustar prioridad multimedia en Windows (Pro Audio)
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", nullptr);
    if (!hTask) {
        hTask = AvSetMmThreadCharacteristicsW(L"Audio", nullptr);
    }

    while (m_threadActive.load()) {
        processAudioIO();
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Muestreo de 5ms
    }

    if (hTask) {
        AvRevertMmThreadCharacteristics(hTask);
    }

    CoUninitialize();
}

void WasapiAudioManager::processAudioIO()
{
    // We split the work into two phases:
    // Phase 1 (under lock): interact with WASAPI COM objects and snapshot routing
    // Phase 2 (no lock):    call into AudioEngine (playbackMix / onAudioInput)
    // This avoids holding m_mutex while the engine acquires TrackModel::m_bufMutex,
    // which would invert the lock order with connectTrackInput on the GUI thread.

    // --- Phase 1: gather data under the lock ---
    struct PendingPlayback {
        float *pFloatData;
        UINT32 avail;
        void *pOutRender; // IAudioRenderClient*
    };
    PendingPlayback playback = {nullptr, 0, nullptr};

    struct CapturedChunk {
        QVector<float> data;
        UINT32 numFrames;
    };
    struct PendingInput {
        int trackIdx;
        int mode;
        int channelIndex;
        QVector<float> capturedData;
        UINT32 numFrames;
    };
    QVector<PendingInput> pendingInputs;

    {
        QMutexLocker lock(&m_mutex);

        // 1. Renderizar salida de AudioEngine
        IAudioClient *pOutClient = static_cast<IAudioClient*>(m_outClient);
        IAudioRenderClient *pOutRender = static_cast<IAudioRenderClient*>(m_outRender);
        if (pOutClient && pOutRender && m_engine) {
            UINT32 padding = 0;
            HRESULT hr = pOutClient->GetCurrentPadding(&padding);
            if (SUCCEEDED(hr)) {
                UINT32 avail = m_outBufferFrames - padding;
                if (avail > 0) {
                    BYTE *pData = nullptr;
                    hr = pOutRender->GetBuffer(avail, &pData);
                    if (SUCCEEDED(hr)) {
                        playback.pFloatData = reinterpret_cast<float*>(pData);
                        playback.avail = avail;
                        playback.pOutRender = pOutRender;
                    }
                }
            }
        }

        // 2. Procesar captura para pistas armadas — read and route buffers
        for (auto it = m_activeInputs.begin(); it != m_activeInputs.end(); ++it) {
            QString devId = it.key();
            InputConnection *conn = it.value();
            if (!conn) continue;

            IAudioCaptureClient *pCapture = static_cast<IAudioCaptureClient*>(conn->capture);
            if (!pCapture) continue;

            UINT32 packetSize = 0;
            HRESULT hr = pCapture->GetNextPacketSize(&packetSize);
            while (SUCCEEDED(hr) && packetSize > 0) {
                BYTE *pData = nullptr;
                UINT32 numFramesRead = 0;
                DWORD flags = 0;

                hr = pCapture->GetBuffer(&pData, &numFramesRead, &flags, nullptr, nullptr);
                if (SUCCEEDED(hr) && numFramesRead > 0) {
                    float *pFloatData = reinterpret_cast<float*>(pData);

                    // Asegurarse de tener buffer de ruteo suficiente
                    if (conn->tempBuffer.size() < static_cast<int>(numFramesRead * 2)) {
                        conn->tempBuffer.resize(numFramesRead * 2);
                    }

                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        std::fill(conn->tempBuffer.begin(), conn->tempBuffer.begin() + numFramesRead * 2, 0.0f);
                    } else {
                        std::copy(pFloatData, pFloatData + numFramesRead * 2, conn->tempBuffer.begin());
                    }

                    // Route to matching tracks — snapshot the routed data
                    for (auto tIt = m_activeTracks.constBegin(); tIt != m_activeTracks.constEnd(); ++tIt) {
                        if (tIt.value().deviceId == devId) {
                            PendingInput pi;
                            pi.trackIdx = tIt.key();
                            pi.mode = tIt.value().mode;
                            pi.channelIndex = tIt.value().channelIndex;
                            pi.numFrames = numFramesRead;
                            pi.capturedData.resize(numFramesRead * 2);

                            int startCh = pi.channelIndex;
                            if (pi.mode == 0) { // Estéreo
                                for (UINT32 f = 0; f < numFramesRead; ++f) {
                                    pi.capturedData[f * 2 + 0] = conn->tempBuffer[f * 2 + startCh];
                                    pi.capturedData[f * 2 + 1] = conn->tempBuffer[f * 2 + startCh + 1];
                                }
                            } else { // Mono
                                for (UINT32 f = 0; f < numFramesRead; ++f) {
                                    float sample = conn->tempBuffer[f * 2 + startCh];
                                    pi.capturedData[f * 2 + 0] = sample;
                                    pi.capturedData[f * 2 + 1] = sample;
                                }
                            }
                            pendingInputs.append(std::move(pi));
                        }
                    }

                    pCapture->ReleaseBuffer(numFramesRead);
                }
                hr = pCapture->GetNextPacketSize(&packetSize);
            }
        }
    }
    // m_mutex is now released — safe to call into engine/trackModel.

    // --- Phase 2: call into the engine without holding m_mutex ---
    if (playback.pFloatData && m_engine) {
        m_engine->playbackMix(playback.pFloatData, static_cast<int>(playback.avail));
        static_cast<IAudioRenderClient*>(playback.pOutRender)->ReleaseBuffer(playback.avail, 0);
    }

    for (const auto &pi : pendingInputs) {
        if (m_engine) {
            m_engine->onAudioInput(pi.trackIdx, pi.capturedData.constData(), static_cast<int>(pi.numFrames));
        }
    }
}

void WasapiAudioManager::onTrackArmed(int trackIndex, bool armed)
{
    if (armed) {
        if (m_trackModel) {
            const auto data = m_trackModel->getTrackData(trackIndex);
            QString devId = data.value("inputDevice").toString();
            if (devId.isEmpty() && m_inputs.size() > 1) {
                // Seleccionar primer dispositivo real en el ComboBox de la UI
                m_trackModel->updateInputDevice(trackIndex, m_inputs[1].id, m_inputs[1].name);
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

void WasapiAudioManager::onTrackDeviceChanged(int trackIndex, const QString & /*deviceId*/)
{
    if (!m_trackModel) return;
    if (m_trackModel->getTrackArmed(trackIndex)) {
        connectTrackInput(trackIndex);
    }
}

void WasapiAudioManager::onPlaybackStarted()
{
    // Reconectar playback si fuera necesario
}

void WasapiAudioManager::onPlaybackStopped()
{
    // Acciones al parar
}
