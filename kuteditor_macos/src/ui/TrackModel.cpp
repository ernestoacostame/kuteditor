#include "TrackModel.h"
#include "audio/AudioEngine.h"
#include "audio/TrackFxChain.h"
#include "audio/fx/RNNoiseEffect.h"
#include "transcription/RNNoiseWorker.h"

#include <QColor>
#include <QCoreApplication>
#include <QtConcurrent>
#include <QPointer>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

#ifdef HAVE_SAMPLERATE
#include <samplerate.h>
#endif

#ifdef HAVE_COREAUDIO
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#endif
#include <QThread>
#include <QWaitCondition>

class RecoveryWorker : public QThread {
public:
    RecoveryWorker(QObject *parent = nullptr) : QThread(parent), m_running(true) {
        m_recoveryDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recovery";
        QDir().mkpath(m_recoveryDir);
        start();
    }

    ~RecoveryWorker() {
        {
            QMutexLocker lock(&m_mutex);
            m_running = false;
            m_cond.wakeAll();
        }
        wait();
    }

    void queueStart(int trackIdx, int sourceIdx, int sampleRate, int channels) {
        QMutexLocker lock(&m_mutex);
        WriteTask task;
        task.type = WriteTask::Start;
        task.trackIdx = trackIdx;
        task.sourceIdx = sourceIdx;
        task.sampleRate = sampleRate;
        task.channels = channels;
        m_queue.append(task);
        m_cond.wakeOne();
    }

    void queueWrite(int trackIdx, int sourceIdx, const float *data, int size) {
        QMutexLocker lock(&m_mutex);
        WriteTask task;
        task.type = WriteTask::Write;
        task.trackIdx = trackIdx;
        task.sourceIdx = sourceIdx;
        task.samples.resize(size);
        std::memcpy(task.samples.data(), data, size * sizeof(float));
        m_queue.append(task);
        m_cond.wakeOne();
    }

    void queueStop(int trackIdx, int sourceIdx) {
        QMutexLocker lock(&m_mutex);
        WriteTask task;
        task.type = WriteTask::Stop;
        task.trackIdx = trackIdx;
        task.sourceIdx = sourceIdx;
        m_queue.append(task);
        m_cond.wakeOne();
    }

protected:
    void run() override {
        while (true) {
            WriteTask task;
            {
                QMutexLocker lock(&m_mutex);
                while (m_queue.isEmpty() && m_running) {
                    m_cond.wait(&m_mutex);
                }
                if (!m_running && m_queue.isEmpty()) {
                    break;
                }
                task = m_queue.takeFirst();
            }

            processTask(task);
        }
        
        // Clean up any remaining open files
        for (auto &f : m_openFiles) {
            if (f.file && f.file->isOpen()) {
                f.file->close();
            }
        }
    }

private:
    struct WriteTask {
        enum Type { Start, Write, Stop };
        Type type;
        int trackIdx = -1;
        int sourceIdx = -1;
        int sampleRate = 48000;
        int channels = 2;
        QVector<float> samples;
    };

    bool m_running;
    QList<WriteTask> m_queue;
    QMutex m_mutex;
    QWaitCondition m_cond;
    QString m_recoveryDir;

    struct FileInfo {
        std::shared_ptr<QFile> file;
        int channels = 2;
        int sampleRate = 48000;
        qint64 totalFrames = 0;
    };
    QMap<QPair<int, int>, FileInfo> m_openFiles;

    void processTask(const WriteTask &task) {
        QPair<int, int> key(task.trackIdx, task.sourceIdx);
        if (task.type == WriteTask::Start) {
            QString filePath = QString("%1/track%2_src%3.wav").arg(m_recoveryDir).arg(task.trackIdx).arg(task.sourceIdx);
            
            // Delete old file if exists
            if (QFile::exists(filePath)) {
                QFile::remove(filePath);
            }

            auto file = std::make_shared<QFile>(filePath);
            if (file->open(QIODevice::WriteOnly)) {
                FileInfo info;
                info.file = file;
                info.channels = task.channels;
                info.sampleRate = task.sampleRate;
                info.totalFrames = 0;

#pragma pack(push, 1)
                struct WavHeader {
                    char riff[4] = {'R', 'I', 'F', 'F'};
                    quint32 riffSize = 36;
                    char wave[4] = {'W', 'A', 'V', 'E'};
                    char fmt[4] = {'f', 'm', 't', ' '};
                    quint32 fmtSize = 16;
                    quint16 audioFormat = 3; // IEEE float
                    quint16 channels = 2;
                    quint32 sampleRate = 48000;
                    quint32 byteRate = 48000 * 2 * 4;
                    quint16 blockAlign = 2 * 4;
                    quint16 bitsPerSample = 32;
                    char data[4] = {'d', 'a', 't', 'a'};
                    quint32 dataSize = 0;
                };
#pragma pack(pop)

                WavHeader hdr;
                hdr.channels = task.channels;
                hdr.sampleRate = task.sampleRate;
                hdr.byteRate = task.sampleRate * task.channels * 4;
                hdr.blockAlign = task.channels * 4;
                hdr.riffSize = 36;
                hdr.dataSize = 0;

                file->write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
                file->flush();

                m_openFiles[key] = info;
            } else {
                qWarning() << "[RecoveryWorker] No se pudo abrir archivo de recuperación:" << filePath;
            }
        } else if (task.type == WriteTask::Write) {
            if (!m_openFiles.contains(key)) return;
            FileInfo &info = m_openFiles[key];
            if (info.file && info.file->isOpen()) {
                info.file->write(reinterpret_cast<const char*>(task.samples.constData()), task.samples.size() * sizeof(float));
                
                int nFrames = task.samples.size() / info.channels;
                info.totalFrames += nFrames;

                quint32 dataSize = info.totalFrames * info.channels * sizeof(float);
                quint32 riffSize = 36 + dataSize;

                // Update WAV header sizes safely in Little Endian without QDataStream
                qint64 currentPos = info.file->pos();
                if (info.file->seek(4)) {
                    char bytes[4];
                    bytes[0] = riffSize & 0xFF;
                    bytes[1] = (riffSize >> 8) & 0xFF;
                    bytes[2] = (riffSize >> 16) & 0xFF;
                    bytes[3] = (riffSize >> 24) & 0xFF;
                    info.file->write(bytes, 4);
                }
                if (info.file->seek(40)) {
                    char bytes[4];
                    bytes[0] = dataSize & 0xFF;
                    bytes[1] = (dataSize >> 8) & 0xFF;
                    bytes[2] = (dataSize >> 16) & 0xFF;
                    bytes[3] = (dataSize >> 24) & 0xFF;
                    info.file->write(bytes, 4);
                }
                info.file->seek(currentPos);
                info.file->flush();
            }
        } else if (task.type == WriteTask::Stop) {
            if (m_openFiles.contains(key)) {
                FileInfo &info = m_openFiles[key];
                if (info.file && info.file->isOpen()) {
                    info.file->close();
                }
                m_openFiles.remove(key);
            }
        }
    }
};

TrackModel::TrackModel(QObject *parent) : QAbstractListModel(parent) {
  m_recoveryWorker = new RecoveryWorker(this);
}

// ============================================================================
//  Qt model boilerplate
// ============================================================================
int TrackModel::rowCount(const QModelIndex &parent) const {
  Q_UNUSED(parent);
  return m_tracks.size();
}

QVariant TrackModel::data(const QModelIndex &index, int role) const {
  if (!isValidIndex(index.row()))
    return {};
  const Track &t = m_tracks[index.row()];
  switch (role) {
  case NameRole:
    return t.name;
  case ColorRole:
    return t.color;
  case InputDeviceRole:
    return t.inputDeviceId;
  case InputDeviceNameRole:
    return t.inputDeviceName;
  case InputChannelIndexRole:
    return t.inputChannelIndex;
  case InputModeRole:
    return t.inputMode;
  case GainRole:
    return t.gain;
  case PanRole:
    return t.pan;
  case MutedRole:
    return t.muted;
  case SoloRole:
    return t.solo;
  case ArmedRole:
    return t.armed;
  case DurationRole: {
    QMutexLocker lock(&m_bufMutex);
    const qint64 frames = trackFrameCount(index.row());
    return t.clips.isEmpty() || t.clips[0].sourceIdx < 0
               ? 0.0
               : double(frames) / t.sources.first().sampleRate;
  }
  case HasAudioRole: {
    QMutexLocker lock(&m_bufMutex);
    return !t.clips.isEmpty();
  }
  }
  return {};
}

bool TrackModel::setData(const QModelIndex &index, const QVariant &value,
                         int role) {
  if (!isValidIndex(index.row()))
    return false;
  Track &t = m_tracks[index.row()];
  bool ok = false;
  switch (role) {
  case NameRole:
    t.name = value.toString();
    ok = true;
    break;
  case ColorRole:
    t.color = value.value<QColor>();
    ok = true;
    break;
  case GainRole:
    t.gain = value.toFloat();
    ok = true;
    break;
  case PanRole:
    t.pan = std::max(-1.0f, std::min(1.0f, value.toFloat()));
    ok = true;
    break;
  case MutedRole:
    t.muted = value.toBool();
    ok = true;
    break;
  case SoloRole:
    t.solo = value.toBool();
    ok = true;
    break;
  case ArmedRole:
    t.armed = value.toBool();
    ok = true;
    break;
  default:
    break;
  }
  if (ok)
    emit dataChanged(index, index, {role});
  return ok;
}

QHash<int, QByteArray> TrackModel::roleNames() const {
  return {
      {NameRole, "name"},
      {ColorRole, "color"},
      {InputDeviceRole, "inputDevice"},
      {InputDeviceNameRole, "inputDeviceName"},
      {InputChannelIndexRole, "inputChannelIndex"},
      {InputModeRole, "inputMode"},
      {GainRole, "gain"},
      {PanRole, "pan"},
      {MutedRole, "muted"},
      {SoloRole, "solo"},
      {ArmedRole, "armed"},
      {DurationRole, "duration"},
      {HasAudioRole, "hasAudio"},
  };
}

void TrackModel::emitRowChanged(int row, const QVector<int> &roles) {
  const QModelIndex idx = index(row);
  emit dataChanged(idx, idx, roles);
}

// ============================================================================
//  Mutaciones de pistas
// ============================================================================
void TrackModel::addTrack(const QString &name) {
  beginInsertRows(QModelIndex(), m_tracks.size(), m_tracks.size());
  Track t;
  t.name = name;
  // Paleta cíclica de colores para que se distingan.
  static const QColor palette[] = {QColor("#e74c3c"), QColor("#3498db"),
                                   QColor("#2ecc71"), QColor("#f39c12"),
                                   QColor("#9b59b6"), QColor("#1abc9c"),
                                   QColor("#e67e22"), QColor("#34495e")};
  t.color = palette[m_tracks.size() % 8];
  t.fxChain = new TrackFxChain(this);
  connect(t.fxChain, &TrackFxChain::changed, this, [this, name]() {
    // Find track index by name or just iterate
    for (int i = 0; i < m_tracks.size(); ++i) {
      if (m_tracks[i].fxChain == qobject_cast<TrackFxChain *>(sender())) {
        // Trigger a non-const method to check and start workers
        QMetaObject::invokeMethod(this, "checkRNNoiseProcessing",
                                  Qt::QueuedConnection, Q_ARG(int, i));
        break;
      }
    }
  });
  m_tracks.append(t);
  endInsertRows();
  emit countChanged();
  qDebug() << "[TrackModel] Pista añadida:" << name;
}

void TrackModel::insertTrackAt(int index, const QString &name) {
  if (index < 0)
    index = 0;
  if (index > m_tracks.size())
    index = m_tracks.size();
  beginInsertRows(QModelIndex(), index, index);
  Track t;
  t.name = name;
  static const QColor palette[] = {QColor("#e74c3c"), QColor("#3498db"),
                                   QColor("#2ecc71"), QColor("#f39c12"),
                                   QColor("#9b59b6"), QColor("#1abc9c"),
                                   QColor("#e67e22"), QColor("#34495e")};
  t.color = palette[index % 8];
  t.fxChain = new TrackFxChain(this);
  connect(t.fxChain, &TrackFxChain::changed, this, [this, name]() {
    for (int i = 0; i < m_tracks.size(); ++i) {
      if (m_tracks[i].fxChain == qobject_cast<TrackFxChain *>(sender())) {
        QMetaObject::invokeMethod(this, "checkRNNoiseProcessing",
                                  Qt::QueuedConnection, Q_ARG(int, i));
        break;
      }
    }
  });
  m_tracks.insert(index, t);
  endInsertRows();
  emit countChanged();
}

void TrackModel::removeTrack(int index) {
  if (!isValidIndex(index))
    return;
  beginRemoveRows(QModelIndex(), index, index);
  m_tracks.remove(index);
  endRemoveRows();
  emit countChanged();
  emit armedChanged();
}

void TrackModel::updateTrackColor(int index, const QColor &color) {
  if (!isValidIndex(index))
    return;
  m_tracks[index].color = color;
  emitRowChanged(index, {ColorRole});
}

void TrackModel::updateInputDevice(int index, const QString &deviceId,
                                   const QString &deviceName) {
  if (!isValidIndex(index))
    return;
  Track &t = m_tracks[index];
  if (t.inputDeviceId == deviceId && t.inputDeviceName == deviceName)
    return;
  t.inputDeviceId = deviceId;
  t.inputDeviceName = deviceName;
  emitRowChanged(index, {InputDeviceRole, InputDeviceNameRole});
  emit trackDeviceChanged(index, deviceId);
}

void TrackModel::updateInputChannel(int index, int mode, int channelIndex) {
  if (!isValidIndex(index))
    return;
  Track &t = m_tracks[index];
  t.inputMode = mode;
  t.inputChannelIndex = channelIndex;
  emitRowChanged(index, {InputModeRole, InputChannelIndexRole});
  emit trackDeviceChanged(index, t.inputDeviceId);
}

void TrackModel::updateGain(int index, float gain) {
  if (!isValidIndex(index))
    return;
  if (gain < 0)
    gain = 0;
  m_tracks[index].gain = gain;
  emitRowChanged(index, {GainRole});
}

void TrackModel::updatePan(int index, float pan) {
  if (!isValidIndex(index))
    return;
  pan = std::max(-1.0f, std::min(1.0f, pan));
  m_tracks[index].pan = pan;
  emitRowChanged(index, {PanRole});
}

void TrackModel::toggleMute(int index) {
  if (!isValidIndex(index))
    return;
  m_tracks[index].muted = !m_tracks[index].muted;
  emitRowChanged(index, {MutedRole});
}

void TrackModel::toggleSolo(int index) {
  if (!isValidIndex(index))
    return;
  m_tracks[index].solo = !m_tracks[index].solo;
  emitRowChanged(index, {SoloRole});
}

void TrackModel::setArmed(int index, bool armed) {
  if (!isValidIndex(index))
    return;
  if (m_tracks[index].armed == armed)
    return;
  m_tracks[index].armed = armed;
  emitRowChanged(index, {ArmedRole});
  emit armedChanged();
  emit trackArmedChanged(index, armed);
}

void TrackModel::toggleArmed(int index) {
  if (!isValidIndex(index))
    return;
  setArmed(index, !m_tracks[index].armed);
}

void TrackModel::updateTrackName(int index, const QString &name) {
  if (!isValidIndex(index))
    return;
  m_tracks[index].name = name;
  emitRowChanged(index, {NameRole});
}

void TrackModel::moveTrack(int from, int to) {
  if (!isValidIndex(from) || to < 0 || to >= m_tracks.size())
    return;
  if (from == to)
    return;
  const int n = m_tracks.size();
  if (from < to) {
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to + 1);
  } else {
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to);
  }
  m_tracks.move(from, to);
  endMoveRows();
  emit countChanged();
}

QVariantMap TrackModel::getTrackData(int index) const {
  QVariantMap m;
  if (!isValidIndex(index))
    return m;
  const Track &t = m_tracks[index];
  m["name"] = t.name;
  m["color"] = t.color;
  m["inputDevice"] = t.inputDeviceId;
  m["inputDeviceName"] = t.inputDeviceName;
  m["inputMode"] = t.inputMode;
  m["inputChannelIndex"] = t.inputChannelIndex;
  m["gain"] = t.gain;
  m["pan"] = t.pan;
  m["muted"] = t.muted;
  m["solo"] = t.solo;
  m["armed"] = t.armed;
  {
    QMutexLocker lock(&m_bufMutex);
    const qint64 frames = trackFrameCount(index);
    const int sr = t.sources.isEmpty() ? 48000 : t.sources.first().sampleRate;
    m["duration"] = double(frames) / sr;
    m["hasAudio"] = !t.clips.isEmpty();
  }
  return m;
}

void TrackModel::clearAudio(int index) {
  if (!isValidIndex(index))
    return;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[index];
    t.sources.clear();
    t.clips.clear();
    t.recordingClipIdx = -1;
    invalidateFlatCache(t);
  }
  emitRowChanged(index, {DurationRole, HasAudioRole});
  emit trackPeaksUpdated(index);
}

TrackFxChain *TrackModel::trackFxChain(int index) {
  if (!isValidIndex(index))
    return nullptr;
  Track &t = m_tracks[index];
  if (!t.fxChain)
    t.fxChain = new TrackFxChain(this);
  return t.fxChain;
}

int TrackModel::armedCount() const {
  int n = 0;
  for (const auto &t : m_tracks)
    if (t.armed)
      n++;
  return n;
}

QList<int> TrackModel::armedTrackIndices() const {
  QList<int> out;
  for (int i = 0; i < m_tracks.size(); ++i)
    if (m_tracks[i].armed)
      out.append(i);
  return out;
}

QString TrackModel::trackDeviceId(int index) const {
  if (!isValidIndex(index))
    return {};
  return m_tracks[index].inputDeviceId;
}

TrackModel::MixerSnapshot TrackModel::mixerSnapshot(int index) const {
  MixerSnapshot s;
  if (!isValidIndex(index))
    return s;
  const Track &t = m_tracks[index];
  s.valid = true;
  s.muted = t.muted;
  s.solo = t.solo;
  s.armed = t.armed;
  s.gain = t.gain;
  s.pan = t.pan;
  return s;
}

bool TrackModel::anyTrackInSolo() const {
  for (const auto &t : m_tracks)
    if (t.solo)
      return true;
  return false;
}

// ============================================================================
//  Helpers internos sobre clips
// ============================================================================
void TrackModel::invalidateFlatCache(Track &t) {
  t.flatCacheValid = false;
  t.flatCache.clear();
  t.flatCache.squeeze();
}

qint64 TrackModel::trackFrameCount(int index) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(index))
    return 0;
  const Track &t = m_tracks[index];
  qint64 maxEnd = 0;
  for (const auto &c : t.clips) {
    const qint64 end = c.timelineStart + c.length;
    if (end > maxEnd)
      maxEnd = end;
  }
  return maxEnd;
}

