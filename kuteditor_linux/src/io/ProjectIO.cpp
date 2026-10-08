#include "ProjectIO.h"
#include "ui/TrackModel.h"
#include "ui/ChapterModel.h"
#include "audio/TrackFxChain.h"
#include "transcription/TranscriptionManager.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QDebug>
#include <QTemporaryDir>
#include <QTimer>
#include <QStandardPaths>
#include <cstring>
#if defined(Q_OS_LINUX)
#include <malloc.h>
#endif

// ============================================================================
//  Construcción / estado
// ============================================================================
ProjectIO::ProjectIO(QObject *parent) : QObject(parent)
{
    // Metadata por defecto (campos nuevos del esquema podcast)
    m_metadata.insert("title",       "");
    m_metadata.insert("podcaster",   "");
    m_metadata.insert("podcast",     "");
    m_metadata.insert("description", "");
    m_metadata.insert("episode",     0);
    m_metadata.insert("season",      0);
    m_metadata.insert("coverPath",   "");

    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setSingleShot(true);
    m_recoveryTimer->setInterval(2000);
    connect(m_recoveryTimer, &QTimer::timeout, this, &ProjectIO::saveRecoveryBackup);
}

ProjectIO::~ProjectIO()
{
    if (m_recoveryTimer) m_recoveryTimer->stop();
}

QString ProjectIO::currentDisplayName() const
{
    if (m_currentPath.isEmpty()) return tr("(sin guardar)");
    // Si el proyecto está en estructura Podcast/Episodio/episodio.kutproj,
    // mostrar "Podcast / Episodio" en vez de solo "episodio".
    const QFileInfo fi(m_currentPath);
    const QDir episodeDir = fi.dir();
    const QString episodeName = episodeDir.dirName();
    QDir podcastDir(episodeDir);
    if (podcastDir.cdUp()) {
        const QString podcastJson = podcastDir.filePath("podcast.json");
        if (QFile::exists(podcastJson)) {
            return podcastDir.dirName() + " / " + episodeName;
        }
    }
    return fi.baseName();
}

void ProjectIO::setMetadata(const QVariantMap &m)
{
    if (m_metadata == m) return;
    m_metadata = m;
    emit metadataChanged();
    setDirty(true);
}

void ProjectIO::setCurrentPath(const QString &p)
{
    if (m_currentPath == p) return;
    m_currentPath = p;
    emit currentPathChanged();
}

void ProjectIO::setDirty(bool d)
{
    if (d && !m_suspendDirty) {
        if (m_recoveryTimer && !m_recoveryTimer->isActive()) {
            qDebug() << "[ProjectIO] starting recovery timer (was not active)";
            m_recoveryTimer->start();
        }
    }

    if (m_dirty == d) return;
    m_dirty = d;
    emit dirtyChanged();
}

void ProjectIO::markDirty() { if (!m_suspendDirty) setDirty(true); }

void ProjectIO::onModelMutated() { if (!m_suspendDirty) setDirty(true); }

void ProjectIO::newProject()
{
    m_suspendDirty = true;
    if (m_trackModel) m_trackModel->reset();
    m_metadata.clear();
    m_metadata.insert("title",       "");
    m_metadata.insert("podcaster",   "");
    m_metadata.insert("podcast",     "");
    m_metadata.insert("description", "");
    m_metadata.insert("episode",     0);
    m_metadata.insert("season",      0);
    m_metadata.insert("coverPath",   "");
    emit metadataChanged();
    setCurrentPath("");
    m_extractDir.reset();  // liberar tempdir del proyecto anterior
    m_suspendDirty = false;
    setDirty(false);
}

// ============================================================================
//  WAV helpers (float32 interleaved)
// ============================================================================
bool ProjectIO::writeWavFloat32(const QString &path,
                                const float *data, qint64 frames,
                                int sampleRate, int channels)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);

    const quint16 audioFormat = 3;  // IEEE float
    const quint16 bitsPerSample = 32;
    const quint32 byteRate = sampleRate * channels * 4;
    const quint16 blockAlign = channels * 4;
    const quint32 dataSize = quint32(frames * channels * 4);
    const quint32 fmtChunkSize = 16;
    const quint32 riffSize = 4 + (8 + fmtChunkSize) + (8 + dataSize);

    f.write("RIFF", 4);
    ds << riffSize;
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    ds << fmtChunkSize;
    ds << audioFormat;
    ds << quint16(channels);
    ds << quint32(sampleRate);
    ds << byteRate;
    ds << blockAlign;
    ds << bitsPerSample;
    f.write("data", 4);
    ds << dataSize;
    f.write(reinterpret_cast<const char*>(data), dataSize);
    return true;
}

