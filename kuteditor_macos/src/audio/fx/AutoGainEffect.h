#pragma once

#include "MasterEffect.h"

/**
 * AutoGainEffect: normalizador de nivel en tiempo real.
 *
 * Mide el RMS de la señal en una ventana temporal y ajusta la ganancia
 * para mantener un nivel objetivo constante. Útil para nivelar entre
 * pistas o mantener un volumen consistente en podcast.
 *
 * - targetDb: nivel RMS objetivo (-30 a 0 dB).
 * - responseMs: velocidad de ajuste (más lento = menos "pumping").
 * - maxGainDb: límite máximo de amplificación (para no amplificar silencio).
 */
class AutoGainEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float targetDb READ targetDb WRITE setTargetDb NOTIFY changed)
    Q_PROPERTY(float responseMs READ responseMs WRITE setResponseMs NOTIFY changed)
    Q_PROPERTY(float maxGainDb READ maxGainDb WRITE setMaxGainDb NOTIFY changed)
    Q_PROPERTY(float currentGainDb READ currentGainDb NOTIFY grChanged)

public:
    explicit AutoGainEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

    float targetDb() const { return m_targetDb; }
    Q_INVOKABLE void setTargetDb(float v) { m_targetDb = std::clamp(v, -30.0f, 0.0f); emit changed(); }

    float responseMs() const { return m_responseMs; }
    Q_INVOKABLE void setResponseMs(float v) { m_responseMs = std::max(50.0f, v); emit changed(); }

    float maxGainDb() const { return m_maxGainDb; }
    Q_INVOKABLE void setMaxGainDb(float v) { m_maxGainDb = std::clamp(v, 0.0f, 24.0f); emit changed(); }

    float currentGainDb() const { return m_currentGainDb; }

    // Reusamos gainReduction para mostrar cuánto se está ajustando
    float gainReduction() const override { return m_currentGainDb; }

    void process(float *buffer, int nFrames, int channels, int sampleRate) override
    {
        const float targetLin = dbToLin(m_targetDb);
        const float maxGainLin = dbToLin(m_maxGainDb);
        const float smoothCoeff = std::exp(-1.0f / (m_responseMs * 0.001f * sampleRate));

        for (int f = 0; f < nFrames; ++f) {
            float &l = buffer[f * channels + 0];
            float &r = buffer[f * channels + (channels > 1 ? 1 : 0)];

            // RMS instantáneo
            const float rms = std::sqrt((l * l + r * r) * 0.5f);

            // Suavizar RMS medido
            m_rmsEnvelope = smoothCoeff * m_rmsEnvelope + (1.0f - smoothCoeff) * rms;

            // Calcular ganancia necesaria
            float desiredGain = 1.0f;
            if (m_rmsEnvelope > 1e-6f) {
                desiredGain = targetLin / m_rmsEnvelope;
            }

            // Limitar ganancia máxima (no amplificar silencios al infinito)
            desiredGain = std::min(desiredGain, maxGainLin);
            // No atenuar más de -20 dB
            desiredGain = std::max(desiredGain, 0.1f);

            // Suavizar cambio de ganancia
            m_currentGain = smoothCoeff * m_currentGain + (1.0f - smoothCoeff) * desiredGain;

            l *= m_currentGain;
            if (channels > 1) r *= m_currentGain;
        }

        m_currentGainDb = linToDb(m_currentGain);
    }

    void reset() override {
        m_rmsEnvelope = 0.0f;
        m_currentGain = 1.0f;
        m_currentGainDb = 0.0f;
    }

private:
    float m_targetDb = -18.0f;
    float m_responseMs = 500.0f;
    float m_maxGainDb = 12.0f;

    float m_rmsEnvelope = 0.0f;
    float m_currentGain = 1.0f;
    float m_currentGainDb = 0.0f;
};