void TrackModel::rebuildFlatCache(const Track &t) const {
  // Construir un buffer interleaved continuo a partir de los clips.
  // Brechas = silencio.
  if (t.sources.isEmpty() || t.clips.isEmpty()) {
    t.flatCache.clear();
    t.flatCacheValid = true;
    return;
  }
  const int ch = t.sources.first().channels;

  qint64 totalFrames = 0;
  for (const auto &c : t.clips) {
    totalFrames = std::max(totalFrames, c.timelineStart + c.length);
  }
  t.flatCache.fill(0.0f, totalFrames * ch);

  for (const auto &c : t.clips) {
    if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
      continue;
    if (c.muted)
      continue;
    const auto &src = t.sources[c.sourceIdx];
    if (src.channels != ch)
      continue;
    const qint64 copyN =
        std::min<qint64>(c.length, (src.samplesVec().size() / ch) - c.sourceOffset);
    if (copyN <= 0)
      continue;

    // Punteros al origen y destino
    const float *srcBuf = src.samplesVec().constData() + c.sourceOffset * ch;
    float *dst = t.flatCache.data() + c.timelineStart * ch;
    const qint64 totalSamples = copyN * ch;

    // ---- Mezcla aditiva con fades aplicados directamente ----
    // En vez de memcpy (que sobreescribe), sumamos el audio de cada
    // clip al flatCache. Esto permite crossfades reales: en la zona
    // de solapamiento, ambos clips contribuyen audio.

    // Determinar wet/dry del denoiser si aplica
    const float *wetBuf = nullptr;
    float denoiseMix = 0.0f;
    if (t.fxChain && t.fxChain->deNoiser() &&
        t.fxChain->deNoiser()->enabled()) {
      denoiseMix = t.fxChain->deNoiser()->reduction();
      if (denoiseMix > 0.0f &&
          src.processedVec().size() == src.samplesVec().size()) {
        wetBuf = src.processedVec().constData() + c.sourceOffset * ch;
      } else {
        denoiseMix = 0.0f;
      }
    }

    if (!c.envelope.isEmpty()) {
      // Envelope mode: add with envelope gain
      const double sr = src.sampleRate > 0 ? double(src.sampleRate) : 48000.0;
      int envIdx = 0;
      const int nNodes = c.envelope.size();
      for (qint64 i = 0; i < copyN; ++i) {
        const double tSec = double(i) / sr;
        float g = 1.0f;
        if (nNodes == 1) {
          g = c.envelope[0].y();
        } else {
          while (envIdx < nNodes - 1 && tSec >= c.envelope[envIdx + 1].x()) {
            envIdx++;
          }
          if (envIdx == 0 && tSec <= c.envelope[0].x()) {
            g = c.envelope[0].y();
          } else if (envIdx >= nNodes - 1) {
            g = c.envelope[nNodes - 1].y();
          } else {
            const double t0 = c.envelope[envIdx].x();
            const float v0 = c.envelope[envIdx].y();
            const double t1 = c.envelope[envIdx + 1].x();
            const float v1 = c.envelope[envIdx + 1].y();
            const double frac = (tSec - t0) / (t1 - t0);
            g = float(v0 + frac * (v1 - v0));
          }
        }
        for (int k = 0; k < ch; ++k) {
          float sample = srcBuf[i * ch + k];
          if (wetBuf)
            sample =
                sample * (1.0f - denoiseMix) + wetBuf[i * ch + k] * denoiseMix;
          dst[i * ch + k] += sample * g;
        }
      }
    } else {
      // Fade in / fade out mode: add with fade gain
      const float cg = c.gain;
      const qint64 fin = std::min<qint64>(c.fadeInLen, copyN);
      const qint64 fout = std::min<qint64>(c.fadeOutLen, copyN);

      // Fade-in region
      for (qint64 i = 0; i < fin; ++i) {
        const float g = float(i + 1) / float(fin) * cg;
        for (int k = 0; k < ch; ++k) {
          float sample = srcBuf[i * ch + k];
          if (wetBuf)
            sample =
                sample * (1.0f - denoiseMix) + wetBuf[i * ch + k] * denoiseMix;
          dst[i * ch + k] += sample * g;
        }
      }
      // Sustain region (between fade-in and fade-out)
      const qint64 sustainN = (copyN - fout) - fin;
      if (sustainN > 0) {
        const float *sPtr = srcBuf + fin * ch;
        float *dPtr = dst + fin * ch;
        const qint64 totalSustainSamples = sustainN * ch;
        if (wetBuf) {
          const float *wPtr = wetBuf + fin * ch;
          const float dryMix = 1.0f - denoiseMix;
          if (cg == 1.0f) {
            for (qint64 i = 0; i < totalSustainSamples; ++i) {
              dPtr[i] += sPtr[i] * dryMix + wPtr[i] * denoiseMix;
            }
          } else {
            for (qint64 i = 0; i < totalSustainSamples; ++i) {
              dPtr[i] += (sPtr[i] * dryMix + wPtr[i] * denoiseMix) * cg;
            }
          }
        } else {
          if (cg == 1.0f) {
            for (qint64 i = 0; i < totalSustainSamples; ++i) {
              dPtr[i] += sPtr[i];
            }
          } else {
            for (qint64 i = 0; i < totalSustainSamples; ++i) {
              dPtr[i] += sPtr[i] * cg;
            }
          }
        }
      }
      // Fade-out region
      for (qint64 i = 0; i < fout; ++i) {
        const float g = float(fout - i) / float(fout) * cg;
        const qint64 f = copyN - fout + i;
        for (int k = 0; k < ch; ++k) {
          float sample = srcBuf[f * ch + k];
          if (wetBuf)
            sample =
                sample * (1.0f - denoiseMix) + wetBuf[f * ch + k] * denoiseMix;
          dst[f * ch + k] += sample * g;
        }
      }
    }
  }
  t.flatCacheValid = true;
}

void TrackModel::updateSourcePeaksInRange(AudioSource &src, qint64 fromFrame,
                                          qint64 toFrame) {
  if (src.channels <= 0)
    return;
  const qint64 totalFrames = src.samplesSize() / src.channels;
  if (totalFrames <= 0)
    return;

  fromFrame = std::max<qint64>(0, fromFrame);
  toFrame = std::min<qint64>(totalFrames, toFrame);
  if (toFrame <= fromFrame)
    return;

  const qint64 firstWin = fromFrame / PEAK_WINDOW_FRAMES;
  const qint64 lastWin = (toFrame - 1) / PEAK_WINDOW_FRAMES;
  const qint64 needed = lastWin + 1;
  if (src.peaks.size() < needed)
    src.peaks.resize(needed);

  const float *sData = src.samplesConstData();
  const int channels = src.channels;

  for (qint64 w = firstWin; w <= lastWin; ++w) {
    const qint64 wStart = w * PEAK_WINDOW_FRAMES;
    const qint64 wEnd =
        std::min<qint64>(wStart + PEAK_WINDOW_FRAMES, totalFrames);
    float maxAbs = 0.0f;
    const float *ptr = sData + wStart * channels;
    const qint64 numSamples = (wEnd - wStart) * channels;
    for (qint64 i = 0; i < numSamples; ++i) {
      const float v = std::fabs(ptr[i]);
      if (v > maxAbs)
        maxAbs = v;
    }
    src.peaks[w] = std::min(1.0f, maxAbs);
  }
}

// ============================================================================
//  Grabación: begin / write / finish
// ============================================================================
int TrackModel::beginRecording(int index, qint64 startFrame, int sampleRate,
                               int channels) {
  if (!isValidIndex(index))
    return -1;

  int clipIdx = -1;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[index];

    // Crear source nuevo
    AudioSource src;
    src.sampleRate = sampleRate;
    src.channels = channels;
    // Create mipmap immediately so the waveform can render during recording
    src.mipmap = std::make_shared<AudioMipmap>();
    src.mipmap->setLiveParams(channels, sampleRate);
    t.sources.append(src);
    const int sourceIdx = t.sources.size() - 1;

    // Crear clip que empieza en startFrame
    Clip c;
    c.sourceIdx = sourceIdx;
    c.timelineStart = startFrame;
    c.sourceOffset = 0;
    c.length = 0;
    c.gain = 1.0f;
    t.clips.append(c);
    clipIdx = t.clips.size() - 1;
    t.recordingClipIdx = clipIdx;

    invalidateFlatCache(t);
    if (!m_recoveryWorker) {
      m_recoveryWorker = new RecoveryWorker(this);
    }
    m_recoveryWorker->queueStart(index, sourceIdx, sampleRate, channels);
  }

  emitRowChanged(index, {DurationRole, HasAudioRole});
  emit clipsChanged(index);
  return clipIdx;
}

void TrackModel::finishRecording(int index) {
  if (!isValidIndex(index))
    return;

  int lastClipIdx = -1;
  {
    QMutexLocker lock(&m_bufMutex);
    lastClipIdx = m_tracks[index].recordingClipIdx;
    m_tracks[index].recordingClipIdx = -1;

    // Build mipmap for the newly recorded source so the waveform
    // renders via the fast mipmap path after recording stops.
    if (lastClipIdx >= 0 && lastClipIdx < m_tracks[index].clips.size()) {
      const Clip &c = m_tracks[index].clips[lastClipIdx];
      if (m_recoveryWorker) {
        m_recoveryWorker->queueStop(index, c.sourceIdx);
      }
      if (c.sourceIdx >= 0 && c.sourceIdx < m_tracks[index].sources.size()) {
        AudioSource &src = m_tracks[index].sources[c.sourceIdx];
        src.isDirty = false;
        if (!src.mipmap) {
          src.mipmap = std::make_shared<AudioMipmap>();
        }
        src.mipmap->loadFromVector(src.samplesVec(), src.channels, src.sampleRate);
      }
    }
  }

  // Emite clipsChanged para que la UI refresque el snapshot (clipsOf) con
  // el endSec final del clip que se estaba grabando. Sin esto, el delegate
  // sigue con el modelData de cuando el clip se creó (length=0) y queda
  // invisible al desactivarse el modo "en vivo".
  emit clipsChanged(index);
  emitRowChanged(index, {DurationRole, HasAudioRole});
}

void TrackModel::writeSamplesAt(int index, qint64 startFrame,
                                const float *interleaved, int nFrames,
                                int sampleRate, int channels) {
  if (!isValidIndex(index) || !interleaved || nFrames <= 0)
    return;

  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[index];

    // Caso 1: hay un clip en grabación → extenderlo.
    if (t.recordingClipIdx >= 0 && t.recordingClipIdx < t.clips.size()) {
      Clip &c = t.clips[t.recordingClipIdx];
      if (c.sourceIdx >= 0 && c.sourceIdx < t.sources.size()) {
        AudioSource &src = t.sources[c.sourceIdx];

        // Verificar que startFrame coincide con la continuación
        // esperada del clip (timelineStart + length).
        const qint64 expectedNext = c.timelineStart + c.length;
        if (startFrame != expectedNext) {
          // Desincronizado (p. ej. seek durante grab). Rellenar con
          // ceros hasta ajustarnos, pero no debería pasar en uso
          // normal.
          if (startFrame > expectedNext) {
            const qint64 gap = startFrame - expectedNext;
            src.samplesVec().resize(src.samplesVec().size() + gap * channels);
            c.length += gap;
          }
        }

        const int old = src.samplesVec().size();
        src.samplesVec().resize(old + nFrames * channels);
        std::memcpy(src.samplesVec().data() + old, interleaved,
                    sizeof(float) * nFrames * channels);
        c.length += nFrames;

        if (m_recoveryWorker) {
          m_recoveryWorker->queueWrite(index, c.sourceIdx, interleaved, nFrames * channels);
        }

        // Peaks incrementales solo en el nuevo rango.
        const qint64 newFrameCountInSrc = src.samplesVec().size() / channels;
        updateSourcePeaksInRange(src, newFrameCountInSrc - nFrames,
                                 newFrameCountInSrc);

        // Incrementally update mipmap for live waveform rendering
        if (src.mipmap) {
          src.mipmap->appendSamples(interleaved, nFrames);
        }

        invalidateFlatCache(t);
      }
    }
    // Si la grabación no está activa (o ya finalizó), descartar buffers sobrantes en tránsito.
  }

  emit trackPeaksUpdated(index);
  emitRowChanged(index, {DurationRole, HasAudioRole});
}

// ============================================================================
//  Clear region (silenciar)
// ============================================================================
void TrackModel::clearRegion(int index, double startSec, double endSec) {
  if (endSec <= startSec)
    return;

  if (index < 0) {
    for (int i = 0; i < m_tracks.size(); ++i) {
      if (m_tracks[i].muted)
        continue;
      clearRegion(i, startSec, endSec);
    }
    return;
  }
  if (!isValidIndex(index))
    return;

  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[index];
    if (t.clips.isEmpty() || t.sources.isEmpty())
      return;

    const int sr = t.sources.first().sampleRate;
    const qint64 startFrame = qint64(startSec * sr);
    const qint64 endFrame = qint64(endSec * sr);

    // Para cada clip que se solape con [startFrame, endFrame), poner a 0
    // los samples en su rango de source correspondiente.
    for (auto &c : t.clips) {
      const qint64 clipStart = c.timelineStart;
      const qint64 clipEnd = c.timelineStart + c.length;
      const qint64 ovStart = std::max(clipStart, startFrame);
      const qint64 ovEnd = std::min(clipEnd, endFrame);
      if (ovEnd <= ovStart)
        continue;

      // Mapear a coordenadas del source.
      const qint64 srcFrom = c.sourceOffset + (ovStart - clipStart);
      const qint64 srcTo = c.sourceOffset + (ovEnd - clipStart);

      if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
        continue;
      AudioSource &src = t.sources[c.sourceIdx];

      const qint64 fromIdx = srcFrom * src.channels;
      const qint64 toIdx = srcTo * src.channels;
      if (toIdx > src.samplesVec().size())
        continue;

      std::fill(src.samplesVec().data() + fromIdx, src.samplesVec().data() + toIdx, 0.0f);

      updateSourcePeaksInRange(src, srcFrom, srcTo);
      // Rebuild mipmap for modified source
      if (src.mipmap)
        src.mipmap->loadFromVector(src.samplesVec(), src.channels, src.sampleRate);
      src.isDirty = true;
    }
    invalidateFlatCache(t);
  }

  emitRowChanged(index, {DurationRole, HasAudioRole});
  emit trackPeaksUpdated(index);
}

// ============================================================================
//  Split, delete region, listar clips (Ronda 2)
// ============================================================================

void TrackModel::splitClipsAt(int index, double atSec) {
  if (atSec < 0)
    return;

  if (index < 0) {
    for (int i = 0; i < m_tracks.size(); ++i)
      splitClipsAt(i, atSec);
    return;
  }
  if (!isValidIndex(index))
    return;

  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[index];
    if (t.sources.isEmpty())
      return;
    const int sr = t.sources.first().sampleRate;
    const qint64 atFrame = std::llround(atSec * sr);
    splitClipsAtLocked(index, atFrame);
  }

  emit clipsChanged(index);
  emitRowChanged(index, {DurationRole, HasAudioRole});
  emit trackPeaksUpdated(index);
}

void TrackModel::splitClipsAtLocked(int index, qint64 atFrame) {
  if (!isValidIndex(index))
    return;

  Track &t = m_tracks[index];
  bool splitAny = false;

  // Buscar un clip que contenga estrictamente atFrame. No partimos en
  // los bordes (sería generar un clip de longitud 0).
  QVector<Clip> newClips;
  newClips.reserve(t.clips.size() + 1);
  for (const auto &c : t.clips) {
    const qint64 clipStart = c.timelineStart;
    const qint64 clipEnd = c.timelineStart + c.length;
    if (atFrame > clipStart && atFrame < clipEnd) {
      const qint64 firstLen = atFrame - clipStart;
      Clip a = c;
      a.length = firstLen;
      a.fadeOutLen = 0;
      // Clampar fadeIn si excede la nueva longitud
      if (a.fadeInLen > a.length)
        a.fadeInLen = a.length;

      Clip b = c;
      b.timelineStart = atFrame;
      b.sourceOffset = c.sourceOffset + firstLen;
      b.length = c.length - firstLen;
      b.fadeInLen = 0;
      // Clampar fadeOut si excede la nueva longitud
      if (b.fadeOutLen > b.length)
        b.fadeOutLen = b.length;

      // ── Dividir nodos del envelope ──────────────────────────────
      // El envelope usa coordenadas x = segundos desde inicio del clip.
      // El punto de corte en segundos relativos al clip original:
      const int sr = (c.sourceIdx >= 0 && c.sourceIdx < t.sources.size())
                         ? t.sources[c.sourceIdx].sampleRate
                         : 48000;
      const double splitSec = double(firstLen) / sr;

      a.envelope.clear();
      b.envelope.clear();
      for (const auto &node : c.envelope) {
        if (node.x() <= splitSec) {
          // Nodo pertenece al clip izquierdo (a)
          a.envelope.append(node);
        }
        if (node.x() >= splitSec) {
          // Nodo pertenece al clip derecho (b), restar el offset
          b.envelope.append(QPointF(node.x() - splitSec, node.y()));
        }
      }

      newClips.append(a);
      newClips.append(b);
      splitAny = true;
    } else {
      newClips.append(c);
    }
  }

  if (splitAny) {
    // Si había recordingClipIdx, recalcularlo: el nuevo clip que
    // contiene el final actual de la grabación es el "activo".
    int newRecIdx = -1;
    if (t.recordingClipIdx >= 0 && t.recordingClipIdx < t.clips.size()) {
      const Clip &oldRec = t.clips[t.recordingClipIdx];
      const qint64 oldEnd = oldRec.timelineStart + oldRec.length;
      for (int i = 0; i < newClips.size(); ++i) {
        const Clip &nc = newClips[i];
        if (oldEnd >= nc.timelineStart &&
            oldEnd <= nc.timelineStart + nc.length) {
          newRecIdx = i;
          break;
        }
      }
    }
    t.clips = newClips;
    t.recordingClipIdx = newRecIdx;
    invalidateFlatCache(t);
  }
}

