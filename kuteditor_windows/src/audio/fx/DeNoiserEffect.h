#pragma once

#include "MasterEffect.h"
#include <vector>
#include <cstring>
#include <QVariant>
#include <QList>

/**
 * DeNoiserEffect: reducción de ruido por spectral gating.
 *
 * Usa FFT (implementación inline radix-2 DIT para no depender de FFTW)
 * con ventana de análisis de 2048 muestras y overlap-add 50%.
 *
 * Parámetros:
 * - reduction: cantidad de reducción de ruido en dB (0-40 dB).
 *   Bins cuya magnitud esté por debajo de este umbral se atenúan.
 * - smoothing: suavizado temporal del espectro de ruido (0-1).
 *   Valores altos producen reducción más suave pero con más latencia.
 */
class DeNoiserEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float reductionDb READ reductionDb WRITE setReductionDb NOTIFY changed)
    Q_PROPERTY(float smoothing READ smoothing WRITE setSmoothing NOTIFY changed)
    Q_PROPERTY(QVariantList noiseMagnitudes READ noiseMagnitudes NOTIFY noiseMagnitudesChanged)
    Q_PROPERTY(QVariantList inputMagnitudes READ inputMagnitudes NOTIFY spectralDataChanged)
    Q_PROPERTY(QVariantList outputMagnitudes READ outputMagnitudes NOTIFY spectralDataChanged)

public:
    explicit DeNoiserEffect(QObject *parent = nullptr) : MasterEffect(parent) {
        initBuffers();
    }

    float reductionDb() const { return m_reductionDb; }
    Q_INVOKABLE void setReductionDb(float v) { m_reductionDb = std::clamp(v, 0.0f, 40.0f); emit changed(); }

    float smoothing() const { return m_smoothing; }
    Q_INVOKABLE void setSmoothing(float v) { m_smoothing = std::clamp(v, 0.0f, 1.0f); emit changed(); }

    QVariantList noiseMagnitudes() const { return m_lastNoiseSnaps; }
    QVariantList inputMagnitudes() const { return m_lastInputSnaps; }
    QVariantList outputMagnitudes() const { return m_lastOutputSnaps; }

    Q_INVOKABLE void resetNoiseProfile() {
        for (int ch = 0; ch < 2; ++ch) {
            m_noiseEstimated[ch] = false;
        }
        emit changed();
    }

    void process(float *buffer, int nFrames, int channels, int /*sampleRate*/) override
    {
        // Procesar cada canal por separado
        for (int ch = 0; ch < std::min(channels, 2); ++ch) {
            for (int f = 0; f < nFrames; ++f) {
                float sample = buffer[f * channels + ch];

                // Alimentar el buffer de entrada
                m_inBuf[ch][m_writePos[ch]] = sample;
                // Leer del buffer de salida (overlap-add result)
                float out = m_outBuf[ch][m_writePos[ch]];
                m_outBuf[ch][m_writePos[ch]] = 0.0f; // limpiar para próximo overlap
                buffer[f * channels + ch] = out;

                m_writePos[ch]++;
                if (m_writePos[ch] >= FFT_SIZE) m_writePos[ch] = 0;

                m_samplesUntilFFT[ch]--;
                if (m_samplesUntilFFT[ch] <= 0) {
                    processFFTBlock(ch);
                    m_samplesUntilFFT[ch] = HOP_SIZE;
                }
            }
        }
    }

    void reset() override {
        initBuffers();
    }

signals:
    void noiseMagnitudesChanged();
    void spectralDataChanged();

