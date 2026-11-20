#pragma once

#include "MasterEffect.h"
#include <algorithm>

/**
 * EqualizerEffect: ecualizador paramétrico de 3 bandas.
 *
 * - Low shelf: refuerzo/corte de graves (frecuencia ajustable)
 * - Mid peaking: refuerzo/corte de medios (frecuencia + Q ajustables)
 * - High shelf: refuerzo/corte de agudos (frecuencia ajustable)
 * - High Pass Filter (HPF): corte de bajas frecuencias
 *
 * Cada banda usa un filtro biquad. Las 4 se procesan en cascada (serie).
 * Ganancia de cada banda: -12 dB a +12 dB.
 */
class EqualizerEffect : public MasterEffect {
  Q_OBJECT
  // HPF
  Q_PROPERTY(float hpfFreq READ hpfFreq WRITE setHpfFreq NOTIFY changed)
  // Low shelf
  Q_PROPERTY(float lowFreq READ lowFreq WRITE setLowFreq NOTIFY changed)
  Q_PROPERTY(float lowGainDb READ lowGainDb WRITE setLowGainDb NOTIFY changed)
  // Mid peaking
  Q_PROPERTY(float midFreq READ midFreq WRITE setMidFreq NOTIFY changed)
  Q_PROPERTY(float midGainDb READ midGainDb WRITE setMidGainDb NOTIFY changed)
  Q_PROPERTY(float midQ READ midQ WRITE setMidQ NOTIFY changed)
  // High shelf
  Q_PROPERTY(float highFreq READ highFreq WRITE setHighFreq NOTIFY changed)
  Q_PROPERTY(
      float highGainDb READ highGainDb WRITE setHighGainDb NOTIFY changed)

public:
  explicit EqualizerEffect(QObject *parent = nullptr) : MasterEffect(parent) {}

  // HPF
  float hpfFreq() const { return m_hpfFreq; }
  Q_INVOKABLE void setHpfFreq(float v) {
    m_hpfFreq = std::clamp(v, 0.0f, 1000.0f); // 0 = disabled
    m_dirty = true;
    emit changed();
  }

  // Low
  float lowFreq() const { return m_lowFreq; }
  Q_INVOKABLE void setLowFreq(float v) {
    m_lowFreq = std::clamp(v, 20.0f, 1000.0f);
    m_dirty = true;
    emit changed();
  }
  float lowGainDb() const { return m_lowGainDb; }
  Q_INVOKABLE void setLowGainDb(float v) {
    m_lowGainDb = std::clamp(v, -12.0f, 12.0f);
    m_dirty = true;
    emit changed();
  }

  // Mid
  float midFreq() const { return m_midFreq; }
  Q_INVOKABLE void setMidFreq(float v) {
    m_midFreq = std::clamp(v, 100.0f, 10000.0f);
    m_dirty = true;
    emit changed();
  }
  float midGainDb() const { return m_midGainDb; }
  Q_INVOKABLE void setMidGainDb(float v) {
    m_midGainDb = std::clamp(v, -12.0f, 12.0f);
    m_dirty = true;
    emit changed();
  }
  float midQ() const { return m_midQ; }
  Q_INVOKABLE void setMidQ(float v) {
    m_midQ = std::clamp(v, 0.1f, 10.0f);
    m_dirty = true;
    emit changed();
  }

  // High
  float highFreq() const { return m_highFreq; }
  Q_INVOKABLE void setHighFreq(float v) {
    m_highFreq = std::clamp(v, 1000.0f, 20000.0f);
    m_dirty = true;
    emit changed();
  }
  float highGainDb() const { return m_highGainDb; }
  Q_INVOKABLE void setHighGainDb(float v) {
    m_highGainDb = std::clamp(v, -12.0f, 12.0f);
    m_dirty = true;
    emit changed();
  }

  void process(float *buffer, int nFrames, int channels,
               int sampleRate) override {
    if (m_dirty || sampleRate != m_lastSR) {
      recalcAll(sampleRate);
      m_dirty = false;
    }

    for (int f = 0; f < nFrames; ++f) {
      for (int ch = 0; ch < std::min(channels, 2); ++ch) {
        float &s = buffer[f * channels + ch];
        // Cascada: hpf -> low → mid → high
        if (m_hpfFreq >= 20.0f) {
            s = processBiquad(m_hpf, ch, s);
        }
        s = processBiquad(m_low, ch, s);
        s = processBiquad(m_mid, ch, s);
        s = processBiquad(m_high, ch, s);
      }
    }
  }