qint64 TrackModel::deleteRegion(int index, double startSec, double endSec) {
  if (endSec <= startSec)
    return 0;

  // Global: aplicar a todas las pistas no muteadas.
  if (index < 0) {
    qint64 maxRemoved = 0;
    for (int i = 0; i < m_tracks.size(); ++i) {
      if (m_tracks[i].muted)
        continue;
      maxRemoved = std::max(maxRemoved, deleteRegion(i, startSec, endSec));
    }
    return maxRemoved;
  }
  if (!isValidIndex(index))
    return 0;

  qint64 removedFrames = 0;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[index];
    if (t.clips.isEmpty() || t.sources.isEmpty())
      return 0;

    const int sr = t.sources.first().sampleRate;
    const qint64 startFrame = qint64(startSec * sr);
    const qint64 endFrame = qint64(endSec * sr);
    if (endFrame <= startFrame)
      return 0;
    removedFrames = endFrame - startFrame;

    QVector<Clip> out;
    out.reserve(t.clips.size());

    for (const auto &c : t.clips) {
      const qint64 cStart = c.timelineStart;
      const qint64 cEnd = c.timelineStart + c.length;

      if (cEnd <= startFrame) {
        // Clip completamente antes de la región: no tocar.
        out.append(c);
      } else if (cStart >= endFrame) {
        // Clip completamente después: desplazar hacia atrás.
        Clip nc = c;
        nc.timelineStart = cStart - removedFrames;
        out.append(nc);
      } else if (cStart >= startFrame && cEnd <= endFrame) {
        // Clip totalmente contenido: eliminar.
        // No lo añadimos.
      } else if (cStart < startFrame && cEnd > endFrame) {
        // Clip cubre toda la región: partirlo en dos alrededor del hueco,
        // y el segundo se pega al final del primero (cerrando la brecha).
        Clip a = c;
        a.length = startFrame - cStart;

        Clip b = c;
        // El segundo arranca en startFrame (posición de línea después
        // de cerrar brecha) y apunta al source a partir del punto
        // correspondiente a endFrame.
        b.timelineStart = startFrame;
        b.sourceOffset = c.sourceOffset + (endFrame - cStart);
        b.length = cEnd - endFrame;

        out.append(a);
        out.append(b);
      } else if (cStart < startFrame && cEnd <= endFrame) {
        // El clip se solapa por la izquierda: recortar.
        Clip a = c;
        a.length = startFrame - cStart;
        out.append(a);
      } else if (cStart >= startFrame && cEnd > endFrame) {
        // El clip se solapa por la derecha: recortar y desplazar.
        Clip b = c;
        b.sourceOffset = c.sourceOffset + (endFrame - cStart);
        b.length = cEnd - endFrame;
        b.timelineStart = startFrame;
        out.append(b);
      }
    }

    // Recalcular recordingClipIdx: el clip "activo" es el que tenga el
    // mismo source que el que teníamos en grabación, si aún existe.
    int newRecIdx = -1;
    if (t.recordingClipIdx >= 0 && t.recordingClipIdx < t.clips.size()) {
      const int oldSrcIdx = t.clips[t.recordingClipIdx].sourceIdx;
      for (int i = 0; i < out.size(); ++i) {
        if (out[i].sourceIdx == oldSrcIdx) {
          newRecIdx = i;
          break;
        }
      }
    }
    t.clips = out;
    t.recordingClipIdx = newRecIdx;
    invalidateFlatCache(t);
  }

  emit clipsChanged(index);
  emitRowChanged(index, {DurationRole, HasAudioRole});
  emit trackPeaksUpdated(index);
  return removedFrames;
}

QVariantList TrackModel::clipsOf(int index) const {
  QVariantList out;
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(index))
    return out;
  const Track &t = m_tracks[index];
  if (t.sources.isEmpty())
    return out;
  const double sr = double(t.sources.first().sampleRate);
  for (int i = 0; i < t.clips.size(); ++i) {
    const Clip &c = t.clips[i];
    QVariantMap m;
    m["clipIndex"] = i;
    m["sourceIdx"] = c.sourceIdx;
    m["startSec"] = double(c.timelineStart) / sr;
    m["endSec"] = double(c.timelineStart + c.length) / sr;
    m["lengthSec"] = double(c.length) / sr;
    m["sourceOffsetSec"] = double(c.sourceOffset) / sr;
    m["fadeInSec"] = double(c.fadeInLen) / sr;
    m["fadeOutSec"] = double(c.fadeOutLen) / sr;
    m["gain"] = c.gain;
    m["muted"] = c.muted;
    QVariantList envList;
    for (const QPointF &pt : c.envelope) {
      QVariantMap node;
      node["x"] = pt.x();
      node["y"] = pt.y();
      envList.append(node);
    }
    m["envelope"] = envList;
    // Valores raw en frames para posicionamiento preciso en QML
    m["startFrame"] = qlonglong(c.timelineStart);
    m["lengthFrame"] = qlonglong(c.length);
    m["sampleRate"] = int(sr);
    out.append(m);
  }
  return out;
}

QVariantList TrackModel::clipPeaks(int trackIndex, int clipIndex,
                                   int pixelWidth) const {
  QVariantList out;
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return out;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return out;
  const Clip &c = t.clips[clipIndex];
  if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
    return out;
  const AudioSource &src = t.sources[c.sourceIdx];
  if (c.length <= 0)
    return out;

  const int ch = src.channels;
  const qint64 totalSrcFrames = ch > 0 ? qint64(src.samplesVec().size()) / ch : 0;
  const qint64 clipStart = c.sourceOffset;
  const qint64 clipEnd = c.sourceOffset + c.length;

  int numBins;
  qint64 framesPerBin;

  if (pixelWidth > 0 && pixelWidth > int((c.length + PEAK_WINDOW_FRAMES - 1) /
                                         PEAK_WINDOW_FRAMES)) {
    numBins = std::min(pixelWidth, 12000);
    framesPerBin = std::max<qint64>(1, c.length / numBins);
  } else {
    numBins = int((c.length + PEAK_WINDOW_FRAMES - 1) / PEAK_WINDOW_FRAMES);
    framesPerBin = PEAK_WINDOW_FRAMES;
  }

  numBins = std::min(numBins, 24000);

  out.reserve(numBins);

  for (int w = 0; w < numBins; ++w) {
    const qint64 wStart = clipStart + qint64(w) * c.length / numBins;
    const qint64 wEnd = clipStart + qint64(w + 1) * c.length / numBins;
    const qint64 safeEnd = std::min(wEnd, std::min(clipEnd, totalSrcFrames));

    if (framesPerBin == PEAK_WINDOW_FRAMES &&
        (wStart % PEAK_WINDOW_FRAMES) == 0 &&
        (safeEnd - wStart) == PEAK_WINDOW_FRAMES) {
      const qint64 srcWin = wStart / PEAK_WINDOW_FRAMES;
      if (srcWin >= 0 && srcWin < src.peaks.size()) {
        out.append(src.peaks[srcWin]);
        continue;
      }
    }
    float peak = 0.0f;
    for (qint64 f = wStart; f < safeEnd; ++f) {
      for (int ci = 0; ci < ch; ++ci) {
        const float v = std::abs(src.samplesVec()[f * ch + ci]);
        if (v > peak)
          peak = v;
      }
    }
    out.append(peak);
  }
  return out;
}

QVariantList TrackModel::clipPeaksVisible(int trackIndex, int clipIndex,
                                          double fracStart, double fracEnd,
                                          int pixelWidth) const {
  QVariantList out;
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return out;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return out;
  const Clip &c = t.clips[clipIndex];
  if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
    return out;
  const AudioSource &src = t.sources[c.sourceIdx];
  if (c.length <= 0 || pixelWidth <= 0)
    return out;

  fracStart = std::max(0.0, std::min(1.0, fracStart));
  fracEnd = std::max(fracStart, std::min(1.0, fracEnd));
  if (fracEnd <= fracStart)
    return out;

  const int ch = src.channels;
  const qint64 totalSrcFrames = ch > 0 ? qint64(src.samplesVec().size()) / ch : 0;
  const qint64 clipStart = c.sourceOffset;

  const qint64 visStart = clipStart + qint64(fracStart * c.length);
  const qint64 visEnd = clipStart + qint64(fracEnd * c.length);
  const qint64 visLen = visEnd - visStart;
  if (visLen <= 0)
    return out;

  const int numBins = std::min(pixelWidth, 24000);
  out.reserve(numBins);

  const qint64 framesPerBin = visLen / numBins;
  const bool usePeakCache =
      (framesPerBin >= PEAK_WINDOW_FRAMES) && !src.peaks.isEmpty();

  for (int w = 0; w < numBins; ++w) {
    const qint64 wStart = visStart + qint64(w) * visLen / numBins;
    const qint64 wEnd = visStart + qint64(w + 1) * visLen / numBins;
    const qint64 safeEnd = std::min(wEnd, totalSrcFrames);

    float peak = 0.0f;

    if (usePeakCache) {
      const qint64 firstWin = wStart / PEAK_WINDOW_FRAMES;
      const qint64 lastWin =
          (safeEnd > 0) ? (safeEnd - 1) / PEAK_WINDOW_FRAMES : firstWin;
      for (qint64 pw = firstWin; pw <= lastWin && pw < src.peaks.size(); ++pw) {
        if (src.peaks[pw] > peak)
          peak = src.peaks[pw];
      }
    } else {
      for (qint64 f = wStart; f < safeEnd; ++f) {
        for (int ci = 0; ci < ch; ++ci) {
          const float v = std::abs(src.samplesVec()[f * ch + ci]);
          if (v > peak)
            peak = v;
        }
      }
    }

    if (c.fadeInLen > 0 || c.fadeOutLen > 0) {
      const qint64 binMid = ((wStart + wEnd) / 2) - clipStart;
      float fadeGain = 1.0f;
      if (c.fadeInLen > 0 && binMid < c.fadeInLen) {
        fadeGain = float(binMid + 1) / float(c.fadeInLen);
      }
      if (c.fadeOutLen > 0 && binMid >= (c.length - c.fadeOutLen)) {
        const float g = float(c.length - binMid) / float(c.fadeOutLen);
        fadeGain = std::min(fadeGain, g);
      }
      peak *= std::max(0.0f, std::min(1.0f, fadeGain));
    }
    out.append(peak);
  }
  return out;
}

QVariantList TrackModel::clipPeaksLive(int trackIndex, int clipIndex,
                                       double secPerPixel) const {
  QVariantList out;
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return out;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return out;
  const Clip &c = t.clips[clipIndex];
  if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
    return out;
  const AudioSource &src = t.sources[c.sourceIdx];
  if (c.length <= 0 || secPerPixel <= 0)
    return out;

  const int ch = src.channels;
  const qint64 totalSrcFrames = ch > 0 ? qint64(src.samplesVec().size()) / ch : 0;
  const qint64 clipStart = c.sourceOffset;
  const qint64 clipEnd = clipStart + c.length;

  const double framesPerBin = secPerPixel * src.sampleRate;
  if (framesPerBin < 1.0)
    return out;

  const int numBins = qCeil(c.length / framesPerBin);
  out.reserve(numBins);

  for (int w = 0; w < numBins; ++w) {
    const qint64 wStart = clipStart + qint64(w * framesPerBin);
    const qint64 wEnd = clipStart + qint64((w + 1) * framesPerBin);
    const qint64 safeEnd = std::min(wEnd, std::min(clipEnd, totalSrcFrames));

    float peak = 0.0f;
    for (qint64 f = wStart; f < safeEnd; ++f) {
      for (int ci = 0; ci < ch; ++ci) {
        const float v = std::abs(src.samplesVec()[f * ch + ci]);
        if (v > peak)
          peak = v;
      }
    }
    out.append(peak);
  }
  return out;
}

QVariantMap TrackModel::clipSnapshot(int trackIndex, int clipIndex) const {
  QVariantMap m;
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return m;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return m;
  const Clip &c = t.clips[clipIndex];
  const double sr =
      t.sources.isEmpty() ? 48000.0 : double(t.sources.first().sampleRate);
  m["sourceIdx"] = c.sourceIdx;
  m["timelineStart"] = qint64(c.timelineStart);
  m["sourceOffset"] = qint64(c.sourceOffset);
  m["length"] = qint64(c.length);
  m["fadeInLen"] = qint64(c.fadeInLen);
  m["fadeOutLen"] = qint64(c.fadeOutLen);
  m["startSec"] = double(c.timelineStart) / sr;
  m["sourceOffsetSec"] = double(c.sourceOffset) / sr;
  m["lengthSec"] = double(c.length) / sr;
  m["fadeInSec"] = double(c.fadeInLen) / sr;
  m["fadeOutSec"] = double(c.fadeOutLen) / sr;
  m["gain"] = c.gain;
  m["muted"] = c.muted;
  QVariantList envList;
  for (const QPointF &pt : c.envelope) {
    QVariantMap node;
    node["x"] = pt.x();
    node["y"] = pt.y();
    envList.append(node);
  }
  m["envelope"] = envList;
  return m;
}

QVariantList TrackModel::removeClips(int trackIndex,
                                     const QVariantList &clipIndices,
                                     bool rippleClose) {
  QVariantList removed;
  if (!isValidIndex(trackIndex))
    return removed;

  QList<int> indices;
  indices.reserve(clipIndices.size());
  for (const QVariant &v : clipIndices)
    indices.append(v.toInt());
  std::sort(indices.begin(), indices.end(), std::greater<int>());

  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    const double sr =
        t.sources.isEmpty() ? 48000.0 : double(t.sources.first().sampleRate);

    for (int i : indices) {
      if (i < 0 || i >= t.clips.size())
        continue;
      const Clip &c = t.clips[i];
      QVariantMap m;
      m["sourceIdx"] = c.sourceIdx;
      m["timelineStart"] = qint64(c.timelineStart);
      m["sourceOffset"] = qint64(c.sourceOffset);
      m["length"] = qint64(c.length);
      m["startSec"] = double(c.timelineStart) / sr;
      m["sourceOffsetSec"] = double(c.sourceOffset) / sr;
      m["lengthSec"] = double(c.length) / sr;
      removed.prepend(m);

      if (rippleClose) {
        const qint64 gapLen = c.length;
        const qint64 fromFrame = c.timelineStart;
        for (int j = 0; j < t.clips.size(); ++j) {
          if (j == i)
            continue;
          if (t.clips[j].timelineStart >= fromFrame) {
            t.clips[j].timelineStart -= gapLen;
            if (t.clips[j].timelineStart < 0)
              t.clips[j].timelineStart = 0;
          }
        }
      }

      if (t.recordingClipIdx == i)
        t.recordingClipIdx = -1;
      else if (t.recordingClipIdx > i)
        t.recordingClipIdx--;

      t.clips.remove(i);
    }
    if (rippleClose) {
      std::sort(t.clips.begin(), t.clips.end(),
                [](const Clip &a, const Clip &b) {
                  return a.timelineStart < b.timelineStart;
                });
    }
    invalidateFlatCache(t);
  }

  emit clipsChanged(trackIndex);
  emitRowChanged(trackIndex, {DurationRole, HasAudioRole});
  emit trackPeaksUpdated(trackIndex);
  return removed;
}

void TrackModel::insertClipSnapshots(int trackIndex,
                                     const QVariantList &snapshots,
                                     bool ripple) {
  if (!isValidIndex(trackIndex) || snapshots.isEmpty())
    return;

  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];

    qint64 earliest = std::numeric_limits<qint64>::max();
    qint64 latest = 0;
    for (const QVariant &v : snapshots) {
      const QVariantMap m = v.toMap();
      const qint64 start = m.value("timelineStart", 0).toLongLong();
      const qint64 len = m.value("length", 0).toLongLong();
      if (start < earliest)
        earliest = start;
      if (start + len > latest)
        latest = start + len;
    }
    const qint64 totalLen = latest - earliest;

    if (ripple && totalLen > 0) {
      splitClipsAtLocked(trackIndex, earliest);
      for (auto &clip : t.clips) {
        if (clip.timelineStart >= earliest) {
          clip.timelineStart += totalLen;
        }
      }
    }

    for (const QVariant &v : snapshots) {
      const QVariantMap m = v.toMap();
      Clip c;
      c.sourceIdx = m.value("sourceIdx", -1).toInt();
      c.timelineStart = m.value("timelineStart", 0).toLongLong();
      c.sourceOffset = m.value("sourceOffset", 0).toLongLong();
      c.length = m.value("length", 0).toLongLong();
      c.fadeInLen = m.value("fadeInLen", 0).toLongLong();
      c.fadeOutLen = m.value("fadeOutLen", 0).toLongLong();
      c.gain = m.value("gain", 1.0f).toFloat();
      const QVariantList envList = m.value("envelope").toList();
      for (const QVariant &v : envList) {
        const QVariantMap node = v.toMap();
        c.envelope.append(QPointF(node["x"].toDouble(), node["y"].toDouble()));
      }
      if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
        continue;
      if (c.length <= 0)
        continue;
      t.clips.append(c);
    }
    std::sort(t.clips.begin(), t.clips.end(), [](const Clip &a, const Clip &b) {
      return a.timelineStart < b.timelineStart;
    });
    invalidateFlatCache(t);
  }

  emit clipsChanged(trackIndex);
  emitRowChanged(trackIndex, {DurationRole, HasAudioRole});
  emit trackPeaksUpdated(trackIndex);
}

QVariantList TrackModel::moveClips(int srcTrackIndex,
                                   const QVariantList &clipIndices,
                                   double deltaSec, int dstTrackIndex) {
  QVariantList result;
  if (!isValidIndex(srcTrackIndex) || clipIndices.isEmpty())
    return result;
  if (dstTrackIndex < 0)
    dstTrackIndex = srcTrackIndex;
  if (!isValidIndex(dstTrackIndex))
    return result;

  if (std::abs(deltaSec) < 0.001 && srcTrackIndex == dstTrackIndex) {
    return result;
  }

  const bool sameTrack = (srcTrackIndex == dstTrackIndex);

  QList<int> indices;
  indices.reserve(clipIndices.size());
  for (const QVariant &v : clipIndices)
    indices.append(v.toInt());
  std::sort(indices.begin(), indices.end(), std::greater<int>());

  struct Extracted {
    int originalSourceIdx;
    qint64 timelineStart;
    qint64 sourceOffset;
    qint64 length;
    qint64 fadeInLen;
    qint64 fadeOutLen;
    float gain;
    bool muted;
    QVector<QPointF> envelope;
  };
  QVector<Extracted> extracted;
  QVector<AudioSource> srcCopies;

  {
    QMutexLocker lock(&m_bufMutex);
    Track &src = m_tracks[srcTrackIndex];

    for (int i : indices) {
      if (i < 0 || i >= src.clips.size())
        continue;
      const Clip &c = src.clips[i];
      Extracted e;
      e.originalSourceIdx = c.sourceIdx;
      e.timelineStart = c.timelineStart;
      e.sourceOffset = c.sourceOffset;
      e.length = c.length;
      e.fadeInLen = c.fadeInLen;
      e.fadeOutLen = c.fadeOutLen;
      e.gain = c.gain;
      e.muted = c.muted;
      e.envelope = c.envelope;
      extracted.prepend(e);

      if (!sameTrack && c.sourceIdx >= 0 && c.sourceIdx < src.sources.size()) {
        srcCopies.prepend(src.sources[c.sourceIdx]);
      }

      if (src.recordingClipIdx == i)
        src.recordingClipIdx = -1;
      else if (src.recordingClipIdx > i)
        src.recordingClipIdx--;

      src.clips.remove(i);
    }
    invalidateFlatCache(src);

    Track &dst = m_tracks[dstTrackIndex];
    const int srcSR =
        src.sources.isEmpty() ? 48000 : src.sources.first().sampleRate;
    const int dstSR =
        dst.sources.isEmpty() ? srcSR : dst.sources.first().sampleRate;

    const qint64 deltaFrames = qint64(std::llround(deltaSec * dstSR));

    for (int k = 0; k < extracted.size(); ++k) {
      const Extracted &e = extracted[k];
      Clip c;
      c.timelineStart = std::max<qint64>(0, e.timelineStart + deltaFrames);
      c.sourceOffset = e.sourceOffset;
      c.length = e.length;
      c.fadeInLen = e.fadeInLen;
      c.fadeOutLen = e.fadeOutLen;
      c.gain = e.gain;
      c.muted = e.muted;
      c.envelope = e.envelope;

      if (sameTrack) {
        c.sourceIdx = e.originalSourceIdx;
      } else {
        dst.sources.append(srcCopies[k]);
        c.sourceIdx = dst.sources.size() - 1;
      }
      dst.clips.append(c);
    }

    std::sort(dst.clips.begin(), dst.clips.end(),
              [](const Clip &a, const Clip &b) {
                return a.timelineStart < b.timelineStart;
              });

    for (int i = 0; i < dst.clips.size(); ++i) {
      Clip &clip = dst.clips[i];
      if (i > 0) {
        const Clip &prev = dst.clips[i - 1];
        const qint64 prevEnd = prev.timelineStart + prev.length;
        if (prevEnd > clip.timelineStart) {
          const qint64 overlap = prevEnd - clip.timelineStart;
          clip.fadeInLen = overlap;
          if (clip.fadeInLen + clip.fadeOutLen > clip.length)
            clip.fadeInLen = clip.length - clip.fadeOutLen;
        }
      }

      if (i < dst.clips.size() - 1) {
        const Clip &next = dst.clips[i + 1];
        const qint64 clipEnd = clip.timelineStart + clip.length;
        if (clipEnd > next.timelineStart) {
          const qint64 overlap = clipEnd - next.timelineStart;
          clip.fadeOutLen = overlap;
          if (clip.fadeInLen + clip.fadeOutLen > clip.length)
            clip.fadeOutLen = clip.length - clip.fadeInLen;
        }
      }
    }

    invalidateFlatCache(dst);

    for (const Extracted &e : extracted) {
      const qint64 target = std::max<qint64>(0, e.timelineStart + deltaFrames);
      for (int i = 0; i < dst.clips.size(); ++i) {
        if (dst.clips[i].timelineStart == target &&
            dst.clips[i].length == e.length &&
            dst.clips[i].sourceOffset == e.sourceOffset) {
          result.append(i);
          break;
        }
      }
    }
  }

  emit clipsChanged(srcTrackIndex);
  if (!sameTrack)
    emit clipsChanged(dstTrackIndex);
  emitRowChanged(srcTrackIndex, {DurationRole, HasAudioRole});
  if (!sameTrack)
    emitRowChanged(dstTrackIndex, {DurationRole, HasAudioRole});
  emit trackPeaksUpdated(srcTrackIndex);
  if (!sameTrack)
    emit trackPeaksUpdated(dstTrackIndex);
  return result;
}

