#include "RNNoiseEffect.h"
#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <rnnoise.h>
}

RNNoiseEffect::RNNoiseEffect(QObject *parent)
    : MasterEffect(parent)
{
    m_states[0] = nullptr;
    m_states[1] = nullptr;
}

RNNoiseEffect::~RNNoiseEffect()
{
    cleanupStates();
}

void RNNoiseEffect::initStates(int channels)
{
    cleanupStates();
    m_lastChannels = std::clamp(channels, 1, 2);
    for (int i = 0; i < m_lastChannels; ++i) {
        m_states[i] = rnnoise_create(nullptr);
        m_fifoIn[i].clear();
        m_fifoOut[i].clear();
    }
}

void RNNoiseEffect::cleanupStates()
{
    for (int i = 0; i < 2; ++i) {
        if (m_states[i]) {
            rnnoise_destroy(m_states[i]);
            m_states[i] = nullptr;
        }
        m_fifoIn[i].clear();
        m_fifoOut[i].clear();
    }
    m_lastChannels = 0;
}

void RNNoiseEffect::reset()
{
    for (int i = 0; i < m_lastChannels; ++i) {
        if (m_states[i]) {
            // Re-create state to clear internal RNN memory
            rnnoise_destroy(m_states[i]);
            m_states[i] = rnnoise_create(nullptr);
        }
        m_fifoIn[i].clear();
        m_fifoOut[i].clear();
    }
}

void RNNoiseEffect::process(float *buffer, int nFrames, int channels, int sampleRate)
{
    if (channels <= 0 || nFrames <= 0 || sampleRate <= 0) {
        return;
    }

    // Initialize or re-create states if channels change
    int useChannels = std::min(channels, 2);
    if (!m_states[0] || m_lastChannels != useChannels || m_lastSR != sampleRate) {
        initStates(useChannels);
        m_lastSR = sampleRate;
    }

    const int FRAME_SIZE = 480; // 10ms at 48kHz
    double ratio = 48000.0 / sampleRate;

    // Process each channel independently
    for (int ch = 0; ch < useChannels; ++ch) {
        // 1. Resample incoming block to 48000 Hz if needed, and push to input FIFO
        if (sampleRate == 48000) {
            for (int i = 0; i < nFrames; ++i) {
                m_fifoIn[ch].push_back(buffer[i * channels + ch]);
            }
        } else {
            int targetFrames = std::round(nFrames * ratio);
            for (int i = 0; i < targetFrames; ++i) {
                double srcPos = i / ratio;
                int idx1 = std::floor(srcPos);
                int idx2 = std::min(idx1 + 1, nFrames - 1);
                float frac = srcPos - idx1;
                float s1 = buffer[idx1 * channels + ch];
                float s2 = buffer[idx2 * channels + ch];
                m_fifoIn[ch].push_back(s1 * (1.0f - frac) + s2 * frac);
            }
        }

        // 2. Process available 480-sample blocks in the input FIFO
        while (m_fifoIn[ch].size() >= (size_t)FRAME_SIZE) {
            float inFrame[FRAME_SIZE];
            float outFrame[FRAME_SIZE];

            // Scale to RNNoise range (short scaled to float)
            for (int i = 0; i < FRAME_SIZE; ++i) {
                inFrame[i] = m_fifoIn[ch][i] * 32768.0f;
            }
            m_fifoIn[ch].erase(m_fifoIn[ch].begin(), m_fifoIn[ch].begin() + FRAME_SIZE);

            rnnoise_process_frame(m_states[ch], outFrame, inFrame);

            // Scale back and push to output FIFO
            for (int i = 0; i < FRAME_SIZE; ++i) {
                m_fifoOut[ch].push_back(outFrame[i] / 32768.0f);
            }
        }
    }

    // 3. Retrieve and write output from output FIFO
    int requiredSrcFrames = (sampleRate == 48000) ? nFrames : std::round(nFrames * ratio);

    // If we don't have enough processed samples in the output FIFO yet (startup latency),
    // we bypass the processing (Dry pass-through) to prevent dropouts/silence.
    if (m_fifoOut[0].size() < (size_t)requiredSrcFrames) {
        return;
    }

    for (int ch = 0; ch < useChannels; ++ch) {
        if (sampleRate == 48000) {
            for (int i = 0; i < nFrames; ++i) {
                float dry = buffer[i * channels + ch];
                float wet = m_fifoOut[ch][i];
                buffer[i * channels + ch] = dry * (1.0f - m_reduction) + wet * m_reduction;
            }
            m_fifoOut[ch].erase(m_fifoOut[ch].begin(), m_fifoOut[ch].begin() + nFrames);
        } else {
            // Resample from 48000 Hz to original sample rate
            for (int i = 0; i < nFrames; ++i) {
                double srcPos = i * ratio;
                int idx1 = std::floor(srcPos);
                int idx2 = std::min(idx1 + 1, requiredSrcFrames - 1);
                float frac = srcPos - idx1;
                float w1 = m_fifoOut[ch][idx1];
                float w2 = m_fifoOut[ch][idx2];
                float wet = w1 * (1.0f - frac) + w2 * frac;
                float dry = buffer[i * channels + ch];
                buffer[i * channels + ch] = dry * (1.0f - m_reduction) + wet * m_reduction;
            }
            m_fifoOut[ch].erase(m_fifoOut[ch].begin(), m_fifoOut[ch].begin() + requiredSrcFrames);
        }
    }
}
