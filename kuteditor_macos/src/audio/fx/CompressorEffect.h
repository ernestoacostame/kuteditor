#pragma once

#include "MasterEffect.h"

/**
 * CompressorEffect: compresor de dinámica master.
 *
 * Envelope follower con attack/release separados, ratio variable,
 * y makeup gain. Migrado desde el código inline de AudioEngine.
 */
class CompressorEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float thresholdDb READ thresholdDb WRITE setThresholdDb NOTIFY changed)
    Q_PROPERTY(float ratio READ ratio WRITE setRatio NOTIFY changed)
    Q_PROPERTY(float attackMs READ attackMs WRITE setAttackMs NOTIFY changed)
    Q_PROPERTY(float releaseMs READ releaseMs WRITE setReleaseMs NOTIFY changed)
    Q_PROPERTY(float makeupDb READ makeupDb WRITE setMakeupDb NOTIFY changed)
    Q_PROPERTY(bool autoMakeup READ autoMakeup WRITE setAutoMakeup NOTIFY changed)

public:
    explicit CompressorEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

    float thresholdDb() const { return m_thresholdDb; }
    Q_INVOKABLE void setThresholdDb(float v) { m_thresholdDb = v; emit changed(); }

    float ratio() const { return m_ratio; }
    Q_INVOKABLE void setRatio(float v) { m_ratio = std::max(1.0f, v); emit changed(); }

    float attackMs() const { return m_attackMs; }
    Q_INVOKABLE void setAttackMs(float v) { m_attackMs = std::max(0.1f, v); emit changed(); }

    float releaseMs() const { return m_releaseMs; }
    Q_INVOKABLE void setReleaseMs(float v) { m_releaseMs = std::max(1.0f, v); emit changed(); }

    float makeupDb() const { return m_makeupDb; }
    Q_INVOKABLE void setMakeupDb(float v) { m_makeupDb = v; emit changed(); }

    bool autoMakeup() const { return m_autoMakeup; }
    Q_INVOKABLE void setAutoMakeup(bool v) { m_autoMakeup = v; emit changed(); }

    float gainReduction() const override { return m_gr; }

    void process(float *buffer, int nFrames, int channels, int sampleRate) override
    {
        const float attackCoeff  = std::exp(-1.0f / (m_attackMs  * 0.001f * sampleRate));
        const float releaseCoeff = std::exp(-1.0f / (m_releaseMs * 0.001f * sampleRate));
        const float threshLin = dbToLin(m_thresholdDb);

        // Auto-makeup: compensa la reducción media esperada.
        // Fórmula estándar: -threshold × (1 - 1/ratio) / 2
        // Esto aproxima cuánto baja el volumen percibido por la compresión
        // y lo compensa automáticamente.
        float effectiveMakeupDb = m_makeupDb;
        if (m_autoMakeup) {
            effectiveMakeupDb = -m_thresholdDb * (1.0f - 1.0f / m_ratio) * 0.5f;
        }
        const float makeupLin = dbToLin(effectiveMakeupDb);

        float maxGR = 0.0f;

        for (int f = 0; f < nFrames; ++f) {
            float &l = buffer[f * channels + 0];
            float &r = buffer[f * channels + (channels > 1 ? 1 : 0)];
            const float peak = std::max(std::abs(l), std::abs(r));

            if (peak > m_envelope)
                m_envelope = attackCoeff * m_envelope + (1.0f - attackCoeff) * peak;
            else
                m_envelope = releaseCoeff * m_envelope + (1.0f - releaseCoeff) * peak;

            float gr = 1.0f;
            if (m_envelope > threshLin && m_envelope > 1e-8f) {
                const float overDb = 20.0f * std::log10(m_envelope / threshLin);
                const float targetDb = overDb / m_ratio;
                const float reductionDb = overDb - targetDb;
                gr = std::pow(10.0f, -reductionDb / 20.0f);
            }

            l *= gr * makeupLin;
            if (channels > 1) r *= gr * makeupLin;

            if (gr < 1.0f) {
                const float grDb = 20.0f * std::log10(gr);
                if (grDb < maxGR) maxGR = grDb;
            }
        }

        m_gr = 0.7f * m_gr + 0.3f * maxGR;
    }

    void reset() override { m_envelope = 0.0f; m_gr = 0.0f; }

private:
    float m_thresholdDb = -20.0f;
    float m_ratio = 4.0f;
    float m_attackMs = 10.0f;
    float m_releaseMs = 100.0f;
    float m_makeupDb = 0.0f;
    bool  m_autoMakeup = true;
    float m_envelope = 0.0f;
    float m_gr = 0.0f;
};
