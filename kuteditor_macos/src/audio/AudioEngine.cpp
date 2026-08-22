#include "AudioEngine.h"
#include "ui/TrackModel.h"
#include "TrackFxChain.h"

#include "fx/MasterEffect.h"
#include "fx/CompressorEffect.h"
#include "fx/LimiterEffect.h"
#include "fx/HighPassFilter.h"
#include "fx/LowPassFilter.h"
#include "fx/NotchFilter.h"
#include "fx/NoiseGateEffect.h"
#include "fx/DeNoiserEffect.h"
#include "fx/DeEsserEffect.h"
#include "fx/ExpanderEffect.h"
#include "fx/AutoDuckEffect.h"
#include "fx/AutoGainEffect.h"
#include "fx/EqualizerEffect.h"
#include "fx/StereoWidener.h"
#include "fx/PhaseInvertEffect.h"
#include "fx/MonoMixerEffect.h"
#include "fx/TrimGainEffect.h"

#include <QDebug>
#include <QMutexLocker>
#include <cmath>
#include <cstring>

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
    // Crear efectos (todos hijos de this → se destruyen automáticamente)
    m_trimGain       = new TrimGainEffect(this);
    m_phaseInvert    = new PhaseInvertEffect(this);
    m_highPassFilter = new HighPassFilter(this);
    m_lowPassFilter  = new LowPassFilter(this);
    m_notchFilter    = new NotchFilter(this);
    m_deNoiser       = new RNNoiseEffect(this);
    m_deepFilter     = new DeepFilterEffect(this);
    m_noiseGate      = new NoiseGateEffect(this);
    m_expander       = new ExpanderEffect(this);
    m_compressor     = new CompressorEffect(this);
    m_deEsser        = new DeEsserEffect(this);
    m_equalizer      = new EqualizerEffect(this);
    m_autoGain       = new AutoGainEffect(this);
    m_autoDuck       = new AutoDuckEffect(this);
    m_stereoWidener  = new StereoWidener(this);
    m_monoMixer      = new MonoMixerEffect(this);
    m_limiter        = new LimiterEffect(this);

    // Cadena en orden de procesamiento:
    // Trim → PhaseInvert → HPF → LPF → Notch → DeNoiser → NoiseGate →
    // Expander → Compresor → DeEsser → EQ → AutoGain → AutoDuck →
    // StereoWidener → MonoMixer → Limitador
    m_fxChain = {
        m_trimGain,
        m_phaseInvert,
        m_highPassFilter,
        m_lowPassFilter,
        m_notchFilter,
        m_deNoiser,
        m_deepFilter,
        m_noiseGate,
        m_expander,
        m_compressor,
        m_deEsser,
        m_equalizer,
        m_autoGain,
        m_autoDuck,
        m_stereoWidener,
        m_monoMixer,
        m_limiter
    };

    // Conectar señales de compatibilidad
    connect(m_limiter, &MasterEffect::changed, this, &AudioEngine::limiterChanged);
    connect(m_compressor, &MasterEffect::changed, this, &AudioEngine::compChanged);

    // Notificar a QML cuando cualquier efecto master se activa/desactiva
    for (auto *fx : m_fxChain) {
        connect(fx, &MasterEffect::changed, this, &AudioEngine::masterFxChanged);
    }

    m_uiTimer.setInterval(33); // ~30 fps
    connect(&m_uiTimer, &QTimer::timeout, this, &AudioEngine::onUpdateTimer);
}

AudioEngine::~AudioEngine()
{
    if (m_state != State::Stopped) {
        stop();
    }
}

// =========================================================================
//  Compatibilidad: delegaciones Limitador
// =========================================================================
bool AudioEngine::limiterEnabled() const { return m_limiter->enabled(); }
void AudioEngine::setLimiterEnabled(bool v) { m_limiter->setEnabled(v); }
float AudioEngine::limiterThresholdDb() const { return m_limiter->thresholdDb(); }
void AudioEngine::setLimiterThresholdDb(float v) { m_limiter->setThresholdDb(v); }
float AudioEngine::limiterGainReduction() const { return m_limiter->gainReduction(); }

