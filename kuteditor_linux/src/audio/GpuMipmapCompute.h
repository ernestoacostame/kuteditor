#pragma once

#include <QMutex>
#include <QVector>
#include <memory>

class QRhi;
class QVulkanInstance;
class QRhiComputePipeline;
class QRhiShaderResourceBindings;
class QRhiBuffer;
class QShader;

struct AudioPeak;

/**
 * GpuMipmapCompute — Headless GPU compute for audio mipmap building.
 *
 * Uses a dedicated QRhi Vulkan instance (no window, no rendering) to
 * dispatch compute shaders that build the LOD peak pyramid in parallel.
 *
 * Usage:
 *   auto &gpu = GpuMipmapCompute::instance();
 *   if (gpu.isAvailable()) {
 *       QVector<MipmapLevel> levels;
 *       if (gpu.buildMipmap(samples, frames, ch, sr, levels)) { ... }
 *   }
 *
 * Thread safety: buildMipmap() is mutex-protected. Safe to call from
 * QtConcurrent worker threads.
 */
class GpuMipmapCompute {
public:
    /// LOD level output — matches AudioMipmap::Level layout
    struct MipmapLevel {
        int blockSize;
        QVector<AudioPeak> peaks; // Interleaved: [ch0, ch1, ch0, ch1...]
    };

    static GpuMipmapCompute &instance();

    /// Returns true if GPU compute is available (Vulkan + compute support)
    bool isAvailable() const { return m_available; }

    /**
     * Build the LOD mipmap pyramid on GPU.
     *
     * @param samples  Pointer to interleaved float32 audio data
     * @param totalFrames  Number of audio frames
     * @param channels  Number of interleaved channels
     * @param sampleRate  Sample rate (informational, not used in computation)
     * @param outLevels  Output: filled with LOD levels on success
     * @return true on success, false if GPU compute failed (caller should
     *         fall back to CPU)
     */
    bool buildMipmap(const float *samples, qint64 totalFrames,
                     int channels, int sampleRate,
                     QVector<MipmapLevel> &outLevels);

private:
    GpuMipmapCompute();
    ~GpuMipmapCompute();
    GpuMipmapCompute(const GpuMipmapCompute &) = delete;
    GpuMipmapCompute &operator=(const GpuMipmapCompute &) = delete;

    bool initialize();
    bool createPipeline();

    // Constants matching mipmap_build.comp and AudioMipmap
    static constexpr int BASE_BLOCK = 256;
    static constexpr int LOD_RATIO  = 4;
    static constexpr int MAX_LEVELS = 6;
    static constexpr int WORKGROUP_SIZE = 256;

    // Push constant layout matching mipmap_build.comp
    struct PushConstants {
        quint32 totalFrames;
        quint32 channels;
        quint32 blockSize;
        quint32 channel;
        quint32 phase;
        quint32 prevBlockSize;
        quint32 numOutputBlocks;
        quint32 outputOffset;
        quint32 prevOffset;
    };

    QMutex m_mutex;
    bool m_available = false;
    bool m_initAttempted = false;

    // Owned QRhi instance (headless Vulkan)
    std::unique_ptr<QVulkanInstance> m_vulkanInstance;
    QRhi *m_rhi = nullptr;  // owned via unique_ptr pattern but QRhi has no unique_ptr support

    // Pipeline resources
    QShader *m_shader = nullptr;
    QRhiComputePipeline *m_pipeline = nullptr;
    QRhiShaderResourceBindings *m_srb = nullptr;
};