private:
    static constexpr int FFT_SIZE = 2048;
    static constexpr int HOP_SIZE = FFT_SIZE / 2; // 50% overlap

    void initBuffers() {
        for (int ch = 0; ch < 2; ++ch) {
            m_inBuf[ch].assign(FFT_SIZE, 0.0f);
            m_outBuf[ch].assign(FFT_SIZE, 0.0f);
            m_writePos[ch] = 0;
            m_samplesUntilFFT[ch] = HOP_SIZE;
            m_noiseMag[ch].assign(FFT_SIZE / 2 + 1, 0.0f);
            m_noiseEstimated[ch] = false;
        }
        m_lastInputBins.assign(FFT_SIZE / 2 + 1, 0.0f);
        m_lastOutputBins.assign(FFT_SIZE / 2 + 1, 0.0f);
        // Precalcular ventana Hann
        m_window.resize(FFT_SIZE);
        for (int i = 0; i < FFT_SIZE; ++i)
            m_window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / FFT_SIZE));
    }

    void processFFTBlock(int ch) {
        // Copiar bloque con ventana
        std::vector<float> re(FFT_SIZE), im(FFT_SIZE);
        for (int i = 0; i < FFT_SIZE; ++i) {
            int idx = (m_writePos[ch] + i) % FFT_SIZE;
            re[i] = m_inBuf[ch][idx] * m_window[i];
            im[i] = 0.0f;
        }

        // FFT
        fft(re.data(), im.data(), FFT_SIZE, false);

        // Spectral gating
        const float threshLin = dbToLin(-m_reductionDb);
        const int bins = FFT_SIZE / 2 + 1;

        for (int b = 0; b < bins; ++b) {
            float mag = std::sqrt(re[b] * re[b] + im[b] * im[b]);
            if (ch == 0) m_lastInputBins[b] = mag;

            // Actualizar estimación de ruido (media móvil exponencial)
            if (!m_noiseEstimated[ch]) {
                m_noiseMag[ch][b] = mag;
            } else {
                m_noiseMag[ch][b] = m_smoothing * m_noiseMag[ch][b] +
                                    (1.0f - m_smoothing) * std::min(mag, m_noiseMag[ch][b] * 1.5f);
            }

            // Soft spectral gate: atenuar bins por debajo del umbral
            float noiseFloor = m_noiseMag[ch][b] / std::max(threshLin, 1e-8f);
            float gain = 1.0f;
            if (mag < noiseFloor && mag > 1e-10f) {
                gain = mag / noiseFloor;
                gain = gain * gain; // cuadrático para suavidad
            }

            re[b] *= gain;
            im[b] *= gain;
            if (ch == 0) m_lastOutputBins[b] = std::sqrt(re[b] * re[b] + im[b] * im[b]);

            // Espejo para la mitad negativa
            if (b > 0 && b < FFT_SIZE / 2) {
                re[FFT_SIZE - b] *= gain;
                im[FFT_SIZE - b] *= gain;
            }
        }
        m_noiseEstimated[ch] = true;

        // Guardar para visualización (solo canal 0 para simplificar el display)
        if (ch == 0) {
            QVariantList inMags, outMags, noiseMags;
            for (int b = 0; b < bins; b += 4) { // Subsampling para rendimiento en UI
                inMags.append(QVariant(m_lastInputBins[b]));
                outMags.append(QVariant(m_lastOutputBins[b]));
                noiseMags.append(QVariant(m_noiseMag[0][b]));
            }
            m_lastInputSnaps = inMags;
            m_lastOutputSnaps = outMags;
            m_lastNoiseSnaps = noiseMags;
            emit spectralDataChanged();
        }

        // IFFT
        fft(re.data(), im.data(), FFT_SIZE, true);

        // Overlap-add con ventana
        for (int i = 0; i < FFT_SIZE; ++i) {
            int outIdx = (m_writePos[ch] + i) % FFT_SIZE;
            m_outBuf[ch][outIdx] += re[i] * m_window[i] * (2.0f / FFT_SIZE);
        }
    }

    // Radix-2 DIT FFT in-place. inverse=true para IFFT.
    static void fft(float *re, float *im, int n, bool inverse) {
        // Bit reversal
        int j = 0;
        for (int i = 1; i < n - 1; ++i) {
            int bit = n >> 1;
            while (j & bit) { j ^= bit; bit >>= 1; }
            j ^= bit;
            if (i < j) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }

        // Butterfly
        const float sign = inverse ? 1.0f : -1.0f;
        for (int len = 2; len <= n; len <<= 1) {
            const float ang = sign * 2.0f * M_PI / len;
            const float wRe = std::cos(ang);
            const float wIm = std::sin(ang);
            for (int i = 0; i < n; i += len) {
                float curRe = 1.0f, curIm = 0.0f;
                for (int k = 0; k < len / 2; ++k) {
                    int u = i + k;
                    int v = i + k + len / 2;
                    float tRe = curRe * re[v] - curIm * im[v];
                    float tIm = curRe * im[v] + curIm * re[v];
                    re[v] = re[u] - tRe;
                    im[v] = im[u] - tIm;
                    re[u] += tRe;
                    im[u] += tIm;
                    float newCurRe = curRe * wRe - curIm * wIm;
                    curIm = curRe * wIm + curIm * wRe;
                    curRe = newCurRe;
                }
            }
        }

        // Normalizar IFFT
        if (inverse) {
            for (int i = 0; i < n; ++i) {
                re[i] /= n;
                im[i] /= n;
            }
        }
    }

    float m_reductionDb = 12.0f;
    float m_smoothing = 0.9f;

    std::vector<float> m_window;
    std::vector<float> m_inBuf[2];
    std::vector<float> m_outBuf[2];
    int m_writePos[2] = {0, 0};
    int m_samplesUntilFFT[2] = {HOP_SIZE, HOP_SIZE};
    std::vector<float> m_noiseMag[2];
    bool m_noiseEstimated[2] = {false, false};

    std::vector<float> m_lastInputBins;
    std::vector<float> m_lastOutputBins;
    QVariantList m_lastInputSnaps;
    QVariantList m_lastOutputSnaps;
    QVariantList m_lastNoiseSnaps;
};
