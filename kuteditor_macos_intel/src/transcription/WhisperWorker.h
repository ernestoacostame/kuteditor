#pragma once

#include <QObject>
#include <QRunnable>
#include <QVector>
#include <QString>
#include <QVariantList>

/**
 * Segmento de transcripción con timestamps.
 * Cada segmento corresponde a una frase o fragmento devuelto por Whisper.
 */
struct TranscriptSegment {
    double startSec = 0.0;
    double endSec   = 0.0;
    QString text;
};

/**
 * WhisperWorker: ejecuta la inferencia de whisper.cpp en un hilo del
 * QThreadPool global. Recibe audio en float32 mono 16 kHz y emite
 * los segmentos resultantes vía signal.
 *
 * Uso:
 *   auto *w = new WhisperWorker(modelPath, samples16k, trackIdx);
 *   connect(w, &WhisperWorker::finished, this, &MyClass::onTranscription);
 *   QThreadPool::globalInstance()->start(w);
 *
 * Se auto-destruye tras ejecutar (autoDelete = true).
 */
class WhisperWorker : public QObject, public QRunnable
{
    Q_OBJECT
public:
    /**
     * @param modelPath  Ruta al archivo .bin del modelo Whisper (ggml).
     * @param samples    Audio en float32, mono, 16 kHz.
     * @param trackIndex Índice de la pista (para identificar el resultado).
     * @param language   Código ISO 639-1 ("es", "en", "auto").
     */
    explicit WhisperWorker(const QString &modelPath,
                           const QVector<float> &samples,
                           int trackIndex,
                           const QString &language = "auto",
                           bool useGpu = false,
                           QObject *parent = nullptr);
    ~WhisperWorker() override = default;

    void run() override;

signals:
    /// Emitida al completar la transcripción con éxito.
    void finished(int trackIndex, QVector<TranscriptSegment> segments);

    /// Emitida en caso de error (modelo no encontrado, fallo de inferencia).
    void failed(int trackIndex, QString errorMessage);

    /// Progreso de 0.0 a 1.0 (estimación basada en duración del audio).
    void progressChanged(int trackIndex, float progress);

private:
    QString m_modelPath;
    QVector<float> m_samples;
    int m_trackIndex;
    QString m_language;
    bool m_useGpu;
};
