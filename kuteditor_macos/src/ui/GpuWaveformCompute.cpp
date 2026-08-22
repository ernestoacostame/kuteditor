#include "GpuWaveformCompute.h"

#include <rhi/qrhi.h>
#include <QFile>
#include <QDebug>
#include <cstring>
#include <cmath>

// ─── Helper: Load .qsb shader from Qt resource ───
static QShader loadShader(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[GpuCompute] Cannot open shader:" << path;
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

// ─── Ctor / Dtor ───

GpuWaveformCompute::GpuWaveformCompute() {}

GpuWaveformCompute::~GpuWaveformCompute() {
    releaseResources();
}

bool GpuWaveformCompute::isSupported(QRhi *rhi) {
    return rhi && rhi->isFeatureSupported(QRhi::Compute);
}

uint GpuWaveformCompute::packColorRGBA8(const QColor &c) {
    return (uint(c.red())    & 0xFF)
         | ((uint(c.green()) & 0xFF) << 8)
         | ((uint(c.blue())  & 0xFF) << 16)
         | ((uint(c.alpha()) & 0xFF) << 24);
}

// ─── Initialize ───

bool GpuWaveformCompute::initialize(QRhi *rhi) {
    if (m_initialized) return true;
    if (!rhi || !isSupported(rhi)) return false;

    m_rhi = rhi;

    // Create a small dummy SSBO for layout SRBs
    m_dummySSBO = m_rhi->newBuffer(
        QRhiBuffer::Static,
        QRhiBuffer::StorageBuffer,
        64);
    if (!m_dummySSBO->create()) {
        qWarning() << "[GpuCompute] Failed to create dummy SSBO";
        return false;
    }

    // ── Mipmap pipeline ──
    {
        QShader cs = loadShader(QStringLiteral(":/shaders/src/shaders/mipmap_build.comp.qsb"));
        if (!cs.isValid()) {
            qWarning() << "[GpuCompute] mipmap_build.comp shader invalid";
            return false;
        }

        // Layout SRB: binding 0 = samples (read), binding 1 = packed mipmap (read/write)
        m_mipmapLayoutSRB = m_rhi->newShaderResourceBindings();
        m_mipmapLayoutSRB->setBindings({
            QRhiShaderResourceBinding::bufferLoadStore(
                0, QRhiShaderResourceBinding::ComputeStage, m_dummySSBO),
            QRhiShaderResourceBinding::bufferLoadStore(
                1, QRhiShaderResourceBinding::ComputeStage, m_dummySSBO),
        });
        if (!m_mipmapLayoutSRB->create()) {
            qWarning() << "[GpuCompute] Failed to create mipmap layout SRB";
            return false;
        }

        m_mipmapPipeline = m_rhi->newComputePipeline();
        m_mipmapPipeline->setShaderStage({QRhiShaderStage::Compute, cs});
        m_mipmapPipeline->setShaderResourceBindings(m_mipmapLayoutSRB);
        if (!m_mipmapPipeline->create()) {
            qWarning() << "[GpuCompute] Failed to create mipmap pipeline";
            return false;
        }
    }

    // ── Vertex gen pipeline ──
    {
        QShader cs = loadShader(QStringLiteral(":/shaders/src/shaders/peak_vertices.comp.qsb"));
        if (!cs.isValid()) {
            qWarning() << "[GpuCompute] peak_vertices.comp shader invalid";
            return false;
        }

        // Layout SRB: 6 bindings (samples, mipmap, peakVerts, rmsVerts, clipFlags, envelope)
        m_vertexLayoutSRB = m_rhi->newShaderResourceBindings();
        m_vertexLayoutSRB->setBindings({
            QRhiShaderResourceBinding::bufferLoadStore(
                0, QRhiShaderResourceBinding::ComputeStage, m_dummySSBO),
            QRhiShaderResourceBinding::bufferLoadStore(
                1, QRhiShaderResourceBinding::ComputeStage, m_dummySSBO),
            QRhiShaderResourceBinding::bufferLoadStore(
                2, QRhiShaderResourceBinding::ComputeStage, m_dummySSBO),
            QRhiShaderResourceBinding::bufferLoadStore(
                3, QRhiShaderResourceBinding::ComputeStage, m_dummySSBO),
            QRhiShaderResourceBinding::bufferLoadStore(
                4, QRhiShaderResourceBinding::ComputeStage, m_dummySSBO),
            QRhiShaderResourceBinding::bufferLoadStore(
                5, QRhiShaderResourceBinding::ComputeStage, m_dummySSBO),
        });
        if (!m_vertexLayoutSRB->create()) {
            qWarning() << "[GpuCompute] Failed to create vertex layout SRB";
            return false;
        }

        m_vertexPipeline = m_rhi->newComputePipeline();
        m_vertexPipeline->setShaderStage({QRhiShaderStage::Compute, cs});
        m_vertexPipeline->setShaderResourceBindings(m_vertexLayoutSRB);
        if (!m_vertexPipeline->create()) {
            qWarning() << "[GpuCompute] Failed to create vertex pipeline";
            return false;
        }
    }

    // Envelope SSBO (tiny initial)
    m_envelopeSSBO = m_rhi->newBuffer(
        QRhiBuffer::Static,
        QRhiBuffer::StorageBuffer,
        sizeof(float) * 4);
    m_envelopeSSBO->create();

    m_initialized = true;
    qDebug() << "[GpuCompute] GPU compute initialized OK";
    return true;
}

// ─── Release ───

void GpuWaveformCompute::releaseResources() {
    delete m_samplesSSBO;       m_samplesSSBO = nullptr;
    delete m_mipmapPackedSSBO;  m_mipmapPackedSSBO = nullptr;
    delete m_envelopeSSBO;      m_envelopeSSBO = nullptr;
    delete m_peakVertexSSBO;    m_peakVertexSSBO = nullptr;
    delete m_rmsVertexSSBO;     m_rmsVertexSSBO = nullptr;
    delete m_clipFlagsSSBO;     m_clipFlagsSSBO = nullptr;
    delete m_mipmapPipeline;    m_mipmapPipeline = nullptr;
    delete m_vertexPipeline;    m_vertexPipeline = nullptr;
    delete m_mipmapLayoutSRB;   m_mipmapLayoutSRB = nullptr;
    delete m_vertexLayoutSRB;   m_vertexLayoutSRB = nullptr;
    delete m_dummySSBO;         m_dummySSBO = nullptr;
    m_initialized = false;
    m_rhi = nullptr;
}

// ─── Upload Samples ───

void GpuWaveformCompute::uploadSamples(QRhiResourceUpdateBatch *batch,
                                        const float *data, qint64 totalFrames,
                                        int channels) {
    if (!m_rhi || !data || totalFrames <= 0) return;

    m_totalFrames = totalFrames;
    m_channels = channels;

    // Samples SSBO
    qint64 sizeBytes = totalFrames * channels * qint64(sizeof(float));
    if (m_samplesSSBO && m_samplesSSBO->size() != sizeBytes) {
        delete m_samplesSSBO;
        m_samplesSSBO = nullptr;
    }
    if (!m_samplesSSBO) {
        m_samplesSSBO = m_rhi->newBuffer(
            QRhiBuffer::Static,
            QRhiBuffer::StorageBuffer,
            sizeBytes);
        m_samplesSSBO->create();
    }
    batch->uploadStaticBuffer(m_samplesSSBO, 0, sizeBytes, data);

    // Compute mipmap level metadata
    m_numLevels = 0;
    m_mipmapTotalPeaks = 0;

    for (int l = 0; l < MAX_LEVELS; ++l) {
        int blockSize = BASE_BLOCK;
        for (int i = 0; i < l; ++i) blockSize *= LOD_RATIO;

        if (blockSize >= m_totalFrames / 2 && l > 0) break;

        m_mipmapBlockSizes[l]  = blockSize;
        m_mipmapBlockCounts[l] = int((m_totalFrames + blockSize - 1) / blockSize);
        m_mipmapLevelOffsets[l] = m_mipmapTotalPeaks;
        m_mipmapTotalPeaks += m_mipmapBlockCounts[l];
        m_numLevels = l + 1;
    }

    // Packed mipmap SSBO (vec2 per peak = 8 bytes)
    qint64 mipmapBytes = qMax(qint64(m_mipmapTotalPeaks) * 8, qint64(64));
    if (m_mipmapPackedSSBO && m_mipmapPackedSSBO->size() != mipmapBytes) {
        delete m_mipmapPackedSSBO;
        m_mipmapPackedSSBO = nullptr;
    }
    if (!m_mipmapPackedSSBO) {
        m_mipmapPackedSSBO = m_rhi->newBuffer(
            QRhiBuffer::Static,
            QRhiBuffer::StorageBuffer,
            mipmapBytes);
        m_mipmapPackedSSBO->create();
    }

    m_mipmapDirty = true;
}

// ─── Upload Envelope ───

void GpuWaveformCompute::uploadEnvelope(QRhiResourceUpdateBatch *batch,
                                         const QVector<float> &envData) {
    m_numEnvNodes = envData.size() / 2;

    qint64 sizeBytes = qMax(qint64(envData.size()) * qint64(sizeof(float)),
                             qint64(sizeof(float) * 4));

    if (m_envelopeSSBO && m_envelopeSSBO->size() != sizeBytes) {
        delete m_envelopeSSBO;
        m_envelopeSSBO = nullptr;
    }
    if (!m_envelopeSSBO) {
        m_envelopeSSBO = m_rhi->newBuffer(
            QRhiBuffer::Static,
            QRhiBuffer::StorageBuffer,
            sizeBytes);
        m_envelopeSSBO->create();
    }

    if (!envData.isEmpty()) {
        batch->uploadStaticBuffer(m_envelopeSSBO, 0,
                                   envData.size() * sizeof(float),
                                   envData.constData());
    }
}

// ─── Ensure Output Buffers ───

void GpuWaveformCompute::ensureOutputBuffers(int numColumns) {
    if (numColumns <= m_maxColumns && m_peakVertexSSBO) return;

    m_maxColumns = qMax(numColumns, 2048);

    // Peak vertices: 2 verts/col × 6 floats/vert
    qint64 vtxBytes = qint64(m_maxColumns) * 2 * 6 * sizeof(float);

    // CRITICAL: Both StorageBuffer (for compute write) AND VertexBuffer (for render read)
    auto usage = QRhiBuffer::StorageBuffer | QRhiBuffer::VertexBuffer;

    delete m_peakVertexSSBO;
    m_peakVertexSSBO = m_rhi->newBuffer(QRhiBuffer::Static, usage, vtxBytes);
    m_peakVertexSSBO->create();

    delete m_rmsVertexSSBO;
    m_rmsVertexSSBO = m_rhi->newBuffer(QRhiBuffer::Static, usage, vtxBytes);
    m_rmsVertexSSBO->create();

    // Clip flags: 1 uint per column
    delete m_clipFlagsSSBO;
    m_clipFlagsSSBO = m_rhi->newBuffer(
        QRhiBuffer::Static,
        QRhiBuffer::StorageBuffer,
        qint64(m_maxColumns) * sizeof(uint));
    m_clipFlagsSSBO->create();
}

// ─── Build Mipmap ───

void GpuWaveformCompute::buildMipmap(QRhiCommandBuffer *cb) {
    if (!m_initialized || !m_mipmapDirty || !m_samplesSSBO || !m_mipmapPackedSSBO) return;
    if (m_totalFrames <= 0) return;

    // Push constant struct matching mipmap_build.comp
    struct MipmapPC {
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

    for (int l = 0; l < m_numLevels; ++l) {
        // Create per-dispatch SRB (layout-compatible with m_mipmapLayoutSRB)
        QRhiShaderResourceBindings *srb = m_rhi->newShaderResourceBindings();
        srb->setBindings({
            QRhiShaderResourceBinding::bufferLoadStore(
                0, QRhiShaderResourceBinding::ComputeStage, m_samplesSSBO),
            QRhiShaderResourceBinding::bufferLoadStore(
                1, QRhiShaderResourceBinding::ComputeStage, m_mipmapPackedSSBO),
        });
        srb->create();

        MipmapPC pc = {};
        pc.totalFrames    = quint32(m_totalFrames);
        pc.channels       = quint32(m_channels);
        pc.blockSize      = quint32(m_mipmapBlockSizes[l]);
        pc.channel        = 0;
        pc.phase          = (l == 0) ? 0u : 1u;
        pc.prevBlockSize  = (l > 0) ? quint32(m_mipmapBlockSizes[l - 1]) : 0u;
        pc.numOutputBlocks = quint32(m_mipmapBlockCounts[l]);
        pc.outputOffset   = quint32(m_mipmapLevelOffsets[l]);
        pc.prevOffset     = (l > 0) ? quint32(m_mipmapLevelOffsets[l - 1]) : 0u;

        cb->beginComputePass();
        cb->setComputePipeline(m_mipmapPipeline);
        cb->setShaderResources(srb);
        cb->dispatch(quint32(m_mipmapBlockCounts[l]), 1, 1);
        cb->endComputePass();

        // SRB must live until command buffer is submitted (end of frame).
        // In QRhi, per-frame SRBs are safe to create; they're destroyed
        // after endFrame(). But we hold them to be safe.
        // NOTE: small leak per frame — acceptable for now.
        // TODO: Pool SRBs or use deferred cleanup.
        // For robustness, delete after frame via QRhi callback if available.
        srb->deleteLater();
    }

    m_mipmapDirty = false;
    qDebug() << "[GpuCompute] Mipmap built:" << m_numLevels << "levels,"
             << m_mipmapTotalPeaks << "total peaks";
}

// ─── Generate Vertices ───

void GpuWaveformCompute::generateVertices(QRhiCommandBuffer *cb,
                                           const ViewportParams &vp) {
    if (!m_initialized || !m_samplesSSBO || !m_mipmapPackedSSBO) return;

    int numColumns = vp.renderEnd - vp.renderStart;
    if (numColumns <= 0) return;

    ensureOutputBuffers(numColumns + 1);

    m_peakVertexCount = (numColumns + 1) * 2;
    m_rmsVertexCount  = vp.showRms ? (numColumns + 1) * 2 : 0;

    // Push constant struct matching peak_vertices.comp
    struct VertexPC {
        float    width;
        float    height;
        float    secPerPx;
        float    baseTime;
        float    clipEndTime;
        float    sampleRate;
        float    gain;
        quint32  renderStart;
        quint32  renderEnd;
        quint32  totalFrames;
        quint32  channels;
        quint32  channel;
        quint32  numLevels;
        quint32  levelOffsets[MAX_LEVELS];
        quint32  levelBlockSizes[MAX_LEVELS];
        quint32  fillColor;
        quint32  warnColor;
        quint32  clipColor;
        quint32  rmsColor;
        quint32  flags;
        quint32  numEnvNodes;
    };

    VertexPC pc = {};
    pc.width         = vp.width;
    pc.height        = vp.height;
    pc.secPerPx      = vp.secPerPx;
    pc.baseTime      = vp.baseTime;
    pc.clipEndTime   = vp.clipEndTime;
    pc.sampleRate    = vp.sampleRate;
    pc.gain          = vp.gain;
    pc.renderStart   = quint32(vp.renderStart);
    pc.renderEnd     = quint32(vp.renderEnd);
    pc.totalFrames   = quint32(m_totalFrames);
    pc.channels      = quint32(m_channels);
    pc.channel       = 0;
    pc.numLevels     = quint32(m_numLevels);

    for (int i = 0; i < MAX_LEVELS; ++i) {
        pc.levelOffsets[i]    = (i < m_numLevels) ? quint32(m_mipmapLevelOffsets[i]) : 0;
        pc.levelBlockSizes[i] = (i < m_numLevels) ? quint32(m_mipmapBlockSizes[i]) : 0;
    }

    pc.fillColor     = packColorRGBA8(vp.fillColor);
    pc.warnColor     = packColorRGBA8(vp.warnColor);
    pc.clipColor     = packColorRGBA8(vp.clipColor);
    pc.rmsColor      = packColorRGBA8(vp.rmsColor);
    pc.flags         = (vp.unipolar ? 1u : 0u) | (vp.showRms ? 2u : 0u);
    pc.numEnvNodes   = quint32(m_numEnvNodes);

    // Create per-dispatch SRB
    QRhiShaderResourceBindings *srb = m_rhi->newShaderResourceBindings();
    srb->setBindings({
        QRhiShaderResourceBinding::bufferLoadStore(
            0, QRhiShaderResourceBinding::ComputeStage, m_samplesSSBO),
        QRhiShaderResourceBinding::bufferLoadStore(
            1, QRhiShaderResourceBinding::ComputeStage, m_mipmapPackedSSBO),
        QRhiShaderResourceBinding::bufferLoadStore(
            2, QRhiShaderResourceBinding::ComputeStage, m_peakVertexSSBO),
        QRhiShaderResourceBinding::bufferLoadStore(
            3, QRhiShaderResourceBinding::ComputeStage, m_rmsVertexSSBO),
        QRhiShaderResourceBinding::bufferLoadStore(
            4, QRhiShaderResourceBinding::ComputeStage, m_clipFlagsSSBO),
        QRhiShaderResourceBinding::bufferLoadStore(
            5, QRhiShaderResourceBinding::ComputeStage, m_envelopeSSBO),
    });
    srb->create();

    int numWorkgroups = (numColumns + 255) / 256;

    cb->beginComputePass();
    cb->setComputePipeline(m_vertexPipeline);
    cb->setShaderResources(srb);
    cb->dispatch(quint32(numWorkgroups), 1, 1);
    cb->endComputePass();

    srb->deleteLater();
}