// =========================================================================
//  Compatibilidad: delegaciones Compresor
// =========================================================================
bool AudioEngine::compEnabled() const { return m_compressor->enabled(); }
void AudioEngine::setCompEnabled(bool v) { m_compressor->setEnabled(v); }
float AudioEngine::compThresholdDb() const { return m_compressor->thresholdDb(); }
void AudioEngine::setCompThresholdDb(float v) { m_compressor->setThresholdDb(v); }
float AudioEngine::compRatio() const { return m_compressor->ratio(); }
void AudioEngine::setCompRatio(float v) { m_compressor->setRatio(v); }
float AudioEngine::compAttackMs() const { return m_compressor->attackMs(); }
void AudioEngine::setCompAttackMs(float v) { m_compressor->setAttackMs(v); }
float AudioEngine::compReleaseMs() const { return m_compressor->releaseMs(); }
void AudioEngine::setCompReleaseMs(float v) { m_compressor->setReleaseMs(v); }
float AudioEngine::compMakeupDb() const { return m_compressor->makeupDb(); }
void AudioEngine::setCompMakeupDb(float v) { m_compressor->setMakeupDb(v); }
float AudioEngine::compGainReduction() const { return m_compressor->gainReduction(); }

// =========================================================================
//  Velocidad de reproducción
// =========================================================================
void AudioEngine::setPlaybackRate(float rate)
{
    rate = std::clamp(rate, 0.5f, 3.0f);
    if (qFuzzyCompare(rate, m_playbackRate)) return;
    m_playbackRate = rate;
    emit playbackRateChanged();
}

// =========================================================================
//  TrackModel
// =========================================================================
void AudioEngine::setTrackModel(TrackModel *model)
{
    m_trackModel = model;
    if (m_trackModel) {
        auto sync = [this]() {
            const double newTotal = double(totalFrames()) / SAMPLE_RATE;
            if (std::abs(newTotal - m_totalTime) > 1e-6) {
                m_totalTime = newTotal;
                emit totalTimeChanged();
            }
        };
        connect(m_trackModel, &TrackModel::clipsChanged, this, [sync](int){ sync(); });
        connect(m_trackModel, &TrackModel::countChanged, this, sync);
        connect(m_trackModel, &QObject::destroyed, this, [this]() {
            m_trackModel = nullptr;
        });
    }
}

// =========================================================================
//  Transporte
// =========================================================================
void AudioEngine::play()
{
    if (m_state == State::Playing) return;
    if (m_state == State::Recording) return;
    if (!m_trackModel || m_trackModel->count() == 0) return;

    const int maxFrames = totalFrames();
    m_totalTime = double(maxFrames) / SAMPLE_RATE;
    emit totalTimeChanged();

    if (maxFrames <= 0) {
        qDebug() << "[AudioEngine] Nada que reproducir (pistas vacías)";
        return;
    }

    {
        qint64 currentHead = m_playheadFrame.load(std::memory_order_relaxed);
        if (currentHead >= maxFrames) {
            currentHead = 0;
            m_playheadFrame.store(0, std::memory_order_relaxed);
        }
        m_playStartFrame.store(currentHead, std::memory_order_relaxed);
    }

    // readMixSegment() lee directamente de los clips sin flatCache,
    // así que no necesitamos prepareForPlayback().

    // Resetear estado de todos los efectos al iniciar playback
    for (auto *fx : m_fxChain) fx->reset();
    // Resetear FX por pista
    for (int i = 0; i < m_trackModel->rowCount(); ++i) {
        TrackFxChain *fc = m_trackModel->trackFxChain(i);
        if (fc) fc->resetAll();
    }

    m_state = State::Playing;
    m_monitoring = false;
    m_uiTimer.start();
    qDebug() << "[AudioEngine] Play desde" << m_playheadFrame << "/" << maxFrames;
    emit stateChanged();
    emit playbackStarted();
}

void AudioEngine::record()
{
    if (m_state == State::Recording) return;
    if (m_state == State::Playing) stop();
    if (!m_trackModel) return;

    const QList<int> armed = m_trackModel->armedTrackIndices();
    if (armed.isEmpty()) {
        qWarning() << "[AudioEngine] No se puede grabar: ninguna pista armada.";
        return;
    }

    qint64 startFrame = m_playheadFrame.load(std::memory_order_relaxed);
    {
        QMutexLocker lock(&m_recordMutex);
        m_recordStartFrame = startFrame;
        m_samplesRecorded.store(0, std::memory_order_relaxed);
        m_recordFramesWritten.clear();
        for (int idx : armed) m_recordFramesWritten[idx] = 0;
    }

    for (int idx : armed) {
        m_trackModel->beginRecording(idx, startFrame, SAMPLE_RATE, CHANNELS);
    }

    m_state = State::Recording;
    m_monitoring = false;
    m_uiTimer.start();

    qDebug() << "[AudioEngine] Record START en" << armed.size()
             << "pistas:" << armed << "desde frame" << startFrame
             << "(" << double(startFrame) / SAMPLE_RATE << "s )";
    emit stateChanged();
    emit recordingStarted();
}

