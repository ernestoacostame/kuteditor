#pragma once

#include <QColor>
#include <QVector>

// Forward declarations — QRhi types from Qt::GuiPrivate
QT_BEGIN_NAMESPACE
class QRhi;
class QRhiBuffer;
class QRhiComputePipeline;
class QRhiShaderResourceBindings;
class QRhiCommandBuffer;
class QRhiResourceUpdateBatch;
QT_END_NAMESPACE

/**
 * GpuWaveformCompute — GPU compute pipeline for waveform peak calculation.
 *
 * Pipeline:
 *   1. uploadSamples()      → raw audio data → GPU SSBO
 *   2. buildMipmap(cb)      → compute shader builds LOD pyramid in packed SSBO
 *   3. generateVertices(cb) → compute shader produces vertex data in SSBO
 *   4. peakVertexBuffer()   → SSBO usable as VertexBuffer for rendering
 *
 * The output vertex SSBOs have both StorageBuffer and VertexBuffer usage,
 * so they can be bound directly in a graphics render pass without readback.
 */
class GpuWaveformCompute {
public:
    GpuWaveformCompute();
    ~GpuWaveformCompute();

    static bool isSupported(QRhi *rhi);

    bool initialize(QRhi *rhi);
    void releaseResources();

    // ── Data upload ──

    void uploadSamples(QRhiResourceUpdateBatch *batch,
                       const float *data, qint64 totalFrames, int channels);

    void uploadEnvelope(QRhiResourceUpdateBatch *batch,
                        const QVector<float> &envData);

    // ── Compute dispatch ──

    /// Build mipmap pyramid (dispatch once per data change).
    void buildMipmap(QRhiCommandBuffer *cb);

    /// Viewport parameters for per-pixel vertex generation
    struct ViewportParams {
        float width, height;
        float secPerPx, baseTime, clipEndTime;
        float sampleRate, gain;
        int   renderStart, renderEnd;
        bool  unipolar, showRms;
        QColor fillColor, warnColor, clipColor, rmsColor;
    };

    /// Generate waveform vertices (dispatch each dirty frame).
    void generateVertices(QRhiCommandBuffer *cb, const ViewportParams &vp);

    // ── Output access ──

    /// The peak vertex SSBO (also usable as VertexBuffer).
    /// Layout: [x, y, r, g, b, a] × 2 per column (triangle strip).
    QRhiBuffer *peakVertexBuffer()  const { return m_peakVertexSSBO; }
    QRhiBuffer *rmsVertexBuffer()   const { return m_rmsVertexSSBO; }
    QRhiBuffer *clipFlagsBuffer()   const { return m_clipFlagsSSBO; }

    int peakVertexCount()  const { return m_peakVertexCount; }
    int rmsVertexCount()   const { return m_rmsVertexCount; }

    bool isMipmapDirty()   const { return m_mipmapDirty; }
    bool isInitialized()   const { return m_initialized; }

    static constexpr int BASE_BLOCK = 256;
    static constexpr int LOD_RATIO  = 4;
    static constexpr int MAX_LEVELS = 6;

private:
    static uint packColorRGBA8(const QColor &c);
    void ensureOutputBuffers(int numColumns);

    QRhi *m_rhi = nullptr;
    bool  m_initialized = false;
    bool  m_mipmapDirty = true;

    // Audio data
    qint64 m_totalFrames = 0;
    int    m_channels    = 1;

    // Input SSBO: raw interleaved samples
    QRhiBuffer *m_samplesSSBO = nullptr;

    // Packed mipmap SSBO (all levels contiguous)
    QRhiBuffer *m_mipmapPackedSSBO = nullptr;
    int m_numLevels = 0;
    int m_mipmapBlockSizes[MAX_LEVELS]  = {};
    int m_mipmapBlockCounts[MAX_LEVELS] = {};
    int m_mipmapLevelOffsets[MAX_LEVELS] = {};
    int m_mipmapTotalPeaks = 0;

    // Envelope SSBO
    QRhiBuffer *m_envelopeSSBO = nullptr;
    int m_numEnvNodes = 0;

    // Output SSBOs (StorageBuffer + VertexBuffer usage)
    QRhiBuffer *m_peakVertexSSBO = nullptr;
    QRhiBuffer *m_rmsVertexSSBO  = nullptr;
    QRhiBuffer *m_clipFlagsSSBO  = nullptr;
    int m_maxColumns      = 0;
    int m_peakVertexCount = 0;
    int m_rmsVertexCount  = 0;

    // Compute pipelines (created once, SRBs created per dispatch)
    QRhiComputePipeline *m_mipmapPipeline = nullptr;
    QRhiComputePipeline *m_vertexPipeline = nullptr;

    // Layout-defining SRBs (kept alive for pipeline lifetime)
    QRhiShaderResourceBindings *m_mipmapLayoutSRB = nullptr;
    QRhiShaderResourceBindings *m_vertexLayoutSRB = nullptr;

    // Dummy buffers for layout SRBs (tiny, just to define binding types)
    QRhiBuffer *m_dummySSBO = nullptr;
};
