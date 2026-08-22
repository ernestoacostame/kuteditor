#include "ExportManager.h"
#include "audio/AudioEngine.h"
#include "audio/TrackFxChain.h"
#include "ui/TrackModel.h"
#include "ui/ChapterModel.h"

#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <QtEndian>
#include <QProcess>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QDir>
#include <cmath>
#include <cstring>
#include <algorithm>

#ifdef HAVE_SAMPLERATE
#include <samplerate.h>
#endif

#ifdef HAVE_TAGLIB
#include "io/ChapterTagger.h"
#endif

ExportManager::ExportManager(QObject *parent) : QObject(parent) {}

// ============================================================================
//  Mixdown
// ============================================================================
bool ExportManager::mixDown(QVector<float> &out, int &sampleRate, int &channels)
{
    if (!m_trackModel) {
        m_lastError = tr("Sin modelo de pistas");
        return false;
    }

    const int trackCount = m_trackModel->rowCount();
    if (trackCount == 0) {
        m_lastError = tr("No hay pistas");
        return false;
    }

    // Descubrir tamaño y parámetros comunes.
    sampleRate = AudioEngine::SAMPLE_RATE;
    channels   = AudioEngine::CHANNELS;
    qint64 maxFrames = 0;
    for (int i = 0; i < trackCount; ++i) {
        int f = 0, ch = 0, sr = 0;
        m_trackModel->trackSamplesData(i, &f, &ch, &sr);
        if (f > maxFrames) maxFrames = f;
    }
    if (maxFrames <= 0) {
        m_lastError = tr("Ninguna pista contiene audio");
        return false;
    }

    // Si hay solos, solo esas pistas se mezclan.
    const bool anySolo = m_trackModel->anyTrackInSolo();

    out.fill(0.0f, maxFrames * channels);

    for (int t = 0; t < trackCount; ++t) {
        const auto snap = m_trackModel->mixerSnapshot(t);
        if (!snap.valid || snap.muted) continue;
        if (anySolo && !snap.solo) continue;

        int f = 0, ch = 0, sr = 0;
        const float *src = m_trackModel->trackSamplesData(t, &f, &ch, &sr);
        if (!src || f <= 0 || ch <= 0) continue;

        const qint64 copyFrames = std::min<qint64>(f, maxFrames);

        // Pan law: constant power.
        // pan = 0 → centro (L=R=1.0), pan = -1 → full left, pan = +1 → full right
        // gainL = cos(pan * π/4 + π/4), gainR = sin(pan * π/4 + π/4)
        // Simplificado: gainL = cos((pan+1) * π/4), gainR = sin((pan+1) * π/4)
        const float angle = (snap.pan + 1.0f) * 0.25f * float(M_PI);
        const float panL = std::cos(angle) * snap.gain;
        const float panR = std::sin(angle) * snap.gain;

        if (ch == channels && channels == 2 && sr == sampleRate) {
            // Estéreo → estéreo con pan
            for (qint64 i = 0; i < copyFrames; ++i) {
                out[i * 2 + 0] += src[i * ch + 0] * panL;
                out[i * 2 + 1] += src[i * ch + 1] * panR;
            }
        } else if (ch >= 1 && channels == 2) {
            // Mono → estéreo con pan
            for (qint64 i = 0; i < copyFrames; ++i) {
                const float v = src[i * ch];
                out[i * 2 + 0] += v * panL;
                out[i * 2 + 1] += v * panR;
            }
        } else {
            // Fallback: mono out o ch == channels != 2
            for (qint64 i = 0; i < copyFrames; ++i) {
                for (int c = 0; c < channels; ++c) {
                    out[i * channels + c] += src[i * std::min(ch, channels) + std::min(c, ch - 1)] * snap.gain;
                }
            }
        }

        // --- Aplicar FX por pista durante export ---
        TrackFxChain *fxc = m_trackModel->trackFxChain(t);
        if (fxc && fxc->hasActiveEffects()) {
            // Necesitamos aplicar FX solo a la contribución de esta pista.
            // Extraemos la contribución, aplicamos FX, y la volvemos a poner.
            // Para simplificar, procesamos por bloques.
            const int blockSize = 1024;
            QVector<float> trackBuf(copyFrames * 2);

            // Re-mezclar esta pista sola en trackBuf
            if (ch == 2 && sr == sampleRate) {
                for (qint64 i = 0; i < copyFrames; ++i) {
                    trackBuf[i * 2 + 0] = src[i * ch + 0] * panL;
                    trackBuf[i * 2 + 1] = src[i * ch + 1] * panR;
                }
            } else {
                for (qint64 i = 0; i < copyFrames; ++i) {
                    const float v = src[i * ch];
                    trackBuf[i * 2 + 0] = v * panL;
                    trackBuf[i * 2 + 1] = v * panR;
                }
            }

            // Restar la mezcla sin FX del bus
            for (qint64 i = 0; i < copyFrames; ++i) {
                out[i * 2 + 0] -= trackBuf[i * 2 + 0];
                out[i * 2 + 1] -= trackBuf[i * 2 + 1];
            }

            // Aplicar FX por bloques
            fxc->resetAll();
            for (qint64 pos = 0; pos < copyFrames; pos += blockSize) {
                const int n = std::min<qint64>(blockSize, copyFrames - pos);

                AutoDuckEffect *ad = fxc->autoDuck();
                if (ad && ad->enabled()) {
                    const int ctrlIdx = ad->controlTrack();
                    if (ctrlIdx >= 0 && ctrlIdx != t) {
                        const auto ctrlSnap = m_trackModel->mixerSnapshot(ctrlIdx);
                        const bool ctrlSilenced = !ctrlSnap.valid || ctrlSnap.muted ||
                                                  (anySolo && !ctrlSnap.solo);
                        if (ctrlSilenced) {
                            ad->reset();
                        } else {
                            int ctrlFrames = 0, ctrlCh = 0, ctrlSR = 0;
                            const float *ctrlSrc = m_trackModel->trackSamplesData(
                                ctrlIdx, &ctrlFrames, &ctrlCh, &ctrlSR);

                            if (ctrlSrc && ctrlFrames > 0 && pos < ctrlFrames) {
                                const qint64 ctrlAvail = std::min<qint64>(n, ctrlFrames - pos);
                                QVector<float> scBuf(n * 2, 0.0f);

                                if (ctrlCh == 2) {
                                    const float *s = ctrlSrc + pos * 2;
                                    for (qint64 f = 0; f < ctrlAvail; ++f) {
                                        scBuf[f * 2 + 0] = s[f * 2 + 0];
                                        scBuf[f * 2 + 1] = s[f * 2 + 1];
                                    }
                                } else {
                                    const float *s = ctrlSrc + pos * ctrlCh;
                                    for (qint64 f = 0; f < ctrlAvail; ++f) {
                                        scBuf[f * 2 + 0] = s[f * ctrlCh];
                                        scBuf[f * 2 + 1] = s[f * ctrlCh];
                                    }
                                }
                                ad->feedSidechain(scBuf.data(), n, 2, sampleRate);
                            } else {
                                ad->reset();
                            }
                        }
                    } else {
                        ad->reset();
                    }
                }

                // process() aplica todos los FX de la cadena excepto AutoDuck
                // (cuyo process() es un no-op; requiere processDuck() explícito)
                fxc->process(trackBuf.data() + pos * 2, n, 2, sampleRate);

                // Aplicar la atenuación real del AutoDuck
                if (ad && ad->enabled()) {
                    ad->processDuck(trackBuf.data() + pos * 2, n, 2, sampleRate);
                }
            }

            // Sumar de nuevo con FX aplicados
            for (qint64 i = 0; i < copyFrames; ++i) {
                out[i * 2 + 0] += trackBuf[i * 2 + 0];
                out[i * 2 + 1] += trackBuf[i * 2 + 1];
            }
        }

        emit exportProgress(5 + int(60.0 * (t + 1) / trackCount));
        QCoreApplication::processEvents();
    }

    // --- Aplicar AutoDuck master y cadena de efectos master durante export ---
    // ORDEN (debe coincidir con AudioEngine::playbackMix):
    //   1) AutoDuck master (sidechain entre pistas) — opera sobre el mix crudo
    //   2) Cadena de efectos master (Trim, HPF, Compressor, EQ, Limiter, etc.)
    if (m_audioEngine) {
        const int blockSize = 1024;

        // =====================================================================
        //  1) AutoDuck master
        // =====================================================================
        AutoDuckEffect *masterAD = m_audioEngine->autoDuck();
        const bool masterDuckActive = masterAD && masterAD->enabled() &&
            masterAD->controlTrack() >= 0 && masterAD->targetTrack() >= 0 &&
            masterAD->controlTrack() != masterAD->targetTrack();

        if (masterDuckActive) {
            masterAD->reset();
            const int ctrlIdx = masterAD->controlTrack();
            const int tgtIdx  = masterAD->targetTrack();
            const auto ctrlSnap = m_trackModel->mixerSnapshot(ctrlIdx);
            const auto tgtSnap  = m_trackModel->mixerSnapshot(tgtIdx);
            const bool ctrlSilenced = !ctrlSnap.valid || ctrlSnap.muted ||
                                      (anySolo && !ctrlSnap.solo);
            const bool tgtSilenced  = !tgtSnap.valid || tgtSnap.muted ||
                                      (anySolo && !tgtSnap.solo);

            if (!ctrlSilenced && !tgtSilenced) {
                // Reconstruir el buffer de la pista objetivo tal como quedó en el mix
                // (con gain, pan, y per-track FX aplicados).
                int tgtF = 0, tgtCh = 0, tgtSR = 0;
                const float *tgtSrc = m_trackModel->trackSamplesData(
                    tgtIdx, &tgtF, &tgtCh, &tgtSR);

                if (tgtSrc && tgtF > 0) {
                    const qint64 tgtCopy = std::min<qint64>(tgtF, maxFrames);
                    const float tgtAngle = (tgtSnap.pan + 1.0f) * 0.25f * float(M_PI);
                    const float tgtPanL = std::cos(tgtAngle) * tgtSnap.gain;
                    const float tgtPanR = std::sin(tgtAngle) * tgtSnap.gain;

                    QVector<float> tgtBuf(maxFrames * 2, 0.0f);
                    if (tgtCh == 2 && tgtSR == sampleRate) {
                        for (qint64 i = 0; i < tgtCopy; ++i) {
                            tgtBuf[i * 2 + 0] = tgtSrc[i * 2 + 0] * tgtPanL;
                            tgtBuf[i * 2 + 1] = tgtSrc[i * 2 + 1] * tgtPanR;
                        }
                    } else {
                        for (qint64 i = 0; i < tgtCopy; ++i) {
                            const float v = tgtSrc[i * tgtCh];
                            tgtBuf[i * 2 + 0] = v * tgtPanL;
                            tgtBuf[i * 2 + 1] = v * tgtPanR;
                        }
                    }

                    // Si la pista objetivo tiene FX activos, aplicarlos
                    TrackFxChain *tgtFxc = m_trackModel->trackFxChain(tgtIdx);
                    if (tgtFxc && tgtFxc->hasActiveEffects()) {
                        tgtFxc->resetAll();
                        for (qint64 pos = 0; pos < tgtCopy; pos += blockSize) {
                            const int n = std::min<qint64>(blockSize, tgtCopy - pos);
                            // AutoDuck per-track de la pista objetivo (si tiene)
                            AutoDuckEffect *tgtAD = tgtFxc->autoDuck();
                            if (tgtAD && tgtAD->enabled()) {
                                const int tgtCtrlIdx = tgtAD->controlTrack();
                                if (tgtCtrlIdx >= 0 && tgtCtrlIdx != tgtIdx) {
                                    int tgtCtrlF = 0, tgtCtrlCh = 0, tgtCtrlSR = 0;
                                    const float *tgtCtrlSrc = m_trackModel->trackSamplesData(
                                        tgtCtrlIdx, &tgtCtrlF, &tgtCtrlCh, &tgtCtrlSR);
                                    if (tgtCtrlSrc && tgtCtrlF > 0 && pos < tgtCtrlF) {
                                        const qint64 avail = std::min<qint64>(n, tgtCtrlF - pos);
                                        QVector<float> scb(n * 2, 0.0f);
                                        if (tgtCtrlCh == 2) {
                                            const float *s = tgtCtrlSrc + pos * 2;
                                            for (qint64 ff = 0; ff < avail; ++ff) {
                                                scb[ff * 2 + 0] = s[ff * 2 + 0];
                                                scb[ff * 2 + 1] = s[ff * 2 + 1];
                                            }
                                        } else {
                                            const float *s = tgtCtrlSrc + pos * tgtCtrlCh;
                                            for (qint64 ff = 0; ff < avail; ++ff) {
                                                scb[ff * 2 + 0] = s[ff * tgtCtrlCh];
                                                scb[ff * 2 + 1] = s[ff * tgtCtrlCh];
                                            }
                                        }
                                        tgtAD->feedSidechain(scb.data(), n, 2, sampleRate);
                                    } else { tgtAD->reset(); }
                                } else { tgtAD->reset(); }
                            }
                            tgtFxc->process(tgtBuf.data() + pos * 2, n, 2, sampleRate);
                            if (tgtAD && tgtAD->enabled())
                                tgtAD->processDuck(tgtBuf.data() + pos * 2, n, 2, sampleRate);
                        }
                    }

                    // Ahora tgtBuf contiene la señal procesada de la pista objetivo.
                    // Procesar el duck por bloques.
                    for (qint64 pos = 0; pos < maxFrames; pos += blockSize) {
                        const int n = std::min<qint64>(blockSize, maxFrames - pos);

                        // Sidechain: leer señal de la pista de control
                        int ctrlF = 0, ctrlCh2 = 0, ctrlSR2 = 0;
                        const float *ctrlSrc2 = m_trackModel->trackSamplesData(
                            ctrlIdx, &ctrlF, &ctrlCh2, &ctrlSR2);

                        if (ctrlSrc2 && ctrlF > 0 && pos < ctrlF) {
                            const qint64 ctrlAvail = std::min<qint64>(n, ctrlF - pos);
                            QVector<float> scBuf(n * 2, 0.0f);
                            if (ctrlCh2 == 2) {
                                const float *s = ctrlSrc2 + pos * 2;
                                for (qint64 f = 0; f < ctrlAvail; ++f) {
                                    scBuf[f * 2 + 0] = s[f * 2 + 0];
                                    scBuf[f * 2 + 1] = s[f * 2 + 1];
                                }
                            } else {
                                const float *s = ctrlSrc2 + pos * ctrlCh2;
                                for (qint64 f = 0; f < ctrlAvail; ++f) {
                                    scBuf[f * 2 + 0] = s[f * ctrlCh2];
                                    scBuf[f * 2 + 1] = s[f * ctrlCh2];
                                }
                            }
                            masterAD->feedSidechain(scBuf.data(), n, 2, sampleRate);

                            // Restar contribución de la pista objetivo del bus
                            for (int f = 0; f < n; ++f) {
                                out[(pos + f) * 2 + 0] -= tgtBuf[(pos + f) * 2 + 0];
                                out[(pos + f) * 2 + 1] -= tgtBuf[(pos + f) * 2 + 1];
                            }

                            // Aplicar duck
                            QVector<float> duckBuf(n * 2);
                            std::memcpy(duckBuf.data(), tgtBuf.data() + pos * 2,
                                        n * 2 * sizeof(float));
                            masterAD->processDuck(duckBuf.data(), n, 2, sampleRate);

                            // Sumar la pista objetivo duckeada de vuelta al bus
                            for (int f = 0; f < n; ++f) {
                                out[(pos + f) * 2 + 0] += duckBuf[f * 2 + 0];
                                out[(pos + f) * 2 + 1] += duckBuf[f * 2 + 1];
                            }
                        } else {
                            masterAD->reset();
                        }
                    }
                }
            }
        }

        // =====================================================================
        //  2) Cadena de efectos master (todos excepto AutoDuck que ya se procesó)
        // =====================================================================
        for (auto *fx : m_audioEngine->masterFxChain()) fx->reset();
        qint64 lastProgressPos = 0;
        for (qint64 pos = 0; pos < maxFrames; pos += blockSize) {
            const int n = std::min<qint64>(blockSize, maxFrames - pos);
            for (auto *fx : m_audioEngine->masterFxChain()) {
                if (fx->enabled())
                    fx->process(out.data() + pos * channels, n, channels, sampleRate);
            }
            // Emitir progreso cada ~50000 frames para no saturar
            if (pos - lastProgressPos > 50000) {
                emit exportProgress(65 + int(5.0 * pos / maxFrames));
                QCoreApplication::processEvents();
                lastProgressPos = pos;
            }
        }
    }

    // Clamp [-1, 1]
    for (int i = 0; i < out.size(); ++i) {
        out[i] = std::max(-1.0f, std::min(1.0f, out[i]));
    }

    return true;
}