bool ProjectIO::readWavFloat32(const QString &path,
                               QVector<float> &outSamples,
                               int &outSampleRate, int &outChannels)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QByteArray riffTag = f.read(4);
    if (riffTag != "RIFF") return false;
    f.read(4);  // riffSize (ignorado)
    QByteArray waveTag = f.read(4);
    if (waveTag != "WAVE") return false;

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);

    quint16 audioFormat = 0, channels = 0, bitsPerSample = 0;
    quint32 sampleRate = 0;
    quint32 dataSize = 0;
    qint64  dataOffset = -1;

    while (!f.atEnd()) {
        QByteArray chunkId = f.read(4);
        if (chunkId.size() < 4) break;
        quint32 chunkSize = 0;
        ds >> chunkSize;
        if (chunkId == "fmt ") {
            ds >> audioFormat;
            quint16 ch; ds >> ch; channels = ch;
            ds >> sampleRate;
            quint32 byteRate; ds >> byteRate;
            quint16 blockAlign; ds >> blockAlign;
            ds >> bitsPerSample;
            // Saltar extra
            if (chunkSize > 16) f.read(chunkSize - 16);
        } else if (chunkId == "data") {
            dataSize = chunkSize;
            dataOffset = f.pos();
            f.seek(f.pos() + chunkSize);
        } else {
            // skip chunk + padding si impar
            f.read(chunkSize);
            if (chunkSize & 1) f.read(1);
        }
    }

    if (audioFormat != 3 || bitsPerSample != 32) {
        qWarning() << "[ProjectIO] WAV no es float32:" << path
                   << "format=" << audioFormat << "bits=" << bitsPerSample;
        return false;
    }
    if (dataOffset < 0) return false;

    outSampleRate = int(sampleRate);
    outChannels   = int(channels);

    // Robustez: si la cabecera dice dataSize = 0, o si es inconsistente con el tamaño real del archivo,
    // usamos la cantidad de bytes que realmente quedan en el archivo.
    qint64 fileSize = f.size();
    qint64 bytesRemaining = fileSize - dataOffset;
    if (dataSize == 0 || dataSize > bytesRemaining) {
        dataSize = quint32(std::max<qint64>(0, bytesRemaining));
    }

    const qint64 sampleCount = dataSize / 4;
    outSamples.resize(int(sampleCount));
    f.seek(dataOffset);
    const qint64 got = f.read(reinterpret_cast<char*>(outSamples.data()), dataSize);

    // Si se leyó menos (por ejemplo, archivo truncado), redimensionar al tamaño real leído
    if (got < dataSize) {
        outSamples.resize(int(got / 4));
    }

    // Retornamos true si leímos algo de audio
    return !outSamples.isEmpty();
}

// ============================================================================
//  TAR (ustar, uncompressed) — implementación mínima
// ============================================================================
namespace {

constexpr int TAR_BLOCK = 512;

struct TarHeader {
    char name[100];      // 0
    char mode[8];        // 100
    char uid[8];         // 108
    char gid[8];         // 116
    char size[12];       // 124
    char mtime[12];      // 136
    char chksum[8];      // 148
    char typeflag;       // 156
    char linkname[100];  // 157
    char magic[6];       // 257
    char version[2];     // 263
    char uname[32];      // 265
    char gname[32];      // 297
    char devmajor[8];    // 329
    char devminor[8];    // 337
    char prefix[155];    // 345
    char padding[12];    // 500
};
static_assert(sizeof(TarHeader) == TAR_BLOCK, "Tar header must be 512 bytes");

static void writeOctal(char *dst, int width, quint64 value) {
    // Escribir en octal con padding de '0' + null terminator en la última pos
    std::memset(dst, '0', width);
    dst[width - 1] = '\0';
    int i = width - 2;
    while (value > 0 && i >= 0) {
        dst[i--] = char('0' + (value & 7));
        value >>= 3;
    }
}

static QByteArray buildTarHeader(const QString &name, quint64 size) {
    QByteArray blk(TAR_BLOCK, '\0');
    TarHeader *h = reinterpret_cast<TarHeader*>(blk.data());
    const QByteArray nm = name.toUtf8().left(99);
    std::memcpy(h->name, nm.constData(), nm.size());
    writeOctal(h->mode,  sizeof(h->mode),  0644);
    writeOctal(h->uid,   sizeof(h->uid),   0);
    writeOctal(h->gid,   sizeof(h->gid),   0);
    writeOctal(h->size,  sizeof(h->size),  size);
    writeOctal(h->mtime, sizeof(h->mtime), 0);
    // checksum: primero rellenar con ' ' y luego calcular
    std::memset(h->chksum, ' ', sizeof(h->chksum));
    h->typeflag = '0';  // regular file
    std::memcpy(h->magic,   "ustar", 5);
    std::memcpy(h->version, "00", 2);

    // checksum: suma de todos los bytes del header
    quint32 sum = 0;
    const unsigned char *p = reinterpret_cast<const unsigned char*>(blk.constData());
    for (int i = 0; i < TAR_BLOCK; ++i) sum += p[i];
    writeOctal(h->chksum, sizeof(h->chksum) - 1, sum);
    h->chksum[6] = '\0';
    h->chksum[7] = ' ';

    return blk;
}

static qint64 parseOctal(const char *s, int maxLen) {
    qint64 v = 0;
    for (int i = 0; i < maxLen; ++i) {
        char c = s[i];
        if (c == 0 || c == ' ') break;
        if (c < '0' || c > '7') return -1;
        v = (v << 3) | (c - '0');
    }
    return v;
}

} // namespace