void AudioEngine::stop()
{
    if (m_state == State::Stopped) return;

    const State prev = m_state;
    m_state = State::Stopped;
    m_monitoring = false;
    m_uiTimer.stop();

    if (prev == State::Recording) {
        int maxFramesRec = 0;
        {
            QMutexLocker lock(&m_recordMutex);
            for (auto it = m_recordFramesWritten.constBegin();
                 it != m_recordFramesWritten.constEnd(); ++it) {
                maxFramesRec = std::max(maxFramesRec, it.value());
            }
            m_recordFramesWritten.clear();
        }

        if (m_trackModel) {
            for (int i = 0; i < m_trackModel->rowCount(); ++i) {
                m_trackModel->finishRecording(i);
            }
            const int maxFrames = totalFrames();
            m_totalTime = double(maxFrames) / SAMPLE_RATE;
            emit totalTimeChanged();
        }
        qDebug() << "[AudioEngine] Record STOP: máximo" << maxFramesRec << "frames esta toma";
        emit recordingStopped(maxFramesRec);
    } else if (prev == State::Playing) {
        qDebug() << "[AudioEngine] Stop playback en frame" << m_playheadFrame;
        if (m_returnPlayheadOnStop) {
            const qint64 startF = m_playStartFrame.load(std::memory_order_relaxed);
            m_playheadFrame.store(startF, std::memory_order_relaxed);
            m_currentTime = double(startF) / SAMPLE_RATE;
            emit timeChanged();
        }
        emit playbackStopped();
    }

    m_masterLeftLevel = 0.0f;
    m_masterRightLevel = 0.0f;
    m_peakAccumL = 0.0f;
    m_peakAccumR = 0.0f;
    m_peakAccumValid = false;
    if (m_trackModel) {
        for (int i = 0; i < m_trackModel->rowCount(); ++i) {
            m_trackModel->updateTrackLevel(i, 0.0f, 0.0f);
        }
        emit m_trackModel->trackLevelsChanged();
        m_trackModel->freeFlatCaches();
    }
    emit levelsChanged();
    emit stateChanged();

    checkMonitoring();
}

void AudioEngine::checkMonitoring()
{
    if (m_state != State::Stopped) return;

    bool hasArmed = false;
    if (m_trackModel) {
        const auto armed = m_trackModel->armedTrackIndices();
        hasArmed = !armed.isEmpty();
    }

    if (hasArmed && !m_monitoring) {
        m_monitoring = true;
        m_uiTimer.start();
    } else if (!hasArmed && m_monitoring) {
        m_monitoring = false;
        m_uiTimer.stop();
        if (m_trackModel) {
            for (int i = 0; i < m_trackModel->rowCount(); ++i)
                m_trackModel->updateTrackLevel(i, 0.0f, 0.0f);
            emit m_trackModel->trackLevelsChanged();
        }
    }
}

void AudioEngine::seekTime(double seconds)
{
    if (m_trackModel) {
        const qint64 tf = totalFrames();
        const double newTotal = double(tf) / SAMPLE_RATE;
        if (std::abs(newTotal - m_totalTime) > 1e-6) {
            m_totalTime = newTotal;
            emit totalTimeChanged();
        }
    }

    const double clamped = std::max(0.0, seconds);
    const qint64 newFrame = static_cast<qint64>(clamped * SAMPLE_RATE);
    m_playheadFrame.store(newFrame, std::memory_order_relaxed);
    m_currentTime = clamped;
    emit timeChanged();
}

void AudioEngine::setLoopRegion(double startSec, double endSec)
{
    if (startSec < 0 || endSec < 0 || endSec <= startSec) {
        m_loopStartFrame.store(-1, std::memory_order_relaxed);
        m_loopEndFrame.store(-1, std::memory_order_relaxed);
    } else {
        m_loopStartFrame.store(qint64(startSec * SAMPLE_RATE), std::memory_order_relaxed);
        m_loopEndFrame.store(qint64(endSec * SAMPLE_RATE), std::memory_order_relaxed);
    }
}

