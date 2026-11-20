#pragma once

#include <QObject>
#include <QFile>
#include <QVector>
#include <QMutex>
#include <QFuture>

struct AudioPeak {
    float max = 0.0f;
    float min = 0.0f;
};

/**
 * AudioMipmap: Efficient audio data access via memory mapping
 * and pre-calculated LOD (Level of Detail) peak pyramids.
 *
 * LOD pyramid structure:
 *   Level 0: 1 peak per 256 frames   (base resolution)
 *   Level 1: 1 peak per 1024 frames  (4x coarser)
 *   Level 2: 1 peak per 4096 frames
 *   Level 3: 1 peak per 16384 frames
 *   Level 4: 1 peak per 65536 frames
 *   Level 5: 1 peak per 262144 frames (coarsest)
 *
 * getPeak() selects the optimal LOD level for the requested range,
 * using raw samples for sub-block ranges and combining mipmap blocks
 * for large ranges.
 */
class AudioMipmap : public QObject {
    Q_OBJECT
public:
    explicit AudioMipmap(QObject *parent = nullptr);
    ~AudioMipmap() override;

    /// LOD level — public so GpuMipmapCompute can populate it
    struct Level {
        int blockSize;                   // Frames per peak entry
        QVector<AudioPeak> peaks;        // Interleaved: [ch0, ch1, ch0, ch1...]
    };

    static constexpr int BASE_BLOCK = 256;
    static constexpr int LOD_RATIO  = 4;
    static constexpr int MAX_LEVELS = 6;

    /**
     * Load an audio file via QFile::map().
     * Supports raw float32 and WAV float32 (auto-detects WAV header).
     * Handles multi-GB files without loading into RAM.
     */
    bool loadFile(const QString &path, int channels, int sampleRate);

    /**
     * Load from an in-memory vector (for recordings or small clips).
     */
    bool loadFromVector(const QVector<float> &samples, int channels, int sampleRate);

    /**
     * Append samples to the in-memory vector and incrementally update mipmaps.
     * Used during live recording to keep the mipmap current without full rebuild.
     */
    void appendSamples(const float *data, int nFrames);

    /**
     * Initialize for live recording: sets channels, sampleRate, creates empty
     * LOD levels, and marks ready immediately so appendSamples() works from
     * the very first audio chunk.
     */
    void setLiveParams(int channels, int sampleRate);

    /**
     * Get the peak (max and min) in a range of frames [startFrame, endFrame).
     * Automatically selects the best LOD level for efficiency.
     */
    AudioPeak getPeak(qint64 startFrame, qint64 endFrame, int channel = 0) const;

    /**
     * Batch version of getPeak to query multiple ranges under a single mutex lock.
     * This avoids massive mutex lock/unlock overhead when querying peaks per pixel.
     */
    void getPeaksBatch(const qint64 *starts, const qint64 *ends, int count, int channel, AudioPeak *outPeaks) const;

    /**
     * Pointer to the raw samples (Level 0).
     * Only valid for float32 data.
     */
    const float* rawData() const;

    qint64 totalFrames() const { return m_totalFrames; }
    int channels() const { return m_channels; }
    bool isReady() const {
        QMutexLocker lock(&m_mutex);
        return m_ready;
    }

signals:
    void ready();

private:
    void calculateMipmaps();
    AudioPeak getRawPeak(qint64 start, qint64 end, int channel) const;

    /// Get a single sample value. Thread-safe only when called under m_mutex.
    float getSample(qint64 frame, int channel) const;

    mutable QMutex m_mutex;
    QFile m_file;
    uchar *m_mappedData = nullptr;
    qint64 m_mappedSize = 0;
    qint64 m_dataOffset = 0; // Skip WAV headers

    QVector<float> m_samples; // RAM fallback

    int m_channels = 1;
    int m_sampleRate = 48000;
    qint64 m_totalFrames = 0;

    // LOD pyramid: level 0 = finest, higher = coarser
    // Block sizes: 256, 1024, 4096, 16384, 65536, 262144
    QVector<Level> m_levels;

    bool m_ready = false;
    QFuture<void> m_calcFuture;
};