bool ProjectIO::writeTarball(const QString &path,
                             const QByteArray &projectJson,
                             const QList<QPair<QString, QByteArray>> &files) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    auto writeEntry = [&](const QString &name, const QByteArray &data) -> bool {
        f.write(buildTarHeader(name, quint64(data.size())));
        f.write(data);
        const int pad = (TAR_BLOCK - (data.size() % TAR_BLOCK)) % TAR_BLOCK;
        if (pad > 0) f.write(QByteArray(pad, '\0'));
        return true;
    };

    if (!writeEntry("project.json", projectJson)) return false;
    for (const auto &kv : files) {
        if (!writeEntry(kv.first, kv.second)) return false;
    }
    // Tar acaba con 2 bloques de ceros
    f.write(QByteArray(TAR_BLOCK * 2, '\0'));
    return true;
}

bool ProjectIO::readTarball(const QString &path,
                            QByteArray &projectJson,
                            const QString &sourcesDir) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QDir().mkpath(sourcesDir);

    char buffer[65536];

    while (!f.atEnd()) {
        QByteArray blk = f.read(TAR_BLOCK);
        if (blk.size() < TAR_BLOCK) break;
        // Bloque de ceros = fin
        bool allZero = true;
        for (char c : blk) { if (c != 0) { allZero = false; break; } }
        if (allZero) break;

        const TarHeader *h = reinterpret_cast<const TarHeader*>(blk.constData());
        QString name = QString::fromUtf8(h->name, qstrnlen(h->name, 100));
        const qint64 sz = parseOctal(h->size, sizeof(h->size));
        if (sz < 0) return false;

        const int pad = (TAR_BLOCK - (sz % TAR_BLOCK)) % TAR_BLOCK;

        if (name == "project.json") {
            projectJson = f.read(sz);
            if (projectJson.size() != sz) return false;
        } else if (name.startsWith("sources/") || name.startsWith("cover")) {
            const QString destPath = sourcesDir + "/" + QFileInfo(name).fileName();
            QFile out(destPath);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return false;
            }
            qint64 bytesLeft = sz;
            while (bytesLeft > 0) {
                const qint64 chunk = std::min<qint64>(bytesLeft, sizeof(buffer));
                const qint64 rd = f.read(buffer, chunk);
                if (rd <= 0) return false;
                out.write(buffer, rd);
                bytesLeft -= rd;
            }
        } else {
            // Ignorar archivo desconocido sin alojar memoria
            qint64 bytesLeft = sz;
            while (bytesLeft > 0) {
                const qint64 chunk = std::min<qint64>(bytesLeft, sizeof(buffer));
                const qint64 rd = f.read(buffer, chunk);
                if (rd <= 0) return false;
                bytesLeft -= rd;
            }
        }

        if (pad > 0) f.read(pad);
    }
    return !projectJson.isEmpty();
}