// =========================================================================
//  Audio input (grabación)
// =========================================================================
void AudioEngine::onAudioInput(int trackIndex, const float *interleaved, int nFrames)
{
    if (!interleaved || nFrames <= 0 || !m_trackModel) return;

    float peakL = 0.0f, peakR = 0.0f;
    float sumL = 0.0f, sumR = 0.0f;
    for (int i = 0; i < nFrames; ++i) {
        const float l = interleaved[i * 2 + 0];
        const float r = interleaved[i * 2 + 1];
        const float al = std::abs(l);
        const float ar = std::abs(r);
        if (al > peakL) peakL = al;
        if (ar > peakR) peakR = ar;
        sumL += l * l;
        sumR += r * r;
    }

    float gainTrack = 1.0f;
    {
        const QVariantMap d = m_trackModel->getTrackData(trackIndex);
        gainTrack = d.value("gain", 1.0f).toFloat();
    }
    peakL = std::min(1.0f, peakL * gainTrack);
    peakR = std::min(1.0f, peakR * gainTrack);

    const float prevL = m_trackModel->getTrackLevelLeft(trackIndex);
    const float prevR = m_trackModel->getTrackLevelRight(trackIndex);
    const float smoothedL = (peakL > prevL) ? peakL : (prevL * 0.85f + peakL * 0.15f);
    const float smoothedR = (peakR > prevR) ? peakR : (prevR * 0.85f + peakR * 0.15f);
    m_trackModel->updateTrackLevel(trackIndex, smoothedL, smoothedR);

    if (m_state == State::Recording) {
        if (peakL > m_peakAccumL) m_peakAccumL = peakL;
        if (peakR > m_peakAccumR) m_peakAccumR = peakR;
        m_peakAccumValid = true;
    }

    if (m_state != State::Recording) return;

    qint64 startFrame;
    int framesAlready;
    {
        QMutexLocker lock(&m_recordMutex);
        auto it = m_recordFramesWritten.find(trackIndex);
        if (it == m_recordFramesWritten.end()) return;
        framesAlready = it.value();
        startFrame = m_recordStartFrame + framesAlready;
        it.value() += nFrames;
        // Update the master sample counter AFTER data is committed
        const qint64 totalNow = framesAlready + nFrames;
        qint64 prevMax = m_samplesRecorded.load(std::memory_order_relaxed);
        while (totalNow > prevMax && !m_samplesRecorded.compare_exchange_weak(prevMax, totalNow, std::memory_order_relaxed)) {
            // Loop until updated or another thread set it higher
        }
    }

    m_trackModel->writeSamplesAt(trackIndex, startFrame, interleaved, nFrames,
                                 SAMPLE_RATE, CHANNELS);

    const qint64 newHead = startFrame + nFrames;
    qint64 current = m_playheadFrame.load(std::memory_order_relaxed);
    while (newHead > current && !m_playheadFrame.compare_exchange_weak(current, newHead, std::memory_order_relaxed)) {
        // Loop until updated or another thread set it higher
    }
}

// =========================================================================
//  Helper: lee samples del source con interpolación lineal para rate != 1.0
//  Escribe `outFrames` frames estéreo en `dst`. Lee desde `src` empezando
//  en `srcStart`, consumiendo `outFrames * rate` frames del source.
// =========================================================================
static void readResampled(float *dst, const float *src,
                          qint64 srcStart, qint64 srcTotalFrames,
                          int srcChannels, int outFrames,
                          float rate, float gainL, float gainR, float gain)
{
    if (qFuzzyCompare(rate, 1.0f)) {
        // Fast path: copia directa sin interpolación
        const qint64 avail = std::min<qint64>(outFrames, srcTotalFrames - srcStart);
        if (srcChannels == 2) {
            const float *s = src + srcStart * 2;
            for (qint64 f = 0; f < avail; ++f) {
                dst[f * 2 + 0] = s[f * 2 + 0] * gain * gainL;
                dst[f * 2 + 1] = s[f * 2 + 1] * gain * gainR;
            }
        } else {
            const float *s = src + srcStart * srcChannels;
            for (qint64 f = 0; f < avail; ++f) {
                const float v = s[f * srcChannels] * gain;
                dst[f * 2 + 0] = v * gainL;
                dst[f * 2 + 1] = v * gainR;
            }
        }
        return;
    }

    // Interpolated path
    for (int f = 0; f < outFrames; ++f) {
        const double srcPos = srcStart + double(f) * rate;
        const qint64 idx0 = qint64(srcPos);
        if (idx0 >= srcTotalFrames) break;
        const qint64 idx1 = std::min(idx0 + 1, srcTotalFrames - 1);
        const float frac = float(srcPos - idx0);

        if (srcChannels == 2) {
            const float l = src[idx0 * 2 + 0] * (1.f - frac) + src[idx1 * 2 + 0] * frac;
            const float r = src[idx0 * 2 + 1] * (1.f - frac) + src[idx1 * 2 + 1] * frac;
            dst[f * 2 + 0] = l * gain * gainL;
            dst[f * 2 + 1] = r * gain * gainR;
        } else {
            const float v0 = src[idx0 * srcChannels];
            const float v1 = src[idx1 * srcChannels];
            const float v = (v0 * (1.f - frac) + v1 * frac) * gain;
            dst[f * 2 + 0] = v * gainL;
            dst[f * 2 + 1] = v * gainR;
        }
    }
}

