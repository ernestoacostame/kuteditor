#pragma once

#include <QObject>
#include <cmath>

/**
 * MasterEffect: clase base abstracta para efectos de la cadena master.
 *
 * Cada efecto implementa process() que recibe un buffer interleaved estéreo
 * y lo modifica in-place. El AudioEngine itera la cadena en orden llamando
 * process() solo si enabled() es true.
 *
 * gainReduction() devuelve el último valor de GR medido (en dB, negativo)
 * para efectos de dinámica. Efectos sin GR devuelven 0.
 */
class MasterEffect : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(float gainReduction READ gainReduction NOTIFY grChanged)

public:
    explicit MasterEffect(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~MasterEffect() override = default;

    bool enabled() const { return m_enabled; }
    Q_INVOKABLE virtual void setEnabled(bool v) {
        if (v != m_enabled) { m_enabled = v; emit changed(); }
    }

    /// Procesar buffer interleaved estéreo in-place.
    virtual void process(float *buffer, int nFrames, int channels, int sampleRate) = 0;

    /// Resetear estado interno (envelopes, filtros, etc).
    virtual void reset() = 0;

    /// GR en dB (negativo). 0 = sin reducción.
    virtual float gainReduction() const { return 0.0f; }

signals:
    void changed();
    void grChanged();

protected:
    bool m_enabled = false;

    // Helpers DSP comunes
    static inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
    static inline float linToDb(float lin) { return 20.0f * std::log10(std::max(lin, 1e-8f)); }
};