bool TrackModel::trimClip(int trackIndex, int clipIndex, double deltaStartSec,
                          double deltaEndSec, bool live) {
  if (!isValidIndex(trackIndex))
    return false;

  bool changed = false;
  double newStartSec = 0.0;
  double newLengthSec = 0.0;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (clipIndex < 0 || clipIndex >= t.clips.size())
      return false;

    Clip &c = t.clips[clipIndex];
    if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
      return false;
    const AudioSource &src = t.sources[c.sourceIdx];

    const int sr = src.sampleRate > 0 ? src.sampleRate : 48000;
    const qint64 totalFrames =
        src.channels > 0 ? src.samplesVec().size() / src.channels : 0;

    qint64 deltaStartFrames = qint64(std::llround(deltaStartSec * sr));
    qint64 deltaEndFrames = qint64(std::llround(deltaEndSec * sr));

    if (deltaStartFrames != 0) {
      qint64 newOffset = c.sourceOffset + deltaStartFrames;
      if (newOffset < 0) {
        deltaStartFrames -= newOffset;
        newOffset = 0;
      }

      qint64 newLength = c.length - deltaStartFrames;
      if (newLength < 1) {
        deltaStartFrames += (newLength - 1);
        newOffset = c.sourceOffset + deltaStartFrames;
        newLength = 1;
      }

      c.sourceOffset = newOffset;
      c.timelineStart += deltaStartFrames;
      c.length = newLength;
      changed = true;
    }

    if (deltaEndFrames != 0) {
      qint64 newLength = c.length + deltaEndFrames;
      if (c.sourceOffset + newLength > totalFrames) {
        newLength = totalFrames - c.sourceOffset;
      }
      if (newLength < 1)
        newLength = 1;

      if (newLength != c.length) {
        c.length = newLength;
        changed = true;
      }
    }

    if (changed) {
      invalidateFlatCache(t);
      newStartSec = (double)c.timelineStart / sr;
      newLengthSec = (double)c.length / sr;
    }
  }

  if (changed) {
    if (!live) {
      emit clipsChanged(trackIndex);
    } else {
      emit clipTrimmed(trackIndex, clipIndex, newStartSec, newLengthSec);
    }
    emitRowChanged(trackIndex, {DurationRole, HasAudioRole});
    emit trackPeaksUpdated(trackIndex);
  }
  return changed;
}

int TrackModel::copyClipToTrack(int srcTrackIndex,
                                const QVariantList &clipIndices,
                                int dstTrackIndex, double timelineStartSec,
                                bool ripple) {
  if (!isValidIndex(srcTrackIndex) || !isValidIndex(dstTrackIndex))
    return 0;
  if (clipIndices.isEmpty())
    return 0;

  int copied = 0;
  {
    QMutexLocker lock(&m_bufMutex);
    const Track &src = m_tracks[srcTrackIndex];
    Track &dst = m_tracks[dstTrackIndex];

    if (src.sources.isEmpty())
      return 0;

    const int srcSR = src.sources.first().sampleRate;
    const int dstSR =
        dst.sources.isEmpty() ? srcSR : dst.sources.first().sampleRate;

    qint64 earliest = std::numeric_limits<qint64>::max();
    qint64 latest = 0;
    for (const QVariant &v : clipIndices) {
      const int ci = v.toInt();
      if (ci < 0 || ci >= src.clips.size())
        continue;
      if (src.clips[ci].timelineStart < earliest)
        earliest = src.clips[ci].timelineStart;
      const qint64 end = src.clips[ci].timelineStart + src.clips[ci].length;
      if (end > latest)
        latest = end;
    }
    if (earliest == std::numeric_limits<qint64>::max())
      return 0;

    const qint64 totalLen = latest - earliest;
    const qint64 pasteFrame = qint64(std::max(0.0, timelineStartSec) * dstSR);

    if (ripple && totalLen > 0) {
      splitClipsAtLocked(dstTrackIndex, pasteFrame);
      for (auto &clip : dst.clips) {
        if (clip.timelineStart >= pasteFrame) {
          clip.timelineStart += totalLen;
        }
      }
    }

    // Mapa de deduplicación: srcSourceIdx → dstSourceIdx.
    QHash<int, int> srcToDstSourceIdx;

    auto findExistingSourceInDst = [&dst](const std::shared_ptr<QVector<float>> &samplesPtr) -> int {
      for (int i = 0; i < dst.sources.size(); ++i) {
        if (dst.sources[i].samples == samplesPtr) return i;
      }
      return -1;
    };

    for (const QVariant &v : clipIndices) {
      const int ci = v.toInt();
      if (ci < 0 || ci >= src.clips.size())
        continue;
      const Clip &srcClip = src.clips[ci];
      if (srcClip.sourceIdx < 0 || srcClip.sourceIdx >= src.sources.size())
        continue;

      int newSourceIdx;
      if (srcToDstSourceIdx.contains(srcClip.sourceIdx)) {
        newSourceIdx = srcToDstSourceIdx[srcClip.sourceIdx];
      } else {
        const AudioSource &srcAudio = src.sources[srcClip.sourceIdx];
        int existing = findExistingSourceInDst(srcAudio.samples);
        if (existing >= 0) {
          newSourceIdx = existing;
        } else {
          AudioSource dstAudio;
          dstAudio.samples = srcAudio.samples;
          dstAudio.sampleRate = srcAudio.sampleRate;
          dstAudio.channels = srcAudio.channels;
          dstAudio.peaks = srcAudio.peaks;
          dstAudio.processedSamples = srcAudio.processedSamples;
          dstAudio.mipmap = srcAudio.mipmap;
          dst.sources.append(std::move(dstAudio));
          newSourceIdx = dst.sources.size() - 1;
        }
        srcToDstSourceIdx[srcClip.sourceIdx] = newSourceIdx;
      }

      const qint64 relOffset = srcClip.timelineStart - earliest;
      Clip dstClip;
      dstClip.sourceIdx = newSourceIdx;
      dstClip.timelineStart = pasteFrame + relOffset;
      dstClip.sourceOffset = srcClip.sourceOffset;
      dstClip.length = srcClip.length;
      dstClip.fadeInLen = srcClip.fadeInLen;
      dstClip.fadeOutLen = srcClip.fadeOutLen;
      dstClip.gain = srcClip.gain;
      dstClip.envelope = srcClip.envelope;
      dst.clips.append(dstClip);
      ++copied;
    }

    std::sort(dst.clips.begin(), dst.clips.end(),
              [](const Clip &a, const Clip &b) {
                return a.timelineStart < b.timelineStart;
              });

    for (int i = 0; i < dst.clips.size(); ++i) {
      Clip &clip = dst.clips[i];
      if (i > 0) {
        const Clip &prev = dst.clips[i - 1];
        const qint64 prevEnd = prev.timelineStart + prev.length;
        if (prevEnd > clip.timelineStart) {
          const qint64 overlap = prevEnd - clip.timelineStart;
          clip.fadeInLen = overlap;
          if (clip.fadeInLen + clip.fadeOutLen > clip.length)
            clip.fadeInLen = clip.length - clip.fadeOutLen;
        }
      }
      if (i < dst.clips.size() - 1) {
        const Clip &next = dst.clips[i + 1];
        const qint64 clipEnd = clip.timelineStart + clip.length;
        if (clipEnd > next.timelineStart) {
          const qint64 overlap = clipEnd - next.timelineStart;
          clip.fadeOutLen = overlap;
          if (clip.fadeInLen + clip.fadeOutLen > clip.length)
            clip.fadeOutLen = clip.length - clip.fadeInLen;
        }
      }
    }

    invalidateFlatCache(dst);
  }

  if (copied > 0) {
    emit clipsChanged(dstTrackIndex);
    emitRowChanged(dstTrackIndex, {DurationRole, HasAudioRole});
    emit trackPeaksUpdated(dstTrackIndex);
  }
  return copied;
}

QVector<float> TrackModel::trackSamples(int index) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(index))
    return {};
  const Track &t = m_tracks[index];
  if (!t.flatCacheValid)
    rebuildFlatCache(t);
  return t.flatCache;
}

const float *TrackModel::trackSamplesData(int index, int *outFrames,
                                           int *outChannels,
                                           int *outSampleRate) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(index)) {
    if (outFrames)
      *outFrames = 0;
    if (outChannels)
      *outChannels = 0;
    if (outSampleRate)
      *outSampleRate = 48000;
    return nullptr;
  }
  const Track &t = m_tracks[index];
  if (!t.flatCacheValid)
    rebuildFlatCache(t);
  const int ch = t.sources.isEmpty() ? 2 : t.sources.first().channels;
  const int sr = t.sources.isEmpty() ? 48000 : t.sources.first().sampleRate;
  if (outFrames)
    *outFrames = ch > 0 ? int(t.flatCache.size() / ch) : 0;
  if (outChannels)
    *outChannels = ch;
  if (outSampleRate)
    *outSampleRate = sr;
  return t.flatCache.constData();
}

// ============================================================================
//  readMixSegment — lectura directa de clips sin flatCache
// ============================================================================
void TrackModel::readMixSegment(int index, qint64 startFrame, int nFrames,
                                float *outBuf) const {
  if (!outBuf || nFrames <= 0) return;

  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(index)) {
    std::memset(outBuf, 0, sizeof(float) * nFrames * 2);
    return;
  }

  const Track &t = m_tracks[index];
  const int ch = t.sources.isEmpty() ? 2 : t.sources.first().channels;

  // Inicializar a silencio
  std::memset(outBuf, 0, sizeof(float) * nFrames * ch);

  if (t.clips.isEmpty() || t.sources.isEmpty()) return;

  const qint64 endFrame = startFrame + nFrames;

  // Iterar solo los clips que se solapan con [startFrame, endFrame)
  for (const auto &c : t.clips) {
    if (c.muted) continue;
    if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size()) continue;

    const qint64 clipEnd = c.timelineStart + c.length;

    // Saltar clips que no se solapan con la ventana solicitada
    if (clipEnd <= startFrame) continue;
    if (c.timelineStart >= endFrame) break; // Los clips están ordenados

    const auto &src = t.sources[c.sourceIdx];
    if (src.channels != ch) continue;
    const qint64 srcTotalFrames = src.samplesSize() / ch;

    // Calcular el rango de solapamiento en coordenadas de línea temporal
    const qint64 ovStart = std::max(c.timelineStart, startFrame);
    const qint64 ovEnd = std::min(clipEnd, endFrame);
    if (ovEnd <= ovStart) continue;

    // Mapear a coordenadas del source
    const qint64 srcOffset = c.sourceOffset + (ovStart - c.timelineStart);
    const qint64 copyN = std::min<qint64>(ovEnd - ovStart,
                                           srcTotalFrames - srcOffset);
    if (copyN <= 0 || srcOffset < 0 || srcOffset >= srcTotalFrames) continue;

    // Punteros al origen y destino
    const float *srcBuf = src.samplesConstData() + srcOffset * ch;
    float *dst = outBuf + (ovStart - startFrame) * ch;

    // Determinar wet/dry del denoiser si aplica
    const float *wetBuf = nullptr;
    float denoiseMix = 0.0f;
    if (t.fxChain && t.fxChain->deNoiser() &&
        t.fxChain->deNoiser()->enabled()) {
      denoiseMix = t.fxChain->deNoiser()->reduction();
      if (denoiseMix > 0.0f &&
          src.processedVec().size() == src.samplesSize()) {
        wetBuf = src.processedVec().constData() + srcOffset * ch;
      } else {
        denoiseMix = 0.0f;
      }
    }

    // Offset del frame dentro del clip (para calcular fades/envelope)
    const qint64 clipLocalStart = ovStart - c.timelineStart;

    if (!c.envelope.isEmpty()) {
      // Envelope mode: mezcla con ganancia de envelope
      const double sr = src.sampleRate > 0 ? double(src.sampleRate) : 48000.0;
      int envIdx = 0;
      const int nNodes = c.envelope.size();
      for (qint64 i = 0; i < copyN; ++i) {
        const double tSec = double(clipLocalStart + i) / sr;
        float g = 1.0f;
        if (nNodes == 1) {
          g = c.envelope[0].y();
        } else {
          while (envIdx < nNodes - 1 && tSec >= c.envelope[envIdx + 1].x()) {
            envIdx++;
          }
          if (envIdx == 0 && tSec <= c.envelope[0].x()) {
            g = c.envelope[0].y();
          } else if (envIdx >= nNodes - 1) {
            g = c.envelope[nNodes - 1].y();
          } else {
            const double t0 = c.envelope[envIdx].x();
            const float v0 = c.envelope[envIdx].y();
            const double t1 = c.envelope[envIdx + 1].x();
            const float v1 = c.envelope[envIdx + 1].y();
            const double frac = (tSec - t0) / (t1 - t0);
            g = float(v0 + frac * (v1 - v0));
          }
        }
        for (int k = 0; k < ch; ++k) {
          float sample = srcBuf[i * ch + k];
          if (wetBuf)
            sample = sample * (1.0f - denoiseMix) + wetBuf[i * ch + k] * denoiseMix;
          dst[i * ch + k] += sample * g;
        }
      }
    } else {
      // Fade in / fade out mode
      const float cg = c.gain;
      const qint64 fin = std::min<qint64>(c.fadeInLen, c.length);
      const qint64 fout = std::min<qint64>(c.fadeOutLen, c.length);

      for (qint64 i = 0; i < copyN; ++i) {
        const qint64 frameInClip = clipLocalStart + i;
        float g = cg;

        // Fade in
        if (frameInClip < fin) {
          g = float(frameInClip + 1) / float(fin) * cg;
        }
        // Fade out
        else if (frameInClip >= c.length - fout) {
          const qint64 fadeFrame = frameInClip - (c.length - fout);
          g = float(fout - fadeFrame) / float(fout) * cg;
        }

        for (int k = 0; k < ch; ++k) {
          float sample = srcBuf[i * ch + k];
          if (wetBuf)
            sample = sample * (1.0f - denoiseMix) + wetBuf[i * ch + k] * denoiseMix;
          dst[i * ch + k] += sample * g;
        }
      }
    }
  }
}

int TrackModel::trackSampleRate(int index) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(index))
    return 48000;
  const Track &t = m_tracks[index];
  return t.sources.isEmpty() ? 48000 : t.sources.first().sampleRate;
}

int TrackModel::trackChannels(int index) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(index))
    return 2;
  const Track &t = m_tracks[index];
  return t.sources.isEmpty() ? 2 : t.sources.first().channels;
}

QVariantList TrackModel::trackPeaks(int index) const {
  QMutexLocker lock(&m_bufMutex);
  QVariantList out;
  if (!isValidIndex(index))
    return out;
  const Track &t = m_tracks[index];
  if (t.clips.isEmpty())
    return out;

  qint64 maxFrames = 0;
  for (const auto &c : t.clips) {
    maxFrames = std::max(maxFrames, c.timelineStart + c.length);
  }
  if (maxFrames <= 0)
    return out;

  const int totalWindows =
      int((maxFrames + PEAK_WINDOW_FRAMES - 1) / PEAK_WINDOW_FRAMES);
  QVector<float> flat(totalWindows, 0.0f);

  for (const auto &c : t.clips) {
    if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
      continue;
    const auto &src = t.sources[c.sourceIdx];
    if (c.length <= 0)
      continue;

    const qint64 srcStart = c.sourceOffset;
    const qint64 srcEnd = c.sourceOffset + c.length;
    const qint64 firstSrcWin = srcStart / PEAK_WINDOW_FRAMES;
    const qint64 lastSrcWin = (srcEnd - 1) / PEAK_WINDOW_FRAMES;
    for (qint64 w = firstSrcWin; w <= lastSrcWin; ++w) {
      if (w < 0 || w >= src.peaks.size())
        continue;
      const qint64 srcWinFrame = w * PEAK_WINDOW_FRAMES;
      const qint64 tlFrame = c.timelineStart + (srcWinFrame - c.sourceOffset);
      const qint64 tlWin = tlFrame / PEAK_WINDOW_FRAMES;
      if (tlWin < 0 || tlWin >= flat.size())
        continue;
      flat[tlWin] = std::max(flat[tlWin], src.peaks[w]);
    }
  }

  out.reserve(flat.size());
  for (float v : flat)
    out.append(v);
  return out;
}

std::shared_ptr<AudioMipmap> TrackModel::clipMipmap(int trackIndex,
                                                    int clipIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return nullptr;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return nullptr;
  const Clip &c = t.clips[clipIndex];
  if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
    return nullptr;
  return t.sources[c.sourceIdx].mipmap;
}

void TrackModel::appendPeak(int index, float /*peak*/) {
  Q_UNUSED(index);
}

