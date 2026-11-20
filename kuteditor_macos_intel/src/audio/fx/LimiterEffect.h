#pragma once

#include "MasterEffect.h"
#include <algorithm>

/**
 * LimiterEffect: brickwall limiter master.
 *
 * Attack instantáneo, release suave (~50ms). Hard-clip de seguridad a 0 dBFS.
 * Migrado desde el código inline de AudioEngine.
 */
class LimiterEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float thresholdDb READ thresholdDb WRITE setThresholdDb NOTIFY changed)

public:
    explicit LimiterEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

    float thresholdDb() const { return m_thresholdDb; }
    Q_INVOKABLE void setThresholdDb(float v) {
        if (v != m_thresholdDb) { m_thresholdDb = v; emit changed(); }
    }

    float gainReduction() const override { return m_gr; }

    void process(float *buffer, int nFrames, int channels, int sampleRate) override
    {
        const float threshold = dbToLin(m_thresholdDb);
        // Release ~50ms
        const float releaseCoeff = std::exp(-1.0f / (0.05f * sampleRate));
        float maxGR = 0.0f;

        for (int f = 0; f < nFrames; ++f) {
            float &l = buffer[f * channels + 0];
            float &r = buffer[f * channels + (channels > 1 ? 1 : 0)];
            const float peak = std::max(std::abs(l), std::abs(r));

            // Envelope: attack instantáneo, release suave
            if (peak > m_envelope)
                m_envelope = peak;
            else
                m_envelope = m_envelope * releaseCoeff + peak * (1.0f - releaseCoeff);

            float gr = 1.0f;
            if (m_envelope > threshold && m_envelope > 1e-8f)
                gr = threshold / m_envelope;

            l *= gr;
            if (channels > 1) r *= gr;

            // Hard-clip de seguridad
            l = std::clamp(l, -1.0f, 1.0f);
            r = std::clamp(r, -1.0f, 1.0f);

            if (gr < 1.0f) {
                const float grDb = 20.0f * std::log10(gr);
                if (grDb < maxGR) maxGR = grDb;
            }
        }

        m_gr = 0.7f * m_gr + 0.3f * maxGR;
    }

    void reset() override { m_envelope = 0.0f; m_gr = 0.0f; }

private:
    float m_thresholdDb = -1.0f;
    float m_envelope = 0.0f;
    float m_gr = 0.0f;
};