// ============================================================================
//  JSON: construir y aplicar
// ============================================================================
QByteArray ProjectIO::buildProjectJson() const
{
    QJsonObject root;
    root["version"] = 1;

    // Metadata
    QJsonObject md;
    for (auto it = m_metadata.constBegin(); it != m_metadata.constEnd(); ++it) {
        md[it.key()] = QJsonValue::fromVariant(it.value());
    }
    root["metadata"] = md;

    // Tracks
    QJsonArray tracksArr;
    const int n = m_trackModel ? m_trackModel->count() : 0;
    int globalSR = 48000;
    for (int i = 0; i < n; ++i) {
        const QVariantMap tm = m_trackModel->getTrackData(i);
        QJsonObject t;
        t["name"]              = tm.value("name").toString();
        t["color"]             = tm.value("color").value<QColor>().name();
        t["inputDeviceId"]     = tm.value("inputDevice").toString();
        t["inputDeviceName"]   = tm.value("inputDeviceName").toString();
        t["inputMode"]         = tm.value("inputMode").toInt();
        t["inputChannelIndex"] = tm.value("inputChannelIndex").toInt();
        t["gain"]              = tm.value("gain").toDouble();
        t["pan"]               = tm.value("pan").toDouble();
        t["muted"]             = tm.value("muted").toBool();
        t["solo"]              = tm.value("solo").toBool();

        // Sources
        QJsonArray srcArr;
        const int srcN = m_trackModel->trackSourceCount(i);
        for (int s = 0; s < srcN; ++s) {
            int sr = 48000, ch = 2;
            (void)m_trackModel->trackSourceSamples(i, s, &sr, &ch);
            QJsonObject so;
            so["id"]         = s;
            so["file"]       = QString("sources/track%1_src%2.wav").arg(i).arg(s);
            so["sampleRate"] = sr;
            so["channels"]   = ch;
            srcArr.append(so);
            globalSR = sr;
        }
        t["sources"] = srcArr;

        // Clips
        QJsonArray clipArr;
        const QVariantList clips = m_trackModel->clipsOf(i);
        const double sr = globalSR;
        for (const QVariant &cv : clips) {
            const QVariantMap cm = cv.toMap();
            QJsonObject co;
            co["sourceIdx"]     = cm.value("sourceIdx").toInt();
            co["timelineStart"] = qint64(cm.value("startSec").toDouble() * sr);
            co["sourceOffset"]  = qint64(cm.value("sourceOffsetSec").toDouble() * sr);
            co["length"]        = qint64(cm.value("lengthSec").toDouble() * sr);
            co["fadeInLen"]     = qint64(cm.value("fadeInSec", 0).toDouble()  * sr);
            co["fadeOutLen"]    = qint64(cm.value("fadeOutSec", 0).toDouble() * sr);
            co["gain"]          = cm.value("gain", 1.0).toDouble();
            if (cm.value("muted").toBool())
                co["muted"]     = true;
            if (m_trackModel->isSourceRecording(i, cm.value("sourceIdx").toInt()))
                co["isRecording"] = true;
            QJsonArray envArr;
            const QVariantList envList = cm.value("envelope").toList();
            for (const QVariant &v : envList) {
                const QVariantMap n = v.toMap();
                QJsonObject node;
                node["x"] = n.value("x").toDouble();
                node["y"] = n.value("y").toDouble();
                envArr.append(node);
            }
            co["envelope"] = envArr;
            clipArr.append(co);
        }
        t["clips"] = clipArr;

        // Cadena de efectos por pista
        TrackFxChain *fxc = m_trackModel->trackFxChain(i);
        if (fxc && fxc->hasActiveEffects()) {
            t["fxChain"] = fxc->toJson();
        }

        tracksArr.append(t);
    }
    root["sampleRate"] = globalSR;
    root["tracks"] = tracksArr;

    // Transcripciones
    if (m_transcription) {
        const QJsonArray transArr = m_transcription->transcriptionsToJson();
        if (!transArr.isEmpty())
            root["transcriptions"] = transArr;
    }

    // Capítulos
    if (m_chapterModel && m_chapterModel->count() > 0) {
        root["chapters"] = m_chapterModel->toJson();
    }

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool ProjectIO::applyProjectJson(const QByteArray &json, const QString &extractDir, bool isRecovery)
{
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) {
        m_lastError = tr("JSON inválido: %1").arg(err.errorString());
        return false;
    }
    const QJsonObject root = doc.object();

    // Limpiar modelo actual
    if (m_trackModel) m_trackModel->reset();

    // Metadata
    const QJsonObject md = root.value("metadata").toObject();
    m_metadata.clear();
    for (auto it = md.constBegin(); it != md.constEnd(); ++it) {
        m_metadata.insert(it.key(), it.value().toVariant());
    }
    // Si el proyecto traía una portada embebida, apuntar coverPath al
    // archivo extraído en el tempdir. readTarball ya lo dejó ahí.
    const QString coverName = m_metadata.value("_coverFileInProject").toString();
    if (!coverName.isEmpty()) {
        const QString extracted = extractDir + "/" + coverName;
        if (QFileInfo(extracted).isFile()) {
            m_metadata.insert("coverPath", extracted);
        }
    }
    emit metadataChanged();

    // Tracks
    const QJsonArray tracksArr = root.value("tracks").toArray();
    for (int i = 0; i < tracksArr.size(); ++i) {
        const QJsonObject t = tracksArr[i].toObject();
        QVariantMap trackMeta;
        trackMeta["name"]              = t.value("name").toString();
        trackMeta["color"]             = t.value("color").toString();
        trackMeta["inputDeviceId"]     = t.value("inputDeviceId").toString();
        trackMeta["inputDeviceName"]   = t.value("inputDeviceName").toString();
        trackMeta["inputMode"]         = t.value("inputMode").toInt();
        trackMeta["inputChannelIndex"] = t.value("inputChannelIndex").toInt(-1);
        trackMeta["gain"]              = t.value("gain").toDouble(1.0);
        trackMeta["pan"]               = t.value("pan").toDouble(0.0);
        trackMeta["muted"]             = t.value("muted").toBool();
        trackMeta["solo"]              = t.value("solo").toBool();

        const int trackIdx = m_trackModel->addTrackFromSnapshot(trackMeta);
        if (trackIdx < 0) continue;

        // Sources: cargar WAV y añadir
        const QJsonArray srcArr = t.value("sources").toArray();
        for (int s = 0; s < srcArr.size(); ++s) {
            const QJsonObject so = srcArr[s].toObject();
            const QString relFile = so.value("file").toString();
            const QString absFile = extractDir + "/" + QFileInfo(relFile).fileName();
            QVector<float> samples;
            int sr = 48000, ch = 2;
            if (!readWavFloat32(absFile, samples, sr, ch)) {
                qWarning() << "[ProjectIO] No pude leer source:" << absFile;
                if (isRecovery) {
                    // Si es una recuperación de emergencia, creamos un source vacío de silencio de 1 segundo
                    // en vez de abortar todo el proyecto.
                    samples.fill(0.0f, sr * ch);
                    qWarning() << "[ProjectIO] Modo recuperación: Rellenando con silencio para" << absFile;
                } else {
                    m_lastError = tr("No se pudo leer el archivo de audio: %1").arg(absFile);
                    return false;
                }
            }
            const int createdIdx = m_trackModel->addSourceToTrack(trackIdx, samples, sr, ch);
            if (createdIdx != s) {
                qWarning() << "[ProjectIO] Desajuste sourceIdx esperado=" << s
                           << "creado=" << createdIdx;
            }
        }

        // Clips
        const QJsonArray clipArr = t.value("clips").toArray();
        for (const QJsonValue &cv : clipArr) {
            const QJsonObject co = cv.toObject();
            QVariantList envList;
            if (co.contains("envelope")) {
                const QJsonArray envArr = co.value("envelope").toArray();
                for (const QJsonValue &nv : envArr) {
                    const QJsonObject node = nv.toObject();
                    QVariantMap n;
                    n["x"] = node.value("x").toDouble();
                    n["y"] = node.value("y").toDouble();
                    envList.append(n);
                }
            }
            int srcIdx = co.value("sourceIdx").toInt(-1);
            qint64 length = qint64(co.value("length").toDouble());
            if (co.value("isRecording").toBool(false)) {
                int sr = 48000, ch = 2;
                QVector<float> sourceSamples = m_trackModel->trackSourceSamples(trackIdx, srcIdx, &sr, &ch);
                if (ch > 0 && !sourceSamples.isEmpty()) {
                    length = sourceSamples.size() / ch;
                    qDebug() << "[ProjectIO] Sobrescribiendo clip length en recuperación para source" << srcIdx << "a" << length << "frames";
                }
            }

            m_trackModel->addClipRaw(trackIdx,
                srcIdx,
                qint64(co.value("timelineStart").toDouble()),
                qint64(co.value("sourceOffset").toDouble()),
                length,
                qint64(co.value("fadeInLen").toDouble()),
                qint64(co.value("fadeOutLen").toDouble()),
                float(co.value("gain").toDouble(1.0)),
                envList);
            // Restaurar mute por clip
            if (co.value("muted").toBool(false)) {
                const int cIdx = m_trackModel->clipsOf(trackIdx).size() - 1;
                if (cIdx >= 0)
                    m_trackModel->setClipMuted(trackIdx, cIdx, true);
            }
        }
        m_trackModel->sortClips(trackIdx);

        // Restaurar cadena de efectos por pista
        if (t.contains("fxChain")) {
            TrackFxChain *fxc = m_trackModel->trackFxChain(trackIdx);
            if (fxc) fxc->fromJson(t.value("fxChain").toObject());
        }
    }

    // Restaurar transcripciones
    if (m_transcription && root.contains("transcriptions")) {
        m_transcription->transcriptionsFromJson(root.value("transcriptions").toArray());
    }

    // Restaurar capítulos
    if (m_chapterModel) {
        if (root.contains("chapters"))
            m_chapterModel->fromJson(root.value("chapters").toArray());
        else
            m_chapterModel->clear();
    }

    return true;
}