// ============================================================================
//  Resample: libsamplerate (SINC) si está disponible; lineal como fallback
// ============================================================================
void ExportManager::resampleLinear(const QVector<float> &in, int srcRate,
                                   QVector<float> &out, int dstRate, int channels)
{
    if (srcRate == dstRate) {
        out = in;
        return;
    }
    const qint64 srcFrames = in.size() / channels;
    if (srcFrames <= 0 || channels <= 0) { out.clear(); return; }
    const double ratio = double(dstRate) / double(srcRate);

#ifdef HAVE_SAMPLERATE
    // libsamplerate: SRC_SINC_MEDIUM_QUALITY balancea calidad y velocidad.
    // SRC_SINC_BEST_QUALITY es ~3x más lento pero máxima calidad.
    const qint64 dstFramesEst = qint64(double(srcFrames) * ratio) + 16;
    out.resize(dstFramesEst * channels);

    SRC_DATA data{};
    data.data_in       = in.constData();
    data.data_out      = out.data();
    data.input_frames  = long(srcFrames);
    data.output_frames = long(dstFramesEst);
    data.src_ratio     = ratio;
    data.end_of_input  = 1;

    const int err = src_simple(&data, SRC_SINC_MEDIUM_QUALITY, channels);
    if (err != 0) {
        qWarning() << "[Export] libsamplerate falló:" << src_strerror(err)
                   << "— fallback a resample lineal";
        out.clear();
        // Cae al bloque lineal de abajo.
    } else {
        out.resize(qint64(data.output_frames_gen) * channels);
        qDebug() << "[Export] Resample SINC" << srcRate << "->" << dstRate
                 << ":" << srcFrames << "->" << data.output_frames_gen << "frames";
        return;
    }
#endif

    // --- Fallback lineal ---
    const qint64 dstFrames = qint64(srcFrames * ratio);
    out.resize(dstFrames * channels);

    for (qint64 i = 0; i < dstFrames; ++i) {
        const double srcPos = double(i) / ratio;
        const qint64 i0 = qint64(srcPos);
        const qint64 i1 = std::min(srcFrames - 1, i0 + 1);
        const float frac = float(srcPos - i0);
        for (int c = 0; c < channels; ++c) {
            const float s0 = in[i0 * channels + c];
            const float s1 = in[i1 * channels + c];
            out[i * channels + c] = s0 + (s1 - s0) * frac;
        }
    }
    qDebug() << "[Export] Resample lineal" << srcRate << "->" << dstRate
             << ":" << srcFrames << "->" << dstFrames << "frames";
}

