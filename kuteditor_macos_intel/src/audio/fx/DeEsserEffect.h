#pragma once

#include "MasterEffect.h"

/**
 * DeEsserEffect: de-esser por compresión de banda de sibilantes.
 *
 * Un filtro bandpass aísla la banda de sibilantes (configurable, típico
 * 4-8 kHz). Cuando la energía en esa banda supera el threshold, se aplica
 * reducción de ganancia a la señal completa (wideband) o solo a la banda
 * (split). Por simplicidad usamos wideband que suena más natural en la
 * mayoría de casos.
 *
 * Internamente usa un biquad bandpass para el sidechain y un envelope
 * follower con attack rápido y release configurable.
 */
class DeEsserEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float frequency READ frequency WRITE setFrequency NOTIFY changed)
    Q_PROPERTY(float thresholdDb READ thresholdDb WRITE setThresholdDb NOTIFY changed)
    Q_PROPERTY(float ratio READ ratio WRITE setRatio NOTIFY changed)
    Q_PROPERTY(float releaseMs READ releaseMs WRITE setReleaseMs NOTIFY changed)

public:
    explicit DeEsserEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

    float frequency() const { return m_freq; }
    Q_INVOKABLE void setFrequency(float v) {
        m_freq = std::clamp(v, 2000.0f, 12000.0f);
        m_dirty = true;
        emit changed();
    }

    float thresholdDb() const { return m_thresholdDb; }
    Q_INVOKABLE void setThresholdDb(float v) { m_thresholdDb = v; emit changed(); }

    float ratio() const { return m_ratio; }
    Q_INVOKABLE void setRatio(float v) { m_ratio = std::max(1.0f, v); emit changed(); }

    float releaseMs() const { return m_releaseMs; }
    Q_INVOKABLE void setReleaseMs(float v) { m_releaseMs = std::max(1.0f, v); emit changed(); }

    float gainReduction() const override { return m_gr; }

    void process(float *buffer, int nFrames, int channels, int sampleRate) override
    {
        if (m_dirty || sampleRate != m_lastSR) {
            recalcBandpass(sampleRate);
            m_dirty = false;
        }

        const float threshLin = dbToLin(m_thresholdDb);
        // Attack muy rápido para sibilantes (0.1ms)
        const float attackCoeff  = std::exp(-1.0f / (0.0001f * sampleRate));
        const float releaseCoeff = std::exp(-1.0f / (m_releaseMs * 0.001f * sampleRate));
        float maxGR = 0.0f;

        for (int f = 0; f < nFrames; ++f) {
            // Sidechain: bandpass de la señal mono (promedio L+R)
            float mono = buffer[f * channels + 0];
            if (channels > 1) mono = (mono + buffer[f * channels + 1]) * 0.5f;

            // Bandpass biquad para aislar sibilantes
            float bp = m_bp_b0 * mono + m_bp_z1;
            m_bp_z1 = m_bp_b1 * mono - m_bp_a1 * bp + m_bp_z2;
            m_bp_z2 = m_bp_b2 * mono - m_bp_a2 * bp;

            float scLevel = std::abs(bp);

            // Envelope follower
            if (scLevel > m_envelope)
                m_envelope = attackCoeff * m_envelope + (1.0f - attackCoeff) * scLevel;
            else
                m_envelope = releaseCoeff * m_envelope + (1.0f - releaseCoeff) * scLevel;

            // Gain reduction
            float gr = 1.0f;
            if (m_envelope > threshLin && m_envelope > 1e-8f) {
                const float overDb = 20.0f * std::log10(m_envelope / threshLin);
                const float targetDb = overDb / m_ratio;
                const float reductionDb = overDb - targetDb;
                gr = std::pow(10.0f, -reductionDb / 20.0f);
            }

            // Aplicar al fullband
            for (int ch = 0; ch < std::min(channels, 2); ++ch)
                buffer[f * channels + ch] *= gr;

            if (gr < 1.0f) {
                const float grDb = 20.0f * std::log10(gr);
                if (grDb < maxGR) maxGR = grDb;
            }
        }

        m_gr = 0.7f * m_gr + 0.3f * maxGR;
    }

    void reset() override {
        m_envelope = 0.0f;
        m_gr = 0.0f;
        m_bp_z1 = m_bp_z2 = 0.0f;
    }

private:
    void recalcBandpass(int sampleRate) {
        m_lastSR = sampleRate;
        // Bandpass con Q=2 centrado en m_freq
        const double w0 = 2.0 * M_PI * m_freq / sampleRate;
        const double cosw0 = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * 2.0); // Q=2

        const double a0 = 1.0 + alpha;
        m_bp_b0 = float(alpha / a0);
        m_bp_b1 = 0.0f;
        m_bp_b2 = float(-alpha / a0);
        m_bp_a1 = float(-2.0 * cosw0 / a0);
        m_bp_a2 = float((1.0 - alpha) / a0);
    }

    float m_freq = 6000.0f;
    float m_thresholdDb = -20.0f;
    float m_ratio = 4.0f;
    float m_releaseMs = 30.0f;
    bool m_dirty = true;
    int m_lastSR = 0;

    // Bandpass sidechain
    float m_bp_b0 = 0, m_bp_b1 = 0, m_bp_b2 = 0;
    float m_bp_a1 = 0, m_bp_a2 = 0;
    float m_bp_z1 = 0, m_bp_z2 = 0;

    float m_envelope = 0.0f;
    float m_gr = 0.0f;
};