// ============================================================================
//  save / open
// ============================================================================
bool ProjectIO::save(const QString &path)
{
    if (!m_trackModel) {
        m_lastError = tr("TrackModel no disponible");
        return false;
    }

    const QString outPath = path.isEmpty() ? m_currentPath : path;
    if (outPath.isEmpty()) {
        m_lastError = tr("No se indicó ruta de guardado");
        return false;
    }

    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = tr("Fallo al abrir el archivo de guardado");
        return false;
    }

    auto writeTarEntry = [&](const QString &name, const char *data, qint64 size) -> bool {
        f.write(buildTarHeader(name, quint64(size)));
        f.write(data, size);
        const int pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
        if (pad > 0) f.write(QByteArray(pad, '\0'));
        return true;
    };

    // Portada: si metadata.coverPath apunta a un archivo existente, incluirlo
    // en el tarball como cover.<ext>. Reescribir metadata.coverPath al nombre
    // relativo para que al reabrir se busque dentro del tarball.
    QString coverNameInTar;
    QByteArray coverBytes;
    const QString coverPathOrig = m_metadata.value("coverPath").toString();
    if (!coverPathOrig.isEmpty() && QFileInfo(coverPathOrig).isFile()) {
        QFile cf(coverPathOrig);
        if (cf.open(QIODevice::ReadOnly)) {
            coverBytes = cf.readAll();
            cf.close();
            const QString ext = QFileInfo(coverPathOrig).suffix().toLower();
            coverNameInTar = ext.isEmpty() ? QStringLiteral("cover")
                                            : QStringLiteral("cover.") + ext;
            // En project.json guardamos el path ORIGINAL para que el autor
            // sepa de dónde vino; pero al abrir, sobrescribimos con la ruta
            // del archivo extraído (ver applyProjectJson).
            m_metadata.insert("_coverFileInProject", coverNameInTar);
        }
    } else {
        m_metadata.remove("_coverFileInProject");
    }

    const QByteArray projectJson = buildProjectJson();
    if (!writeTarEntry("project.json", projectJson.constData(), projectJson.size())) {
        m_lastError = tr("Fallo al escribir project.json en el proyecto");
        f.close();
        return false;
    }

    const int nTracks = m_trackModel->count();
    int filesSavedCount = 0;
    for (int i = 0; i < nTracks; ++i) {
        const int nSrc = m_trackModel->trackSourceCount(i);
        for (int s = 0; s < nSrc; ++s) {
            int sr = 48000, ch = 2;
            QVector<float> samples = m_trackModel->trackSourceSamples(i, s, &sr, &ch);
            if (samples.isEmpty()) continue;

            const qint64 frames = samples.size() / ch;
            const quint32 dataSize = quint32(frames * ch * 4);
            const quint32 wavSize = 44 + dataSize;
            const QString nameInTar = QString("sources/track%1_src%2.wav").arg(i).arg(s);

            // Escribir cabecera tar
            f.write(buildTarHeader(nameInTar, quint64(wavSize)));

            // Escribir WAV header a un buffer temporal en memoria (44 bytes)
            QByteArray wavHeader;
            QDataStream ds(&wavHeader, QIODevice::WriteOnly);
            ds.setByteOrder(QDataStream::LittleEndian);

            const quint16 audioFormat = 3;  // IEEE float
            const quint16 bitsPerSample = 32;
            const quint32 byteRate = sr * ch * 4;
            const quint16 blockAlign = ch * 4;
            const quint32 fmtChunkSize = 16;
            const quint32 riffSize = 4 + (8 + fmtChunkSize) + (8 + dataSize);

            ds.writeRawData("RIFF", 4);
            ds << riffSize;
            ds.writeRawData("WAVE", 4);
            ds.writeRawData("fmt ", 4);
            ds << fmtChunkSize;
            ds << audioFormat;
            ds << quint16(ch);
            ds << quint32(sr);
            ds << byteRate;
            ds << blockAlign;
            ds << bitsPerSample;
            ds.writeRawData("data", 4);
            ds << dataSize;

            f.write(wavHeader);

            // Escribir datos float
            f.write(reinterpret_cast<const char*>(samples.constData()), dataSize);

            // Padding del tar
            const int pad = (TAR_BLOCK - (wavSize % TAR_BLOCK)) % TAR_BLOCK;
            if (pad > 0) f.write(QByteArray(pad, '\0'));

            filesSavedCount++;
        }
    }

    if (!coverNameInTar.isEmpty() && !coverBytes.isEmpty()) {
        if (!writeTarEntry(coverNameInTar, coverBytes.constData(), coverBytes.size())) {
            m_lastError = tr("Fallo al escribir portada");
            f.close();
            return false;
        }
    }

    // Tar acaba con 2 bloques de ceros
    f.write(QByteArray(TAR_BLOCK * 2, '\0'));
    f.close();

    setCurrentPath(outPath);
    setDirty(false);
    discardRecovery();
    emit projectSaved();
    qDebug() << "[ProjectIO] Proyecto guardado (streaming):" << outPath
             << "(" << nTracks << "pistas,"
             << filesSavedCount << "sources )";
    return true;
}