// ============================================================================
//  LUFS BS.1770-4
// ============================================================================
//
// Implementación del medidor de loudness integrado según ITU-R BS.1770-4:
//  1) Pre-filter "high-frequency shelving" (shelf +4 dB alrededor de 1681 Hz).
//  2) RLB high-pass a ~38 Hz (revised low-frequency B-weighting).
//  3) Mean-square por bloques de 400 ms con solape del 75% (hops de 100 ms).
//  4) Gating absoluto: descartar bloques con loudness < -70 LUFS.
//  5) Gating relativo: descartar bloques < (mean de los pasados - 10 LU).
//  6) Loudness integrado = -0.691 + 10*log10(mean-square de bloques restantes).
//
// Coeficientes (biquads canónicos) para 48 kHz. Para 44.1 kHz usaríamos otros;
// aquí siempre medimos a la tasa del buffer recibido (ya esté a 44.1 o 48k)
// recalculando los coeficientes con las fórmulas de BS.1770.

namespace {

struct Biquad {
    double b0, b1, b2, a1, a2;
    // Estado por canal
    double z1 = 0.0, z2 = 0.0;

    inline double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0; }
};

// Pre-filter K (shelving high-frequency boost). Coeficientes normalizados
// del estándar, reescalados para el sampleRate dado.
static Biquad kPreFilter(int sampleRate)
{
    // Valores de referencia a 48 kHz según BS.1770-4 (y la implementación
    // de libebur128).
    const double f0 = 1681.974450955533;
    const double G  = 3.999843853973347;     // dB
    const double Q  = 0.7071752369554196;

    const double K  = std::tan(M_PI * f0 / sampleRate);
    const double Vh = std::pow(10.0, G / 20.0);
    const double Vb = std::pow(Vh, 0.4996667741545416);

    const double a0 = 1.0 + K / Q + K * K;
    Biquad f;
    f.b0 = (Vh + Vb * K / Q + K * K) / a0;
    f.b1 = 2.0 * (K * K - Vh) / a0;
    f.b2 = (Vh - Vb * K / Q + K * K) / a0;
    f.a1 = 2.0 * (K * K - 1.0) / a0;
    f.a2 = (1.0 - K / Q + K * K) / a0;
    return f;
}

