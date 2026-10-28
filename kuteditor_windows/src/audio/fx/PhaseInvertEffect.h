#pragma once

#include "MasterEffect.h"

/**
 * PhaseInvertEffect: invierte la polaridad de uno o ambos canales.
 *
 * Útil para corregir problemas de fase en grabaciones multi-micro,
 * o para comprobar compatibilidad de fase. Literalmente multiplica
 * por -1 los canales seleccionados.
 */
class PhaseInvertEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(bool invertLeft READ invertLeft WRITE setInvertLeft NOTIFY changed)
    Q_PROPERTY(bool invertRight READ invertRight WRITE setInvertRight NOTIFY changed)

public:
    explicit PhaseInvertEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

    bool invertLeft() const { return m_invertL; }
    Q_INVOKABLE void setInvertLeft(bool v) { m_invertL = v; emit changed(); }

    bool invertRight() const { return m_invertR; }
    Q_INVOKABLE void setInvertRight(bool v) { m_invertR = v; emit changed(); }

    void process(float *buffer, int nFrames, int channels, int /*sampleRate*/) override
    {
        const float mulL = m_invertL ? -1.0f : 1.0f;
        const float mulR = m_invertR ? -1.0f : 1.0f;

        for (int f = 0; f < nFrames; ++f) {
            buffer[f * channels + 0] *= mulL;
            if (channels > 1)
                buffer[f * channels + 1] *= mulR;
        }
    }

    void reset() override {}

private:
    bool m_invertL = true;
    bool m_invertR = true;
};