qint64 TrackModel::clipCurrentLength(int trackIndex, int clipIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return 0;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return 0;
  return t.clips[clipIndex].length;
}

QVector<float> TrackModel::clipRawPeaks(int trackIndex, int clipIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return {};
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return {};
  const Clip &c = t.clips[clipIndex];
  if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
    return {};
  return t.sources[c.sourceIdx].peaks;
}

void TrackModel::rebuildPeaks(int index) {
  if (!isValidIndex(index))
    return;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[index];
    for (auto &src : t.sources) {
      const qint64 frames =
          src.channels > 0 ? src.samplesVec().size() / src.channels : 0;
      src.peaks.clear();
      updateSourcePeaksInRange(src, 0, frames);
      if (src.mipmap)
        src.mipmap->loadFromVector(src.samplesVec(), src.channels, src.sampleRate);
    }
  }
  emit trackPeaksUpdated(index);
}

void TrackModel::clearPeaks(int index) {
  if (!isValidIndex(index))
    return;
  {
    QMutexLocker lock(&m_bufMutex);
    for (auto &src : m_tracks[index].sources)
      src.peaks.clear();
  }
  emit trackPeaksUpdated(index);
}

void TrackModel::updateTrackLevel(int index, float leftLevel,
                                  float rightLevel) {
  if (!isValidIndex(index))
    return;
  {
    QMutexLocker lock(&m_bufMutex);
    m_tracks[index].levelLeft = leftLevel;
    m_tracks[index].levelRight = rightLevel;
  }
}

float TrackModel::getTrackLevelLeft(int index) const {
  if (!isValidIndex(index))
    return 0.0f;
  QMutexLocker lock(&m_bufMutex);
  return m_tracks[index].levelLeft;
}

float TrackModel::getTrackLevelRight(int index) const {
  if (!isValidIndex(index))
    return 0.0f;
  QMutexLocker lock(&m_bufMutex);
  return m_tracks[index].levelRight;
}

float TrackModel::getTrackPan(int index) const {
  if (!isValidIndex(index))
    return 0.0f;
  return m_tracks[index].pan;
}

void TrackModel::prepareForPlayback() {
  QMutexLocker lock(&m_bufMutex);
  for (const auto &t : m_tracks) {
    if (!t.flatCacheValid)
      rebuildFlatCache(t);
  }
}

void TrackModel::freeFlatCaches() {
  QMutexLocker lock(&m_bufMutex);
  for (auto &t : m_tracks) {
    invalidateFlatCache(t);
  }
  qDebug() << "[TrackModel] Flat caches freed to reclaim RAM";
}

int TrackModel::trackSourceCount(int trackIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return 0;
  return m_tracks[trackIndex].sources.size();
}

QVector<float> TrackModel::trackSourceSamples(int trackIndex, int sourceIdx,
                                              int *outSampleRate,
                                              int *outChannels) const {
  QMutexLocker lock(&m_bufMutex);
  if (outSampleRate)
    *outSampleRate = 48000;
  if (outChannels)
    *outChannels = 2;
  if (!isValidIndex(trackIndex))
    return {};
  const Track &t = m_tracks[trackIndex];
  if (sourceIdx < 0 || sourceIdx >= t.sources.size())
    return {};
  const AudioSource &src = t.sources[sourceIdx];
  if (outSampleRate)
    *outSampleRate = src.sampleRate;
  if (outChannels)
    *outChannels = src.channels;
  return src.samplesVec();
}

bool TrackModel::isSourceDirty(int trackIndex, int sourceIdx) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return false;
  const Track &t = m_tracks[trackIndex];
  if (sourceIdx < 0 || sourceIdx >= t.sources.size())
    return false;
  return t.sources[sourceIdx].isDirty;
}

void TrackModel::setSourceDirty(int trackIndex, int sourceIdx, bool dirty) {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return;
  Track &t = m_tracks[trackIndex];
  if (sourceIdx < 0 || sourceIdx >= t.sources.size())
    return;
  t.sources[sourceIdx].isDirty = dirty;
}

bool TrackModel::isSourceRecording(int trackIndex, int sourceIdx) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return false;
  const Track &t = m_tracks[trackIndex];
  if (t.recordingClipIdx < 0 || t.recordingClipIdx >= t.clips.size())
    return false;
  return t.clips[t.recordingClipIdx].sourceIdx == sourceIdx;
}

void TrackModel::stopRecoveryWorker() {
  if (m_recoveryWorker) {
    delete m_recoveryWorker;
    m_recoveryWorker = nullptr;
  }
}

QVariantMap TrackModel::getSourceSamples(int trackIndex, int sourceIdx) const {
  QMutexLocker lock(&m_bufMutex);
  QVariantMap result;

  if (!isValidIndex(trackIndex)) {
    result["error"] = "Invalid track index";
    return result;
  }

  const Track &t = m_tracks[trackIndex];
  if (sourceIdx < 0 || sourceIdx >= t.sources.size()) {
    result["error"] = "Invalid source index";
    return result;
  }

  const AudioSource &src = t.sources[sourceIdx];

  QVariantList samplesList;
  samplesList.reserve(src.samplesVec().size());
  for (const float &sample : src.samplesVec()) {
    samplesList.append(sample);
  }

  result["samples"] = samplesList;
  result["sampleRate"] = src.sampleRate;
  result["channels"] = src.channels;
  result["success"] = true;

  return result;
}

void TrackModel::reset() {
  beginResetModel();
  {
    QMutexLocker lock(&m_bufMutex);
    m_tracks.clear();
  }
  endResetModel();
  emit countChanged();
  emit armedChanged();
}

int TrackModel::addTrackFromSnapshot(const QVariantMap &meta) {
  beginInsertRows(QModelIndex(), m_tracks.size(), m_tracks.size());
  Track t;
  t.name = meta.value("name", "Pista").toString();
  t.color = QColor(meta.value("color", "#3498db").toString());
  t.inputDeviceId = meta.value("inputDeviceId").toString();
  t.inputDeviceName = meta.value("inputDeviceName").toString();
  t.inputMode = meta.value("inputMode", 0).toInt();
  t.inputChannelIndex = meta.value("inputChannelIndex", -1).toInt();
  t.gain = meta.value("gain", 1.0f).toFloat();
  t.pan = std::max(-1.0f, std::min(1.0f, meta.value("pan", 0.0f).toFloat()));
  t.muted = meta.value("muted", false).toBool();
  t.solo = meta.value("solo", false).toBool();
  t.armed = false;
  t.fxChain = new TrackFxChain(this);
  m_tracks.append(t);
  endInsertRows();
  emit countChanged();
  return m_tracks.size() - 1;
}

int TrackModel::addSourceToTrack(int trackIndex, const QVector<float> &samples,
                                 int sampleRate, int channels) {
  if (!isValidIndex(trackIndex))
    return -1;
  int idx;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    AudioSource s;
    s.samples = std::make_shared<QVector<float>>(samples);
    s.sampleRate = sampleRate;
    s.channels = channels;
    s.mipmap = std::make_shared<AudioMipmap>();
    s.mipmap->loadFromVector(s.samplesVec(), channels, sampleRate);
    t.sources.append(std::move(s));
    idx = t.sources.size() - 1;
    const qint64 frames =
        channels > 0 ? t.sources[idx].samplesVec().size() / channels : 0;
    updateSourcePeaksInRange(t.sources[idx], 0, frames);
    invalidateFlatCache(t);
  }
  emit trackPeaksUpdated(trackIndex);
  return idx;
}

int TrackModel::addSourceToTrack(int trackIndex, QVector<float> &&samples,
                                 int sampleRate, int channels) {
  if (!isValidIndex(trackIndex))
    return -1;
  int idx;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    AudioSource s;
    s.sampleRate = sampleRate;
    s.channels = channels;

    const qint64 totalFloats = samples.size();
    if (totalFloats >= MmapAudioBuffer::MMAP_THRESHOLD_FLOATS) {
      auto mmBuf = std::make_shared<MmapAudioBuffer>();
      if (mmBuf->fromVector(samples.constData(), totalFloats)) {
        s.mmapBuffer = std::move(mmBuf);
        s.samples = std::make_shared<QVector<float>>();
        qDebug() << "[MmapAudioBuffer] Source mapeado en disco:"
                 << totalFloats << "floats (" << (totalFloats * 4 / 1048576) << "MB)";
      } else {
        qWarning() << "[MmapAudioBuffer] mmap falló, usando heap como fallback";
        s.samples = std::make_shared<QVector<float>>(std::move(samples));
      }
    } else {
      s.samples = std::make_shared<QVector<float>>(std::move(samples));
    }

    s.mipmap = std::make_shared<AudioMipmap>();
    if (s.isMmapped()) {
      QVector<float> tmpView(s.mmapBuffer->size());
      std::memcpy(tmpView.data(), s.mmapBuffer->constData(),
                  s.mmapBuffer->size() * sizeof(float));
      s.mipmap->loadFromVector(tmpView, channels, sampleRate);
    } else {
      s.mipmap->loadFromVector(s.samplesVec(), channels, sampleRate);
    }

    t.sources.append(std::move(s));
    idx = t.sources.size() - 1;
    const qint64 frames =
        channels > 0 ? t.sources[idx].samplesSize() / channels : 0;
    updateSourcePeaksInRange(t.sources[idx], 0, frames);
    invalidateFlatCache(t);
  }
  emit trackPeaksUpdated(trackIndex);
  return idx;
}

void TrackModel::addClipRaw(int trackIndex, int sourceIdx, qint64 timelineStart,
                            qint64 sourceOffset, qint64 length,
                            qint64 fadeInLen, qint64 fadeOutLen, float gain,
                            const QVariantList &envelope) {
  if (!isValidIndex(trackIndex) || length <= 0)
    return;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (sourceIdx < 0 || sourceIdx >= t.sources.size())
      return;
    Clip c;
    c.sourceIdx = sourceIdx;
    c.timelineStart = timelineStart;
    c.sourceOffset = sourceOffset;
    c.length = length;
    c.fadeInLen = std::max<qint64>(0, std::min(fadeInLen, length));
    c.fadeOutLen =
        std::max<qint64>(0, std::min(fadeOutLen, length - c.fadeInLen));
    c.gain = gain;
    for (const QVariant &v : envelope) {
      const QVariantMap node = v.toMap();
      c.envelope.append(QPointF(node["x"].toDouble(), node["y"].toDouble()));
    }
    t.clips.append(c);
    invalidateFlatCache(t);
  }
  emit clipsChanged(trackIndex);
  emitRowChanged(trackIndex, {DurationRole, HasAudioRole});
}

double TrackModel::findNearestClipEdge(double timeSec,
                                       double thresholdSec) const {
  QMutexLocker lock(&m_bufMutex);
  double nearestTime = timeSec;
  double minDistance = thresholdSec;

  for (const Track &t : m_tracks) {
    const double sr = t.sources.isEmpty()
                          ? 48000.0
                          : static_cast<double>(t.sources.first().sampleRate);
    if (sr <= 0)
      continue;

    for (const Clip &c : t.clips) {
      double startSec = static_cast<double>(c.timelineStart) / sr;
      double endSec = static_cast<double>(c.timelineStart + c.length) / sr;

      if (std::abs(startSec - timeSec) < minDistance) {
        minDistance = std::abs(startSec - timeSec);
        nearestTime = startSec;
      }
      if (std::abs(endSec - timeSec) < minDistance) {
        minDistance = std::abs(endSec - timeSec);
        nearestTime = endSec;
      }
    }
  }

  return nearestTime;
}

void TrackModel::sortClips(int trackIndex) {
  if (!isValidIndex(trackIndex))
    return;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    std::sort(t.clips.begin(), t.clips.end(), [](const Clip &a, const Clip &b) {
      return a.timelineStart < b.timelineStart;
    });
    invalidateFlatCache(t);
  }
  emit clipsChanged(trackIndex);
  emitRowChanged(trackIndex, {DurationRole, HasAudioRole});
}

int TrackModel::mergeClips(int trackIndex, const QVariantList &clipIndices) {
  if (!isValidIndex(trackIndex) || clipIndices.size() < 2)
    return 0;

  QVector<int> indices;
  indices.reserve(clipIndices.size());
  for (const QVariant &v : clipIndices) {
    const int ci = v.toInt();
    indices.append(ci);
  }
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  if (indices.size() < 2)
    return 0;

  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (t.clips.isEmpty() || t.sources.isEmpty())
      return 0;

    for (int ci : indices) {
      if (ci < 0 || ci >= t.clips.size())
        return 0;
    }

    const int sr = t.sources.first().sampleRate;
    const int ch = t.sources.first().channels;

    qint64 mergeStart = std::numeric_limits<qint64>::max();
    qint64 mergeEnd = 0;
    for (int ci : indices) {
      const Clip &c = t.clips[ci];
      mergeStart = std::min(mergeStart, c.timelineStart);
      mergeEnd = std::max(mergeEnd, c.timelineStart + c.length);
    }
    if (mergeEnd <= mergeStart)
      return 0;

    const qint64 totalFrames = mergeEnd - mergeStart;

    QVector<float> merged(totalFrames * ch, 0.0f);

    for (int ci : indices) {
      const Clip &c = t.clips[ci];
      if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
        continue;
      const auto &src = t.sources[c.sourceIdx];
      if (src.channels != ch)
        continue;

      const qint64 copyN = std::min<qint64>(
          c.length, (src.samplesVec().size() / ch) - c.sourceOffset);
      if (copyN <= 0)
        continue;

      const qint64 dstOffset = c.timelineStart - mergeStart;
      float *dst = merged.data() + dstOffset * ch;

      std::memcpy(dst, src.samplesVec().constData() + c.sourceOffset * ch,
                  sizeof(float) * copyN * ch);

      const float cg = c.gain;
      const qint64 fin = std::min<qint64>(c.fadeInLen, copyN);
      const qint64 fout = std::min<qint64>(c.fadeOutLen, copyN);
      for (qint64 i = 0; i < fin; ++i) {
        const float g = float(i + 1) / float(fin) * cg;
        for (int k = 0; k < ch; ++k)
          dst[i * ch + k] *= g;
      }
      for (qint64 i = fin; i < copyN - fout; ++i) {
        if (cg != 1.0f) {
          for (int k = 0; k < ch; ++k)
            dst[i * ch + k] *= cg;
        }
      }
      for (qint64 i = 0; i < fout; ++i) {
        const float g = float(fout - i) / float(fout) * cg;
        const qint64 f = copyN - fout + i;
        for (int k = 0; k < ch; ++k)
          dst[f * ch + k] *= g;
      }
    }

    AudioSource newSrc;
    newSrc.sampleRate = sr;
    newSrc.channels = ch;
    newSrc.samples = std::make_shared<QVector<float>>(std::move(merged));
    updateSourcePeaksInRange(newSrc, 0, totalFrames);
    newSrc.mipmap = std::make_shared<AudioMipmap>();
    newSrc.mipmap->loadFromVector(newSrc.samplesVec(), ch, sr);
    t.sources.append(std::move(newSrc));
    const int newSrcIdx = t.sources.size() - 1;

    for (int i = indices.size() - 1; i >= 0; --i) {
      t.clips.remove(indices[i]);
    }

    Clip newClip;
    newClip.sourceIdx = newSrcIdx;
    newClip.timelineStart = mergeStart;
    newClip.sourceOffset = 0;
    newClip.length = totalFrames;
    newClip.gain = 1.0f;
    newClip.fadeInLen = 0;
    newClip.fadeOutLen = 0;
    t.clips.append(newClip);

    std::sort(t.clips.begin(), t.clips.end(), [](const Clip &a, const Clip &b) {
      return a.timelineStart < b.timelineStart;
    });

    invalidateFlatCache(t);
  }

  emit clipsChanged(trackIndex);
  emitRowChanged(trackIndex, {DurationRole, HasAudioRole});
  emit trackPeaksUpdated(trackIndex);
  return 1;
}

int TrackModel::mergeAllClips(int trackIndex) {
  if (!isValidIndex(trackIndex))
    return 0;
  const Track &t = m_tracks[trackIndex];
  if (t.clips.size() < 2)
    return t.clips.size();

  QVariantList allIndices;
  for (int i = 0; i < t.clips.size(); ++i)
    allIndices.append(i);
  return mergeClips(trackIndex, allIndices);
}

qint64 TrackModel::removeGaps(int trackIndex) {
  if (trackIndex < 0) {
    qint64 total = 0;
    for (int i = 0; i < m_tracks.size(); ++i)
      total += removeGaps(i);
    return total;
  }

  if (!isValidIndex(trackIndex))
    return 0;

  qint64 removed = 0;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (t.clips.size() < 1)
      return 0;

    std::sort(t.clips.begin(), t.clips.end(), [](const Clip &a, const Clip &b) {
      return a.timelineStart < b.timelineStart;
    });

    qint64 nextStart = 0;
    for (int i = 0; i < t.clips.size(); ++i) {
      const qint64 gap = t.clips[i].timelineStart - nextStart;
      if (gap > 0) {
        t.clips[i].timelineStart = nextStart;
        removed += gap;
      }
      nextStart = t.clips[i].timelineStart + t.clips[i].length;
    }

    if (removed > 0)
      invalidateFlatCache(t);
  }

  if (removed > 0) {
    emit clipsChanged(trackIndex);
    emitRowChanged(trackIndex, {DurationRole, HasAudioRole});
    emit trackPeaksUpdated(trackIndex);
  }
  return removed;
}

double TrackModel::recordingClipLengthSec(int trackIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return -1.0;
  const Track &t = m_tracks[trackIndex];
  if (t.recordingClipIdx < 0 || t.recordingClipIdx >= t.clips.size())
    return -1.0;
  const Clip &c = t.clips[t.recordingClipIdx];
  if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
    return -1.0;
  const int sr = t.sources[c.sourceIdx].sampleRate;
  return sr > 0 ? double(c.length) / sr : -1.0;
}

int TrackModel::recordingClipIndex(int trackIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return -1;
  return m_tracks[trackIndex].recordingClipIdx;
}

double TrackModel::computeTrackLoudnessDb(int trackIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return -120.0;
  const Track &t = m_tracks[trackIndex];
  if (t.clips.isEmpty() || t.sources.isEmpty())
    return -120.0;

  double sumSq = 0.0;
  qint64 sampleCount = 0;

  for (const auto &c : t.clips) {
    if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
      continue;
    const AudioSource &src = t.sources[c.sourceIdx];
    if (src.channels <= 0)
      continue;
    const qint64 srcFrames = src.samplesVec().size() / src.channels;
    const qint64 from = c.sourceOffset;
    const qint64 to = std::min<qint64>(srcFrames, c.sourceOffset + c.length);
    if (to <= from)
      continue;

    for (qint64 f = from; f < to; ++f) {
      for (int ch = 0; ch < src.channels; ++ch) {
        const float s = src.samplesVec()[f * src.channels + ch];
        sumSq += double(s) * double(s);
        ++sampleCount;
      }
    }
  }

  if (sampleCount <= 0)
    return -120.0;
  const double rms = std::sqrt(sumSq / double(sampleCount));
  if (rms < 1e-7)
    return -120.0;
  return 20.0 * std::log10(rms);
}