// Versión sin gain/pan (para sidechains)
static void readResampledRaw(float *dst, const float *src,
                             qint64 srcStart, qint64 srcTotalFrames,
                             int srcChannels, int outFrames, float rate)
{
    if (qFuzzyCompare(rate, 1.0f)) {
        const qint64 avail = std::min<qint64>(outFrames, srcTotalFrames - srcStart);
        if (srcChannels == 2) {
            const float *s = src + srcStart * 2;
            for (qint64 f = 0; f < avail; ++f) {
                dst[f * 2 + 0] = s[f * 2 + 0];
                dst[f * 2 + 1] = s[f * 2 + 1];
            }
        } else {
            const float *s = src + srcStart * srcChannels;
            for (qint64 f = 0; f < avail; ++f) {
                dst[f * 2 + 0] = s[f * srcChannels];
                dst[f * 2 + 1] = s[f * srcChannels];
            }
        }
        return;
    }

    for (int f = 0; f < outFrames; ++f) {
        const double srcPos = srcStart + double(f) * rate;
        const qint64 idx0 = qint64(srcPos);
        if (idx0 >= srcTotalFrames) break;
        const qint64 idx1 = std::min(idx0 + 1, srcTotalFrames - 1);
        const float frac = float(srcPos - idx0);

        if (srcChannels == 2) {
            dst[f * 2 + 0] = src[idx0 * 2 + 0] * (1.f - frac) + src[idx1 * 2 + 0] * frac;
            dst[f * 2 + 1] = src[idx0 * 2 + 1] * (1.f - frac) + src[idx1 * 2 + 1] * frac;
        } else {
            const float v = src[idx0 * srcChannels] * (1.f - frac) + src[idx1 * srcChannels] * frac;
            dst[f * 2 + 0] = v;
            dst[f * 2 + 1] = v;
        }
    }
}