bool ProjectIO::open(const QString &path)
{
    if (!m_trackModel) {
        m_lastError = tr("TrackModel no disponible");
        return false;
    }

    // Crear tempdir como miembro para que los archivos extraídos (portada,
    // WAVs) sigan vivos mientras el proyecto esté cargado. Se reemplaza al
    // abrir otro proyecto o llamar newProject().
    m_extractDir.reset(new QTemporaryDir());
    if (!m_extractDir->isValid()) {
        m_lastError = tr("No pude crear directorio temporal");
        m_extractDir.reset();
        return false;
    }

    QByteArray projectJson;
    bool isTarball = readTarball(path, projectJson, m_extractDir->path());

    if (!isTarball) {
        // Fallback: intentar leer como JSON plano (proyecto nuevo creado
        // desde la biblioteca, aún sin sources/WAVs).
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            projectJson = f.readAll();
            f.close();
            // Verificar que sea JSON válido
            QJsonParseError err{};
            QJsonDocument::fromJson(projectJson, &err);
            if (err.error != QJsonParseError::NoError) {
                m_lastError = tr("Archivo inválido o corrupto");
                m_extractDir.reset();
                return false;
            }
        } else {
            m_lastError = tr("No se pudo abrir el archivo");
            m_extractDir.reset();
            return false;
        }
    }

    m_suspendDirty = true;
    const bool ok = applyProjectJson(projectJson, m_extractDir->path());
    m_suspendDirty = false;

    if (!ok) {
        m_extractDir.reset();
        return false;
    }

    // Sincronizar metadata desde podcast.json del directorio padre.
    // Estructura: .../MiPodcast/Episodio1/MiPodcast.kutproj
    // → podcast.json está en .../MiPodcast/podcast.json
    const QFileInfo fi(path);
    QDir episodeDir = fi.dir();            // .../MiPodcast/Episodio1
    QDir podcastDir(episodeDir);
    if (podcastDir.cdUp()) {               // .../MiPodcast
        const QString podcastJsonPath = podcastDir.filePath("podcast.json");
        if (QFile::exists(podcastJsonPath)) {
            QFile pf(podcastJsonPath);
            if (pf.open(QIODevice::ReadOnly)) {
                QJsonDocument pdoc = QJsonDocument::fromJson(pf.readAll());
                pf.close();
                const QJsonObject pobj = pdoc.object();
                // Sincronizar campos del podcast → metadata del proyecto
                // (solo si el proyecto no los tiene o están vacíos)
                if (m_metadata.value("podcast").toString().isEmpty())
                    m_metadata.insert("podcast", pobj.value("name").toString());
                if (m_metadata.value("podcaster").toString().isEmpty())
                    m_metadata.insert("podcaster", pobj.value("author").toString());
                // Sincronizar episodio/temporada desde podcast.json si el
                // proyecto tiene valores por defecto (0). podcast.json guarda
                // lastEpisode/lastSeason que se actualizan al exportar.
                if (m_metadata.value("episode").toInt() == 0
                    && pobj.contains("lastEpisode")) {
                    m_metadata.insert("episode", pobj.value("lastEpisode").toInt() + 1);
                }
                if (m_metadata.value("season").toInt() == 0
                    && pobj.contains("lastSeason")) {
                    m_metadata.insert("season", pobj.value("lastSeason").toInt());
                }
                // Portada del podcast: si el proyecto no tiene una propia,
                // usar la del podcast
                if (m_metadata.value("coverPath").toString().isEmpty()) {
                    const QStringList exts = {"jpg", "jpeg", "png", "webp"};
                    for (const QString &ext : exts) {
                        const QString cp = podcastDir.filePath("cover." + ext);
                        if (QFile::exists(cp)) {
                            m_metadata.insert("coverPath", cp);
                            break;
                        }
                    }
                }
                emit metadataChanged();
            }
        }
    }

    setCurrentPath(path);
    setDirty(false);
    if (m_trackModel) m_trackModel->freeFlatCaches();
