#include "AudioMipmap.h"
#include "GpuMipmapCompute.h"
#include <QtConcurrent>
#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <cstring>

AudioMipmap::AudioMipmap(QObject *parent) : QObject(parent) {}

AudioMipmap::~AudioMipmap() {
    m_calcFuture.cancel();
    m_calcFuture.waitForFinished();
    if (m_mappedData) {
        m_file.unmap(m_mappedData);
    }
}

bool AudioMipmap::loadFile(const QString &path, int channels, int sampleRate) {
    QMutexLocker lock(&m_mutex);
    if (m_mappedData) {
        m_file.unmap(m_mappedData);
        m_mappedData = nullptr;
    }
    m_samples.clear();
    m_levels.clear();
    m_ready = false;

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        return false;
    }

    m_mappedSize = m_file.size();
    m_mappedData = m_file.map(0, m_mappedSize);
    if (!m_mappedData) {
        return false;
    }

    m_channels = channels;
    m_sampleRate = sampleRate;
    m_dataOffset = 0;

    // Simple WAV detection: find the 'data' chunk
    if (m_mappedSize > 44 && std::memcmp(m_mappedData, "RIFF", 4) == 0) {
        for (qint64 i = 12; i < m_mappedSize - 8; ++i) {
            if (std::memcmp(m_mappedData + i, "data", 4) == 0) {
                m_dataOffset = i + 8;
                break;
            }
        }
    }

    m_totalFrames = (m_mappedSize - m_dataOffset) / (m_channels * sizeof(float));

    // Launch async mipmap calculation
    lock.unlock();
    m_calcFuture = QtConcurrent::run([this]() { calculateMipmaps(); });
    return true;
}

bool AudioMipmap::loadFromVector(const QVector<float> &samples, int channels, int sampleRate) {
    {
        QMutexLocker lock(&m_mutex);
        m_samples = samples;
        m_channels = channels;
        m_sampleRate = sampleRate;
        m_totalFrames = channels > 0 ? samples.size() / channels : 0;
        if (m_levels.isEmpty()) {
            m_ready = false;
        }
    }

    m_calcFuture = QtConcurrent::run([this]() { calculateMipmaps(); });
    return true;
}

void AudioMipmap::setLiveParams(int channels, int sampleRate) {
    QMutexLocker lock(&m_mutex);
    m_channels = channels;
    m_sampleRate = sampleRate;
    m_totalFrames = 0;
    m_samples.clear();
    m_levels.clear();

    // Pre-create empty LOD levels so appendSamples() can work immediately
    for (int l = 0; l < MAX_LEVELS; ++l) {
        int blockSize = BASE_BLOCK;
        for (int i = 0; i < l; ++i) blockSize *= LOD_RATIO;
        Level level;
        level.blockSize = blockSize;
        m_levels.append(std::move(level));
    }

    m_ready = true;
}

void AudioMipmap::appendSamples(const float *data, int nFrames) {
    QMutexLocker lock(&m_mutex);
    if (!data || nFrames <= 0) return;

    const int oldSize = m_samples.size();
    m_samples.resize(oldSize + nFrames * m_channels);
    std::memcpy(m_samples.data() + oldSize, data, sizeof(float) * nFrames * m_channels);

    const qint64 oldTotalFrames = m_totalFrames;
    m_totalFrames = m_channels > 0 ? m_samples.size() / m_channels : 0;

    // Incrementally update mipmap level 0
    if (m_levels.isEmpty()) return; // Not yet computed

    Level &lv0 = m_levels[0];
    const int bs = lv0.blockSize;

    // Which blocks need updating?
    qint64 firstDirtyBlock = oldTotalFrames / bs;
    qint64 lastBlock = (m_totalFrames + bs - 1) / bs;

    // Extend the peaks vector
    lv0.peaks.resize(lastBlock * m_channels);

    for (qint64 b = firstDirtyBlock; b < lastBlock; ++b) {
        for (int ch = 0; ch < m_channels; ++ch) {
            qint64 start = b * bs;
            qint64 end = std::min(start + bs, m_totalFrames);
            lv0.peaks[b * m_channels + ch] = getRawPeak(start, end, ch);
        }
    }

    // Propagate up to higher levels
    for (int l = 1; l < m_levels.size(); ++l) {
        Level &lvl = m_levels[l];
        const Level &prev = m_levels[l - 1];
        const int ratio = lvl.blockSize / prev.blockSize;

        qint64 numBlocks = (m_totalFrames + lvl.blockSize - 1) / lvl.blockSize;
        lvl.peaks.resize(numBlocks * m_channels);

        qint64 firstDirty = (firstDirtyBlock * m_levels[0].blockSize) / lvl.blockSize;
        for (qint64 b = firstDirty; b < numBlocks; ++b) {
            for (int ch = 0; ch < m_channels; ++ch) {
                AudioPeak p = {-2.0f, 2.0f};
                for (int i = 0; i < ratio; ++i) {
                    qint64 prevIdx = b * ratio + i;
                    qint64 peakIdx = prevIdx * m_channels + ch;
                    if (peakIdx >= prev.peaks.size()) break;
                    AudioPeak pp = prev.peaks[peakIdx];
                    if (pp.max > p.max) p.max = pp.max;
                    if (pp.min < p.min) p.min = pp.min;
                }
                if (p.max < -1.0f) p.max = 0.0f;
                if (p.min >  1.0f) p.min = 0.0f;
                lvl.peaks[b * m_channels + ch] = p;
            }
        }
    }
}