// =========================================================================
//  Playback mix
// =========================================================================
void AudioEngine::playbackMix(float *out, int nFrames)
{
    if (!out || nFrames <= 0) return;

    std::memset(out, 0, sizeof(float) * nFrames * CHANNELS);

    if ((m_state != State::Playing && m_state != State::Recording) || !m_trackModel) return;

    const int trackCount = m_trackModel->rowCount();
    if (trackCount == 0) return;

    const bool anySolo = m_trackModel->anyTrackInSolo();

    qint64 startFrame = m_playheadFrame.load(std::memory_order_relaxed);

    const float rate = m_playbackRate;
    // Cuántos frames del source consumiremos en este bloque
    const qint64 sourceAdvance = qint64(std::ceil(double(nFrames) * rate));
    qint64 maxFrameSeen = 0;

    for (int t = 0; t < trackCount; ++t) {
        const auto snap = m_trackModel->mixerSnapshot(t);
        if (!snap.valid) continue;
        if (snap.muted) {
            m_trackModel->updateTrackLevel(t, 0.0f, 0.0f);
            continue;
        }
        if (m_state == State::Recording && snap.armed) {
            continue;
        }
        if (anySolo && !snap.solo) {
            m_trackModel->updateTrackLevel(t, 0.0f, 0.0f);
            continue;
        }
        const float gain = snap.gain;

        const float panVal = m_trackModel->getTrackPan(t);
        const float gainR = std::min(1.0f, 1.0f + panVal);
        const float gainL = std::min(1.0f, 1.0f - panVal);

        // Verificar que la pista tiene audio y no hemos pasado el final
        const qint64 trackFrames = m_trackModel->trackFrameCount(t);
        if (trackFrames <= 0) {
            m_trackModel->updateTrackLevel(t, 0.0f, 0.0f);
            continue;
        }
        if (trackFrames > maxFrameSeen) maxFrameSeen = trackFrames;
        if (startFrame >= trackFrames) {
            m_trackModel->updateTrackLevel(t, 0.0f, 0.0f);
            continue;
        }

        // --- Buffer temporal para esta pista (estéreo) ---
        thread_local QVector<float> trackBuf;
        if (trackBuf.size() < nFrames * 2)
            trackBuf.resize(nFrames * 2);
        std::memset(trackBuf.data(), 0, sizeof(float) * nFrames * 2);

        if (qFuzzyCompare(rate, 1.0f)) {
            thread_local QVector<float> segBuf;
            if (segBuf.size() < nFrames * 2)
                segBuf.resize(nFrames * 2);

            m_trackModel->readMixSegment(t, startFrame, nFrames, segBuf.data());

            for (int f = 0; f < nFrames; ++f) {
                trackBuf[f * 2 + 0] = segBuf[f * 2 + 0] * gain * gainL;
                trackBuf[f * 2 + 1] = segBuf[f * 2 + 1] * gain * gainR;
            }
        } else {
            const int srcFramesNeeded = int(sourceAdvance) + 2;
            thread_local QVector<float> segBuf;
            if (segBuf.size() < srcFramesNeeded * 2)
                segBuf.resize(srcFramesNeeded * 2);

            m_trackModel->readMixSegment(t, startFrame, srcFramesNeeded, segBuf.data());

            for (int f = 0; f < nFrames; ++f) {
                const double srcPos = double(f) * rate;
                const qint64 idx0 = qint64(srcPos);
                if (idx0 >= srcFramesNeeded - 1) break;
                const qint64 idx1 = idx0 + 1;
                const float frac = float(srcPos - idx0);

                const float l = segBuf[idx0 * 2 + 0] * (1.f - frac) + segBuf[idx1 * 2 + 0] * frac;
                const float r = segBuf[idx0 * 2 + 1] * (1.f - frac) + segBuf[idx1 * 2 + 1] * frac;
                trackBuf[f * 2 + 0] = l * gain * gainL;
                trackBuf[f * 2 + 1] = r * gain * gainR;
            }
        }

        // --- Aplicar cadena FX de la pista ---
        TrackFxChain *fxc = m_trackModel->trackFxChain(t);
        if (fxc && fxc->hasActiveEffects()) {
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
                        const qint64 ctrlFrames = m_trackModel->trackFrameCount(ctrlIdx);

                        if (ctrlFrames > 0 && startFrame < ctrlFrames) {
                            thread_local QVector<float> trackScBuf;
                            if (trackScBuf.size() < nFrames * 2) trackScBuf.resize(nFrames * 2);
                            std::memset(trackScBuf.data(), 0, sizeof(float) * nFrames * 2);

                            if (qFuzzyCompare(rate, 1.0f)) {
                                m_trackModel->readMixSegment(ctrlIdx, startFrame, nFrames, trackScBuf.data());
                            } else {
                                const int srcN = int(sourceAdvance) + 2;
                                thread_local QVector<float> scSeg;
                                if (scSeg.size() < srcN * 2) scSeg.resize(srcN * 2);
                                m_trackModel->readMixSegment(ctrlIdx, startFrame, srcN, scSeg.data());
                                for (int f = 0; f < nFrames; ++f) {
                                    const double srcPos = double(f) * rate;
                                    const qint64 idx0 = qint64(srcPos);
                                    if (idx0 >= srcN - 1) break;
                                    const qint64 idx1 = idx0 + 1;
                                    const float frac = float(srcPos - idx0);
                                    trackScBuf[f * 2 + 0] = scSeg[idx0 * 2 + 0] * (1.f - frac) + scSeg[idx1 * 2 + 0] * frac;
                                    trackScBuf[f * 2 + 1] = scSeg[idx0 * 2 + 1] * (1.f - frac) + scSeg[idx1 * 2 + 1] * frac;
                                }
                            }
                            ad->feedSidechain(trackScBuf.data(), nFrames, 2, SAMPLE_RATE);
                        } else {
                            ad->reset();
                        }
                    }
                } else {
                    ad->reset();
                }
            }

            fxc->process(trackBuf.data(), nFrames, 2, SAMPLE_RATE);
        }

        // --- Sumar al bus master y medir niveles ---
        float peakTL = 0.0f, peakTR = 0.0f;
        for (int f = 0; f < nFrames; ++f) {
            const float lv = trackBuf[f * 2 + 0];
            const float rv = trackBuf[f * 2 + 1];
            out[f * 2 + 0] += lv;
            out[f * 2 + 1] += rv;
            const float al = std::abs(lv);
            const float ar = std::abs(rv);
            if (al > peakTL) peakTL = al;
            if (ar > peakTR) peakTR = ar;
        }

        const float prevL = m_trackModel->getTrackLevelLeft(t);
        const float prevR = m_trackModel->getTrackLevelRight(t);
        const float smoothedL = (peakTL > prevL) ? peakTL : (prevL * 0.85f + peakTL * 0.15f);
        const float smoothedR = (peakTR > prevR) ? peakTR : (prevR * 0.85f + peakTR * 0.15f);
        m_trackModel->updateTrackLevel(t, std::min(1.0f, smoothedL), std::min(1.0f, smoothedR));
    }

    // =====================================================================
    //  AutoDuck con sidechain entre pistas (master)
    // =====================================================================
    if (m_autoDuck->enabled() && m_autoDuck->controlTrack() >= 0 &&
        m_autoDuck->targetTrack() >= 0 &&
        m_autoDuck->controlTrack() != m_autoDuck->targetTrack()) {

        const int ctrlIdx = m_autoDuck->controlTrack();
        const int tgtIdx  = m_autoDuck->targetTrack();

        const auto ctrlSnap = m_trackModel->mixerSnapshot(ctrlIdx);
        const auto tgtSnap  = m_trackModel->mixerSnapshot(tgtIdx);

        const bool ctrlSilenced = !ctrlSnap.valid || ctrlSnap.muted ||
                                  (anySolo && !ctrlSnap.solo);
        const bool tgtSilenced  = !tgtSnap.valid || tgtSnap.muted ||
                                  (anySolo && !tgtSnap.solo);

        if (ctrlSilenced || tgtSilenced) {
            m_autoDuck->reset();
        } else {

        const qint64 ctrlFrames = m_trackModel->trackFrameCount(ctrlIdx);

        if (ctrlFrames > 0 && startFrame < ctrlFrames) {
            thread_local QVector<float> scBuf;
            if (scBuf.size() < nFrames * 2) scBuf.resize(nFrames * 2);
            std::memset(scBuf.data(), 0, sizeof(float) * nFrames * 2);

            if (qFuzzyCompare(rate, 1.0f)) {
                m_trackModel->readMixSegment(ctrlIdx, startFrame, nFrames, scBuf.data());
            } else {
                const int srcN = int(sourceAdvance) + 2;
                thread_local QVector<float> scSeg;
                if (scSeg.size() < srcN * 2) scSeg.resize(srcN * 2);
                m_trackModel->readMixSegment(ctrlIdx, startFrame, srcN, scSeg.data());
                for (int f = 0; f < nFrames; ++f) {
                    const double srcPos = double(f) * rate;
                    const qint64 idx0 = qint64(srcPos);
                    if (idx0 >= srcN - 1) break;
                    const qint64 idx1 = idx0 + 1;
                    const float frac = float(srcPos - idx0);
                    scBuf[f * 2 + 0] = scSeg[idx0 * 2 + 0] * (1.f - frac) + scSeg[idx1 * 2 + 0] * frac;
                    scBuf[f * 2 + 1] = scSeg[idx0 * 2 + 1] * (1.f - frac) + scSeg[idx1 * 2 + 1] * frac;
                }
            }

            m_autoDuck->feedSidechain(scBuf.data(), nFrames, 2, SAMPLE_RATE);

            const qint64 tgtFrames = m_trackModel->trackFrameCount(tgtIdx);

            if (tgtFrames > 0 && startFrame < tgtFrames) {
                const QVariantMap tgtSnapData = m_trackModel->getTrackData(tgtIdx);
                const float tgtGain = tgtSnapData.value("gain", 1.0f).toFloat();
                const float tgtPan = m_trackModel->getTrackPan(tgtIdx);
                const float tgtGainL = std::min(1.0f, 1.0f - tgtPan);
                const float tgtGainR = std::min(1.0f, 1.0f + tgtPan);

                thread_local QVector<float> tgtBuf;
                if (tgtBuf.size() < nFrames * 2) tgtBuf.resize(nFrames * 2);
                std::memset(tgtBuf.data(), 0, sizeof(float) * nFrames * 2);

                thread_local QVector<float> tgtSegBuf;
                if (qFuzzyCompare(rate, 1.0f)) {
                    if (tgtSegBuf.size() < nFrames * 2) tgtSegBuf.resize(nFrames * 2);
                    m_trackModel->readMixSegment(tgtIdx, startFrame, nFrames, tgtSegBuf.data());
                    for (int f = 0; f < nFrames; ++f) {
                        tgtBuf[f * 2 + 0] = tgtSegBuf[f * 2 + 0] * tgtGain * tgtGainL;
                        tgtBuf[f * 2 + 1] = tgtSegBuf[f * 2 + 1] * tgtGain * tgtGainR;
                    }
                } else {
                    const int srcN = int(sourceAdvance) + 2;
                    if (tgtSegBuf.size() < srcN * 2) tgtSegBuf.resize(srcN * 2);
                    m_trackModel->readMixSegment(tgtIdx, startFrame, srcN, tgtSegBuf.data());
                    for (int f = 0; f < nFrames; ++f) {
                        const double srcPos = double(f) * rate;
                        const qint64 idx0 = qint64(srcPos);
                        if (idx0 >= srcN - 1) break;
                        const qint64 idx1 = idx0 + 1;
                        const float frac = float(srcPos - idx0);
                        const float l = tgtSegBuf[idx0 * 2 + 0] * (1.f - frac) + tgtSegBuf[idx1 * 2 + 0] * frac;
                        const float r = tgtSegBuf[idx0 * 2 + 1] * (1.f - frac) + tgtSegBuf[idx1 * 2 + 1] * frac;
                        tgtBuf[f * 2 + 0] = l * tgtGain * tgtGainL;
                        tgtBuf[f * 2 + 1] = r * tgtGain * tgtGainR;
                    }
                }

                for (int f = 0; f < nFrames; ++f) {
                    out[f * 2 + 0] -= tgtBuf[f * 2 + 0];
                    out[f * 2 + 1] -= tgtBuf[f * 2 + 1];
                }

                m_autoDuck->processDuck(tgtBuf.data(), nFrames, 2, SAMPLE_RATE);

                for (int f = 0; f < nFrames; ++f) {
                    out[f * 2 + 0] += tgtBuf[f * 2 + 0];
                    out[f * 2 + 1] += tgtBuf[f * 2 + 1];
                }
            }
        }
        } // else (tracks not silenced)
    }

    // Avanzar playhead con posible wrap por loop.
    // Con rate > 1 consumimos más frames del source por cada bloque de output.
    qint64 newPlayhead;
    if (m_state == State::Playing) {
        qint64 current = m_playheadFrame.load(std::memory_order_relaxed);
        qint64 loopStart = m_loopStartFrame.load(std::memory_order_relaxed);
        qint64 loopEnd = m_loopEndFrame.load(std::memory_order_relaxed);
        while (true) {
            qint64 target = current + sourceAdvance;
            if (loopEnd > loopStart && target >= loopEnd) {
                target = loopStart;
            }
            if (m_playheadFrame.compare_exchange_weak(current, target, std::memory_order_relaxed)) {
                newPlayhead = target;
                break;
            }
        }
    } else {
        newPlayhead = m_playheadFrame.load(std::memory_order_relaxed);
    }

    // =====================================================================
    //  Cadena de efectos master
    // =====================================================================
    for (auto *fx : m_fxChain) {
        if (fx->enabled()) {
            fx->process(out, nFrames, CHANNELS, SAMPLE_RATE);
        }
    }

    // --- Medición de niveles post-FX (peak, no RMS) ---
    float peakL = 0.0f, peakR = 0.0f;
    for (int f = 0; f < nFrames; ++f) {
        // Hard-clip de seguridad final (el limitador ya lo hace, pero por si
        // está desactivado)
        out[f * 2 + 0] = std::max(-1.0f, std::min(1.0f, out[f * 2 + 0]));
        out[f * 2 + 1] = std::max(-1.0f, std::min(1.0f, out[f * 2 + 1]));
        const float al = std::abs(out[f * 2 + 0]);
        const float ar = std::abs(out[f * 2 + 1]);
        if (al > peakL) peakL = al;
        if (ar > peakR) peakR = ar;
    }

    if (peakL > m_peakAccumL) m_peakAccumL = peakL;
    if (peakR > m_peakAccumR) m_peakAccumR = peakR;
    m_peakAccumValid = true;

    if (m_state == State::Playing && newPlayhead >= maxFrameSeen && maxFrameSeen > 0) {
        QMetaObject::invokeMethod(this, "stop", Qt::QueuedConnection);
    }
}