double TrackModel::computeClipLoudnessLufs(int trackIndex,
                                           int clipIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return -100.0;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return -100.0;
  const Clip &c = t.clips[clipIndex];
  if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
    return -100.0;
  const AudioSource &src = t.sources[c.sourceIdx];
  if (src.channels <= 0)
    return -100.0;

  const int ch = src.channels;
  const int sr = src.sampleRate;
  const qint64 srcFrames = src.samplesVec().size() / ch;
  const qint64 from = c.sourceOffset;
  const qint64 to = std::min<qint64>(srcFrames, c.sourceOffset + c.length);
  const qint64 clipFrames = to - from;
  if (clipFrames < sr / 2)
    return -100.0;

  const double f0k = 1681.974450955533;
  const double Gk = 3.999843853973347;
  const double Qk = 0.7071752369554196;
  const double Kk = std::tan(M_PI * f0k / sr);
  const double Vhk = std::pow(10.0, Gk / 20.0);
  const double Vbk = std::pow(Vhk, 0.4996667741545416);
  const double a0k = 1.0 + Kk / Qk + Kk * Kk;
  double kb0 = (Vhk + Vbk * Kk / Qk + Kk * Kk) / a0k;
  double kb1 = 2.0 * (Kk * Kk - Vhk) / a0k;
  double kb2 = (Vhk - Vbk * Kk / Qk + Kk * Kk) / a0k;
  double ka1 = 2.0 * (Kk * Kk - 1.0) / a0k;
  double ka2 = (1.0 - Kk / Qk + Kk * Kk) / a0k;

  const double f0r = 38.13547087602444;
  const double Qr = 0.5003270373238773;
  const double Kr = std::tan(M_PI * f0r / sr);
  const double a0r = 1.0 + Kr / Qr + Kr * Kr;
  double rb0 = 1.0 / a0r;
  double rb1 = -2.0 / a0r;
  double rb2 = 1.0 / a0r;
  double ra1 = 2.0 * (Kr * Kr - 1.0) / a0r;
  double ra2 = (1.0 - Kr / Qr + Kr * Kr) / a0r;

  double kz1[2] = {}, kz2[2] = {}, rz1[2] = {}, rz2[2] = {};

  const qint64 blockFrames = qint64(0.4 * sr);
  const qint64 hopFrames = qint64(0.1 * sr);
  if (clipFrames < blockFrames)
    return -100.0;

  QVector<double> blockMS;
  blockMS.reserve((clipFrames - blockFrames) / hopFrames + 1);

  QVector<double> kSq(clipFrames * ch, 0.0);
  const int useCh = std::min(ch, 2);
  for (qint64 i = 0; i < clipFrames; ++i) {
    for (int c2 = 0; c2 < useCh; ++c2) {
      double x = src.samplesVec()[(from + i) * ch + c2];
      double yk = kb0 * x + kz1[c2];
      kz1[c2] = kb1 * x - ka1 * yk + kz2[c2];
      kz2[c2] = kb2 * x - ka2 * yk;
      double yr = rb0 * yk + rz1[c2];
      rz1[c2] = rb1 * yk - ra1 * yr + rz2[c2];
      rz2[c2] = rb2 * yk - ra2 * yr;
      kSq[i * useCh + c2] = yr * yr;
    }
  }

  for (qint64 start = 0; start + blockFrames <= clipFrames;
       start += hopFrames) {
    double sum = 0.0;
    for (qint64 i = 0; i < blockFrames; ++i) {
      for (int c2 = 0; c2 < useCh; ++c2) {
        sum += kSq[(start + i) * useCh + c2];
      }
    }
    blockMS.append(sum / double(blockFrames));
  }
  if (blockMS.isEmpty())
    return -100.0;

  auto msToLufs = [](double ms) {
    return ms > 0.0 ? (-0.691 + 10.0 * std::log10(ms)) : -100.0;
  };

  QVector<double> absGated;
  for (double ms : blockMS)
    if (msToLufs(ms) >= -70.0)
      absGated.append(ms);
  if (absGated.isEmpty())
    return -100.0;

  double meanMS = 0.0;
  for (double v : absGated)
    meanMS += v;
  meanMS /= double(absGated.size());
  const double relThresh = msToLufs(meanMS) - 10.0;

  double sumFinal = 0.0;
  int countFinal = 0;
  for (double ms : absGated) {
    if (msToLufs(ms) >= relThresh) {
      sumFinal += ms;
      countFinal++;
    }
  }
  if (countFinal == 0)
    return -100.0;
  return msToLufs(sumFinal / countFinal);
}

double TrackModel::computeClipPeakDb(int trackIndex, int clipIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return -120.0;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return -120.0;
  const Clip &c = t.clips[clipIndex];
  if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
    return -120.0;
  const AudioSource &src = t.sources[c.sourceIdx];
  if (src.channels <= 0)
    return -120.0;
  const int ch = src.channels;
  const qint64 srcFrames = src.samplesVec().size() / ch;
  const qint64 from = c.sourceOffset;
  const qint64 to = std::min<qint64>(srcFrames, c.sourceOffset + c.length);
  if (to <= from)
    return -120.0;

  float maxAbs = 0.0f;
  for (qint64 f = from; f < to; ++f) {
    for (int c2 = 0; c2 < ch; ++c2) {
      const float a = std::abs(src.samplesVec()[f * ch + c2]);
      if (a > maxAbs)
        maxAbs = a;
    }
  }
  if (maxAbs < 1e-8f)
    return -120.0;
  return 20.0 * std::log10(maxAbs);
}

float TrackModel::bakeClipGain(int trackIndex, int clipIndex,
                               float gainFactor) {
  if (!isValidIndex(trackIndex))
    return 1.0f;
  if (std::abs(gainFactor - 1.0f) < 1e-6f)
    return 1.0f;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (clipIndex < 0 || clipIndex >= t.clips.size())
      return 1.0f;
    Clip &c = t.clips[clipIndex];
    if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
      return 1.0f;
    AudioSource &src = t.sources[c.sourceIdx];
    const int ch = src.channels;
    const qint64 srcFrames = src.samplesVec().size() / ch;
    const qint64 from = c.sourceOffset;
    const qint64 to = std::min<qint64>(srcFrames, c.sourceOffset + c.length);

    for (qint64 f = from; f < to; ++f) {
      for (int c2 = 0; c2 < ch; ++c2) {
        src.samplesVec()[f * ch + c2] *= gainFactor;
      }
    }

    if (gainFactor > 1.0f) {
      const float ceiling = 0.891250938f;

      float maxPeak = 0.0f;
      for (qint64 f = from; f < to; ++f) {
        for (int c2 = 0; c2 < ch; ++c2) {
          const float a = std::abs(src.samplesVec()[f * ch + c2]);
          if (a > maxPeak)
            maxPeak = a;
        }
      }

      if (maxPeak > ceiling) {
        for (qint64 f = from; f < to; ++f) {
          for (int c2 = 0; c2 < ch; ++c2) {
            float &s = src.samplesVec()[f * ch + c2];
            if (s > ceiling) {
              s = ceiling;
            } else if (s < -ceiling) {
              s = -ceiling;
            }
          }
        }
        qDebug() << "[DynNorm] True peak limiting (hard clip) aplicado al clip" << clipIndex
                 << "de pista" << trackIndex << "peak=" << maxPeak << "-> ceiling=" << ceiling;
      }
    }

    updateSourcePeaksInRange(src, from, to);

    if (src.mipmap) {
      src.mipmap->loadFromVector(src.samplesVec(), src.channels, src.sampleRate);
    } else {
      src.mipmap = std::make_shared<AudioMipmap>();
      src.mipmap->loadFromVector(src.samplesVec(), src.channels, src.sampleRate);
    }

    invalidateFlatCache(t);
    src.isDirty = true;
  }
  emit trackPeaksUpdated(trackIndex);
  emit clipsChanged(trackIndex);
  return gainFactor;
}

int TrackModel::autoLevelTracks(double targetDb) {
  int affected = 0;
  const int n = m_tracks.size();
  for (int i = 0; i < n; ++i) {
    const double db = computeTrackLoudnessDb(i);
    if (db <= -119.0)
      continue;
    const double deltaDb = targetDb - db;
    double gain = std::pow(10.0, deltaDb / 20.0);
    if (gain < 0.05)
      gain = 0.05;
    if (gain > 8.0)
      gain = 8.0;
    const float oldGain = m_tracks[i].gain;
    float newGain = float(oldGain * gain);
    if (newGain < 0.05f)
      newGain = 0.05f;
    if (newGain > 8.0f)
      newGain = 8.0f;
    m_tracks[i].gain = newGain;
    emitRowChanged(i, {GainRole});
    ++affected;
    qDebug() << "[AutoLevel] Pista" << i << "rms=" << db << "dB ->"
             << "gain*" << gain << "nuevo=" << newGain;
  }
  return affected;
}

float TrackModel::bakeGainIntoSamples(int trackIndex) {
  if (!isValidIndex(trackIndex))
    return 1.0f;
  float oldGain;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    oldGain = t.gain;
    if (std::abs(oldGain - 1.0f) < 1e-6f)
      return 1.0f;

    for (auto &src : t.sources) {
      for (float &s : src.samplesVec())
        s *= oldGain;
      const qint64 frames =
          src.channels > 0 ? src.samplesVec().size() / src.channels : 0;
      src.peaks.clear();
      updateSourcePeaksInRange(src, 0, frames);
      if (src.mipmap) {
        src.mipmap->loadFromVector(src.samplesVec(), src.channels, src.sampleRate);
      } else {
        src.mipmap = std::make_shared<AudioMipmap>();
        src.mipmap->loadFromVector(src.samplesVec(), src.channels, src.sampleRate);
      }
      src.isDirty = true;
    }
    t.gain = 1.0f;
    invalidateFlatCache(t);
  }
  emitRowChanged(trackIndex, {GainRole});
  emit trackPeaksUpdated(trackIndex);
  emit clipsChanged(trackIndex);
  return oldGain;
}

int TrackModel::truncateSilence(int trackIndex, float thresholdDb,
                                double minDurationSec, double truncateToSec) {
  if (!isValidIndex(trackIndex))
    return 0;

  int totalFramesInt = 0, ch = 0, sr = 0;
  const float *data = trackSamplesData(trackIndex, &totalFramesInt, &ch, &sr);
  if (!data || totalFramesInt <= 0 || ch <= 0 || sr <= 0)
    return 0;

  const qint64 totalFrames = totalFramesInt;
  const float threshLin = std::pow(10.0f, thresholdDb / 20.0f);
  const qint64 minSilenceFrames = qint64(minDurationSec * sr);
  const qint64 truncateToFrames = qint64(truncateToSec * sr);

  struct SilenceRegion {
    qint64 start;
    qint64 end;
  };
  QVector<SilenceRegion> regions;

  bool inSilence = false;
  qint64 silenceStart = 0;

  for (qint64 f = 0; f < totalFrames; ++f) {
    float peak = 0.0f;
    for (int c = 0; c < ch; ++c) {
      float v = std::abs(data[f * ch + c]);
      if (v > peak)
        peak = v;
    }

    if (peak < threshLin) {
      if (!inSilence) {
        silenceStart = f;
        inSilence = true;
      }
    } else {
      if (inSilence) {
        const qint64 len = f - silenceStart;
        if (len >= minSilenceFrames) {
          regions.append({silenceStart, f});
        }
        inSilence = false;
      }
    }
  }
  if (inSilence) {
    const qint64 len = totalFrames - silenceStart;
    if (len >= minSilenceFrames) {
      regions.append({silenceStart, totalFrames});
    }
  }

  if (regions.isEmpty())
    return 0;

  QMutexLocker lock(&m_bufMutex);
  Track &t = m_tracks[trackIndex];
  int processed = 0;

  for (int r = regions.size() - 1; r >= 0; --r) {
    const SilenceRegion &reg = regions[r];
    const qint64 silLen = reg.end - reg.start;
    const qint64 keepFrames = std::min(silLen, truncateToFrames);
    const qint64 removeFrames = silLen - keepFrames;

    if (removeFrames <= 0)
      continue;

    const qint64 cutStart = reg.start + keepFrames;
    const qint64 cutEnd = cutStart + removeFrames;

    for (int ci = t.clips.size() - 1; ci >= 0; --ci) {
      Clip &c = t.clips[ci];
      const qint64 clipEnd = c.timelineStart + c.length;

      if (c.timelineStart >= cutEnd) {
        c.timelineStart -= removeFrames;
      } else if (clipEnd <= cutStart) {
      } else if (c.timelineStart >= cutStart && clipEnd <= cutEnd) {
        t.clips.remove(ci);
      } else if (c.timelineStart < cutStart && clipEnd > cutEnd) {
        Clip right;
        right.sourceIdx = c.sourceIdx;
        right.timelineStart = cutStart;
        right.sourceOffset = c.sourceOffset + (cutEnd - c.timelineStart);
        right.length = clipEnd - cutEnd;
        right.fadeOutLen = c.fadeOutLen;
        right.fadeInLen = 0;

        c.length = cutStart - c.timelineStart;
        c.fadeOutLen = 0;

        right.timelineStart -= removeFrames;
        t.clips.insert(ci + 1, right);
      } else if (c.timelineStart < cutStart && clipEnd > cutStart) {
        c.length = cutStart - c.timelineStart;
        c.fadeOutLen = std::min(c.fadeOutLen, c.length);
      } else if (c.timelineStart < cutEnd && clipEnd > cutEnd) {
        const qint64 trimLeft = cutEnd - c.timelineStart;
        c.sourceOffset += trimLeft;
        c.length -= trimLeft;
        c.timelineStart = cutStart;
        c.timelineStart -= removeFrames;
        c.fadeInLen = std::min(c.fadeInLen, c.length);
      }
    }

    processed++;
  }

  std::sort(t.clips.begin(), t.clips.end(), [](const Clip &a, const Clip &b) {
    return a.timelineStart < b.timelineStart;
  });

  invalidateFlatCache(t);
  lock.unlock();

  emit clipsChanged(trackIndex);
  emit trackPeaksUpdated(trackIndex);
  emitRowChanged(trackIndex, {DurationRole, HasAudioRole});

  qDebug() << "[TruncateSilence] Procesadas" << processed << "regiones en pista"
           << trackIndex;
  return processed;
}

QVariant TrackModel::saveTrackAudioState(int trackIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return QVariant();
  const Track &t = m_tracks[trackIndex];

  TrackAudioState state;
  state.sources = t.sources;
  state.clips = t.clips;

  return QVariant::fromValue(state);
}

void TrackModel::restoreTrackAudioState(int trackIndex,
                                        const QVariant &stateVar) {
  if (!isValidIndex(trackIndex) || !stateVar.canConvert<TrackAudioState>())
    return;
  const TrackAudioState state = stateVar.value<TrackAudioState>();

  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    t.sources = state.sources;
    t.clips = state.clips;
    t.recordingClipIdx = -1;
    invalidateFlatCache(t);
  }

  emitRowChanged(trackIndex, {DurationRole, HasAudioRole});
  emit trackPeaksUpdated(trackIndex);
  emit clipsChanged(trackIndex);
}

static void nrFFT(float *re, float *im, int n, bool inverse) {
  int j = 0;
  for (int i = 1; i < n - 1; ++i) {
    int bit = n >> 1;
    while (j & bit) {
      j ^= bit;
      bit >>= 1;
    }
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  const float sign = inverse ? 1.0f : -1.0f;
  for (int len = 2; len <= n; len <<= 1) {
    const float ang = sign * 2.0f * float(M_PI) / len;
    const float wRe = std::cos(ang), wIm = std::sin(ang);
    for (int i = 0; i < n; i += len) {
      float curRe = 1.0f, curIm = 0.0f;
      for (int k = 0; k < len / 2; ++k) {
        int u = i + k, v = i + k + len / 2;
        float tRe = curRe * re[v] - curIm * im[v];
        float tIm = curRe * im[v] + curIm * re[v];
        re[v] = re[u] - tRe;
        im[v] = im[u] - tIm;
        re[u] += tRe;
        im[u] += tIm;
        float newCurRe = curRe * wRe - curIm * wIm;
        curIm = curRe * wIm + curIm * wRe;
        curRe = newCurRe;
      }
    }
  }
  if (inverse) {
    for (int i = 0; i < n; ++i) {
      re[i] /= n;
      im[i] /= n;
    }
  }
}

bool TrackModel::noiseProfileCapture(int trackIndex, int clipIndex,
                                     double startSec, double endSec) {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return false;
  Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return false;
  const Clip &c = t.clips[clipIndex];
  if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
    return false;
  const AudioSource &src = t.sources[c.sourceIdx];
  const int ch = src.channels;
  const int sr = src.sampleRate;
  if (ch <= 0 || sr <= 0)
    return false;

  qint64 frameStart = qint64(startSec * sr);
  qint64 frameEnd = qint64(endSec * sr);
  if (frameEnd <= frameStart) {
    frameStart = 0;
    frameEnd = std::min<qint64>(qint64(0.5 * sr), c.length);
  }
  frameStart = std::clamp<qint64>(frameStart, 0, c.length);
  frameEnd = std::clamp<qint64>(frameEnd, frameStart, c.length);

  const qint64 regionLen = frameEnd - frameStart;
  if (regionLen < NR_FFT_SIZE)
    return false;

  QVector<float> window(NR_FFT_SIZE);
  for (int i = 0; i < NR_FFT_SIZE; ++i)
    window[i] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * i / NR_FFT_SIZE));

  const int bins = NR_FFT_SIZE / 2 + 1;
  m_noiseProfile.fill(0.0f, bins);
  int numWindows = 0;

  QVector<float> re(NR_FFT_SIZE), im(NR_FFT_SIZE);
  const int hop = NR_FFT_SIZE / 2;

  for (qint64 pos = frameStart; pos + NR_FFT_SIZE <= frameEnd; pos += hop) {
    for (int i = 0; i < NR_FFT_SIZE; ++i) {
      const qint64 sIdx = (c.sourceOffset + pos + i) * ch;
      float mono = 0.0f;
      for (int ci = 0; ci < ch; ++ci)
        mono += src.samplesVec()[sIdx + ci];
      mono /= ch;
      re[i] = mono * window[i];
      im[i] = 0.0f;
    }

    nrFFT(re.data(), im.data(), NR_FFT_SIZE, false);

    for (int b = 0; b < bins; ++b) {
      float mag = std::sqrt(re[b] * re[b] + im[b] * im[b]);
      m_noiseProfile[b] += mag;
    }
    numWindows++;
  }

  if (numWindows > 0) {
    for (int b = 0; b < bins; ++b)
      m_noiseProfile[b] /= numWindows;
    emit noiseProfileChanged();
  }

  qDebug() << "[NoiseReduction] Perfil capturado:" << numWindows
           << "ventanas de" << NR_FFT_SIZE << "muestras";
  return true;
}

QVariantList TrackModel::noiseProfileMagnitudes() const {
  QVariantList res;
  for (int b = 0; b < m_noiseProfile.size(); b += 4)
    res.append(m_noiseProfile[b]);
  return res;
}

