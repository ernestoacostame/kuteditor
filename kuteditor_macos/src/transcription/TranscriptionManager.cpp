#include "TranscriptionManager.h"
#include "ui/TrackModel.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <QProcess>
#include <QCoreApplication>
#include <QTextStream>
#include <QGuiApplication>
#include <QClipboard>
#include <QSettings>
#include <cmath>

// ============================================================================
//  Constructor
// ============================================================================

TranscriptionManager::TranscriptionManager(QObject *parent)
    : QObject(parent)
{
    // Cargar preferencias persistentes
    QSettings settings;
    m_activeModel = settings.value("transcription/activeModel", "base").toString();
    m_language    = settings.value("transcription/language", "auto").toString();

    // Crear el directorio de modelos si no existe
    QDir().mkpath(modelsDir());

    // Detectar GPU al iniciar
    detectGpu();
}

// ============================================================================
//  Detección de GPU en runtime
// ============================================================================
void TranscriptionManager::detectGpu()
{
    m_gpuBackend = "CPU";
    m_gpuName    = "No detectada";

    // --- Método 1: leer /sys/class/drm para obtener el nombre de la GPU ---
    // Esto funciona en Linux sin necesidad de Vulkan ni CUDA instalados.
    QDir drmDir("/sys/class/drm");
    if (drmDir.exists()) {
        const QStringList cards = drmDir.entryList({"card?"}, QDir::Dirs);
        for (const QString &card : cards) {
            // Leer el vendor y device name
            QFile nameFile(drmDir.filePath(card + "/device/product_name"));
            if (!nameFile.exists())
                nameFile.setFileName(drmDir.filePath(card + "/device/label"));
            if (nameFile.open(QIODevice::ReadOnly)) {
                m_gpuName = QString::fromUtf8(nameFile.readAll()).trimmed();
                break;
            }
            // Fallback: parsear uevent
            QFile ueventFile(drmDir.filePath(card + "/device/uevent"));
            if (ueventFile.open(QIODevice::ReadOnly)) {
                const QString uevent = QString::fromUtf8(ueventFile.readAll());
                // PCI_SLOT_NAME o DRIVER puede dar pistas
                if (uevent.contains("amdgpu", Qt::CaseInsensitive)) {
                    m_gpuName = "AMD GPU";
                } else if (uevent.contains("nvidia", Qt::CaseInsensitive)
                        || uevent.contains("nouveau", Qt::CaseInsensitive)) {
                    m_gpuName = "NVIDIA GPU";
                } else if (uevent.contains("i915", Qt::CaseInsensitive)
                        || uevent.contains("xe", Qt::CaseInsensitive)) {
                    m_gpuName = "Intel GPU";
                }
                if (m_gpuName != "No detectada") break;
            }
        }
    }

    // --- Método 2: ejecutar vulkaninfo para nombre exacto y confirmar Vulkan ---
    QProcess vulkanProc;
    vulkanProc.start("vulkaninfo", {"--summary"});
    if (vulkanProc.waitForFinished(3000) && vulkanProc.exitCode() == 0) {
        const QString out = QString::fromUtf8(vulkanProc.readAllStandardOutput());
        for (const QString &line : out.split('\n')) {
            const QString l = line.trimmed();
            if (l.startsWith("deviceName", Qt::CaseInsensitive)) {
                // "deviceName    = AMD Radeon 680M (RADV REMBRANDT)"
                int eq = l.indexOf('=');
                if (eq > 0) {
                    m_gpuName = l.mid(eq + 1).trimmed();
                    m_gpuBackend = "Vulkan";
                }
                break;
            }
        }
    }

    // --- Método 3: detectar CUDA (nvidia-smi) si Vulkan no encontró nada ---
    if (m_gpuBackend == "CPU") {
        QProcess nvProc;
        nvProc.start("nvidia-smi",
                     {"--query-gpu=name", "--format=csv,noheader,nounits"});
        if (nvProc.waitForFinished(3000) && nvProc.exitCode() == 0) {
            const QString name = QString::fromUtf8(nvProc.readAllStandardOutput()).trimmed();
            if (!name.isEmpty()) {
                m_gpuName = name;
                m_gpuBackend = "CUDA";
            }
        }
    }

    qDebug() << "[Transcription] GPU detectada:" << m_gpuName
             << "| Backend:" << m_gpuBackend;
}

