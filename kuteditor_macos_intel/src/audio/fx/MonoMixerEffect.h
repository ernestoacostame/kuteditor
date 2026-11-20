#pragma once

#include "MasterEffect.h"

/**
 * MonoMixerEffect: convierte la señal estéreo a mono.
 *
 * Suma L y R, divide entre 2 para mantener el nivel, y envía
 * el resultado a ambos canales. Útil para verificar compatibilidad
 * mono o para emisión en mono.
 */
class MonoMixerEffect : public MasterEffect
{
    Q_OBJECT

public:
    explicit MonoMixerEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

    void process(float *buffer, int nFrames, int channels, int /*sampleRate*/) override
    {
        if (channels < 2) return;

        for (int f = 0; f < nFrames; ++f) {
            const float mono = (buffer[f * channels + 0] + buffer[f * channels + 1]) * 0.5f;
            buffer[f * channels + 0] = mono;
            buffer[f * channels + 1] = mono;
        }
    }

    void reset() override {}
};