// RLB high-pass a ~38 Hz.
static Biquad rlbHighpass(int sampleRate)
{
    const double f0 = 38.13547087602444;
    const double Q  = 0.5003270373238773;
    const double K  = std::tan(M_PI * f0 / sampleRate);
    const double a0 = 1.0 + K / Q + K * K;
    Biquad f;
    f.b0 = 1.0 / a0;
    f.b1 = -2.0 / a0;
    f.b2 = 1.0 / a0;
    f.a1 = 2.0 * (K * K - 1.0) / a0;
    f.a2 = (1.0 - K / Q + K * K) / a0;
    return f;
}

} // namespace

double ExportManager::measureLUFS(const QVector<float> &samples,
                                  int sampleRate, int channels)
{
    if (samples.isEmpty() || channels <= 0 || sampleRate <= 0) return -100.0;

    const qint64 frames = samples.size() / channels;
    if (frames < sampleRate) return -100.0; // necesitamos al menos 1s

    // Un filtro por canal.
    std::vector<Biquad> kFilt(channels, kPreFilter(sampleRate));
    std::vector<Biquad> rFilt(channels, rlbHighpass(sampleRate));

    // Pesos de canales G (BS.1770): L=R=1.0, C=1.0, Ls=Rs=1.41. Para estéreo
    // usamos 1.0 en ambos.
    std::vector<double> chWeight(channels, 1.0);

    // Bloques de 400 ms con hop de 100 ms.
    const qint64 blockFrames = qint64(0.400 * sampleRate);
    const qint64 hopFrames   = qint64(0.100 * sampleRate);
    if (frames < blockFrames) return -100.0;

    // Pre-procesar: aplicar K-weighting a todo el buffer y almacenar cuadrados.
    // Esto simplifica la gating posterior.
    QVector<double> sq(frames * channels, 0.0);
    for (qint64 i = 0; i < frames; ++i) {
        for (int c = 0; c < channels; ++c) {
            double x = samples[i * channels + c];
            x = kFilt[c].process(x);
            x = rFilt[c].process(x);
            sq[i * channels + c] = x * x;
        }
    }

    // Calcular mean-square por bloque (ponderado por canal).
    QVector<double> blockMS;
    blockMS.reserve((frames - blockFrames) / hopFrames + 1);
    for (qint64 start = 0; start + blockFrames <= frames; start += hopFrames) {
        double sum = 0.0;
        for (qint64 i = 0; i < blockFrames; ++i) {
            for (int c = 0; c < channels; ++c) {
                sum += chWeight[c] * sq[(start + i) * channels + c];
            }
        }
        const double ms = sum / double(blockFrames);
        blockMS.append(ms);
    }
    if (blockMS.isEmpty()) return -100.0;

    auto msToLufs = [](double ms) {
        return ms > 0.0 ? (-0.691 + 10.0 * std::log10(ms)) : -100.0;
    };

    // Gating absoluto: descartar bloques < -70 LUFS.
    QVector<double> absGated;
    absGated.reserve(blockMS.size());
    for (double ms : blockMS) {
        if (msToLufs(ms) >= -70.0) absGated.append(ms);
    }
    if (absGated.isEmpty()) return -100.0;

    // Gating relativo: umbral = mean(absGated) - 10 LU.
    double meanMS = 0.0;
    for (double v : absGated) meanMS += v;
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
    if (countFinal == 0) return -100.0;
    const double finalMS = sumFinal / countFinal;
    return msToLufs(finalMS);
}