// ============================================================================
//  Propiedades
// ============================================================================

bool TranscriptionManager::isAvailable() const
{
#ifdef HAVE_WHISPER
    return true;
#else
    return false;
#endif
}

bool TranscriptionManager::isModelReady() const
{
    return QFile::exists(modelFilePath(m_activeModel));
}

void TranscriptionManager::setActiveModel(const QString &model)
{
    if (m_activeModel == model) return;
    m_activeModel = model;
    QSettings().setValue("transcription/activeModel", model);
    emit modelStatusChanged();
}

void TranscriptionManager::setLanguage(const QString &lang)
{
    if (m_language == lang) return;
    m_language = lang;
    QSettings().setValue("transcription/language", lang);
    emit languageChanged();
}

QString TranscriptionManager::modelsDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/whisper-models";
}

QString TranscriptionManager::modelFilePath(const QString &modelName) const
{
    return modelsDir() + "/ggml-" + modelName + ".bin";
}

QVariantList TranscriptionManager::modelsList() const
{
    QVariantList list;
    for (const auto &m : availableModels()) {
        QVariantMap map;
        map["name"]        = m.name;
        map["displayName"] = m.displayName;
        map["sizeMB"]      = m.sizeMB;
        map["description"] = m.description;
        map["downloaded"]  = QFile::exists(modelFilePath(m.name));
        map["active"]      = (m.name == m_activeModel);
        list.append(map);
    }
    return list;
}

QVector<TranscriptionManager::ModelInfo>
TranscriptionManager::availableModels()
{
    return {
        { "tiny",   "Tiny",   75,  "Más rápido, menor calidad. Ideal para pruebas." },
        { "base",   "Base",   142, "Buen balance velocidad/calidad para podcasts." },
        { "small",  "Small",  466, "Mayor precisión. Recomendado con buena CPU/GPU." },
        { "medium", "Medium", 1500, "Alta precisión. Requiere bastante RAM (~5 GB)." },
        { "large-v3-turbo", "Turbo", 1600, "Rápido y preciso. Variante optimizada de Large. (~6 GB RAM)" },
        { "large-v3", "Large v3", 3100, "Máxima precisión. Requiere mucha RAM (~10 GB)." },
    };
}

QString TranscriptionManager::modelUrl(const QString &modelName)
{
    // Repositorio oficial de ggerganov con modelos GGML
    return QString(
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-%1.bin"
    ).arg(modelName);
}

// ============================================================================
//  Transcripción
// ============================================================================

void TranscriptionManager::transcribeTrack(int trackIndex)
{
#ifndef HAVE_WHISPER
    emit transcriptionError(trackIndex,
        tr("Kut se compiló sin soporte de transcripción.\n"
           "Recompila con -DHAVE_WHISPER=ON y whisper.cpp instalado."));
    return;
#endif

    if (!m_trackModel) {
        emit transcriptionError(trackIndex, tr("TrackModel no disponible."));
        return;
    }
    if (!isModelReady()) {
        emit transcriptionError(trackIndex,
            tr("Modelo '%1' no descargado.\n"
               "Ve a Ajustes > Transcripción para descargarlo.").arg(m_activeModel));
        return;
    }
    if (m_transcriptions.contains(trackIndex) &&
        m_transcriptions[trackIndex].transcribing) {
        qDebug() << "[Transcription] Ya se está transcribiendo la pista" << trackIndex;
        return;
    }

    // Extraer y resamplear el audio
    QVector<float> samples16k = extractAndResample(trackIndex);
    if (samples16k.isEmpty()) {
        emit transcriptionError(trackIndex,
            tr("No hay audio en la pista %1.").arg(trackIndex + 1));
        return;
    }

    // Marcar como transcribiendo
    m_transcriptions[trackIndex].transcribing = true;
    m_transcriptions[trackIndex].progress = 0.0f;
    m_transcriptions[trackIndex].segments.clear();
    emit transcriptionStateChanged(trackIndex);

    // Crear y lanzar worker
    auto *worker = new WhisperWorker(
        modelFilePath(m_activeModel), samples16k, trackIndex, m_language, hasGpu());

    connect(worker, &WhisperWorker::finished,
            this, &TranscriptionManager::onWorkerFinished,
            Qt::QueuedConnection);
    connect(worker, &WhisperWorker::failed,
            this, &TranscriptionManager::onWorkerFailed,
            Qt::QueuedConnection);
    connect(worker, &WhisperWorker::progressChanged,
            this, &TranscriptionManager::onWorkerProgress,
            Qt::QueuedConnection);

    QThreadPool::globalInstance()->start(worker);
    qDebug() << "[Transcription] Iniciada transcripción de pista" << trackIndex
             << "(" << samples16k.size() / 16000.0 << "s de audio)";
}