#if defined(Q_OS_LINUX)
    malloc_trim(0);
#endif
    emit projectLoaded();
    qDebug() << "[ProjectIO] Proyecto cargado:" << path;
    return true;
}

QString ProjectIO::recoveryDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recovery";
}

bool ProjectIO::hasRecoveryProject() const
{
    QFile f(recoveryDir() + "/project_recovery.json");
    return f.exists();
}

QByteArray ProjectIO::buildProjectJsonForRecovery() const
{
    QByteArray standardJson = buildProjectJson();
    QJsonDocument doc = QJsonDocument::fromJson(standardJson);
    QJsonObject root = doc.object();
    root["recoveryOriginalPath"] = m_currentPath;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

void ProjectIO::saveRecoveryBackup()
{
    qDebug() << "[ProjectIO] Entering saveRecoveryBackup()...";
    if (!m_trackModel) {
        qWarning() << "[ProjectIO] m_trackModel is null in saveRecoveryBackup";
        return;
    }

    QString recDir = recoveryDir();
    qDebug() << "[ProjectIO] Recovery dir is:" << recDir;
    QDir().mkpath(recDir);

    const int nTracks = m_trackModel->count();
    qDebug() << "[ProjectIO] Number of tracks:" << nTracks;
    for (int i = 0; i < nTracks; ++i) {
        const int nSrc = m_trackModel->trackSourceCount(i);
        qDebug() << "[ProjectIO] Track" << i << "source count:" << nSrc;
        for (int s = 0; s < nSrc; ++s) {
            if (m_trackModel->isSourceRecording(i, s)) {
                qDebug() << "[ProjectIO] Source" << s << "on track" << i << "is actively recording, skipping WAV backup";
                continue;
            }

            QString wavPath = QString("%1/track%2_src%3.wav").arg(recDir).arg(i).arg(s);
            qDebug() << "[ProjectIO] Checking WAV backup for track" << i << "source" << s << "at" << wavPath;

            if (m_trackModel->isSourceDirty(i, s) || !QFile::exists(wavPath)) {
                int sr = 48000, ch = 2;
                QVector<float> samples = m_trackModel->trackSourceSamples(i, s, &sr, &ch);
                qDebug() << "[ProjectIO] Source dirty or WAV doesn't exist. Samples size:" << samples.size();
                if (!samples.isEmpty()) {
                    const qint64 frames = samples.size() / ch;
                    qDebug() << "[ProjectIO] Writing WAV backup of" << frames << "frames, sr" << sr << "ch" << ch;
                    if (writeWavFloat32(wavPath, samples.constData(), frames, sr, ch)) {
                        m_trackModel->setSourceDirty(i, s, false);
                        qDebug() << "[ProjectIO] WAV backup successful";
                    } else {
                        qWarning() << "[ProjectIO] Failed to write WAV backup to" << wavPath;
                    }
                }
            }
        }
    }

    QString jsonPath = recDir + "/project_recovery.json";
    QFile f(jsonPath);
    if (f.open(QIODevice::WriteOnly)) {
        QByteArray jsonBytes = buildProjectJsonForRecovery();
        f.write(jsonBytes);
        f.close();
        qDebug() << "[ProjectIO] project_recovery.json written successfully. Size:" << jsonBytes.size();
        emit hasRecoveryChanged();
    } else {
        qWarning() << "[ProjectIO] Failed to write project_recovery.json to" << jsonPath;
    }
}

bool ProjectIO::recoverProject()
{
    QString recDir = recoveryDir();
    QString jsonPath = recDir + "/project_recovery.json";
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        m_lastError = tr("No se pudo abrir el archivo de recuperación");
        return false;
    }
    QByteArray projectJson = f.readAll();
    f.close();

    m_extractDir.reset(new QTemporaryDir());
    if (!m_extractDir->isValid()) {
        m_lastError = tr("No pude crear directorio temporal");
        m_extractDir.reset();
        return false;
    }

    QDir dir(recDir);
    QStringList files = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &file : files) {
        if (file == "project_recovery.json") continue;
        QString src = recDir + "/" + file;
        QString dst = m_extractDir->path() + "/" + file;
        if (QFile::exists(dst)) QFile::remove(dst);
        if (!QFile::copy(src, dst)) {
            m_lastError = tr("Error al copiar archivo de recuperación %1: %2").arg(file).arg(QFile(src).errorString());
            m_extractDir.reset();
            return false;
        }
    }

    const QJsonObject rootObj = QJsonDocument::fromJson(projectJson).object();
    QString origPath = rootObj.value("recoveryOriginalPath").toString();

    m_suspendDirty = true;
    const bool ok = applyProjectJson(projectJson, m_extractDir->path(), true);
    m_suspendDirty = false;

    if (!ok) {
        m_extractDir.reset();
        return false;
    }

    setCurrentPath(origPath);
    setDirty(true);
    emit projectLoaded();

    discardRecovery();
    return true;
}