  void reset() override {
    resetBiquad(m_hpf);
    resetBiquad(m_low);
    resetBiquad(m_mid);
    resetBiquad(m_high);
  }

private:
  struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0;
    float a1 = 0, a2 = 0;
    float z1[2] = {0, 0};
    float z2[2] = {0, 0};
  };

  static float processBiquad(Biquad &bq, int ch, float x) {
    float y = bq.b0 * x + bq.z1[ch];
    bq.z1[ch] = bq.b1 * x - bq.a1 * y + bq.z2[ch];
    bq.z2[ch] = bq.b2 * x - bq.a2 * y;
    return y;
  }

  static void resetBiquad(Biquad &bq) {
    bq.z1[0] = bq.z1[1] = 0;
    bq.z2[0] = bq.z2[1] = 0;
  }

  void recalcAll(int sr) {
    m_lastSR = sr;
    if (m_hpfFreq >= 20.0f) {
        calcHighPass(m_hpf, m_hpfFreq, sr);
    }
    calcLowShelf(m_low, m_lowFreq, m_lowGainDb, sr);
    calcPeaking(m_mid, m_midFreq, m_midGainDb, m_midQ, sr);
    calcHighShelf(m_high, m_highFreq, m_highGainDb, sr);
  }

  static void calcHighPass(Biquad &bq, float freq, int sr) {
    const double w0 = 2.0 * M_PI * freq / sr;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * 0.7071);

    const double a0 = 1.0 + alpha;
    bq.b0 = float((1.0 + cosw0) / 2.0 / a0);
    bq.b1 = float(-(1.0 + cosw0) / a0);
    bq.b2 = float((1.0 + cosw0) / 2.0 / a0);
    bq.a1 = float(-2.0 * cosw0 / a0);
    bq.a2 = float((1.0 - alpha) / a0);
  }

  static void calcLowShelf(Biquad &bq, float freq, float gainDb, int sr) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * freq / sr;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha =
        sinw0 / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / 0.7071 - 1.0) + 2.0);
    const double sqrtA2alpha = 2.0 * std::sqrt(A) * alpha;

    const double a0 = (A + 1.0) + (A - 1.0) * cosw0 + sqrtA2alpha;
    bq.b0 = float(A * ((A + 1.0) - (A - 1.0) * cosw0 + sqrtA2alpha) / a0);
    bq.b1 = float(2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0) / a0);
    bq.b2 = float(A * ((A + 1.0) - (A - 1.0) * cosw0 - sqrtA2alpha) / a0);
    bq.a1 = float(-2.0 * ((A - 1.0) + (A + 1.0) * cosw0) / a0);
    bq.a2 = float(((A + 1.0) + (A - 1.0) * cosw0 - sqrtA2alpha) / a0);
  }

  static void calcPeaking(Biquad &bq, float freq, float gainDb, float Q,
                          int sr) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * freq / sr;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * Q);

    const double a0 = 1.0 + alpha / A;
    bq.b0 = float((1.0 + alpha * A) / a0);
    bq.b1 = float(-2.0 * cosw0 / a0);
    bq.b2 = float((1.0 - alpha * A) / a0);
    bq.a1 = float(-2.0 * cosw0 / a0);
    bq.a2 = float((1.0 - alpha / A) / a0);
  }

  static void calcHighShelf(Biquad &bq, float freq, float gainDb, int sr) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * freq / sr;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha =
        sinw0 / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / 0.7071 - 1.0) + 2.0);
    const double sqrtA2alpha = 2.0 * std::sqrt(A) * alpha;

    const double a0 = (A + 1.0) - (A - 1.0) * cosw0 + sqrtA2alpha;
    bq.b0 = float(A * ((A + 1.0) + (A - 1.0) * cosw0 + sqrtA2alpha) / a0);
    bq.b1 = float(-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0) / a0);
    bq.b2 = float(A * ((A + 1.0) + (A - 1.0) * cosw0 - sqrtA2alpha) / a0);
    bq.a1 = float(2.0 * ((A - 1.0) - (A + 1.0) * cosw0) / a0);
    bq.a2 = float(((A + 1.0) - (A - 1.0) * cosw0 - sqrtA2alpha) / a0);
  }

  float m_hpfFreq = 0.0f; // 0 = OFF
  float m_lowFreq = 200.0f;
  float m_lowGainDb = 0.0f;
  float m_midFreq = 1000.0f;
  float m_midGainDb = 0.0f;
  float m_midQ = 1.0f;
  float m_highFreq = 5000.0f;
  float m_highGainDb = 0.0f;

  bool m_dirty = true;
  int m_lastSR = 0;

  Biquad m_hpf, m_low, m_mid, m_high;
};