void TranscriptionManager::cancelTranscription(int trackIndex)
{
    // whisper.cpp no tiene cancel nativo; marcamos como no-transcribiendo
    // y descartamos el resultado cuando llegue.
    if (m_transcriptions.contains(trackIndex)) {
        m_transcriptions[trackIndex].transcribing = false;
        m_transcriptions[trackIndex].progress = 0.0f;
        emit transcriptionStateChanged(trackIndex);
    }
}

bool TranscriptionManager::isTranscribing(int trackIndex) const
{
    auto it = m_transcriptions.constFind(trackIndex);
    return it != m_transcriptions.constEnd() && it->transcribing;
}

float TranscriptionManager::transcriptionProgress(int trackIndex) const
{
    auto it = m_transcriptions.constFind(trackIndex);
    return it != m_transcriptions.constEnd() ? it->progress : 0.0f;
}

bool TranscriptionManager::hasTranscription(int trackIndex) const
{
    auto it = m_transcriptions.constFind(trackIndex);
    return it != m_transcriptions.constEnd() && !it->segments.isEmpty();
}

QVariantList TranscriptionManager::transcriptionSegments(int trackIndex) const
{
    QVariantList list;
    auto it = m_transcriptions.constFind(trackIndex);
    if (it == m_transcriptions.constEnd()) return list;

    for (const auto &seg : it->segments) {
        QVariantMap m;
        m["startSec"] = seg.startSec;
        m["endSec"]   = seg.endSec;
        m["text"]     = seg.text;
        list.append(m);
    }
    return list;
}

void TranscriptionManager::clearTranscription(int trackIndex)
{
    m_transcriptions.remove(trackIndex);
    emit transcriptionStateChanged(trackIndex);
}

QString TranscriptionManager::textAtTime(int trackIndex, double timeSec) const
{
    auto it = m_transcriptions.constFind(trackIndex);
    if (it == m_transcriptions.constEnd()) return {};

    for (const auto &seg : it->segments) {
        if (timeSec >= seg.startSec && timeSec < seg.endSec)
            return seg.text;
    }
    return {};
}

// ============================================================================
//  Export
// ============================================================================

bool TranscriptionManager::exportSRT(int trackIndex, const QString &filePath) const
{
    auto it = m_transcriptions.constFind(trackIndex);
    if (it == m_transcriptions.constEnd() || it->segments.isEmpty())
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    int idx = 1;
    for (const auto &seg : it->segments) {
        // Formato SRT: HH:MM:SS,mmm --> HH:MM:SS,mmm
        auto formatTime = [](double sec) -> QString {
            int totalMs = static_cast<int>(sec * 1000.0);
            int h  = totalMs / 3600000;
            int m  = (totalMs % 3600000) / 60000;
            int s  = (totalMs % 60000) / 1000;
            int ms = totalMs % 1000;
            return QString("%1:%2:%3,%4")
                .arg(h, 2, 10, QChar('0'))
                .arg(m, 2, 10, QChar('0'))
                .arg(s, 2, 10, QChar('0'))
                .arg(ms, 3, 10, QChar('0'));
        };

        out << idx++ << "\n";
        out << formatTime(seg.startSec) << " --> " << formatTime(seg.endSec) << "\n";
        out << seg.text << "\n\n";
    }

    return true;
}