void ExportManager::applyLUFSNormalization(QVector<float> &samples,
                                           int sampleRate, int channels,
                                           double targetLUFS)
{
    const double current = measureLUFS(samples, sampleRate, channels);
    if (current <= -99.0) {
        qDebug() << "[Export] LUFS no medible, se omite normalización";
        return;
    }
    const double deltaDB = targetLUFS - current;
    const float gain = float(std::pow(10.0, deltaDB / 20.0));
    qDebug() << "[Export] LUFS actual:" << current
             << "objetivo:" << targetLUFS
             << "→ gain:" << (20.0 * std::log10(gain)) << "dB";

    // Asegurar no clipping: reducir si hace falta.
    float maxAbs = 0.0f;
    for (float v : samples) {
        const float a = std::fabs(v);
        if (a > maxAbs) maxAbs = a;
    }
    const float prospective = maxAbs * gain;
    float finalGain = gain;
    if (prospective > 0.99f) {
        finalGain = 0.99f / std::max(0.0001f, maxAbs);
        qDebug() << "[Export] Clip guard: gain reducido a"
                 << (20.0 * std::log10(finalGain)) << "dB para evitar clipping";
    }

    for (float &v : samples) v *= finalGain;
}

// ============================================================================
//  WAV writer
// ============================================================================
bool ExportManager::writeWavFile(const QString &filePath, const QVector<float> &samples,
                                 int sampleRate, int channels, int bitDepth)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) {
        m_lastError = tr("No se puede abrir el archivo para escribir: %1").arg(filePath);
        return false;
    }

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);

    // Formato: 1=PCM, 3=IEEE float
    const quint16 audioFormat = (bitDepth == 32) ? 3 : 1;
    const quint16 bitsPerSample = bitDepth;
    const quint32 byteRate = sampleRate * channels * (bitsPerSample / 8);
    const quint16 blockAlign = channels * (bitsPerSample / 8);
    const quint32 dataSize = quint32(samples.size()) * (bitsPerSample / 8);
    const quint32 fmtChunkSize = 16;
    const quint32 riffSize = 4 + (8 + fmtChunkSize) + (8 + dataSize);

    // RIFF header
    f.write("RIFF", 4);
    ds << riffSize;
    f.write("WAVE", 4);

    // fmt chunk
    f.write("fmt ", 4);
    ds << fmtChunkSize;
    ds << audioFormat;
    ds << quint16(channels);
    ds << quint32(sampleRate);
    ds << byteRate;
    ds << blockAlign;
    ds << bitsPerSample;

    // data chunk
    f.write("data", 4);
    ds << dataSize;

    // Samples
    if (bitDepth == 32) {
        f.write(reinterpret_cast<const char*>(samples.constData()),
                samples.size() * sizeof(float));
    } else if (bitDepth == 24) {
        QByteArray buf;
        buf.resize(samples.size() * 3);
        for (int i = 0; i < samples.size(); ++i) {
            const float clipped = std::max(-1.0f, std::min(1.0f, samples[i]));
            const qint32 v = qint32(clipped * 8388607.0f);
            buf[i*3 + 0] = char(v & 0xFF);
            buf[i*3 + 1] = char((v >> 8) & 0xFF);
            buf[i*3 + 2] = char((v >> 16) & 0xFF);
        }
        f.write(buf);
    } else {
        // 16-bit PCM por defecto
        QByteArray buf;
        buf.resize(samples.size() * 2);
        qint16 *p = reinterpret_cast<qint16*>(buf.data());
        for (int i = 0; i < samples.size(); ++i) {
            const float clipped = std::max(-1.0f, std::min(1.0f, samples[i]));
            p[i] = qint16(clipped * 32767.0f);
        }
        f.write(buf);
    }

    f.close();
    return true;
}

