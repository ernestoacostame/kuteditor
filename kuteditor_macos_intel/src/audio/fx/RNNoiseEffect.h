#pragma once

#include "audio/fx/MasterEffect.h"
#include <QVariantList>

class RNNoiseEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float reduction READ reduction WRITE setReduction NOTIFY changed)
    Q_PROPERTY(bool isProcessing READ isProcessing WRITE setIsProcessing NOTIFY processingChanged)
    Q_PROPERTY(QVariantList noiseMagnitudes READ noiseMagnitudes NOTIFY spectralDataChanged)
    Q_PROPERTY(QVariantList inputMagnitudes READ inputMagnitudes NOTIFY spectralDataChanged)
    Q_PROPERTY(QVariantList outputMagnitudes READ outputMagnitudes NOTIFY spectralDataChanged)

public:
    explicit RNNoiseEffect(QObject *parent = nullptr);
    ~RNNoiseEffect() override;

    float reduction() const { return m_reduction; }
    Q_INVOKABLE void setReduction(float v) { 
        m_reduction = std::clamp(v, 0.0f, 1.0f); 
        emit changed(); 
    }

    bool isProcessing() const { return m_isProcessing; }
    void setIsProcessing(bool v) {
        if (m_isProcessing != v) {
            m_isProcessing = v;
            emit processingChanged();
        }
    }

    QVariantList noiseMagnitudes() const { return m_noiseMagnitudes; }
    QVariantList inputMagnitudes() const { return m_inputMagnitudes; }
    QVariantList outputMagnitudes() const { return m_outputMagnitudes; }

    void process(float *buffer, int nFrames, int channels, int sampleRate) override;
    void reset() override;

signals:
    void processingChanged();
    void spectralDataChanged();

private:
    void initStates(int channels);
    void cleanupStates();

    float m_reduction = 1.0f; // 0.0 = Dry, 1.0 = Wet
    bool m_isProcessing = false;
    
    QVariantList m_inputMagnitudes;
    QVariantList m_outputMagnitudes;
    QVariantList m_noiseMagnitudes;

    struct DenoiseState *m_states[2];
    int m_lastChannels = 0;
    int m_lastSR = 0;

    // Buffers circulares (FIFO) para cada canal
    std::vector<float> m_fifoIn[2];
    std::vector<float> m_fifoOut[2];
};
