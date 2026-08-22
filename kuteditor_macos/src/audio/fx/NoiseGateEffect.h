#pragma once

#include "MasterEffect.h"

/**
 * NoiseGateEffect: puerta de ruido master.
 *
 * Silencia la señal cuando el nivel cae por debajo del threshold.
 * Usa attack/release/hold para transiciones suaves.
 * - Threshold: nivel en dB por debajo del cual se cierra la puerta.
 * - Attack: tiempo en ms para abrir la puerta.
 * - Hold: tiempo en ms que la puerta permanece abierta tras caer bajo el threshold.
 * - Release: tiempo en ms para cerrar la puerta.
 * - Range: atenuación máxima en dB cuando la puerta está cerrada (ej: -80 dB).
 */
class NoiseGateEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float thresholdDb READ thresholdDb WRITE setThresholdDb NOTIFY changed)
    Q_PROPERTY(float attackMs READ attackMs WRITE setAttackMs NOTIFY changed)
    Q_PROPERTY(float holdMs READ holdMs WRITE setHoldMs NOTIFY changed)
    Q_PROPERTY(float releaseMs READ releaseMs WRITE setReleaseMs NOTIFY changed)
    Q_PROPERTY(float rangeDb READ rangeDb WRITE setRangeDb NOTIFY changed)

public:
    explicit NoiseGateEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

    float thresholdDb() const { return m_thresholdDb; }
    Q_INVOKABLE void setThresholdDb(float v) { m_thresholdDb = v; emit changed(); }

    float attackMs() const { return m_attackMs; }
    Q_INVOKABLE void setAttackMs(float v) { m_attackMs = std::max(0.1f, v); emit changed(); }

    float holdMs() const { return m_holdMs; }
    Q_INVOKABLE void setHoldMs(float v) { m_holdMs = std::max(0.0f, v); emit changed(); }

    float releaseMs() const { return m_releaseMs; }
    Q_INVOKABLE void setReleaseMs(float v) { m_releaseMs = std::max(1.0f, v); emit changed(); }

    float rangeDb() const { return m_rangeDb; }
    Q_INVOKABLE void setRangeDb(float v) { m_rangeDb = std::clamp(v, -80.0f, 0.0f); emit changed(); }

    float gainReduction() const override { return m_gr; }

    void process(float *buffer, int nFrames, int channels, int sampleRate) override
    {
        const float threshLin = dbToLin(m_thresholdDb);
        const float rangeLin = dbToLin(m_rangeDb);
        const float attackCoeff  = std::exp(-1.0f / (m_attackMs  * 0.001f * sampleRate));
        const float releaseCoeff = std::exp(-1.0f / (m_releaseMs * 0.001f * sampleRate));
        const int holdSamples = int(m_holdMs * 0.001f * sampleRate);
        float maxGR = 0.0f;

        for (int f = 0; f < nFrames; ++f) {
            float &l = buffer[f * channels + 0];
            float &r = buffer[f * channels + (channels > 1 ? 1 : 0)];
            const float peak = std::max(std::abs(l), std::abs(r));

            // Determinar target: puerta abierta (1.0) o cerrada (rangeLin)
            float target;
            if (peak >= threshLin) {
                target = 1.0f;
                m_holdCounter = holdSamples;
            } else if (m_holdCounter > 0) {
                target = 1.0f;
                m_holdCounter--;
            } else {
                target = rangeLin;
            }

            // Suavizar la ganancia con attack/release
            if (target > m_gateGain) {
                // Abriendo (attack)
                m_gateGain = attackCoeff * m_gateGain + (1.0f - attackCoeff) * target;
            } else {
                // Cerrando (release)
                m_gateGain = releaseCoeff * m_gateGain + (1.0f - releaseCoeff) * target;
            }

            l *= m_gateGain;
            if (channels > 1) r *= m_gateGain;

            if (m_gateGain < 0.99f) {
                const float grDb = linToDb(m_gateGain);
                if (grDb < maxGR) maxGR = grDb;
            }
        }

        m_gr = 0.7f * m_gr + 0.3f * maxGR;
    }

    void reset() override {
        m_gateGain = 1.0f;
        m_holdCounter = 0;
        m_gr = 0.0f;
    }

private:
    float m_thresholdDb = -40.0f;
    float m_attackMs = 1.0f;
    float m_holdMs = 50.0f;
    float m_releaseMs = 50.0f;
    float m_rangeDb = -80.0f;

    float m_gateGain = 1.0f;
    int m_holdCounter = 0;
    float m_gr = 0.0f;
};