// ============================================================================
//  ffmpeg (subproceso)
// ============================================================================
bool ExportManager::exportViaFfmpeg(const QString &filePath, const QVector<float> &samples,
                                    int sampleRate, int channels, const QString &format,
                                    const QVariantMap &metadata)
{
    // Detectar ffmpeg en PATH.
    const QString ffmpegPath = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpegPath.isEmpty()) {
        m_lastError = tr("ffmpeg no encontrado en PATH. Instala ffmpeg para exportar a %1.")
                        .arg(format);
        return false;
    }

    const QString coverPath = metadata.value("coverPath").toString().trimmed();
    // M4A/iPod no soporta imagen embebida via video stream en ffmpeg.
    // Solo MP3, OGG y FLAC soportan portada via -map.
    const bool hasCover = !coverPath.isEmpty() && QFileInfo(coverPath).isFile()
                          && (format == "mp3" || format == "flac" || format == "ogg");

    QStringList args;
    args << "-hide_banner" << "-y"
         << "-f" << "f32le"
         << "-ar" << QString::number(sampleRate)
         << "-ac" << QString::number(channels)
         << "-channel_layout" << (channels == 2 ? "stereo" : "mono")
         << "-i" << "-";

    if (hasCover) {
        args << "-i" << coverPath;
    }

    // Capítulos: generar archivo FFMETADATA temporal si hay capítulos.
    // FFmpeg lo lee como input con -i y lo aplica al contenedor de salida.
    //
    // EXCEPCIÓN: cuando TagLib está disponible y el formato es MP3,
    // NO pasamos capítulos via FFMETADATA. TagLib los escribirá después
    // con soporte completo (APIC artwork + WXXX URL embebidos).
    // Esto evita conflictos entre FFmpeg y TagLib escribiendo CHAP frames.
    QTemporaryFile *chapterMetaFile = nullptr;
    const bool taglibHandlesChapters =
