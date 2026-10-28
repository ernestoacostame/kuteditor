#pragma once

#include "MasterEffect.h"
#include <algorithm>

/**
 * AutoDuckEffect: atenuación automática con sidechain entre pistas.
 *
 * Estilo Audacity: una pista de control (voz) controla la atenuación
 * de una pista objetivo (música). Cuando hay señal en la pista de
 * control por encima del threshold, la pista objetivo se atenúa.
 *
 * A diferencia del modo inline, este efecto NO procesa su propio buffer.
 * En su lugar, AudioEngine le pasa el sidechain (pista de control) y
 * aplica la ganancia resultante a la pista objetivo.
 *
 * Parámetros (estilo Audacity):
 * - duckAmountDb:   atenuación máxima en dB (ej: -12 dB)
 * - innerFadeDownMs: tiempo de fade hacia abajo al detectar voz
 * - innerFadeUpMs:   tiempo de fade hacia arriba al dejar de detectar
 * - outerFadeDownMs: tiempo de pre-fade antes de que empiece la voz
 * - outerFadeUpMs:   tiempo de post-fade después de que termina la voz
 * - thresholdDb:     nivel en la pista de control para activar el duck
 * - controlTrack:    índice de la pista de control (-1 = desactivado)
 * - targetTrack:     índice de la pista objetivo (-1 = desactivado)
 */
class AutoDuckEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float duckAmountDb READ duckAmountDb WRITE setDuckAmountDb NOTIFY changed)
    Q_PROPERTY(float thresholdDb READ thresholdDb WRITE setThresholdDb NOTIFY changed)
    Q_PROPERTY(float innerFadeDownMs READ innerFadeDownMs WRITE setInnerFadeDownMs NOTIFY changed)
    Q_PROPERTY(float innerFadeUpMs READ innerFadeUpMs WRITE setInnerFadeUpMs NOTIFY changed)
    Q_PROPERTY(float outerFadeDownMs READ outerFadeDownMs WRITE setOuterFadeDownMs NOTIFY changed)
    Q_PROPERTY(float outerFadeUpMs READ outerFadeUpMs WRITE setOuterFadeUpMs NOTIFY changed)
    Q_PROPERTY(int controlTrack READ controlTrack WRITE setControlTrack NOTIFY changed)
    Q_PROPERTY(int targetTrack READ targetTrack WRITE setTargetTrack NOTIFY changed)

public:
    explicit AutoDuckEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

    float duckAmountDb() const { return m_duckAmountDb; }
    Q_INVOKABLE void setDuckAmountDb(float v) { m_duckAmountDb = std::clamp(v, -40.0f, 0.0f); emit changed(); }

    float thresholdDb() const { return m_thresholdDb; }
    Q_INVOKABLE void setThresholdDb(float v) { m_thresholdDb = v; emit changed(); }

    float innerFadeDownMs() const { return m_innerFadeDownMs; }
    Q_INVOKABLE void setInnerFadeDownMs(float v) { m_innerFadeDownMs = std::max(1.0f, v); emit changed(); }

    float innerFadeUpMs() const { return m_innerFadeUpMs; }
    Q_INVOKABLE void setInnerFadeUpMs(float v) { m_innerFadeUpMs = std::max(1.0f, v); emit changed(); }

    float outerFadeDownMs() const { return m_outerFadeDownMs; }
    Q_INVOKABLE void setOuterFadeDownMs(float v) { m_outerFadeDownMs = std::max(0.0f, v); emit changed(); }

    float outerFadeUpMs() const { return m_outerFadeUpMs; }
    Q_INVOKABLE void setOuterFadeUpMs(float v) { m_outerFadeUpMs = std::max(0.0f, v); emit changed(); }

    int controlTrack() const { return m_controlTrack; }
    Q_INVOKABLE void setControlTrack(int v) { m_controlTrack = v; emit changed(); }

    int targetTrack() const { return m_targetTrack; }
    Q_INVOKABLE void setTargetTrack(int v) { m_targetTrack = v; emit changed(); }

    float gainReduction() const override { return m_gr; }

    /**
     * Alimentar con muestras del sidechain (pista de control).
     * Debe llamarse ANTES de processDuck().
     */
    void feedSidechain(const float *scBuffer, int nFrames, int channels, int sampleRate)
    {
        const float threshLin = dbToLin(m_thresholdDb);
        const float innerDownCoeff = std::exp(-1.0f / (m_innerFadeDownMs * 0.001f * sampleRate));
        const float innerUpCoeff   = std::exp(-1.0f / (m_innerFadeUpMs   * 0.001f * sampleRate));

        for (int f = 0; f < nFrames; ++f) {
            float peak = std::abs(scBuffer[f * channels]);
            if (channels > 1) peak = std::max(peak, std::abs(scBuffer[f * channels + 1]));

            // Envelope follower del sidechain
            if (peak > m_scEnvelope)
                m_scEnvelope = innerDownCoeff * m_scEnvelope + (1.0f - innerDownCoeff) * peak;
            else
                m_scEnvelope = innerUpCoeff * m_scEnvelope + (1.0f - innerUpCoeff) * peak;

            // ¿Hay señal de control?
            m_scActive = (m_scEnvelope > threshLin);
        }
    }

    /**
     * Aplicar duck a la pista objetivo.
     * Debe llamarse DESPUÉS de feedSidechain().
     */
    void processDuck(float *targetBuffer, int nFrames, int channels, int sampleRate)
    {
        const float duckLin = dbToLin(m_duckAmountDb);
        const float attackCoeff  = std::exp(-1.0f / (m_innerFadeDownMs * 0.001f * sampleRate));
        const float releaseCoeff = std::exp(-1.0f / (m_innerFadeUpMs   * 0.001f * sampleRate));
        float maxGR = 0.0f;

        for (int f = 0; f < nFrames; ++f) {
            float targetGain = m_scActive ? duckLin : 1.0f;

            // Suavizar la ganancia
            if (targetGain < m_duckGain) {
                m_duckGain = attackCoeff * m_duckGain + (1.0f - attackCoeff) * targetGain;
            } else {
                m_duckGain = releaseCoeff * m_duckGain + (1.0f - releaseCoeff) * targetGain;
            }

            for (int ch = 0; ch < channels; ++ch)
                targetBuffer[f * channels + ch] *= m_duckGain;

            if (m_duckGain < 0.99f) {
                const float grDb = linToDb(m_duckGain);
                if (grDb < maxGR) maxGR = grDb;
            }
        }

        m_gr = 0.7f * m_gr + 0.3f * maxGR;
    }

    /// process() es un no-op para AutoDuck — usa feedSidechain + processDuck
    void process(float * /*buffer*/, int /*nFrames*/, int /*channels*/, int /*sampleRate*/) override {}

    void reset() override {
        m_scEnvelope = 0.0f;
        m_scActive = false;
        m_duckGain = 1.0f;
        m_gr = 0.0f;
    }

private:
    float m_duckAmountDb = -12.0f;
    float m_thresholdDb = -30.0f;
    float m_innerFadeDownMs = 100.0f;
    float m_innerFadeUpMs = 500.0f;
    float m_outerFadeDownMs = 50.0f;
    float m_outerFadeUpMs = 200.0f;
    int m_controlTrack = -1;  // -1 = no configurado
    int m_targetTrack = -1;

    float m_scEnvelope = 0.0f;
    bool m_scActive = false;
    float m_duckGain = 1.0f;
    float m_gr = 0.0f;
};
