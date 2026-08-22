#pragma once

#include "MasterEffect.h"

/**
 * LowPassFilter: filtro paso bajo biquad de 2º orden (Butterworth).
 *
 * Elimina frecuencias por encima de la frecuencia de corte.
 * Útil para quitar hiss, silbidos, ruido de alta frecuencia.
 * Frecuencia ajustable de 1 kHz a 20 kHz.
 */
class LowPassFilter : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float cutoffHz READ cutoffHz WRITE setCutoffHz NOTIFY changed)

public:
    explicit LowPassFilter(QObject *parent = nullptr) : MasterEffect(parent) {
        recalcCoeffs(48000);
    }

    float cutoffHz() const { return m_cutoffHz; }
    Q_INVOKABLE void setCutoffHz(float v) {
        m_cutoffHz = std::clamp(v, 1000.0f, 20000.0f);
        m_coeffsDirty = true;
        emit changed();
    }

    void process(float *buffer, int nFrames, int channels, int sampleRate) override
    {
        if (m_coeffsDirty || sampleRate != m_lastSR) {
            recalcCoeffs(sampleRate);
            m_coeffsDirty = false;
        }

        for (int f = 0; f < nFrames; ++f) {
            for (int ch = 0; ch < std::min(channels, 2); ++ch) {
                float &s = buffer[f * channels + ch];
                float x = s;
                float y = m_b0 * x + m_z1[ch];
                m_z1[ch] = m_b1 * x - m_a1 * y + m_z2[ch];
                m_z2[ch] = m_b2 * x - m_a2 * y;
                s = y;
            }
        }
    }

    void reset() override {
        m_z1[0] = m_z1[1] = 0.0f;
        m_z2[0] = m_z2[1] = 0.0f;
    }

private:
    void recalcCoeffs(int sampleRate) {
        m_lastSR = sampleRate;
        const double w0 = 2.0 * M_PI * m_cutoffHz / sampleRate;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * 0.7071);

        const double a0 = 1.0 + alpha;
        m_b0 = float((1.0 - cosw0) / 2.0 / a0);
        m_b1 = float((1.0 - cosw0) / a0);
        m_b2 = m_b0;
        m_a1 = float(-2.0 * cosw0 / a0);
        m_a2 = float((1.0 - alpha) / a0);
    }

    float m_cutoffHz = 8000.0f;
    bool m_coeffsDirty = true;
    int m_lastSR = 0;

    float m_b0 = 0, m_b1 = 0, m_b2 = 0;
    float m_a1 = 0, m_a2 = 0;
    float m_z1[2] = {0, 0};
    float m_z2[2] = {0, 0};
};
