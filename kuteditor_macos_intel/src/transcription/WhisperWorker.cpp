#include "WhisperWorker.h"

#include <QDebug>
#include <QFile>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <thread>

#ifdef HAVE_WHISPER
#include "whisper.h"
#include <ggml-backend.h>
#endif

WhisperWorker::WhisperWorker(const QString &modelPath,
                             const QVector<float> &samples,
                             int trackIndex,
                             const QString &language,
                             bool useGpu,
                             QObject *parent)
    : QObject(parent)
    , m_modelPath(modelPath)
    , m_samples(samples)
    , m_trackIndex(trackIndex)
    , m_language(language)
    , m_useGpu(useGpu)
{
    setAutoDelete(true);
}

void WhisperWorker::run()
{
#ifdef HAVE_WHISPER
    // ---- Validar modelo ----
    if (!QFile::exists(m_modelPath)) {
        emit failed(m_trackIndex,
                    tr("Modelo Whisper no encontrado: %1").arg(m_modelPath));
        return;
    }

    if (m_samples.isEmpty()) {
        emit failed(m_trackIndex, tr("No hay audio para transcribir."));
        return;
    }

    QElapsedTimer timer;
    timer.start();

    // ---- Cargar explícitamente backends de ggml ----
    QString appDir = QCoreApplication::applicationDirPath();
    qDebug() << "[WhisperWorker] Cargando backends de ggml desde:" << appDir;
    ggml_backend_load_all_from_path(appDir.toUtf8().constData());

    // ---- Inicializar contexto Whisper ----
    struct whisper_context_params cparams = whisper_context_default_params();
    // Usar GPU si está disponible (NO-OP si se compiló sin soporte)
    cparams.use_gpu = m_useGpu;

    struct whisper_context *ctx = whisper_init_from_file_with_params(
        m_modelPath.toUtf8().constData(), cparams);

    if (!ctx) {
        emit failed(m_trackIndex,
                    tr("Error al cargar el modelo Whisper: %1").arg(m_modelPath));
        return;
    }

    emit progressChanged(m_trackIndex, 0.1f);

    // ---- Configurar parámetros de inferencia ----
    struct whisper_full_params wparams =
        whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    // Idioma explícito para probar si el auto-detect falla
    wparams.language = "es";
    wparams.detect_language = false;

    // Usaremos los parámetros estrictamente por defecto (salvo el idioma y el progreso)
    // para descartar que alguna optimización rompa la salida en tu versión 1.8.4.
    wparams.n_threads = std::min(4, (int)std::thread::hardware_concurrency());

    // Callback de progreso (whisper.cpp lo llama periódicamente)
    struct ProgressData {
        WhisperWorker *self;
        int trackIndex;
    };
    ProgressData pd { this, m_trackIndex };

    wparams.progress_callback = [](struct whisper_context * /*ctx*/,
                                   struct whisper_state   * /*state*/,
                                   int progress, void *user_data) {
        auto *data = static_cast<ProgressData*>(user_data);
        // progress va de 0 a 100 en whisper.cpp
        float p = 0.1f + 0.85f * (progress / 100.0f);
        emit data->self->progressChanged(data->trackIndex, p);
    };
    wparams.progress_callback_user_data = &pd;

    emit progressChanged(m_trackIndex, 0.15f);

    // ---- Ejecutar inferencia ----
    int ret = whisper_full(ctx, wparams,
                           m_samples.constData(), m_samples.size());

    if (ret != 0) {
        whisper_free(ctx);
        emit failed(m_trackIndex,
                    tr("Error de inferencia Whisper (código %1)").arg(ret));
        return;
    }

    emit progressChanged(m_trackIndex, 0.95f);

    // ---- Extraer segmentos ----
    const int nSegments = whisper_full_n_segments(ctx);
    QVector<TranscriptSegment> segments;
    segments.reserve(nSegments);

    for (int i = 0; i < nSegments; ++i) {
        TranscriptSegment seg;
        seg.startSec = whisper_full_get_segment_t0(ctx, i) / 100.0;
        seg.endSec   = whisper_full_get_segment_t1(ctx, i) / 100.0;
        seg.text     = QString::fromUtf8(whisper_full_get_segment_text(ctx, i)).trimmed();

        qDebug() << "[WhisperWorker] Segmento raw" << i << "Start:" << seg.startSec << "End:" << seg.endSec << "Text:" << seg.text;

        // Ignorar segmentos vacíos o de solo ruido
        if (!seg.text.isEmpty() && seg.endSec > seg.startSec) {
            segments.append(seg);
        }
    }

    whisper_free(ctx);

    qDebug() << "[WhisperWorker] Transcripción completada:"
             << segments.size() << "segmentos en"
             << timer.elapsed() << "ms";

    emit progressChanged(m_trackIndex, 1.0f);
    emit finished(m_trackIndex, segments);

#else
    // whisper.cpp no compilado — no debería llegar aquí, pero por seguridad
    emit failed(m_trackIndex,
                tr("Kut se compiló sin soporte de transcripción (whisper.cpp)."));
#endif
}
