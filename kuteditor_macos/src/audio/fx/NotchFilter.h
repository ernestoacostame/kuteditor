#pragma once

#include "MasterEffect.h"

/**
 * NotchFilter: filtro notch (band-reject) biquad.
 *
 * Elimina una frecuencia específica con un ancho de banda controlado por Q.
 * Uso principal: quitar el zumbido de red eléctrica (50 Hz en Europa,
 * 60 Hz en América) y sus armónicos.
 * Frecuencia: 20-20000 Hz. Q: 0.5-30 (más alto = más estrecho).
 */
class NotchFilter : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float frequency READ frequency WRITE setFrequency NOTIFY changed)
    Q_PROPERTY(float q READ q WRITE setQ NOTIFY changed)

public:
    explicit NotchFilter(QObject *parent = nullptr) : MasterEffect(parent) {}

    float frequency() const { return m_freq; }
    Q_INVOKABLE void setFrequency(float v) {
        m_freq = std::clamp(v, 20.0f, 20000.0f);
        m_dirty = true;
        emit changed();
    }

    float q() const { return m_q; }
    Q_INVOKABLE void setQ(float v) {
        m_q = std::clamp(v, 0.5f, 30.0f);
        m_dirty = true;
        emit changed();
    }

    void process(float *buffer, int nFrames, int channels, int sampleRate) override
    {
        if (m_dirty || sampleRate != m_lastSR) {
            recalcCoeffs(sampleRate);
            m_dirty = false;
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
        const double w0 = 2.0 * M_PI * m_freq / sampleRate;
        const double cosw0 = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * m_q);

        const double a0 = 1.0 + alpha;
        m_b0 = float(1.0 / a0);
        m_b1 = float(-2.0 * cosw0 / a0);
        m_b2 = m_b0;
        m_a1 = m_b1;
        m_a2 = float((1.0 - alpha) / a0);
    }

    float m_freq = 50.0f;
    float m_q = 10.0f;
    bool m_dirty = true;
    int m_lastSR = 0;

    float m_b0 = 0, m_b1 = 0, m_b2 = 0;
    float m_a1 = 0, m_a2 = 0;
    float m_z1[2] = {0, 0};
    float m_z2[2] = {0, 0};
};