bool TrackModel::noiseReduce(int trackIndex, int clipIndex, float reductionDb,
                             float sensitivity) {
  if (m_noiseProfile.isEmpty())
    return false;

  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return false;
  Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return false;
  const Clip &c = t.clips[clipIndex];
  if (c.sourceIdx < 0 || c.sourceIdx >= t.sources.size())
    return false;
  AudioSource &src = t.sources[c.sourceIdx];
  const int ch = src.channels;
  const int sr = src.sampleRate;
  if (ch <= 0 || sr <= 0)
    return false;

  const int bins = NR_FFT_SIZE / 2 + 1;
  if (m_noiseProfile.size() != bins)
    return false;

  QVector<float> samplesCopy = src.samplesVec();
  QVector<float> noiseProfileCopy = m_noiseProfile;
  const qint64 sourceOffset = c.sourceOffset;
  const qint64 clipLength = c.length;
  const int sourceIdx = c.sourceIdx;

  lock.unlock();

  QPointer<TrackModel> weakThis(this);

  (void) QtConcurrent::run([weakThis, trackIndex, sourceIdx, samplesCopy = std::move(samplesCopy),
                            noiseProfileCopy = std::move(noiseProfileCopy), ch, sr, bins,
                            sourceOffset, clipLength, reductionDb, sensitivity]() mutable {
      const float reductionLin = std::pow(10.0f, -reductionDb / 20.0f);
      const float sensitivityMul = std::pow(10.0f, sensitivity / 20.0f);

      QVector<float> window(NR_FFT_SIZE);
      for (int i = 0; i < NR_FFT_SIZE; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * i / NR_FFT_SIZE));

      const int hop = NR_FFT_SIZE / 2;
      QVector<float> re(NR_FFT_SIZE), im(NR_FFT_SIZE);
      const qint64 totalSrcFrames = ch > 0 ? samplesCopy.size() / ch : 0;

      QVector<QVector<float>> outBuf(ch);
      for (int ci = 0; ci < ch; ++ci)
        outBuf[ci].fill(0.0f, int(clipLength) + NR_FFT_SIZE);

      for (int ci = 0; ci < ch; ++ci) {
        for (qint64 pos = 0; pos + NR_FFT_SIZE <= clipLength; pos += hop) {
          for (int i = 0; i < NR_FFT_SIZE; ++i) {
            const qint64 sIdx = sourceOffset + pos + i;
            if (sIdx < totalSrcFrames)
              re[i] = samplesCopy[sIdx * ch + ci] * window[i];
            else
              re[i] = 0.0f;
            im[i] = 0.0f;
          }

          nrFFT(re.data(), im.data(), NR_FFT_SIZE, false);

          for (int b = 0; b < bins; ++b) {
            float mag = std::sqrt(re[b] * re[b] + im[b] * im[b]);
            float phase_re = (mag > 1e-10f) ? re[b] / mag : 0.0f;
            float phase_im = (mag > 1e-10f) ? im[b] / mag : 0.0f;

            float noiseThresh = noiseProfileCopy[b] * sensitivityMul;
            float newMag = (mag < noiseThresh) ? (mag * reductionLin) : std::max(0.0f, mag - noiseProfileCopy[b]);

            re[b] = newMag * phase_re;
            im[b] = newMag * phase_im;

            if (b > 0 && b < NR_FFT_SIZE / 2) {
              re[NR_FFT_SIZE - b] = newMag * phase_re;
              im[NR_FFT_SIZE - b] = -newMag * phase_im;
            }
          }

          nrFFT(re.data(), im.data(), NR_FFT_SIZE, true);

          for (int i = 0; i < NR_FFT_SIZE; ++i) {
            if (pos + i < clipLength + NR_FFT_SIZE)
              outBuf[ci][int(pos + i)] += re[i] * window[i] * (2.0f / NR_FFT_SIZE);
          }
        }

        for (qint64 i = 0; i < clipLength; ++i) {
          const qint64 sIdx = (sourceOffset + i) * ch + ci;
          if (sIdx < samplesCopy.size())
            samplesCopy[int(sIdx)] = outBuf[ci][int(i)];
        }
      }

      QMetaObject::invokeMethod(weakThis.data(), [weakThis, trackIndex, sourceIdx, samplesCopy = std::move(samplesCopy), reductionDb, sensitivity]() mutable {
          if (!weakThis) return;
          QMutexLocker mainLock(&weakThis->m_bufMutex);
          if (!weakThis->isValidIndex(trackIndex))
              return;
          Track &t = weakThis->m_tracks[trackIndex];
          if (sourceIdx < 0 || sourceIdx >= t.sources.size())
              return;

          AudioSource &src = t.sources[sourceIdx];
          src.samples = std::make_shared<QVector<float>>(std::move(samplesCopy));
          src.isDirty = true;

          const qint64 frames = src.channels > 0 ? src.samplesVec().size() / src.channels : 0;
          src.peaks.clear();
          weakThis->updateSourcePeaksInRange(src, 0, frames);

          if (src.mipmap)
              src.mipmap->loadFromVector(src.samplesVec(), src.channels, src.sampleRate);
          else {
              src.mipmap = std::make_shared<AudioMipmap>();
              src.mipmap->loadFromVector(src.samplesVec(), src.channels, src.sampleRate);
          }
          weakThis->invalidateFlatCache(t);

          mainLock.unlock();
          emit weakThis->trackPeaksUpdated(trackIndex);
          emit weakThis->clipsChanged(trackIndex);

          qDebug() << "[NoiseReduction] Applied in background successfully. Reduction:"
                   << reductionDb << "dB, Sensitivity:" << sensitivity;
      });
  });

  return true;
}

static QString findFfmpegExecutable() {
  QString ffmpegPath = QCoreApplication::applicationDirPath() + "/ffmpeg";
#ifdef Q_OS_WIN
  ffmpegPath += ".exe";
#endif
  if (QFile::exists(ffmpegPath)) {
    return ffmpegPath;
  }

#ifdef Q_OS_MAC
  const QStringList macPaths = {
      "/opt/homebrew/bin/ffmpeg",
      "/usr/local/bin/ffmpeg",
      "/opt/local/bin/ffmpeg"
  };
  for (const QString &path : macPaths) {
    if (QFile::exists(path)) {
      return path;
    }
  }
#endif

  return QStandardPaths::findExecutable("ffmpeg");
}

#if defined(Q_OS_MAC) || defined(HAVE_COREAUDIO)
static bool readAudioFile_CoreAudio(const QString &path, QVector<float> &outSamples,
                                    int &outSampleRate, int &outChannels) {
  CFURLRef fileURL = QUrl::fromLocalFile(path).toCFURL();
  if (!fileURL) return false;

  ExtAudioFileRef audioFile = nullptr;
  OSStatus status = ExtAudioFileOpenURL(fileURL, &audioFile);
  CFRelease(fileURL);

  if (status != noErr || !audioFile) {
    qWarning() << "[CoreAudio] ExtAudioFileOpenURL falló para:" << path << "status:" << status;
    return false;
  }

  AudioStreamBasicDescription fileFormat;
  UInt32 propSize = sizeof(fileFormat);
  status = ExtAudioFileGetProperty(audioFile, kExtAudioFileProperty_FileDataFormat, &propSize, &fileFormat);
  if (status != noErr) {
    qWarning() << "[CoreAudio] ExtAudioFileGetProperty FileDataFormat falló:" << status;
    ExtAudioFileDispose(audioFile);
    return false;
  }

  outSampleRate = (fileFormat.mSampleRate > 0) ? int(fileFormat.mSampleRate) : 48000;
  outChannels = (fileFormat.mChannelsPerFrame > 0) ? int(fileFormat.mChannelsPerFrame) : 2;

  AudioStreamBasicDescription clientFormat = {};
  clientFormat.mSampleRate = outSampleRate;
  clientFormat.mFormatID = kAudioFormatLinearPCM;
  clientFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  clientFormat.mBitsPerChannel = 32;
  clientFormat.mChannelsPerFrame = outChannels;
  clientFormat.mBytesPerFrame = 4 * outChannels;
  clientFormat.mFramesPerPacket = 1;
  clientFormat.mBytesPerPacket = clientFormat.mBytesPerFrame;

  status = ExtAudioFileSetProperty(audioFile, kExtAudioFileProperty_ClientDataFormat,
                                   sizeof(clientFormat), &clientFormat);
  if (status != noErr) {
    qWarning() << "[CoreAudio] ExtAudioFileSetProperty ClientDataFormat falló:" << status;
    ExtAudioFileDispose(audioFile);
    return false;
  }

  SInt64 totalFrames = 0;
  propSize = sizeof(totalFrames);
  status = ExtAudioFileGetProperty(audioFile, kExtAudioFileProperty_FileLengthFrames,
                                   &propSize, &totalFrames);
  if (status != noErr || totalFrames <= 0) {
    qWarning() << "[CoreAudio] ExtAudioFileGetProperty FileLengthFrames falló:" << status << "totalFrames:" << totalFrames;
    ExtAudioFileDispose(audioFile);
    return false;
  }

  outSamples.resize(totalFrames * outChannels);

  constexpr UInt32 CHUNK_FRAMES = 32768;
  UInt32 framesReadTotal = 0;

  AudioBufferList bufferList;
  bufferList.mNumberBuffers = 1;
  bufferList.mBuffers[0].mNumberChannels = outChannels;

  while (framesReadTotal < UInt32(totalFrames)) {
    UInt32 framesToRead = std::min(CHUNK_FRAMES, UInt32(totalFrames) - framesReadTotal);
    bufferList.mBuffers[0].mDataByteSize = framesToRead * outChannels * sizeof(float);
    bufferList.mBuffers[0].mData = outSamples.data() + (framesReadTotal * outChannels);

    UInt32 framesReadThisChunk = framesToRead;
    status = ExtAudioFileRead(audioFile, &framesReadThisChunk, &bufferList);
    if (status != noErr) {
      qWarning() << "[CoreAudio] Error leyendo frames:" << status;
      break;
    }
    if (framesReadThisChunk == 0) break;
    framesReadTotal += framesReadThisChunk;
  }

  ExtAudioFileDispose(audioFile);

  if (framesReadTotal == 0) {
    qWarning() << "[CoreAudio] 0 frames leídos.";
    return false;
  }

  outSamples.resize(framesReadTotal * outChannels);
  qDebug() << "[CoreAudio] Decodificado exitoso:" << framesReadTotal << "frames ("
           << outChannels << "ch," << outSampleRate << "Hz) de:" << path;
  return true;
}
#endif

static bool readAudioFile_WAV(const QString &path, QVector<float> &outSamples,
                              int &outSampleRate, int &outChannels) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning() << "[WAV] No se pudo abrir:" << path;
    return false;
  }
  if (f.read(4) != "RIFF") {
    qWarning() << "[WAV] No es RIFF:" << path;
    return false;
  }
  f.read(4);
  if (f.read(4) != "WAVE") {
    qWarning() << "[WAV] No es WAVE:" << path;
    return false;
  }

  QDataStream ds(&f);
  ds.setByteOrder(QDataStream::LittleEndian);

  quint16 audioFormat = 0, channels = 0, bitsPerSample = 0;
  quint32 sampleRate = 0;
  qint64 dataOffset = -1;
  qint64 dataSize = 0;

  while (!f.atEnd()) {
    QByteArray id = f.read(4);
    if (id.size() < 4)
      break;
    quint32 sz = 0;
    ds >> sz;
    if (id == "fmt ") {
      ds >> audioFormat;
      quint16 ch;
      ds >> ch;
      channels = ch;
      ds >> sampleRate;
      quint32 br;
      ds >> br;
      quint16 ba;
      ds >> ba;
      ds >> bitsPerSample;
      if (audioFormat == 0xFFFE && sz >= 40) {
        quint16 cbSize;
        ds >> cbSize;
        quint16 validBits;
        ds >> validBits;
        quint32 channelMask;
        ds >> channelMask;
        quint16 subFormat;
        ds >> subFormat;
        audioFormat = subFormat;
        qDebug() << "[WAV] EXTENSIBLE subFormat:" << subFormat
                 << "validBits:" << validBits;
        qint64 remaining = sz - 40;
        if (remaining > 0)
          f.read(remaining);
      } else if (sz > 16) {
        f.read(sz - 16);
      }
    } else if (id == "data") {
      dataSize = sz;
      if (sz == 0xFFFFFFFF)
        dataSize = f.size() - f.pos();
      dataOffset = f.pos();
      break;
    } else {
      f.read(sz);
      if (sz & 1)
        f.read(1);
    }
  }

  qDebug() << "[WAV] format:" << audioFormat << "channels:" << channels
           << "sr:" << sampleRate << "bps:" << bitsPerSample
           << "dataOffset:" << dataOffset << "dataSize:" << dataSize;

  if (dataOffset < 0 || channels == 0) {
    qWarning() << "[WAV] Cabecera inválida";
    return false;
  }
  outSampleRate = int(sampleRate);
  outChannels = int(channels);

  f.seek(dataOffset);
  const int bps = bitsPerSample / 8;
  const qint64 frames = dataSize / (bps * channels);
  outSamples.resize(frames * channels);

  qDebug() << "[WAV] frames:" << frames
           << "total samples:" << (frames * channels);

  if (audioFormat == 3 && bitsPerSample == 32) {
    f.read(reinterpret_cast<char *>(outSamples.data()), dataSize);
    return true;
  }
  if (audioFormat == 1 && bitsPerSample == 16) {
    QByteArray raw = f.read(dataSize);
    const qint16 *p = reinterpret_cast<const qint16 *>(raw.constData());
    const qint64 n = frames * channels;
    for (qint64 i = 0; i < n; ++i) {
      outSamples[i] = float(p[i]) / 32768.0f;
    }
    return true;
  }
  if (audioFormat == 1 && bitsPerSample == 24) {
    QByteArray raw = f.read(dataSize);
    const unsigned char *p =
        reinterpret_cast<const unsigned char *>(raw.constData());
    const qint64 n = frames * channels;
    for (qint64 i = 0; i < n; ++i) {
      const qint32 v = (p[i * 3] | (p[i * 3 + 1] << 8) | (p[i * 3 + 2] << 16))
                       << 8;
      outSamples[i] = float(v >> 8) / 8388608.0f;
    }
    return true;
  }
  qWarning() << "[WAV] Formato no soportado: audioFormat=" << audioFormat
             << "bitsPerSample=" << bitsPerSample;
  return false;
}

bool TrackModel::importAudioFile(int trackIndex, const QString &filePath,
                                 double timelineStartSec) {
  QString resolvedPath = filePath;
  const QUrl url(filePath);
  if (url.isLocalFile()) {
    resolvedPath = url.toLocalFile();
  }

  qDebug() << "[Import] importAudioFile (async started):" << resolvedPath
           << "(original:" << filePath << ")"
           << "track=" << trackIndex << "pos=" << timelineStartSec;

  if (!isValidIndex(trackIndex)) {
    qWarning() << "[Import] Índice de pista inválido:" << trackIndex;
    return false;
  }
  if (!QFileInfo(resolvedPath).isFile()) {
    qWarning() << "[Import] Archivo no encontrado:" << resolvedPath;
    return false;
  }

  QPointer<TrackModel> weakThis(this);

  (void) QtConcurrent::run([weakThis, trackIndex, resolvedPath, timelineStartSec]() {
    QVector<float> samples;
    int sr = 48000, ch = 2;
    bool ok = false;

    const QString ext = QFileInfo(resolvedPath).suffix().toLower();

    if (ext == "wav" || ext == "wave") {
      ok = readAudioFile_WAV(resolvedPath, samples, sr, ch);
    }

#if defined(Q_OS_MAC) || defined(HAVE_COREAUDIO)
    if (!ok) {
      ok = readAudioFile_CoreAudio(resolvedPath, samples, sr, ch);
    }
#endif

    if (!ok) {
      const QString ffmpegPath = findFfmpegExecutable();
      if (ffmpegPath.isEmpty()) {
        qWarning() << "[Import] ffmpeg no encontrado; solo WAV o decodificación nativa soportada.";
        return;
      }

      QString tmpPath;
      {
        QTemporaryFile tmp(QDir::tempPath() + "/import_XXXXXX.wav");
        tmp.setAutoRemove(false);
        if (!tmp.open()) {
          qWarning() << "[Import] No se pudo crear archivo temporal en"
                     << QDir::tempPath();
          return;
        }
        tmpPath = tmp.fileName();
        tmp.close();
      }

      QProcess ffmpeg;
      QStringList args;
      args << "-hide_banner" << "-y"
           << "-i" << resolvedPath << "-f" << "wav"
           << "-acodec" << "pcm_s16le"
           << "-ar" << "48000"
           << "-ac" << "2" << tmpPath;

      ffmpeg.start(ffmpegPath, args);

      if (!ffmpeg.waitForStarted(5000)) {
        qWarning() << "[Import] ffmpeg no arrancó:" << ffmpeg.errorString();
        QFile::remove(tmpPath);
        return;
      }
      if (!ffmpeg.waitForFinished(120000)) {
        qWarning() << "[Import] ffmpeg timeout";
        ffmpeg.kill();
        QFile::remove(tmpPath);
        return;
      }
      if (ffmpeg.exitCode() != 0) {
        qWarning() << "[Import] ffmpeg falló (exit" << ffmpeg.exitCode() << ")";
        QFile::remove(tmpPath);
        return;
      }

      ok = readAudioFile_WAV(tmpPath, samples, sr, ch);
      QFile::remove(tmpPath);
    }

    if (!ok || samples.isEmpty() || ch <= 0) {
      qWarning() << "[Import] Falló la decodificación del archivo:" << resolvedPath;
      return;
    }

    constexpr int TARGET_SR = 48000;
    if (sr != TARGET_SR) {
#ifdef HAVE_SAMPLERATE
      const qint64 srcFrames = samples.size() / ch;
      const double ratio = double(TARGET_SR) / double(sr);
      const qint64 dstFrames = qint64(srcFrames * ratio) + 4;
      QVector<float> resampled(dstFrames * ch);

      SRC_DATA data;
      data.data_in = samples.constData();
      data.input_frames = srcFrames;
      data.data_out = resampled.data();
      data.output_frames = dstFrames;
      data.src_ratio = ratio;
      data.end_of_input = 1;

      const int err = src_simple(&data, SRC_SINC_MEDIUM_QUALITY, ch);
      if (err != 0) {
        qWarning() << "[Import] Resample falló:" << src_strerror(err);
        return;
      }
      resampled.resize(data.output_frames_gen * ch);
      samples = std::move(resampled);
      sr = TARGET_SR;
#else
      qWarning() << "[Import] Archivo a" << sr
                 << "Hz pero libsamplerate no disponible. Usando resample lineal de fallback.";
      const qint64 srcFrames = samples.size() / ch;
      const double ratio = double(TARGET_SR) / double(sr);
      const qint64 dstFrames = qint64(srcFrames * ratio);
      QVector<float> resampled(dstFrames * ch);

      for (qint64 i = 0; i < dstFrames; ++i) {
          const double srcPos = double(i) / ratio;
          const qint64 idx1 = qint64(std::floor(srcPos));
          const qint64 idx2 = std::min(idx1 + 1, srcFrames - 1);
          const float frac = float(srcPos - idx1);

          for (int c = 0; c < ch; ++c) {
              float s1 = samples[idx1 * ch + c];
              float s2 = samples[idx2 * ch + c];
              resampled[i * ch + c] = s1 * (1.0f - frac) + s2 * frac;
          }
      }
      samples = std::move(resampled);
      sr = TARGET_SR;
#endif
    }

    if (weakThis) {
      QMetaObject::invokeMethod(weakThis.data(), [weakThis, trackIndex, samples = std::move(samples), sr, ch, timelineStartSec, resolvedPath]() mutable {
        if (!weakThis) return;
        if (!weakThis->isValidIndex(trackIndex)) {
          qWarning() << "[Import] La pista destino ya no es válida:" << trackIndex;
          return;
        }

        const qint64 frames = samples.size() / ch;
        const int srcIdx = weakThis->addSourceToTrack(trackIndex, std::move(samples), sr, ch);
        if (srcIdx < 0)
          return;

        const qint64 tlStart = qint64(std::max(0.0, timelineStartSec) * sr);
        weakThis->addClipRaw(trackIndex, srcIdx, tlStart, 0, frames);

        qDebug() << "[Import] Importado de forma asíncrona:" << QFileInfo(resolvedPath).fileName()
                 << "(" << frames << "frames," << ch << "ch," << sr << "Hz ) en pista"
                 << trackIndex;
      }, Qt::QueuedConnection);
    }
  });

  return true;
}

