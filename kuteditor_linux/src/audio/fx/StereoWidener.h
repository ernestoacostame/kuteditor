#pragma once

#include "MasterEffect.h"
#include <vector>

/**
 * StereoWidener: ensanchamiento de imagen estéreo.
 *
 * Técnica Mid/Side: descompone la señal en Mid (L+R) y Side (L-R),
 * luego amplifica el Side respecto al Mid para ensanchar la imagen.
 *
 * - width: 0.0 = mono, 1.0 = normal (sin cambio), 2.0 = máximo ensanche.
 *   Valores >1 amplifican la diferencia entre canales.
 *   Valores <1 reducen la diferencia (hacia mono).
 */
class StereoWidener : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float width READ width WRITE setWidth NOTIFY changed)

public:
    explicit StereoWidener(QObject *parent = nullptr) : MasterEffect(parent) {}

    float width() const { return m_width; }
    Q_INVOKABLE void setWidth(float v) {
        m_width = std::clamp(v, 0.0f, 2.0f);
        emit changed();
    }

    void process(float *buffer, int nFrames, int channels, int /*sampleRate*/) override
    {
        if (channels < 2) return; // solo funciona en estéreo

        for (int f = 0; f < nFrames; ++f) {
            float &l = buffer[f * channels + 0];
            float &r = buffer[f * channels + 1];

            // Descomposición Mid/Side
            const float mid  = (l + r) * 0.5f;
            const float side = (l - r) * 0.5f;

            // Recomponer con Side amplificado/reducido
            const float newSide = side * m_width;

            l = mid + newSide;
            r = mid - newSide;
        }
    }

    void reset() override {}

private:
    float m_width = 1.0f;
};