bool TranscriptionManager::exportText(int trackIndex, const QString &filePath) const
{
    auto it = m_transcriptions.constFind(trackIndex);
    if (it == m_transcriptions.constEnd() || it->segments.isEmpty())
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    for (const auto &seg : it->segments) {
        out << seg.text << " ";
    }
    out << "\n";

    return true;
}

// ============================================================================
//  Descarga de modelos
// ============================================================================

void TranscriptionManager::downloadModel(const QString &modelName)
{
    if (m_downloading) {
        emit downloadError(tr("Ya hay una descarga en curso."));
        return;
    }

    const QString url  = modelUrl(modelName);
    const QString dest = modelFilePath(modelName);

    // Crear directorio si no existe
    QDir().mkpath(modelsDir());

    m_downloading = true;
    m_downloadProgress = 0.0f;
    emit downloadStarted(modelName);
    emit modelStatusChanged();

    // Usar QProcess con curl para no bloquear la UI.
    // curl muestra progreso que parseamos para la barra.
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(modelsDir());

    // Descargar a un archivo temporal y renombrar al completar
    const QString tmpDest = dest + ".part";

    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
        // curl con --progress-bar escribe el progreso en stderr
        const QByteArray data = proc->readAllStandardError();
        const QString line = QString::fromUtf8(data).trimmed();
        // Buscar porcentaje en formato "  XX.X%"
        for (const auto &part : line.split(QChar(' '), Qt::SkipEmptyParts)) {
            if (part.endsWith('%')) {
                bool ok = false;
                float pct = part.chopped(1).toFloat(&ok);
                if (ok) {
                    m_downloadProgress = pct / 100.0f;
                    emit downloadProgressChanged(m_downloadProgress);
                }
            }
        }
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, modelName, tmpDest, dest]
            (int exitCode, QProcess::ExitStatus status) {
        proc->deleteLater();
        m_downloading = false;
        m_downloadProgress = 0.0f;

        if (status == QProcess::NormalExit && exitCode == 0) {
            // Renombrar de .part a final
            if (QFile::exists(dest)) QFile::remove(dest);
            if (QFile::rename(tmpDest, dest)) {
                qDebug() << "[Transcription] Modelo descargado:" << modelName;
                emit downloadFinished(modelName);
            } else {
                emit downloadError(tr("Error al mover el archivo descargado."));
            }
        } else {
            // Limpiar archivo parcial
            QFile::remove(tmpDest);
            emit downloadError(
                tr("Error de descarga (código %1). "
                   "Verifica tu conexión a internet.").arg(exitCode));
        }
        emit modelStatusChanged();
    });

    // Ejecutar curl
    proc->start("curl", {
        "-L",                    // seguir redirecciones
        "--progress-bar",        // progreso en stderr
        "-o", tmpDest,           // archivo de salida
        url                      // URL del modelo
    });

    if (!proc->waitForStarted(3000)) {
        proc->deleteLater();
        m_downloading = false;
        emit downloadError(
            tr("No se pudo iniciar curl. Instálalo con:\n"
               "  sudo pacman -S curl"));
        emit modelStatusChanged();
    }
}

void TranscriptionManager::deleteModel(const QString &modelName)
{
    const QString path = modelFilePath(modelName);
    if (QFile::exists(path)) {
        QFile::remove(path);
        qDebug() << "[Transcription] Modelo eliminado:" << modelName;
        emit modelStatusChanged();
    }
}