// =========================================================================
//  UI timer
// =========================================================================
void AudioEngine::onUpdateTimer()
{
    {
        QMutexLocker lock(&m_playbackMutex);
        m_currentTime = double(m_playheadFrame) / SAMPLE_RATE;
    }
    emit timeChanged();

    // Emit samplesRecorded in the SAME tick as timeChanged so QML
    // updates the cursor and waveform in the same render frame.
    if (m_state == State::Recording)
        emit samplesRecordedChanged();

    if (m_peakAccumValid) {
        const float peakL = m_peakAccumL;
        const float peakR = m_peakAccumR;
        // Smoothed peak: sube instantáneamente, baja suavemente (como las pistas)
        m_masterLeftLevel  = (peakL > m_masterLeftLevel)  ? peakL : (m_masterLeftLevel * 0.85f + peakL * 0.15f);
        m_masterRightLevel = (peakR > m_masterRightLevel) ? peakR : (m_masterRightLevel * 0.85f + peakR * 0.15f);
        m_peakAccumL = 0.0f;
        m_peakAccumR = 0.0f;
        m_peakAccumValid = false;
    } else {
        m_masterLeftLevel  *= 0.85f;
        m_masterRightLevel *= 0.85f;
    }
    emit levelsChanged();

    if (m_trackModel)
        emit m_trackModel->trackLevelsChanged();
}

QVector<float> AudioEngine::trackSamples(int trackIndex) const
{
    if (!m_trackModel) return {};
    return m_trackModel->trackSamples(trackIndex);
}

int AudioEngine::totalFrames() const
{
    if (!m_trackModel) return 0;
    qint64 maxFrames = 0;
    for (int i = 0; i < m_trackModel->rowCount(); ++i) {
        maxFrames = std::max(maxFrames, m_trackModel->trackFrameCount(i));
    }
    return int(maxFrames);
}