float AudioMipmap::getSample(qint64 frame, int channel) const {
    if (frame < 0 || frame >= m_totalFrames) return 0.0f;
    if (channel < 0 || channel >= m_channels) return 0.0f;

    const float *data = nullptr;
    if (m_mappedData) {
        data = reinterpret_cast<const float*>(m_mappedData + m_dataOffset);
    } else if (!m_samples.isEmpty()) {
        data = m_samples.constData();
    }
    if (!data) return 0.0f;

    return data[frame * m_channels + channel];
}

void AudioMipmap::calculateMipmaps() {
    // Build the LOD pyramid from level 0 (finest) to MAX_LEVELS-1 (coarsest).
    // Each level has blockSize = BASE_BLOCK * LOD_RATIO^level.

    QElapsedTimer timer;
    timer.start();

    // ── Try GPU compute path first ──
    const float *rawPtr = nullptr;
    if (m_mappedData) {
        rawPtr = reinterpret_cast<const float *>(m_mappedData + m_dataOffset);
    } else if (!m_samples.isEmpty()) {
        rawPtr = m_samples.constData();
    }

    auto &gpu = GpuMipmapCompute::instance();
    if (gpu.isAvailable() && rawPtr && m_totalFrames >= 4096) {
        QVector<GpuMipmapCompute::MipmapLevel> gpuLevels;
        if (gpu.buildMipmap(rawPtr, m_totalFrames, m_channels, m_sampleRate, gpuLevels)) {
            // Convert GpuMipmapCompute::MipmapLevel → AudioMipmap::Level
            QVector<Level> newLevels;
            newLevels.reserve(gpuLevels.size());
            for (auto &gl : gpuLevels) {
                Level lv;
                lv.blockSize = gl.blockSize;
                lv.peaks = std::move(gl.peaks);
                newLevels.append(std::move(lv));
            }

            {
                QMutexLocker lock(&m_mutex);
                m_levels = std::move(newLevels);
                m_samples.clear();
                m_samples.squeeze();
                m_ready = true;
            }
            qDebug() << "[AudioMipmap] GPU path:" << timer.elapsed() << "ms |"
                     << m_totalFrames << "frames";
            emit ready();
            return;
        }
    }

    // ── CPU fallback ──
    qDebug() << "[AudioMipmap] Using CPU fallback for mipmap...";

    QVector<Level> newLevels;

    for (int l = 0; l < MAX_LEVELS; ++l) {
        int blockSize = BASE_BLOCK;
        for (int i = 0; i < l; ++i) blockSize *= LOD_RATIO;

        // No point in levels coarser than half the total frames
        if (blockSize >= m_totalFrames / 2 && l > 0) break;

        Level level;
        level.blockSize = blockSize;
        qint64 numBlocks = (m_totalFrames + blockSize - 1) / blockSize;
        level.peaks.resize(numBlocks * m_channels);

        for (qint64 b = 0; b < numBlocks; ++b) {
            for (int ch = 0; ch < m_channels; ++ch) {
                AudioPeak p;
                if (l == 0) {
                    // Level 0: compute from raw samples
                    qint64 start = b * blockSize;
                    qint64 end = std::min(start + blockSize, m_totalFrames);
                    p = getRawPeak(start, end, ch);
                } else {
                    // Higher levels: compute from previous level
                    const Level &prev = newLevels[l - 1];
                    p.max = -2.0f;
                    p.min = 2.0f;
                    for (int i = 0; i < LOD_RATIO; ++i) {
                        qint64 prevIdx = b * LOD_RATIO + i;
                        qint64 peakIdx = prevIdx * m_channels + ch;
                        if (peakIdx >= prev.peaks.size()) break;
                        AudioPeak pp = prev.peaks[peakIdx];
                        if (pp.max > p.max) p.max = pp.max;
                        if (pp.min < p.min) p.min = pp.min;
                    }
                    if (p.max < -1.0f) p.max = 0.0f;
                    if (p.min >  1.0f) p.min = 0.0f;
                }
                level.peaks[b * m_channels + ch] = p;
            }
        }
        newLevels.append(std::move(level));
    }

    {
        QMutexLocker lock(&m_mutex);
        m_levels = std::move(newLevels);
        m_samples.clear();
        m_samples.squeeze();
        m_ready = true;
    }
    qDebug() << "[AudioMipmap] CPU path:" << timer.elapsed() << "ms |"
             << m_totalFrames << "frames";
    emit ready();
}

