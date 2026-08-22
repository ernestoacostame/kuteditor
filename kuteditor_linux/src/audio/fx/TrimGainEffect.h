#pragma once

#include "MasterEffect.h"
#include <algorithm>

/**
 * TrimGainEffect: ganancia simple pre/post cadena.
 *
 * Aplica una ganancia en dB al buffer. Útil para ajustar niveles
 * entre efectos o para compensar pérdidas/ganancias de la cadena.
 * Rango: -24 dB a +24 dB.
 */
class TrimGainEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float gainDb READ gainDb WRITE setGainDb NOTIFY changed)

public:
    explicit TrimGainEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

    float gainDb() const { return m_gainDb; }
    Q_INVOKABLE void setGainDb(float v) {
        m_gainDb = std::clamp(v, -24.0f, 24.0f);
        m_gainLin = dbToLin(m_gainDb);
        emit changed();
    }

    void process(float *buffer, int nFrames, int channels, int /*sampleRate*/) override
    {
        for (int f = 0; f < nFrames; ++f) {
            for (int ch = 0; ch < channels; ++ch) {
                buffer[f * channels + ch] *= m_gainLin;
            }
        }
    }

    void reset() override {}

private:
    float m_gainDb = 0.0f;
    float m_gainLin = 1.0f;
};
