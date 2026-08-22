#pragma once

#include "MasterEffect.h"

/**
 * ExpanderEffect: expander de dinámica.
 *
 * Lo inverso del compresor: por debajo del threshold, atenúa la señal
 * según el ratio, aumentando el rango dinámico. Más suave que una puerta
 * de ruido: en vez de cortar abruptamente, reduce progresivamente.
 *
 * Útil para limpiar silencios con ruido de fondo sin los cortes bruscos
 * de un noise gate.
 */
class ExpanderEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float thresholdDb READ thresholdDb WRITE setThresholdDb NOTIFY changed)
    Q_PROPERTY(float ratio READ ratio WRITE setRatio NOTIFY changed)
    Q_PROPERTY(float attackMs READ attackMs WRITE setAttackMs NOTIFY changed)
    Q_PROPERTY(float releaseMs READ releaseMs WRITE setReleaseMs NOTIFY changed)

public:
    explicit ExpanderEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

    float thresholdDb() const { return m_thresholdDb; }
    Q_INVOKABLE void setThresholdDb(float v) { m_thresholdDb = v; emit changed(); }

    float ratio() const { return m_ratio; }
    Q_INVOKABLE void setRatio(float v) { m_ratio = std::max(1.0f, v); emit changed(); }

    float attackMs() const { return m_attackMs; }
    Q_INVOKABLE void setAttackMs(float v) { m_attackMs = std::max(0.1f, v); emit changed(); }

    float releaseMs() const { return m_releaseMs; }
    Q_INVOKABLE void setReleaseMs(float v) { m_releaseMs = std::max(1.0f, v); emit changed(); }

    float gainReduction() const override { return m_gr; }

    void process(float *buffer, int nFrames, int channels, int sampleRate) override
    {
        const float threshLin = dbToLin(m_thresholdDb);
        const float attackCoeff  = std::exp(-1.0f / (m_attackMs  * 0.001f * sampleRate));
        const float releaseCoeff = std::exp(-1.0f / (m_releaseMs * 0.001f * sampleRate));
        float maxGR = 0.0f;

        for (int f = 0; f < nFrames; ++f) {
            float &l = buffer[f * channels + 0];
            float &r = buffer[f * channels + (channels > 1 ? 1 : 0)];
            const float peak = std::max(std::abs(l), std::abs(r));

            // Envelope follower
            if (peak > m_envelope)
                m_envelope = attackCoeff * m_envelope + (1.0f - attackCoeff) * peak;
            else
                m_envelope = releaseCoeff * m_envelope + (1.0f - releaseCoeff) * peak;

            // Expansion: actúa por DEBAJO del threshold (inverso del compresor)
            float gr = 1.0f;
            if (m_envelope < threshLin && m_envelope > 1e-8f) {
                // Cuántos dB por debajo del threshold
                const float underDb = 20.0f * std::log10(threshLin / m_envelope);
                // Expandir: amplificar la diferencia por (ratio-1)
                const float extraReductionDb = underDb * (m_ratio - 1.0f) / m_ratio;
                gr = std::pow(10.0f, -extraReductionDb / 20.0f);
            }

            l *= gr;
            if (channels > 1) r *= gr;

            if (gr < 1.0f) {
                const float grDb = 20.0f * std::log10(gr);
                if (grDb < maxGR) maxGR = grDb;
            }
        }

        m_gr = 0.7f * m_gr + 0.3f * maxGR;
    }

    void reset() override { m_envelope = 0.0f; m_gr = 0.0f; }

private:
    float m_thresholdDb = -40.0f;
    float m_ratio = 2.0f;
    float m_attackMs = 5.0f;
    float m_releaseMs = 50.0f;
    float m_envelope = 0.0f;
    float m_gr = 0.0f;
};