AudioPeak AudioMipmap::getRawPeak(qint64 start, qint64 end, int channel) const {
    if (start >= end || start < 0) return {0, 0};

    const float *data = nullptr;

    if (m_mappedData) {
        data = reinterpret_cast<const float*>(m_mappedData + m_dataOffset);
    } else {
        data = m_samples.constData();
    }

    if (!data) return {0, 0};

    // Clamp to valid range
    if (end > m_totalFrames) end = m_totalFrames;

    AudioPeak p = {-2.0f, 2.0f};

    for (qint64 f = start; f < end; ++f) {
        float v = data[f * m_channels + channel];
        if (v > p.max) p.max = v;
        if (v < p.min) p.min = v;
    }

    if (p.max < -1.0f) p.max = 0.0f;
    if (p.min >  1.0f) p.min = 0.0f;

    return p;
}

AudioPeak AudioMipmap::getPeak(qint64 startFrame, qint64 endFrame, int channel) const {
    if (startFrame >= endFrame) return {0, 0};

    QMutexLocker lock(&m_mutex);

    // Clamp to valid range
    if (startFrame < 0) startFrame = 0;
    if (endFrame > m_totalFrames) endFrame = m_totalFrames;
    if (startFrame >= endFrame) return {0, 0};

    const qint64 range = endFrame - startFrame;

    // ---------------------------------------------------------------
    //  Select the best LOD level.
    //  We want a level where the block size is <= range / 2 so we get
    //  at least ~2 blocks covering the range (good visual resolution).
    //  For very small ranges, fall through to raw sample access.
    // ---------------------------------------------------------------
    int bestLevel = -1;
    for (int i = m_levels.size() - 1; i >= 0; --i) {
        if (m_levels[i].blockSize <= range / 2) {
            bestLevel = i;
            break;
        }
    }

    if (bestLevel == -1) {
        // Range is smaller than the finest mipmap block — use raw samples
        return getRawPeak(startFrame, endFrame, channel);
    }

    // ---------------------------------------------------------------
    //  Three-phase scan for accuracy at block boundaries:
    //  1. Head: raw samples from startFrame to the first aligned block boundary
    //  2. Body: mipmap blocks entirely within the range
    //  3. Tail: raw samples from the last aligned block boundary to endFrame
    // ---------------------------------------------------------------
    const Level &lv = m_levels[bestLevel];
    const int bs = lv.blockSize;

    // First full block that starts at or after startFrame
    qint64 firstFullBlock = (startFrame + bs - 1) / bs;
    // Last full block that ends at or before endFrame
    qint64 lastFullBlock = endFrame / bs;

    AudioPeak result = {-2.0f, 2.0f};

    // Head: raw samples [startFrame, firstFullBlock * bs)
    if (firstFullBlock * bs > startFrame) {
        qint64 headEnd = std::min(firstFullBlock * bs, endFrame);
        AudioPeak headPeak = getRawPeak(startFrame, headEnd, channel);
        if (headPeak.max > result.max) result.max = headPeak.max;
        if (headPeak.min < result.min) result.min = headPeak.min;
    }

    // Body: mipmap blocks [firstFullBlock, lastFullBlock)
    for (qint64 b = firstFullBlock; b < lastFullBlock; ++b) {
        qint64 idx = b * m_channels + channel;
        if (idx >= lv.peaks.size()) break;
        AudioPeak bp = lv.peaks[idx];
        if (bp.max > result.max) result.max = bp.max;
        if (bp.min < result.min) result.min = bp.min;
    }

    // Tail: raw samples [lastFullBlock * bs, endFrame)
    if (lastFullBlock * bs < endFrame && lastFullBlock >= firstFullBlock) {
        qint64 tailStart = lastFullBlock * bs;
        AudioPeak tailPeak = getRawPeak(tailStart, endFrame, channel);
        if (tailPeak.max > result.max) result.max = tailPeak.max;
        if (tailPeak.min < result.min) result.min = tailPeak.min;
    }

    if (result.max < -1.0f) result.max = 0.0f;
    if (result.min >  1.0f) result.min = 0.0f;

    return result;
}

