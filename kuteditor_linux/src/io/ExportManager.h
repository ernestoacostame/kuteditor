#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class TrackModel;
class AudioEngine;
class ChapterModel;

/**
 * ExportManager: hace el bounce de las pistas a un archivo.
 *
 * Formatos:
 *  - WAV (PCM 16/24/32 o float32): nativo, sin dependencias.
 *  - MP3 / OGG / FLAC / M4A: vía ffmpeg como subproceso (QProcess).
 *    Requiere que el usuario tenga ffmpeg en PATH.
 *
 * Opciones:
 *  - sampleRate: 44100 o 48000 (el proyecto graba a 48k; se resample para 44.1).
 *  - normalizeLUFS: si > -100, se normaliza con gain uniforme para alcanzar
 *                   ese objetivo LUFS integrado (según BS.1770-4).
 *  - metadata: mapa con title/artist/album/comment/genre/year; los formatos
 *              que soportan tags los incrustarán (ffmpeg con -metadata).
 *
 * La normalización LUFS implementa el medidor BS.1770-4 (pre-filter K +
 * RLB high-pass + mean-square gating a -70 / relativo -10).
 */
class ExportManager : public QObject
{
    Q_OBJECT
public:
    explicit ExportManager(QObject *parent = nullptr);

    void setTrackModel(TrackModel *m)  { m_trackModel  = m; }
    void setAudioEngine(AudioEngine *e) { m_audioEngine = e; }
    void setChapterModel(ChapterModel *c) { m_chapterModel = c; }

    /**
     * Exporta con opciones desde QML.
     *   opts.format        : "wav" | "mp3" | "ogg" | "flac" | "m4a"
     *   opts.sampleRate    : 44100 | 48000
     *   opts.bitDepth      : 16 | 24 | 32 (solo WAV; ignorado en los demás)
     *   opts.normalizeLUFS : valor objetivo LUFS, o 0/100/NaN para desactivar.
     *                        Valores típicos: -14 (streaming), -16 (podcast),
     *                        -23 (broadcast EBU), -24 (broadcast USA).
     *   opts.metadata      : QVariantMap con claves title/artist/album/...
     */
    Q_INVOKABLE bool exportWithOptions(const QString &filePath,
                                       const QVariantMap &opts);

    // API vieja mantenida por compatibilidad.
    Q_INVOKABLE bool exportToWAV(const QString &filePath);

    Q_INVOKABLE QString lastError() const { return m_lastError; }

signals:
    void exportProgress(int percent);
    void exportFinished(bool success, const QString &filePath, const QString &errorMessage);

private:
    /// Mezcla todas las pistas en un único buffer interleaved float32.
    bool mixDown(QVector<float> &out, int &sampleRate, int &channels);

    /// Resample simple lineal de srcRate a dstRate (solo 48k↔44.1k).
    /// Suficiente para export final; no es alta calidad pero funcional.
    void resampleLinear(const QVector<float> &in, int srcRate,
                        QVector<float> &out, int dstRate, int channels);

    /// Calcula LUFS integrado según BS.1770-4. Devuelve valor en LUFS
    /// (negativo). Si no se puede medir, devuelve -100.
    double measureLUFS(const QVector<float> &samples, int sampleRate, int channels);

    /// Aplica gain uniforme al buffer para llevar el LUFS medido al targetLUFS.
    void applyLUFSNormalization(QVector<float> &samples, int sampleRate,
                                int channels, double targetLUFS);

    /// Escribe un WAV con el bitDepth indicado.
    bool writeWavFile(const QString &filePath, const QVector<float> &samples,
                      int sampleRate, int channels, int bitDepth);

    /// Exporta vía ffmpeg: escribe stdin con F32LE, ffmpeg codifica al formato.
    bool exportViaFfmpeg(const QString &filePath, const QVector<float> &samples,
                         int sampleRate, int channels, const QString &format,
                         const QVariantMap &metadata);

    TrackModel *m_trackModel = nullptr;
    AudioEngine *m_audioEngine = nullptr;
    ChapterModel *m_chapterModel = nullptr;
    QString m_lastError;
};
