#pragma once

#include <QQuickRhiItem>
#include <QColor>
#include <QVector>
#include <memory>

class GpuWaveformCompute;
class AudioMipmap;
class TrackModel;

QT_BEGIN_NAMESPACE
class QRhi;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiResourceUpdateBatch;
class QRhiRenderTarget;
QT_END_NAMESPACE

/**
 * WaveformRenderer — QQuickRhiItemRenderer that drives GPU waveform rendering.
 *
 * Lifecycle (called by Qt on the render thread):
 *   initialize()  → create pipelines, check compute support
 *   synchronize() → copy viewport/data from WaveformItem (GUI thread blocked)
 *   render()      → generate vertices + draw
 *
 * If GPU compute is not available, falls back to CPU vertex generation
 * using the existing AudioMipmap + manual vertex loop.
 */
class WaveformRenderer : public QQuickRhiItemRenderer {
public:
    WaveformRenderer();
    ~WaveformRenderer() override;

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    // ── GPU compute resources ──
    std::unique_ptr<GpuWaveformCompute> m_gpuCompute;
    bool m_gpuAvailable = false;

    // ── Render pipeline ──
    QRhiGraphicsPipeline *m_pipeline = nullptr;
    QRhiShaderResourceBindings *m_srb = nullptr;
    QRhiBuffer *m_uniformBuffer = nullptr;

    // ── Vertex buffers ──
    QRhiBuffer *m_vertexBuffer     = nullptr;  // Peak waveform
    QRhiBuffer *m_rmsVertexBuffer  = nullptr;  // RMS overlay
    QRhiBuffer *m_clipVertexBuffer = nullptr;  // Clip indicators
    int m_peakVertexCount = 0;
    int m_rmsVertexCount  = 0;
    int m_clipVertexCount = 0;

    // ── CPU vertex data (generated each dirty frame) ──
    QVector<float> m_peakData;
    QVector<float> m_rmsData;
    QVector<int>   m_clipColumns;

    // ── Synced state from WaveformItem (GUI thread → render thread) ──
    struct SyncedState {
        double width = 0, height = 0;
        double timelineScrollX = 0;
        double timelineZoom = 1.0;
        double secPerPixel = 0;
        double itemX = 0;
        double clipTimelineStart = 0;
        double clipSourceOffset = 0;
        double clipLength = 0;
        double visibleStartPx = 0;
        double visibleEndPx = 1e9;
        float  gain = 1.0f;
        double sampleRate = 48000.0;
        bool   liveMode = false;
        bool   unipolar = true;
        bool   showRms = true;

        QColor fillColor{255, 255, 255, 140};
        QColor warnColor{250, 209, 56, 160};
        QColor clipColor{242, 77, 61, 180};
        QColor rmsColor{180, 210, 255, 120};

        // Envelope data (packed x,y pairs)
        QVector<float> envelopeData;
        int numEnvNodes = 0;

        // Audio data
        std::shared_ptr<AudioMipmap> mipmap;
        const float *rawSamples = nullptr;
        qint64 totalFrames = 0;
        int channels = 1;

        // Dirty flags
        bool samplesDirty = false;
        bool envelopeDirty = false;
        bool viewportDirty = false;
        bool geometryDirty = false;
    } m_state;

    bool m_needsInit = true;

    // ── Methods ──
    void createRenderPipeline();
    void cpuGenerateVertices();
    void uploadVertexData(QRhiResourceUpdateBatch *batch);
    void drawPass(QRhiCommandBuffer *cb, QRhiRenderTarget *rt,
                  QRhiResourceUpdateBatch *batch = nullptr);
    float envelopeGainAt(float t) const;
};