void AudioMipmap::getPeaksBatch(const qint64 *starts, const qint64 *ends, int count, int channel, AudioPeak *outPeaks) const {
    if (count <= 0 || !outPeaks) return;

    QMutexLocker lock(&m_mutex);

    for (int i = 0; i < count; ++i) {
        qint64 startFrame = starts[i];
        qint64 endFrame = ends[i];

        if (startFrame >= endFrame) {
            outPeaks[i] = {0.0f, 0.0f};
            continue;
        }

        if (startFrame < 0) startFrame = 0;
        if (endFrame > m_totalFrames) endFrame = m_totalFrames;
        if (startFrame >= endFrame) {
            outPeaks[i] = {0.0f, 0.0f};
            continue;
        }

        const qint64 range = endFrame - startFrame;
        int bestLevel = -1;
        for (int l = m_levels.size() - 1; l >= 0; --l) {
            if (m_levels[l].blockSize <= range / 2) {
                bestLevel = l;
                break;
            }
        }

        if (bestLevel == -1) {
            outPeaks[i] = getRawPeak(startFrame, endFrame, channel);
            continue;
        }

        const Level &lv = m_levels[bestLevel];
        const int bs = lv.blockSize;

        qint64 firstFullBlock = (startFrame + bs - 1) / bs;
        qint64 lastFullBlock = endFrame / bs;

        AudioPeak result = {-2.0f, 2.0f};

        if (firstFullBlock * bs > startFrame) {
            qint64 headEnd = std::min(firstFullBlock * bs, endFrame);
            AudioPeak headPeak = getRawPeak(startFrame, headEnd, channel);
            if (headPeak.max > result.max) result.max = headPeak.max;
            if (headPeak.min < result.min) result.min = headPeak.min;
        }

        for (qint64 b = firstFullBlock; b < lastFullBlock; ++b) {
            qint64 idx = b * m_channels + channel;
            if (idx >= lv.peaks.size()) break;
            AudioPeak bp = lv.peaks[idx];
            if (bp.max > result.max) result.max = bp.max;
            if (bp.min < result.min) result.min = bp.min;
        }

        if (lastFullBlock * bs < endFrame && lastFullBlock >= firstFullBlock) {
            qint64 tailStart = lastFullBlock * bs;
            AudioPeak tailPeak = getRawPeak(tailStart, endFrame, channel);
            if (tailPeak.max > result.max) result.max = tailPeak.max;
            if (tailPeak.min < result.min) result.min = tailPeak.min;
        }

        if (result.max < -1.0f) result.max = 0.0f;
        if (result.min >  1.0f) result.min = 0.0f;

        outPeaks[i] = result;
    }
}

const float *AudioMipmap::rawData() const {
    QMutexLocker lock(&m_mutex);
    if (m_mappedData) {
        return reinterpret_cast<const float *>(m_mappedData + m_dataOffset);
    }
    if (!m_samples.isEmpty()) {
        return m_samples.constData();
    }
    return nullptr;
}