#ifdef HAVE_TAGLIB
        (format == "mp3");
#else
        false;
#endif

    if (m_chapterModel && m_chapterModel->count() > 0) {
        // Sincronizar duración del episodio antes de generar metadata.
        // Esto garantiza que el último CHAP.endTime == duración total (ID3 req).
        const qint64 durationMs = qint64(double(samples.size()) / channels / sampleRate * 1000.0);
        m_chapterModel->setEpisodeDurationMs(durationMs);

        if (!taglibHandlesChapters) {
            // Formatos no-MP3: usar FFMETADATA (OGG, FLAC, M4A)
            chapterMetaFile = new QTemporaryFile(QDir::tempPath() + "/kut_chapters_XXXXXX.txt");
            if (chapterMetaFile->open()) {
                const QByteArray meta = m_chapterModel->toFfmetadata().toUtf8();
                chapterMetaFile->write(meta);
                chapterMetaFile->flush();

                // Verificar que el archivo se escribió correctamente
                if (QFileInfo(chapterMetaFile->fileName()).size() > 0) {
                    args << "-i" << chapterMetaFile->fileName();
                    qDebug() << "[Export] Capítulos FFMETADATA:" << chapterMetaFile->fileName()
                             << "(" << m_chapterModel->count() << "capítulos,"
                             << meta.size() << "bytes)";
                } else {
                    qWarning() << "[Export] Error: archivo FFMETADATA vacío, capítulos omitidos";
                    delete chapterMetaFile;
                    chapterMetaFile = nullptr;
                }
            } else {
                qWarning() << "[Export] Error al crear archivo temporal para capítulos";
                delete chapterMetaFile;
                chapterMetaFile = nullptr;
            }
        } else {
            qDebug() << "[Export] MP3: capítulos serán escritos por TagLib post-export"
                     << "(" << m_chapterModel->count() << "capítulos)";
        }
    }

    // Metadatos: -metadata key=value
    auto addMeta = [&](const QString &key, const QString &ffKey) {
        const QString v = metadata.value(key).toString().trimmed();
        if (!v.isEmpty()) {
            args << "-metadata" << QString("%1=%2").arg(ffKey, v);
        }
    };
    addMeta("title",   "title");
    addMeta("artist",  "artist");
    addMeta("album",   "album");
    addMeta("comment", "comment");
    addMeta("genre",   "genre");
    addMeta("year",    "date");
    addMeta("track",   "track");

    // Mapeo si hay portada: input 0 = audio, input 1 = imagen.
    // Mapeamos audio del primero y video (imagen) del segundo. Sin -c:v:
    // ffmpeg elige el codec adecuado por formato.
    if (hasCover) {
        args << "-map" << "0:a" << "-map" << "1:v";
    }

    // Mapeo de capítulos desde FFMETADATA input.
    // El índice del FFMETADATA input depende de cuántos inputs hay antes:
    //   0 = audio (pipe), 1 = cover (si hay), siguiente = chapters
    if (chapterMetaFile) {
        int chapterInputIdx = 1 + (hasCover ? 1 : 0);
        args << "-map_metadata" << QString("%1").arg(chapterInputIdx);
    }

    // Codec y bitrate por formato.
    if (format == "mp3") {
        args << "-c:a" << "libmp3lame" << "-b:a" << "192k";   // CBR 192 kbps para compatibilidad de búsqueda
        // Forzar ID3v2.3 SIEMPRE para MP3. Razones:
        //   1. Capítulos (CHAP frames) son mejor soportados en v2.3
        //   2. Windows Explorer no lee v2.4 correctamente
        //   3. Sistemas de infoentretenimiento de coches ignoran v2.4
        //   4. Apple Podcasts y Overcast prefieren v2.3 para CHAP
        args << "-id3v2_version" << "3"
             << "-write_id3v1" << "1";
        if (hasCover) {
            args << "-metadata:s:v:0" << "title=Album cover"
                 << "-metadata:s:v:0" << "comment=Cover (front)";
        }
    } else if (format == "ogg") {
        args << "-c:a" << "libvorbis" << "-q:a" << "6";
    } else if (format == "flac") {
        args << "-c:a" << "flac" << "-compression_level" << "5";
        if (hasCover) {
            args << "-disposition:v:0" << "attached_pic";
        }
    } else if (format == "m4a") {
        args << "-c:a" << "aac" << "-b:a" << "192k";
    } else {
        m_lastError = tr("Formato no soportado: %1").arg(format);
        return false;
    }

    args << filePath;

    // Debug: imprimir el comando completo para poder copiar/pegar.
    qDebug().noquote() << "[Export] Ejecutando:" << ffmpegPath << args.join(" ");

    QProcess ff;
    ff.setProcessChannelMode(QProcess::MergedChannels);
    ff.start(ffmpegPath, args);
    if (!ff.waitForStarted(5000)) {
        m_lastError = tr("No se pudo iniciar ffmpeg: %1").arg(ff.errorString());
        return false;
    }

    // Escribir los samples en trozos.
    const char *raw = reinterpret_cast<const char*>(samples.constData());
    const qint64 total = samples.size() * qint64(sizeof(float));
    const qint64 chunk = 16 * 1024;  // 16KB para updates frecuentes de progreso
    qint64 written = 0;
    while (written < total) {
        const qint64 toWrite = std::min(chunk, total - written);
        const qint64 w = ff.write(raw + written, toWrite);
        if (w <= 0) {
            m_lastError = tr("Error escribiendo a ffmpeg: %1").arg(ff.errorString());
            ff.kill();
            ff.waitForFinished(2000);
            return false;
        }
        written += w;
        emit exportProgress(80 + int(15.0 * written / total));
        QCoreApplication::processEvents();
    }
    ff.closeWriteChannel();

    if (!ff.waitForFinished(60000)) {
        m_lastError = tr("ffmpeg no terminó a tiempo");
        ff.kill();
        return false;
    }
    const QString out = QString::fromUtf8(ff.readAll());
    qDebug().noquote() << "[Export] ffmpeg output:\n" << out;
    if (ff.exitCode() != 0) {
        m_lastError = tr("ffmpeg devolvió error:\n%1").arg(out);
        delete chapterMetaFile;
        return false;
    }
    delete chapterMetaFile;

    // ── Post-procesamiento TagLib: CHAP frames con artwork + URL ─────
    // FFmpeg solo escribe CHAP básicos (título + timestamps). TagLib
    // reescribe los frames con sub-frames APIC (imagen) y WXXX (URL)
    // embebidos, que es lo que necesitan Apple Podcasts y Pocket Casts.