void TrackModel::setClipFades(int trackIndex, int clipIndex, double fadeInSec,
                              double fadeOutSec) {
  if (!isValidIndex(trackIndex))
    return;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (clipIndex < 0 || clipIndex >= t.clips.size())
      return;
    Clip &c = t.clips[clipIndex];
    const int sr = t.sources.isEmpty() ? 48000 : t.sources.first().sampleRate;
    qint64 inLen = qint64(std::max(0.0, fadeInSec) * sr);
    qint64 outLen = qint64(std::max(0.0, fadeOutSec) * sr);
    if (inLen + outLen > c.length) {
      if (inLen > c.length)
        inLen = c.length;
      outLen = std::max<qint64>(0, c.length - inLen);
    }
    c.fadeInLen = inLen;
    c.fadeOutLen = outLen;
    invalidateFlatCache(t);
  }
  emit clipsChanged(trackIndex);
  emit trackPeaksUpdated(trackIndex);
}

void TrackModel::setClipGain(int trackIndex, int clipIndex, float gain) {
  if (!isValidIndex(trackIndex))
    return;
  bool changed = false;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (clipIndex < 0 || clipIndex >= t.clips.size())
      return;
    Clip &c = t.clips[clipIndex];
    if (gain < 0.0f)
      gain = 0.0f;
    if (std::abs(c.gain - gain) > 1e-4) {
      c.gain = gain;
      changed = true;
      invalidateFlatCache(t);
    }
  }
  if (changed) {
    emit clipGainChanged(trackIndex, clipIndex);
  }
}

void TrackModel::setClipMuted(int trackIndex, int clipIndex, bool muted) {
  if (!isValidIndex(trackIndex))
    return;
  bool changed = false;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (clipIndex < 0 || clipIndex >= t.clips.size())
      return;
    Clip &c = t.clips[clipIndex];
    if (c.muted != muted) {
      c.muted = muted;
      changed = true;
      invalidateFlatCache(t);
    }
  }
  if (changed) {
    emit clipGainChanged(trackIndex, clipIndex);
  }
}

bool TrackModel::clipMuted(int trackIndex, int clipIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return false;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return false;
  return t.clips[clipIndex].muted;
}

void TrackModel::addEnvelopeNode(int trackIndex, int clipIndex, double timeSec,
                                 float gain, bool autoAnchors) {
  if (!isValidIndex(trackIndex))
    return;
  bool changed = false;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (clipIndex >= 0 && clipIndex < t.clips.size()) {
      Clip &c = t.clips[clipIndex];

      if (autoAnchors && c.envelope.isEmpty()) {
        const int srcIdx = c.sourceIdx;
        const double sr = (srcIdx >= 0 && srcIdx < t.sources.size())
                              ? double(t.sources[srcIdx].sampleRate)
                              : 48000.0;
        const double clipLenSec = double(c.length) / sr;
        const double fadeInSec = double(c.fadeInLen) / sr;
        const double fadeOutSec = double(c.fadeOutLen) / sr;
        const float clipGain = c.gain;

        if (fadeInSec > 0) {
          c.envelope.append(QPointF(0.0, 0.0));
          c.envelope.append(QPointF(fadeInSec, clipGain));
        } else {
          c.envelope.append(QPointF(0.0, clipGain));
        }

        if (fadeOutSec > 0) {
          c.envelope.append(QPointF(clipLenSec - fadeOutSec, clipGain));
          c.envelope.append(QPointF(clipLenSec, 0.0));
        } else {
          c.envelope.append(QPointF(clipLenSec, clipGain));
        }

        c.fadeInLen = 0;
        c.fadeOutLen = 0;
      }

      QPointF pt(timeSec, gain);
      auto it = std::lower_bound(
          c.envelope.begin(), c.envelope.end(), pt,
          [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
      c.envelope.insert(it, pt);
      changed = true;
      invalidateFlatCache(t);
    }
  }
  if (changed) {
    emit clipsChanged(trackIndex);
  }
}

void TrackModel::removeEnvelopeNode(int trackIndex, int clipIndex,
                                    int nodeIndex) {
  if (!isValidIndex(trackIndex))
    return;
  bool changed = false;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (clipIndex >= 0 && clipIndex < t.clips.size()) {
      Clip &c = t.clips[clipIndex];
      if (nodeIndex >= 0 && nodeIndex < c.envelope.size()) {
        c.envelope.remove(nodeIndex);
        changed = true;
        invalidateFlatCache(t);
      }
    }
  }
  if (changed) {
    emit clipsChanged(trackIndex);
  }
}

void TrackModel::setEnvelopeNode(int trackIndex, int clipIndex, int nodeIndex,
                                 double timeSec, float gain) {
  if (!isValidIndex(trackIndex))
    return;
  bool changed = false;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[trackIndex];
    if (clipIndex >= 0 && clipIndex < t.clips.size()) {
      Clip &c = t.clips[clipIndex];
      if (nodeIndex >= 0 && nodeIndex < c.envelope.size()) {
        c.envelope[nodeIndex] = QPointF(timeSec, gain);
        std::sort(
            c.envelope.begin(), c.envelope.end(),
            [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
        changed = true;
        invalidateFlatCache(t);
      }
    }
  }
  if (changed) {
    emit clipsChanged(trackIndex);
  }
}

QVariantList TrackModel::getEnvelopeNodes(int trackIndex, int clipIndex) const {
  QMutexLocker lock(&m_bufMutex);
  QVariantList list;
  if (isValidIndex(trackIndex)) {
    const Track &t = m_tracks[trackIndex];
    if (clipIndex >= 0 && clipIndex < t.clips.size()) {
      const Clip &c = t.clips[clipIndex];
      for (const QPointF &pt : c.envelope) {
        QVariantMap node;
        node["x"] = pt.x();
        node["y"] = pt.y();
        list.append(node);
      }
    }
  }
  return list;
}

double TrackModel::clipFadeInSec(int trackIndex, int clipIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return 0.0;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return 0.0;
  const int sr = t.sources.isEmpty() ? 48000 : t.sources.first().sampleRate;
  return double(t.clips[clipIndex].fadeInLen) / sr;
}

double TrackModel::clipFadeOutSec(int trackIndex, int clipIndex) const {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return 0.0;
  const Track &t = m_tracks[trackIndex];
  if (clipIndex < 0 || clipIndex >= t.clips.size())
    return 0.0;
  const int sr = t.sources.isEmpty() ? 48000 : t.sources.first().sampleRate;
  return double(t.clips[clipIndex].fadeOutLen) / sr;
}

qint64 TrackModel::deleteRegionNoRipple(int index, double startSec,
                                        double endSec) {
  if (endSec <= startSec)
    return 0;

  if (index < 0) {
    qint64 maxRemoved = 0;
    for (int i = 0; i < m_tracks.size(); ++i) {
      if (m_tracks[i].muted)
        continue;
      maxRemoved =
          std::max(maxRemoved, deleteRegionNoRipple(i, startSec, endSec));
    }
    return maxRemoved;
  }
  if (!isValidIndex(index))
    return 0;

  qint64 removedFrames = 0;
  {
    QMutexLocker lock(&m_bufMutex);
    Track &t = m_tracks[index];
    if (t.clips.isEmpty() || t.sources.isEmpty())
      return 0;

    const int sr = t.sources.first().sampleRate;
    const qint64 startFrame = qint64(startSec * sr);
    const qint64 endFrame = qint64(endSec * sr);
    if (endFrame <= startFrame)
      return 0;
    removedFrames = endFrame - startFrame;

    QVector<Clip> out;
    out.reserve(t.clips.size() + t.clips.size());

    for (const auto &c : t.clips) {
      const qint64 cStart = c.timelineStart;
      const qint64 cEnd = c.timelineStart + c.length;

      if (cEnd <= startFrame || cStart >= endFrame) {
        out.append(c);
        continue;
      }

      if (cStart < startFrame) {
        Clip left = c;
        left.length = startFrame - cStart;
        if (left.length > 0)
          out.append(left);
      }
      if (cEnd > endFrame) {
        Clip right = c;
        const qint64 offset = endFrame - cStart;
        right.timelineStart = endFrame;
        right.sourceOffset = c.sourceOffset + offset;
        right.length = cEnd - endFrame;
        if (right.length > 0)
          out.append(right);
      }
    }

    t.clips = out;
    invalidateFlatCache(t);
  }

  emit clipsChanged(index);
  emitRowChanged(index, {DurationRole, HasAudioRole});
  emit trackPeaksUpdated(index);
  return removedFrames;
}

void TrackModel::onRNNoiseFinished(int trackIndex, int sourceIdx,
                                   const QVector<float> &processedSamples) {
  QMutexLocker lock(&m_bufMutex);
  if (!isValidIndex(trackIndex))
    return;
  Track &t = m_tracks[trackIndex];
  if (sourceIdx < 0 || sourceIdx >= t.sources.size())
    return;

  t.sources[sourceIdx].processedSamples = std::make_shared<QVector<float>>(processedSamples);
  m_noiseWorkers.remove(trackIndex);
  invalidateFlatCache(t);
  emit trackPeaksUpdated(trackIndex);
}

void TrackModel::checkRNNoiseProcessing(int trackIndex) {
  if (!isValidIndex(trackIndex))
    return;
  QMutexLocker lock(&m_bufMutex);
  Track &t = m_tracks[trackIndex];
  if (!t.fxChain || !t.fxChain->deNoiser() ||
      !t.fxChain->deNoiser()->enabled() ||
      t.fxChain->deNoiser()->reduction() <= 0.0f) {
    for (auto &src : t.sources) {
      if (src.processedSamples) {
        src.processedSamples->clear();
        src.processedSamples->squeeze();
        src.processedSamples.reset();
      }
    }
    invalidateFlatCache(t);
    return;
  }

  bool startedAny = false;
  for (int s = 0; s < t.sources.size(); ++s) {
    AudioSource &src = t.sources[s];
    if (src.samplesVec().isEmpty())
      continue;
    if (src.processedVec().size() == src.samplesVec().size())
      continue;

    int workerKey = trackIndex * 1000 + s;
    if (m_noiseWorkers.contains(workerKey))
      continue;

    auto *worker = new RNNoiseWorker(this);
    m_noiseWorkers.insert(workerKey, worker);

    connect(worker, &RNNoiseWorker::processingFinished, this,
            [this, workerKey](int tIdx, int sIdx,
                               const QVector<float> &outSamples) {
              onRNNoiseFinished(tIdx, sIdx, outSamples);
              auto *w = m_noiseWorkers.take(workerKey);
              if (w)
                w->deleteLater();
            });

    worker->processOffline(trackIndex, s, src.samplesVec(), src.sampleRate,
                           src.channels);
    startedAny = true;
  }

  if (!startedAny) {
    invalidateFlatCache(t);
  }
}

double TrackModel::detectHumFrequency(int trackIndex, double startSec, double endSec) const {
  int totalFramesInt = 0, ch = 0, sr = 0;
  const float *data = trackSamplesData(trackIndex, &totalFramesInt, &ch, &sr);
  if (!data || totalFramesInt <= 0 || ch <= 0 || sr <= 0)
    return 50.0;

  qint64 startFrame = std::clamp<qint64>(startSec * sr, 0, totalFramesInt);
  qint64 endFrame = std::clamp<qint64>(endSec * sr, 0, totalFramesInt);
  qint64 N = std::min<qint64>(endFrame - startFrame, 2 * sr);
  if (N < 256) {
    return 50.0;
  }

  auto goertzelMag = [](const float *channelData, int step, double freq, int sampleRate, int length) -> double {
    int k = (int)(0.5 + (length * freq) / sampleRate);
    double omega = 2.0 * M_PI * k / length;
    double coeff = 2.0 * std::cos(omega);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < length; ++i) {
      float x = channelData[i * step];
      s0 = x + coeff * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
  };

  const float *subData = data + startFrame * ch;

  double m50 = goertzelMag(subData, ch, 50.0, sr, N);
  double m60 = goertzelMag(subData, ch, 60.0, sr, N);
  double center = (m60 > m50) ? 60.0 : 50.0;

  double bestFreq = center;
  double bestMag = 0.0;
  for (double f = center - 2.0; f <= center + 2.0; f += 0.1) {
    double m = goertzelMag(subData, ch, f, sr, N);
    if (m > bestMag) {
      bestMag = m;
      bestFreq = f;
    }
  }

  return bestFreq;
}

int TrackModel::deClickSelection(int trackIndex, double startSec, double endSec, float thresholdDb, double sensitivity) {
  if (!isValidIndex(trackIndex))
    return 0;

  QMutexLocker lock(&m_bufMutex);
  Track &t = m_tracks[trackIndex];
  if (t.clips.isEmpty() || t.sources.isEmpty())
    return 0;

  const int sr = t.sources.first().sampleRate;
  const int ch = t.sources.first().channels;

  qint64 startFrame = std::clamp<qint64>(startSec * sr, 0, std::numeric_limits<qint64>::max());
  qint64 endFrame = std::clamp<qint64>(endSec * sr, 0, std::numeric_limits<qint64>::max());
  if (endFrame <= startFrame)
    return 0;

  int totalClicks = 0;

  double k = 6.5 - (std::clamp(sensitivity, 0.0, 1.0) * 3.5);
  float absFloor = std::pow(10.0f, thresholdDb / 20.0f);

  for (auto &src : t.sources) {
    if (src.channels != ch) continue;

    qint64 srcFrames = src.samplesVec().size() / ch;

    for (const auto &clip : t.clips) {
      if (clip.sourceIdx < 0 || &t.sources[clip.sourceIdx] != &src) continue;

      qint64 clipTimelineStart = clip.timelineStart;
      qint64 clipTimelineEnd = clip.timelineStart + clip.length;

      qint64 intersectStart = std::max(startFrame, clipTimelineStart);
      qint64 intersectEnd = std::min(endFrame, clipTimelineEnd);
      if (intersectEnd <= intersectStart) continue;

      qint64 fromSourceOffset = clip.sourceOffset + (intersectStart - clip.timelineStart);
      qint64 toSourceOffset = clip.sourceOffset + (intersectEnd - clip.timelineStart);

      qint64 len = toSourceOffset - fromSourceOffset;
      if (len < 128) continue;

      int maxW = std::max(8, (int)(sr * 0.002));
      int minW = 2;
      int margin = 2;

      for (int c2 = 0; c2 < ch; ++c2) {
        float *x = src.samplesVec().data();
        qint64 offset = fromSourceOffset;

        double ema = 0.0;
        int boot = std::min<int>(2048, len);
        for (int i = 1; i < boot; ++i) {
          ema += std::abs(x[(offset + i) * ch + c2] - x[(offset + i - 1) * ch + c2]);
        }
        ema = ema / (boot - 1);
        if (ema < 1e-4) ema = 1e-4;

        double alpha = 1.0 / 2048.0;
        qint64 i = 4;

        while (i < len - maxW - 4) {
          qint64 idx = offset + i;
          float sd = x[idx * ch + c2] - x[(idx - 1) * ch + c2];
          float d = std::abs(sd);

          if (d > ema * k && d > absFloor) {
            float thresh = d * 0.4f;
            qint64 end = i + 1;
            qint64 maxEnd = i + maxW;
            float posMax = sd;
            float negMax = sd;

            while (end < maxEnd && std::abs(x[(offset + end) * ch + c2] - x[(offset + end - 1) * ch + c2]) > thresh) {
              float dd = x[(offset + end) * ch + c2] - x[(offset + end - 1) * ch + c2];
              if (dd > posMax) posMax = dd;
              if (dd < negMax) negMax = dd;
              ++end;
            }

            qint64 w = end - i;
            bool hitMax = (end >= maxEnd);
            bool bipolar = (posMax > thresh && -negMax > thresh);

            if (w >= minW && !hitMax && bipolar) {
              qint64 s = i - margin;
              qint64 e = end + margin;
              if (s < 2) s = 2;
              if (e > len - 2) e = len - 2;

              float x0 = x[(offset + s - 1) * ch + c2];
              float x1 = x[(offset + e) * ch + c2];
              float m0 = 0.5f * (x[(offset + s) * ch + c2] - x[(offset + s - 2) * ch + c2]);
              float m1 = 0.5f * (x[(offset + e + 1) * ch + c2] - x[(offset + e - 1) * ch + c2]);
              float span = e - (s - 1);

              for (qint64 j = s; j < e; ++j) {
                float t_val = (j - (s - 1)) / span;
                float t2 = t_val * t_val;
                float t3 = t2 * t_val;
                float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
                float h10 = t3 - 2.0f * t2 + t_val;
                float h01 = -2.0f * t3 + 3.0f * t2;
                float h11 = t3 - t2;
                x[(offset + j) * ch + c2] = h00 * x0 + h10 * m0 * span + h01 * x1 + h11 * m1 * span;
              }

              totalClicks++;
              i = e + 4;
              continue;
            }
          }

          ema = ema * (1.0 - alpha) + d * alpha;
          if (ema < 1e-4) ema = 1e-4;
          ++i;
        }
      }

      updateSourcePeaksInRange(src, fromSourceOffset, toSourceOffset);

      if (src.mipmap) {
        src.mipmap->loadFromVector(src.samplesVec(), src.channels, src.sampleRate);
      }
      src.isDirty = true;
    }
  }

  if (totalClicks > 0) {
    invalidateFlatCache(t);
    lock.unlock();

    emit trackPeaksUpdated(trackIndex);
    emit clipsChanged(trackIndex);
  }

  return totalClicks;
}
