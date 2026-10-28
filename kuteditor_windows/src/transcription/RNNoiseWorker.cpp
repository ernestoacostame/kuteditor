#include "RNNoiseWorker.h"
#include <QtConcurrent>
#include <cmath>
#include <algorithm>
#include <cstring>

extern "C" {
#include <rnnoise.h>
}

#ifdef HAVE_SAMPLERATE
#include <samplerate.h>
#endif

// Helper function to resample mono channel audio
static QVector<float> resampleAudio(const QVector<float>& input, int fromRate, int toRate) {
    if (fromRate == toRate || input.isEmpty()) return input;
    double ratio = (double)toRate / fromRate;
    int outSize = (int)std::round(input.size() * ratio);
    QVector<float> output(outSize);

#ifdef HAVE_SAMPLERATE
    SRC_DATA srcData;
    srcData.data_in = input.constData();
    srcData.input_frames = input.size();
    srcData.data_out = output.data();
    srcData.output_frames = outSize;
    srcData.src_ratio = ratio;
    
    int err = src_simple(&srcData, SRC_SINC_FASTEST, 1);
    if (err == 0) {
        output.resize(srcData.output_frames_gen);
        return output;
    }
#endif

    // Fallback: simple linear resampling
    for (int i = 0; i < outSize; ++i) {
        double srcPos = i / ratio;
        int idx1 = (int)std::floor(srcPos);
        int idx2 = std::min(idx1 + 1, (int)input.size() - 1);
        float frac = srcPos - idx1;
        if (idx1 >= 0 && idx1 < input.size()) {
            output[i] = input[idx1] * (1.0f - frac) + input[idx2] * frac;
        } else {
            output[i] = 0.0f;
        }
    }
    return output;
}

RNNoiseWorker::RNNoiseWorker(QObject *parent) : QObject(parent) {}

RNNoiseWorker::~RNNoiseWorker() {
    if (m_future.isRunning()) {
        m_future.cancel();
        m_future.waitForFinished();
    }
}

void RNNoiseWorker::processOffline(int trackIndex, int sourceIdx, const QVector<float>& inSamples, int sampleRate, int channels) {
    if (m_processing) return;
    m_processing = true;
    emit processingStarted(trackIndex, sourceIdx);

    m_future = QtConcurrent::run([this, trackIndex, sourceIdx, inSamples, sampleRate, channels]() -> QVector<float> {
        if (inSamples.isEmpty() || channels <= 0 || sampleRate <= 0) {
            return inSamples;
        }

        // Split interleaved input channels
        int inputFrames = inSamples.size() / channels;
        QVector<QVector<float>> chSamples(channels);
        for (int ch = 0; ch < channels; ++ch) {
            chSamples[ch].resize(inputFrames);
            for (int i = 0; i < inputFrames; ++i) {
                chSamples[ch][i] = inSamples[i * channels + ch];
            }
        }

        const int FRAME_SIZE = 480; // RNNoise strictly requires 480 samples (10ms at 48kHz)

        // Process each channel independently
        for (int ch = 0; ch < channels; ++ch) {
            // Resample to 48kHz if not already 48kHz
            QVector<float> working = resampleAudio(chSamples[ch], sampleRate, 48000);
            int nFrames = working.size();

            DenoiseState *st = rnnoise_create(NULL);
            float frame[FRAME_SIZE];

            for (int i = 0; i < nFrames; i += FRAME_SIZE) {
                if (m_future.isCanceled()) break;

                int chunk = std::min(FRAME_SIZE, nFrames - i);
                if (chunk < FRAME_SIZE) {
                    std::memset(frame, 0, sizeof(frame));
                }

                // Scale to RNNoise range (short range scaled to float)
                for (int j = 0; j < chunk; ++j) {
                    frame[j] = working[i + j] * 32768.0f;
                }

                rnnoise_process_frame(st, frame, frame);

                for (int j = 0; j < chunk; ++j) {
                    working[i + j] = frame[j] / 32768.0f;
                }
            }

            rnnoise_destroy(st);

            // Resample back to original sample rate
            chSamples[ch] = resampleAudio(working, 48000, sampleRate);
        }

        // Re-interleave output channels
        QVector<float> outSamples(inSamples.size());
        int outputFrames = outSamples.size() / channels;
        for (int ch = 0; ch < channels; ++ch) {
            int chSize = chSamples[ch].size();
            for (int i = 0; i < outputFrames; ++i) {
                if (i < chSize) {
                    outSamples[i * channels + ch] = chSamples[ch][i];
                } else {
                    outSamples[i * channels + ch] = 0.0f;
                }
            }
        }

        return outSamples;
    });

    auto *watcher = new QFutureWatcher<QVector<float>>(this);
    connect(watcher, &QFutureWatcher<QVector<float>>::finished, this, [this, watcher, trackIndex, sourceIdx]() {
        m_processing = false;
        emit processingFinished(trackIndex, sourceIdx, m_future.result());
        watcher->deleteLater();
    });
    watcher->setFuture(m_future);
}
