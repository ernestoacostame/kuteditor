#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QHash>
#include <QMutex>
#include <QElapsedTimer>
#include <memory>
#include <atomic>

#include "fx/MasterEffect.h"
#include "fx/CompressorEffect.h"
#include "fx/LimiterEffect.h"
#include "fx/HighPassFilter.h"
#include "fx/LowPassFilter.h"
#include "fx/NotchFilter.h"
#include "fx/NoiseGateEffect.h"
#include "fx/RNNoiseEffect.h"
#include "fx/DeepFilterEffect.h"
#include "fx/DeEsserEffect.h"
#include "fx/ExpanderEffect.h"
#include "fx/AutoDuckEffect.h"
#include "fx/AutoGainEffect.h"
#include "fx/EqualizerEffect.h"
#include "fx/StereoWidener.h"
#include "fx/PhaseInvertEffect.h"
#include "fx/MonoMixerEffect.h"
#include "fx/TrimGainEffect.h"

class TrackModel;

/**
 * AudioEngine: Motor de transporte, grabación y reproducción multipista.
 *
 * La cadena de efectos master se procesa en playbackMix() después de la
 * mezcla de pistas, en este orden:
 *   HPF → DeNoiser → NoiseGate → Compresor → EQ → AutoDuck → Limitador
 *
 * Cada efecto hereda de MasterEffect y se expone a QML como propiedad.
 */
class AudioEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(float masterLeftLevel READ masterLeftLevel NOTIFY levelsChanged)
    Q_PROPERTY(float masterRightLevel READ masterRightLevel NOTIFY levelsChanged)
    Q_PROPERTY(double currentTime READ currentTime NOTIFY timeChanged)
    Q_PROPERTY(double totalTime READ totalTime NOTIFY totalTimeChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY stateChanged)
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY stateChanged)
    Q_PROPERTY(qint64 samplesRecorded READ samplesRecorded NOTIFY samplesRecordedChanged)
    Q_PROPERTY(float playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(bool returnPlayheadOnStop READ returnPlayheadOnStop WRITE setReturnPlayheadOnStop NOTIFY returnPlayheadOnStopChanged)

    // --- Efectos master expuestos a QML ---
    Q_PROPERTY(CompressorEffect* compressor READ compressor CONSTANT)
    Q_PROPERTY(LimiterEffect* limiter READ limiter CONSTANT)
    Q_PROPERTY(HighPassFilter* highPassFilter READ highPassFilter CONSTANT)
    Q_PROPERTY(LowPassFilter* lowPassFilter READ lowPassFilter CONSTANT)
    Q_PROPERTY(NotchFilter* notchFilter READ notchFilter CONSTANT)
    Q_PROPERTY(NoiseGateEffect* noiseGate READ noiseGate CONSTANT)
    Q_PROPERTY(RNNoiseEffect* deNoiser READ deNoiser CONSTANT)
    Q_PROPERTY(DeepFilterEffect* deepFilter READ deepFilter CONSTANT)
    Q_PROPERTY(DeEsserEffect* deEsser READ deEsser CONSTANT)
    Q_PROPERTY(ExpanderEffect* expander READ expander CONSTANT)
    Q_PROPERTY(AutoDuckEffect* autoDuck READ autoDuck CONSTANT)
    Q_PROPERTY(AutoGainEffect* autoGain READ autoGain CONSTANT)
    Q_PROPERTY(EqualizerEffect* equalizer READ equalizer CONSTANT)
    Q_PROPERTY(StereoWidener* stereoWidener READ stereoWidener CONSTANT)
    Q_PROPERTY(PhaseInvertEffect* phaseInvert READ phaseInvert CONSTANT)
    Q_PROPERTY(MonoMixerEffect* monoMixer READ monoMixer CONSTANT)
    Q_PROPERTY(TrimGainEffect* trimGain READ trimGain CONSTANT)
    Q_PROPERTY(int activeMasterFxCount READ activeMasterFxCount NOTIFY masterFxChanged)

    // --- Compatibilidad: propiedades delegadas al compresor/limitador ---
    Q_PROPERTY(bool limiterEnabled READ limiterEnabled WRITE setLimiterEnabled NOTIFY limiterChanged)
    Q_PROPERTY(float limiterThresholdDb READ limiterThresholdDb WRITE setLimiterThresholdDb NOTIFY limiterChanged)
    Q_PROPERTY(float limiterGainReduction READ limiterGainReduction NOTIFY levelsChanged)

    Q_PROPERTY(bool compEnabled READ compEnabled WRITE setCompEnabled NOTIFY compChanged)
    Q_PROPERTY(float compThresholdDb READ compThresholdDb WRITE setCompThresholdDb NOTIFY compChanged)
    Q_PROPERTY(float compRatio READ compRatio WRITE setCompRatio NOTIFY compChanged)
    Q_PROPERTY(float compAttackMs READ compAttackMs WRITE setCompAttackMs NOTIFY compChanged)
    Q_PROPERTY(float compReleaseMs READ compReleaseMs WRITE setCompReleaseMs NOTIFY compChanged)
    Q_PROPERTY(float compMakeupDb READ compMakeupDb WRITE setCompMakeupDb NOTIFY compChanged)
    Q_PROPERTY(float compGainReduction READ compGainReduction NOTIFY levelsChanged)

public:
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNELS = 2;

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    void setTrackModel(TrackModel *model);

    Q_INVOKABLE void play();
    Q_INVOKABLE void record();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekTime(double seconds);
    Q_INVOKABLE void setLoopRegion(double startSec, double endSec);
    Q_INVOKABLE void clearLoopRegion() { setLoopRegion(-1, -1); }
    Q_INVOKABLE void setPlaybackRate(float rate);
    float playbackRate() const { return m_playbackRate; }
    bool returnPlayheadOnStop() const { return m_returnPlayheadOnStop; }
    void setReturnPlayheadOnStop(bool v) {
        if (m_returnPlayheadOnStop == v) return;
        m_returnPlayheadOnStop = v;
        emit returnPlayheadOnStopChanged();
    }

    QVector<float> trackSamples(int trackIndex) const;
    int totalFrames() const;

    float masterLeftLevel() const { return m_masterLeftLevel; }
    float masterRightLevel() const { return m_masterRightLevel; }
    double currentTime() const { return m_currentTime; }
    /// Read the playhead position directly from the audio thread (fresh, not cached).
    /// Use during recording to avoid the 33ms timer lag of currentTime.
    Q_INVOKABLE double freshPlayheadTime() const {
        return double(m_playheadFrame.load(std::memory_order_relaxed)) / SAMPLE_RATE;
    }
    double totalTime() const { return m_totalTime; }
    bool isPlaying() const { return m_state == State::Playing; }
    bool isRecording() const { return m_state == State::Recording; }
    qint64 samplesRecorded() const {
        return m_samplesRecorded.load(std::memory_order_relaxed);
    }

    // Accesores a los efectos
    CompressorEffect* compressor() const { return m_compressor; }
    LimiterEffect* limiter() const { return m_limiter; }
    HighPassFilter* highPassFilter() const { return m_highPassFilter; }
    LowPassFilter* lowPassFilter() const { return m_lowPassFilter; }
    NotchFilter* notchFilter() const { return m_notchFilter; }
    NoiseGateEffect* noiseGate() const { return m_noiseGate; }
    RNNoiseEffect* deNoiser() const { return m_deNoiser; }
    DeepFilterEffect* deepFilter() const { return m_deepFilter; }
    DeEsserEffect* deEsser() const { return m_deEsser; }
    ExpanderEffect* expander() const { return m_expander; }
    AutoDuckEffect* autoDuck() const { return m_autoDuck; }
    AutoGainEffect* autoGain() const { return m_autoGain; }
    EqualizerEffect* equalizer() const { return m_equalizer; }
    StereoWidener* stereoWidener() const { return m_stereoWidener; }
    PhaseInvertEffect* phaseInvert() const { return m_phaseInvert; }
    MonoMixerEffect* monoMixer() const { return m_monoMixer; }
    TrimGainEffect* trimGain() const { return m_trimGain; }

    int activeMasterFxCount() const {
        int count = 0;
        for (auto *fx : m_fxChain)
            if (fx->enabled()) ++count;
        return count;
    }

    // --- Compatibilidad: delegaciones al limitador ---
    bool limiterEnabled() const;
    Q_INVOKABLE void setLimiterEnabled(bool v);
    float limiterThresholdDb() const;
    Q_INVOKABLE void setLimiterThresholdDb(float v);
    float limiterGainReduction() const;

    // --- Compatibilidad: delegaciones al compresor ---
    bool compEnabled() const;
    Q_INVOKABLE void setCompEnabled(bool v);
    float compThresholdDb() const;
    Q_INVOKABLE void setCompThresholdDb(float v);
    float compRatio() const;
    Q_INVOKABLE void setCompRatio(float v);
    float compAttackMs() const;
    Q_INVOKABLE void setCompAttackMs(float v);
    float compReleaseMs() const;
    Q_INVOKABLE void setCompReleaseMs(float v);
    float compMakeupDb() const;
    Q_INVOKABLE void setCompMakeupDb(float v);
    float compGainReduction() const;

    // Acceso a la cadena master (para ExportManager)
    const QVector<MasterEffect*>& masterFxChain() const { return m_fxChain; }

    void onAudioInput(int trackIndex, const float *interleaved, int nFrames);
    void playbackMix(float *out, int nFrames);
    bool isOutputActive() const { return m_state == State::Playing; }
    Q_INVOKABLE void checkMonitoring();

signals:
    void levelsChanged();
    void timeChanged();
    void totalTimeChanged();
    void stateChanged();
    void masterFxChanged();
    void recordingStarted();
    void recordingStopped(int totalFrames);
    void samplesRecordedChanged();
    void playbackStarted();
    void playbackStopped();
    void playbackRateChanged();
    void returnPlayheadOnStopChanged();
    void limiterChanged();
    void compChanged();

private slots:
    void onUpdateTimer();

private:
    enum class State { Stopped, Playing, Recording };

    State m_state = State::Stopped;
    bool m_monitoring = false;
    TrackModel *m_trackModel = nullptr;

    // --- Grabación ---
    mutable QMutex m_recordMutex;
    qint64 m_recordStartFrame = 0;
    std::atomic<qint64> m_samplesRecorded{0};
    QHash<int, int> m_recordFramesWritten;

    // --- Playback ---
    mutable QMutex m_playbackMutex;
    std::atomic<qint64> m_playheadFrame{0};
    std::atomic<qint64> m_loopStartFrame{-1};
    std::atomic<qint64> m_loopEndFrame{-1};
    float  m_playbackRate   = 1.0f;
    bool   m_returnPlayheadOnStop = false;
    std::atomic<qint64> m_playStartFrame{0};

    // --- Niveles master ---
    float m_masterLeftLevel = 0.0f;
    float m_masterRightLevel = 0.0f;
    float m_peakAccumL = 0.0f;
    float m_peakAccumR = 0.0f;
    bool  m_peakAccumValid = false;

    double m_currentTime = 0.0;
    double m_totalTime = 0.0;

    // --- Cadena de efectos master ---
    CompressorEffect   *m_compressor = nullptr;
    LimiterEffect      *m_limiter = nullptr;
    HighPassFilter     *m_highPassFilter = nullptr;
    LowPassFilter      *m_lowPassFilter = nullptr;
    NotchFilter        *m_notchFilter = nullptr;
    NoiseGateEffect    *m_noiseGate = nullptr;
    RNNoiseEffect     *m_deNoiser = nullptr;
    DeepFilterEffect   *m_deepFilter = nullptr;
    DeEsserEffect      *m_deEsser = nullptr;
    ExpanderEffect     *m_expander = nullptr;
    AutoDuckEffect     *m_autoDuck = nullptr;
    AutoGainEffect     *m_autoGain = nullptr;
    EqualizerEffect    *m_equalizer = nullptr;
    StereoWidener      *m_stereoWidener = nullptr;
    PhaseInvertEffect  *m_phaseInvert = nullptr;
    MonoMixerEffect    *m_monoMixer = nullptr;
    TrimGainEffect     *m_trimGain = nullptr;

    // Punteros en orden de procesamiento (no-owning, los objetos son hijos de this)
    QVector<MasterEffect*> m_fxChain;

    QTimer m_uiTimer;
};