void ProjectIO::discardRecovery()
{
    if (m_recoveryTimer) m_recoveryTimer->stop();
    if (m_trackModel) {
        m_trackModel->stopRecoveryWorker();
    }
    QString recDir = recoveryDir();
    QDir dir(recDir);
    if (dir.exists()) {
        dir.removeRecursively();
    }
    emit hasRecoveryChanged();
}

bool ProjectIO::updateProjectPathAndTitle(const QString &oldProjPath, const QString &newProjPath, const QString &newTitle)
{
    // Actualizar m_currentPath si el proyecto renombrado estaba actualmente abierto
    if (!m_currentPath.isEmpty()) {
        const QString oldDir = QFileInfo(oldProjPath).absolutePath();
        const QString curDir = QFileInfo(m_currentPath).absolutePath();
        if (m_currentPath == oldProjPath || curDir == oldDir) {
            setCurrentPath(newProjPath);
            m_metadata.insert("title", newTitle);
            emit metadataChanged();
        }
    }

    if (!QFile::exists(newProjPath)) return false;

    // Verificar si es tarball (ustar) o JSON plano
    QFile checkFile(newProjPath);
    if (!checkFile.open(QIODevice::ReadOnly)) return false;
    QByteArray firstBlk = checkFile.read(512);
    checkFile.close();

    bool isTar = false;
    if (firstBlk.size() == 512) {
        const TarHeader *h = reinterpret_cast<const TarHeader*>(firstBlk.constData());
        if (std::memcmp(h->magic, "ustar", 5) == 0) {
            isTar = true;
        }
    }

    if (isTar) {
        // Actualizar project.json en el TAR mediante copia streaming (bloques de 64KB)
        const QString tempPath = newProjPath + ".tmp_rename";
        QFile inFile(newProjPath);
        QFile outFile(tempPath);
        if (!inFile.open(QIODevice::ReadOnly) || !outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }

        bool success = true;
        while (!inFile.atEnd()) {
            QByteArray blk = inFile.read(TAR_BLOCK);
            if (blk.size() < TAR_BLOCK) break;

            bool allZero = true;
            for (char c : blk) { if (c != 0) { allZero = false; break; } }
            if (allZero) {
                outFile.write(QByteArray(TAR_BLOCK * 2, '\0'));
                break;
            }

            const TarHeader *h = reinterpret_cast<const TarHeader*>(blk.constData());
            const QString name = QString::fromUtf8(h->name, qstrnlen(h->name, 100));
            const qint64 sz = parseOctal(h->size, sizeof(h->size));
            if (sz < 0) { success = false; break; }

            if (name == "project.json") {
                QByteArray data = inFile.read(sz);
                if (data.size() != sz) { success = false; break; }
                const int pad = (TAR_BLOCK - (sz % TAR_BLOCK)) % TAR_BLOCK;
                if (pad > 0) inFile.read(pad);

                QJsonParseError err{};
                QJsonDocument doc = QJsonDocument::fromJson(data, &err);
                if (err.error == QJsonParseError::NoError) {
                    QJsonObject root = doc.object();
                    QJsonObject md = root.value("metadata").toObject();
                    md["title"] = newTitle;
                    root["metadata"] = md;
                    data = QJsonDocument(root).toJson(QJsonDocument::Indented);
                }

                outFile.write(buildTarHeader(name, quint64(data.size())));
                outFile.write(data);
                const int outPad = (TAR_BLOCK - (data.size() % TAR_BLOCK)) % TAR_BLOCK;
                if (outPad > 0) outFile.write(QByteArray(outPad, '\0'));
            } else {
                // Copiar cabecera intacta y hacer streaming de datos por bloques de 64KB
                outFile.write(blk);
                qint64 remaining = sz;
                char buf[65536];
                while (remaining > 0) {
                    const qint64 toRead = std::min<qint64>(remaining, sizeof(buf));
                    const qint64 got = inFile.read(buf, toRead);
                    if (got <= 0) { success = false; break; }
                    outFile.write(buf, got);
                    remaining -= got;
                }
                if (!success) break;
                const int pad = (TAR_BLOCK - (sz % TAR_BLOCK)) % TAR_BLOCK;
                if (pad > 0) {
                    QByteArray padBuf = inFile.read(pad);
                    outFile.write(padBuf);
                }
            }
        }

        inFile.close();
        outFile.close();

        if (success) {
            QFile::remove(newProjPath);
            return QFile::rename(tempPath, newProjPath);
        } else {
            QFile::remove(tempPath);
            return false;
        }
    } else {
        // Archivo JSON plano
        QFile f(newProjPath);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QByteArray data = f.readAll();
        f.close();

        QJsonParseError err{};
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) return false;

        QJsonObject root = doc.object();
        QJsonObject md = root.value("metadata").toObject();
        md["title"] = newTitle;
        root["metadata"] = md;
        QByteArray newJson = QJsonDocument(root).toJson(QJsonDocument::Indented);

        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(newJson);
        f.close();
    }

    return true;
}