void TranscriptionManager::checkModelUpdates()
{
    if (m_downloading) {
        emit modelCheckResult(tr("Hay una descarga en curso. Espera a que termine."), false);
        return;
    }

    // Construir lista de modelos descargados con su tamaño local
    struct ModelCheck { QString name; qint64 localSize; };
    QVector<ModelCheck> toCheck;
    for (const auto &m : availableModels()) {
        QFileInfo fi(modelFilePath(m.name));
        if (fi.exists())
            toCheck.append({m.name, fi.size()});
    }

    if (toCheck.isEmpty()) {
        emit modelCheckResult(tr("No hay modelos descargados para verificar."), false);
        return;
    }

    // Estado compartido: un contador atómico y la lista de resultados.
    // Usamos un QObject* con properties para mantenerlo vivo.
    auto *tracker = new QObject(this);
    tracker->setProperty("_total", toCheck.size());
    tracker->setProperty("_done", 0);
    tracker->setProperty("_outdated", QStringList());

    for (const auto &mc : toCheck) {
        auto *proc = new QProcess(tracker); // hijo del tracker → se limpia junto
        const QString name = mc.name;
        const qint64 localSize = mc.localSize;

        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc, tracker, name, localSize]
                (int exitCode, QProcess::ExitStatus) {
            if (exitCode == 0) {
                // curl -sIL vuelca headers a stdout.
                // Buscamos el ÚLTIMO Content-Length (el de la URL final)
                const QString out = QString::fromUtf8(proc->readAllStandardOutput());
                qint64 remoteSize = -1;
                for (const auto &line : out.split('\n')) {
                    const QString l = line.trimmed().toLower();
                    if (l.startsWith("content-length:")) {
                        bool ok;
                        qint64 cl = l.mid(15).trimmed().toLongLong(&ok);
                        if (ok && cl > 1024) // ignorar responses de redirección pequeñas
                            remoteSize = cl;
                    }
                }
                if (remoteSize > 0 && std::abs(localSize - remoteSize) > 1024) {
                    auto list = tracker->property("_outdated").toStringList();
                    list << name;
                    tracker->setProperty("_outdated", list);
                    qDebug() << "[Transcription] Modelo" << name
                             << "local:" << localSize << "remoto:" << remoteSize;
                }
            }

            int done = tracker->property("_done").toInt() + 1;
            tracker->setProperty("_done", done);
            int total = tracker->property("_total").toInt();

            if (done >= total) {
                auto outdated = tracker->property("_outdated").toStringList();
                if (outdated.isEmpty()) {
                    emit modelCheckResult(
                        tr("Todos los modelos están actualizados."), false);
                } else {
                    emit modelCheckResult(
                        tr("Modelos con posible actualización: %1.\n"
                           "Elimínalos y descárgalos de nuevo.")
                        .arg(outdated.join(", ")), true);
                }
                emit modelStatusChanged();
                tracker->deleteLater();
            }
        });

        proc->start("curl", {"-s", "-I", "-L", modelUrl(name)});
        if (!proc->waitForStarted(3000)) {
            proc->deleteLater();
            // Contar como terminado para no bloquear el tracker
            int done = tracker->property("_done").toInt() + 1;
            tracker->setProperty("_done", done);
        }
    }
}

void TranscriptionManager::copyToClipboard(const QString &text) const
{
    qDebug() << "[Transcription] Copiando al portapapeles (" << text.length() << "chars)";
    QGuiApplication::clipboard()->setText(text);
}

QJsonArray TranscriptionManager::transcriptionsToJson() const
{
    QJsonArray arr;
    for (auto it = m_transcriptions.constBegin(); it != m_transcriptions.constEnd(); ++it) {
        const int trackIdx = it.key();
        const TrackTranscription &tt = it.value();
        if (tt.segments.isEmpty()) continue;

        QJsonObject trackObj;
        trackObj["trackIndex"] = trackIdx;

        QJsonArray segsArr;
        for (const auto &seg : tt.segments) {
            QJsonObject s;
            s["startSec"] = seg.startSec;
            s["endSec"]   = seg.endSec;
            s["text"]     = seg.text;
            segsArr.append(s);
        }
        trackObj["segments"] = segsArr;
        arr.append(trackObj);
    }
    return arr;
}