#ifdef HAVE_TAGLIB
    if (format == "mp3" && m_chapterModel && m_chapterModel->count() > 0) {
        if (!ChapterTagger::writeChapters(filePath, m_chapterModel)) {
            qWarning() << "[Export] TagLib: falló al escribir capítulos con artwork";
            // No es un error fatal: el MP3 ya existe con capítulos básicos.
        }
    }
#endif

    return true;
}

// ============================================================================
//  API principal
// ============================================================================
bool ExportManager::exportWithOptions(const QString &filePath, const QVariantMap &opts)
{
    m_lastError.clear();
    emit exportProgress(0); QCoreApplication::processEvents();

    QVector<float> mix;
    int srcRate = 0, channels = 0;
    if (!mixDown(mix, srcRate, channels)) {
        emit exportFinished(false, filePath, m_lastError);
        return false;
    }
    emit exportProgress(65); QCoreApplication::processEvents();

    // Sample rate deseado
    int dstRate = opts.value("sampleRate", AudioEngine::SAMPLE_RATE).toInt();
    if (dstRate != 44100 && dstRate != 48000) dstRate = AudioEngine::SAMPLE_RATE;

    QVector<float> samples;
    if (dstRate != srcRate) {
        resampleLinear(mix, srcRate, samples, dstRate, channels);
    } else {
        samples = std::move(mix);
    }
    emit exportProgress(70); QCoreApplication::processEvents();

    // Normalización LUFS
    // Interpretamos cualquier valor entre -40 y -5 como objetivo real;
    // fuera de ese rango desactiva normalización.
    const QVariant normVar = opts.value("normalizeLUFS");
    if (normVar.isValid() && !normVar.isNull()) {
        const double target = normVar.toDouble();
        if (target <= -5.0 && target >= -40.0) {
            applyLUFSNormalization(samples, dstRate, channels, target);
        }
    }
    emit exportProgress(78); QCoreApplication::processEvents();

    const QString format = opts.value("format", "wav").toString().toLower();
    const QVariantMap metadata = opts.value("metadata").toMap();

    bool ok;
    if (format == "wav") {
        int bitDepth = opts.value("bitDepth", 24).toInt();
        if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) bitDepth = 24;
        ok = writeWavFile(filePath, samples, dstRate, channels, bitDepth);
    } else {
        ok = exportViaFfmpeg(filePath, samples, dstRate, channels, format, metadata);
    }

    if (ok) {
        emit exportProgress(100); QCoreApplication::processEvents();
        emit exportFinished(true, filePath, QString());
    } else {
        emit exportFinished(false, filePath, m_lastError);
    }
    return ok;
}

bool ExportManager::exportToWAV(const QString &filePath)
{
    QVariantMap opts;
    opts["format"] = "wav";
    opts["sampleRate"] = AudioEngine::SAMPLE_RATE;
    opts["bitDepth"] = 24;
    return exportWithOptions(filePath, opts);
}