void TranscriptionManager::transcriptionsFromJson(const QJsonArray &arr)
{
    m_transcriptions.clear();
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        const int trackIdx = obj["trackIndex"].toInt(-1);
        if (trackIdx < 0) continue;

        TrackTranscription tt;
        const QJsonArray segs = obj["segments"].toArray();
        tt.segments.reserve(segs.size());
        for (const QJsonValue &sv : segs) {
            const QJsonObject s = sv.toObject();
            TranscriptSegment seg;
            seg.startSec = s["startSec"].toDouble();
            seg.endSec   = s["endSec"].toDouble();
            seg.text     = s["text"].toString();
            tt.segments.append(seg);
        }
        tt.progress = 1.0f;
        tt.transcribing = false;
        m_transcriptions[trackIdx] = tt;
    }
    // Notify all tracks that transcription data changed
    for (auto it = m_transcriptions.constBegin(); it != m_transcriptions.constEnd(); ++it) {
        emit transcriptionFinished(it.key());
    }
}

// ============================================================================
//  Slots internos (resultados del worker)
// ============================================================================

void TranscriptionManager::onWorkerFinished(int trackIndex,
                                             QVector<TranscriptSegment> segments)
{
    auto &data = m_transcriptions[trackIndex];
    // Si fue cancelado mientras corría, descartar
    if (!data.transcribing) {
        qDebug() << "[Transcription] Resultado descartado (cancelado) para pista"
                 << trackIndex;
        return;
    }
    data.segments = std::move(segments);
    data.transcribing = false;
    data.progress = 1.0f;
    emit transcriptionStateChanged(trackIndex);
    emit transcriptionFinished(trackIndex);
}

void TranscriptionManager::onWorkerFailed(int trackIndex, QString error)
{
    auto &data = m_transcriptions[trackIndex];
    data.transcribing = false;
    data.progress = 0.0f;
    emit transcriptionStateChanged(trackIndex);
    emit transcriptionError(trackIndex, error);
}

void TranscriptionManager::onWorkerProgress(int trackIndex, float progress)
{
    if (m_transcriptions.contains(trackIndex)) {
        m_transcriptions[trackIndex].progress = progress;
        emit transcriptionProgressChanged(trackIndex, progress);
    }
}

// ============================================================================
//  Helpers de audio
// ============================================================================

QVector<float> TranscriptionManager::extractAndResample(int trackIndex) const
{
    if (!m_trackModel || !m_trackModel->isValidIndex(trackIndex))
        return {};

    // Obtener el buffer aplanado de la pista (interleaved, típicamente 48 kHz estéreo)
    int frameCount = 0;
    int trackCh    = 0;
    int trackSR    = 0;
    const float *data = m_trackModel->trackSamplesData(trackIndex,
                                                        &frameCount, &trackCh, &trackSR);
    if (!data || frameCount <= 0 || trackCh <= 0 || trackSR <= 0)
        return {};

    // Paso 1: Mezclar a mono
    QVector<float> mono(frameCount);
    for (int f = 0; f < frameCount; ++f) {
        float sum = 0.0f;
        for (int c = 0; c < trackCh; ++c) {
            sum += data[f * trackCh + c];
        }
        mono[f] = sum / trackCh;
    }

    // Paso 2: Resamplear de trackSR a 16000 Hz (lo que espera Whisper)
    // Resample lineal simple — suficiente para speech-to-text.
    const int targetSR = 16000;
    const double ratio = static_cast<double>(targetSR) / trackSR;
    const int outFrames = static_cast<int>(frameCount * ratio);

    QVector<float> resampled(outFrames);
    for (int i = 0; i < outFrames; ++i) {
        double srcPos = i / ratio;
        int idx0 = static_cast<int>(srcPos);
        double frac = srcPos - idx0;
        int idx1 = std::min(idx0 + 1, frameCount - 1);
        resampled[i] = static_cast<float>(
            mono[idx0] * (1.0 - frac) + mono[idx1] * frac);
    }

    float maxVal = 0.0f;
    for (float v : resampled) {
        if (std::abs(v) > maxVal) maxVal = std::abs(v);
    }
    qDebug() << "[Transcription] Audio extraído. Max abs val:" << maxVal;

    return resampled;
}
