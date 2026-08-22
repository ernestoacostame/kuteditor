#include "WaveformRenderer.h"
#include "GpuWaveformCompute.h"
#include "WaveformItem.h"
#include "audio/AudioMipmap.h"

#include <rhi/qrhi.h>
#include <QFile>
#include <QDebug>
#include <QQuickWindow>
#include <cmath>
#include <algorithm>

static constexpr int VIEWPORT_MARGIN = 32;
static constexpr int CLIP_TICK_PX = 3;

// ─── Helper: Load .qsb shader ───
static QShader loadShader(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[WaveformRenderer] Failed to open shader:" << path;
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

// ─── Constructor / Destructor ───

WaveformRenderer::WaveformRenderer() {}

WaveformRenderer::~WaveformRenderer() {
    delete m_pipeline;
    delete m_srb;
    delete m_vertexBuffer;
    delete m_rmsVertexBuffer;
    delete m_clipVertexBuffer;
    delete m_uniformBuffer;
}

// ─── Initialize ───

void WaveformRenderer::initialize(QRhiCommandBuffer *cb) {
    Q_UNUSED(cb);
    QRhi *r = rhi();
    if (!r) return;

    // ── Try GPU compute path ──
    m_gpuCompute = std::make_unique<GpuWaveformCompute>();
    m_gpuAvailable = m_gpuCompute->initialize(r);

    if (m_gpuAvailable) {
        qDebug() << "[WaveformRenderer] GPU compute path active";
    } else {
        qDebug() << "[WaveformRenderer] GPU compute NOT available, using CPU fallback";
        m_gpuCompute.reset();
    }

    // ── Create the render pipeline for drawing vertex-color geometry ──
    // This is used by BOTH GPU and CPU paths to draw the final waveform.
    createRenderPipeline();

    m_needsInit = false;
}

// ─── Create Render Pipeline ──

void WaveformRenderer::createRenderPipeline() {
    QRhi *r = rhi();
    if (!r) return;

    // Uniform buffer for the viewport transform (vec4: width, height, 0, 0)
    // Must be at least 16 bytes for std140 vec4 alignment
    if (!m_uniformBuffer) {
        m_uniformBuffer = r->newBuffer(QRhiBuffer::Dynamic,
                                        QRhiBuffer::UniformBuffer,
                                        4 * sizeof(float));
        m_uniformBuffer->create();
    }

    // Shader resource bindings (uniform buffer for transform)
    delete m_srb;
    m_srb = r->newShaderResourceBindings();
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage, m_uniformBuffer)
    });
    m_srb->create();

    // Graphics pipeline: vertex-color, triangle strip, alpha blending
    delete m_pipeline;
    m_pipeline = r->newGraphicsPipeline();

    // Shaders
    QShader vs = loadShader(QStringLiteral(":/shaders/src/shaders/waveform_render.vert.qsb"));
    QShader fs = loadShader(QStringLiteral(":/shaders/src/shaders/waveform_render.frag.qsb"));

    if (!vs.isValid() || !fs.isValid()) {
        qWarning() << "[WaveformRenderer] Render shaders failed to load";
        delete m_pipeline;
        m_pipeline = nullptr;
        return;
    }

    m_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs}
    });

    // Vertex input layout: pos(2f) + color(4f) = 6 floats per vertex
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({
        {6 * sizeof(float)} // stride
    });
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},                    // position
        {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)}    // color
    });
    m_pipeline->setVertexInputLayout(inputLayout);

    // Topology: triangle strip
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);

    // Blending: pre-multiplied alpha
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_pipeline->setTargetBlends({blend});

    m_pipeline->setShaderResourceBindings(m_srb);
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_pipeline->create()) {
        qWarning() << "[WaveformRenderer] Failed to create graphics pipeline";
        delete m_pipeline;
        m_pipeline = nullptr;
    }
}

// ─── Synchronize (GUI thread blocked, render thread active) ───

void WaveformRenderer::synchronize(QQuickRhiItem *item) {
    auto *wi = static_cast<WaveformItem *>(item);
    if (!wi) return;

    auto &s = m_state;

    // Check if anything changed
    bool vpChanged = false;

    auto checkSet = [&](auto &dst, const auto &src) {
        if (dst != src) { dst = src; vpChanged = true; }
    };

    checkSet(s.width,            wi->width());
    checkSet(s.height,           wi->height());
    checkSet(s.timelineScrollX,  wi->timelineScrollX());
    checkSet(s.timelineZoom,     wi->timelineZoom());
    checkSet(s.secPerPixel,      wi->secPerPixel());
    checkSet(s.itemX,            wi->x());
    double startSec = wi->clipTimelineStart();
    double offsetSec = wi->clipSourceOffset();
    double lenSec = wi->clipLength();

    TrackModel *model = wi->model();
    if (model) {
        QVariantMap snap = model->clipSnapshot(wi->trackIndex(), wi->clipIndex());
        if (!snap.isEmpty()) {
            startSec = snap.value("startSec").toDouble();
            offsetSec = snap.value("sourceOffsetSec").toDouble();
            lenSec = snap.value("lengthSec").toDouble();
        }
    }

    checkSet(s.clipTimelineStart, startSec);
    checkSet(s.clipSourceOffset,  offsetSec);
    checkSet(s.clipLength,        lenSec);
    checkSet(s.visibleStartPx,   wi->visibleStartPx());
    checkSet(s.visibleEndPx,     wi->visibleEndPx());
    checkSet(s.gain,             wi->gain());
    checkSet(s.sampleRate,       wi->sampleRate());
    checkSet(s.liveMode,         wi->liveMode());
    checkSet(s.unipolar,         wi->unipolar());
    checkSet(s.showRms,          wi->showRms());
    checkSet(s.fillColor,        wi->fillColor());
    checkSet(s.warnColor,        wi->warnColor());
    checkSet(s.clipColor,        wi->clipColor());
    checkSet(s.rmsColor,         wi->rmsColor());

    if (vpChanged) s.viewportDirty = true;

    // Sync envelope data
    const auto &envList = wi->envelope();
    QVector<float> newEnvData;
    newEnvData.reserve(envList.size() * 2);
    for (const auto &v : envList) {
        auto m = v.toMap();
        newEnvData.append(m.value("x", 0.0f).toFloat());
        newEnvData.append(m.value("y", 1.0f).toFloat());
    }
    if (newEnvData != s.envelopeData) {
        s.envelopeData = newEnvData;
        s.numEnvNodes = newEnvData.size() / 2;
        s.envelopeDirty = true;
    }

    // Sync audio data
    TrackModel *model = wi->model();
    if (model) {
        auto mipmap = model->clipMipmap(wi->trackIndex(), wi->clipIndex());
        if (mipmap != s.mipmap) {
            s.mipmap = mipmap;
            s.samplesDirty = true;
        }
        if (s.mipmap && s.mipmap->isReady()) {
            const float *rawPtr = s.mipmap->rawData();
            qint64 frames = s.mipmap->totalFrames();
            int ch = s.mipmap->channels();
            if (rawPtr != s.rawSamples || frames != s.totalFrames || ch != s.channels) {
                s.rawSamples = rawPtr;
                s.totalFrames = frames;
                s.channels = ch;
                s.samplesDirty = true;
            }
        }
    }

    // Check WaveformItem's dirty flag
    if (wi->isGeometryDirty()) {
        s.geometryDirty = true;
        wi->clearGeometryDirty();
    }
}

// ─── Render ───

void WaveformRenderer::render(QRhiCommandBuffer *cb) {
    if (m_needsInit) return;

    auto &s = m_state;
    const float W = float(s.width);
    const float H = float(s.height);
    if (W <= 0 || H <= 0) return;

    QRhi *r = rhi();
    if (!r) return;

    QRhiRenderTarget *rt = renderTarget();
    if (!rt) return;

    if (!m_pipeline) {
        createRenderPipeline();
        if (!m_pipeline) return;
    }

    const double effectiveLenSec = s.clipLength;

    // ── No data: clear to transparent ──
    if (!s.mipmap || effectiveLenSec <= 0 || W < 1.0f) {
        QRhiResourceUpdateBatch *u = r->nextResourceUpdateBatch();
        cb->beginPass(rt, QColor(0, 0, 0, 0), {1.0f, 0}, u);
        cb->endPass();
        return;
    }

    // ── Check if anything needs updating ──
    bool needsUpdate = s.geometryDirty || s.viewportDirty || s.samplesDirty || s.envelopeDirty;

    // ── Viewport culling ──
    int renderStart = qMax(0, int(std::floor(s.visibleStartPx)) - VIEWPORT_MARGIN);
    int renderEnd   = qMin(int(std::ceil(W)), int(std::ceil(s.visibleEndPx)) + VIEWPORT_MARGIN);
    if (renderEnd <= renderStart) {
        QRhiResourceUpdateBatch *u = r->nextResourceUpdateBatch();
        cb->beginPass(rt, QColor(0, 0, 0, 0), {1.0f, 0}, u);
        cb->endPass();
        return;
    }

    // ════════════════════════════════════════════════════
    //  GPU COMPUTE PATH — TEMPORARILY DISABLED
    //  Using CPU fallback only until compute pipeline is validated.
    // ════════════════════════════════════════════════════
    // TODO: Re-enable GPU compute path after CPU fallback is verified.

    // ════════════════════════════════════════════════════
    //  CPU FALLBACK PATH
    // ════════════════════════════════════════════════════
    if (!needsUpdate && m_peakVertexCount > 0) {
        // Nothing changed, redraw with existing vertex data
        drawPass(cb, rt);
        return;
    }

    cpuGenerateVertices();

    QRhiResourceUpdateBatch *batch = r->nextResourceUpdateBatch();
    uploadVertexData(batch);
    drawPass(cb, rt, batch);

    s.geometryDirty = false;
    s.viewportDirty = false;
    s.samplesDirty = false;
    s.envelopeDirty = false;
}

// ─── Draw Pass ───

void WaveformRenderer::drawPass(QRhiCommandBuffer *cb, QRhiRenderTarget *rt,
                                 QRhiResourceUpdateBatch *batch) {
    QRhi *r = rhi();

    // Update uniform buffer with viewport dimensions (vec4 for std140)
    if (!batch) batch = r->nextResourceUpdateBatch();

    float ubo[4] = {float(m_state.width), float(m_state.height), 0.0f, 0.0f};
    batch->updateDynamicBuffer(m_uniformBuffer, 0, sizeof(ubo), ubo);

    // Begin render pass
    cb->beginPass(rt, QColor(0, 0, 0, 0), {1.0f, 0}, batch);

    cb->setGraphicsPipeline(m_pipeline);
    cb->setShaderResources(m_srb);

    // Set viewport
    const QSize outputSize = rt->pixelSize();
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});

    // Draw peak waveform (triangle strip)
    if (m_peakVertexCount > 0 && m_vertexBuffer) {
        const QRhiCommandBuffer::VertexInput vbufBinding(m_vertexBuffer, 0);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(m_peakVertexCount);
    }

    // Draw RMS overlay (triangle strip)
    if (m_rmsVertexCount > 0 && m_rmsVertexBuffer) {
        const QRhiCommandBuffer::VertexInput vbufBinding(m_rmsVertexBuffer, 0);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(m_rmsVertexCount);
    }

    cb->endPass();
}

// ─── CPU Vertex Generation ───

void WaveformRenderer::cpuGenerateVertices() {
    auto &s = m_state;
    const float W = float(s.width);
    const float H = float(s.height);
    const float h = H;

    const double effectiveLenSec = s.clipLength;
    if (effectiveLenSec <= 0 || W < 1.0f) {
        m_peakVertexCount = 0;
        m_rmsVertexCount = 0;
        return;
    }

    int renderStart = qMax(0, int(std::floor(s.visibleStartPx)) - VIEWPORT_MARGIN);
    int renderEnd   = qMin(int(std::ceil(W)), int(std::ceil(s.visibleEndPx)) + VIEWPORT_MARGIN);
    if (renderEnd <= renderStart) {
        m_peakVertexCount = 0;
        m_rmsVertexCount = 0;
        return;
    }

    const int cols = renderEnd - renderStart;
    const int vtxCount = (cols + 1) * 2;
    const int floatsPerVtx = 6; // x, y, r, g, b, a

    m_peakData.resize(vtxCount * floatsPerVtx);
    float *peakV = m_peakData.data();

    m_rmsData.resize(s.showRms ? vtxCount * floatsPerVtx : 0);
    float *rmsV = s.showRms ? m_rmsData.data() : nullptr;

    m_clipColumns.clear();

    auto mipmap = s.mipmap;
    const float midY = h * 0.5f;
    double secPerPx = s.secPerPixel;
    double baseTime = s.clipSourceOffset;
    if (secPerPx > 0.0) {
        baseTime = s.clipSourceOffset + s.itemX * secPerPx;
    } else {
        secPerPx = effectiveLenSec / W;
    }
    const double clipEndT = s.clipSourceOffset + effectiveLenSec;

    for (int i = 0; i <= cols; ++i) {
        const int x = renderStart + i;
        const float px = float(x);
        const int vi = i * 2;
        const int base0 = vi * floatsPerVtx;
        const int base1 = (vi + 1) * floatsPerVtx;

        const double t0 = baseTime + px * secPerPx;
        const double t1 = baseTime + (px + 1.0) * secPerPx;

        // Default: transparent
        auto setVtx = [](float *buf, int off, float px, float y,
                         float r, float g, float b, float a) {
            buf[off + 0] = px; buf[off + 1] = y;
            buf[off + 2] = r;  buf[off + 3] = g;
            buf[off + 4] = b;  buf[off + 5] = a;
        };

        setVtx(peakV, base0, px, h, 0, 0, 0, 0);
        setVtx(peakV, base1, px, h, 0, 0, 0, 0);
        if (rmsV) {
            setVtx(rmsV, base0, px, h, 0, 0, 0, 0);
            setVtx(rmsV, base1, px, h, 0, 0, 0, 0);
        }

        if (t0 >= clipEndT) continue;

        const double sf0 = t0 * s.sampleRate;
        const double sf1 = t1 * s.sampleRate;
        const qint64 sS = qMax(qint64(0), qint64(sf0));
        const qint64 sE = qMax(sS + 1, qint64(std::ceil(sf1)));

        AudioPeak peak = {0, 0};
        bool hasData = false;

        if (mipmap && mipmap->isReady()) {
            peak = mipmap->getPeak(sS, sE);
            hasData = true;
        }

        if (!hasData) {
            float fr = s.fillColor.redF(), fg = s.fillColor.greenF();
            float fb = s.fillColor.blueF(), fa = 100.0f / 255.0f;
            if (s.unipolar) {
                setVtx(peakV, base0, px, h - 2, fr, fg, fb, fa);
                setVtx(peakV, base1, px, h,     fr, fg, fb, fa);
            } else {
                setVtx(peakV, base0, px, midY - 1, fr, fg, fb, fa);
                setVtx(peakV, base1, px, midY + 1, fr, fg, fb, fa);
            }
            continue;
        }

        float g = s.gain;
        if (s.numEnvNodes > 0) {
            g *= envelopeGainAt(float(t0 - s.clipSourceOffset));
        }

        float maxA = std::clamp(peak.max * g, -1.0f, 1.0f);
        float minA = std::clamp(peak.min * g, -1.0f, 1.0f);
        float absMax = std::max(std::abs(maxA), std::abs(minA));

        // Clipping detection
        bool isClipping = (std::abs(peak.max * g) > 0.99f || std::abs(peak.min * g) > 0.99f);
        if (isClipping) m_clipColumns.append(x);

        // Color selection
        QColor col = s.fillColor;
        if (absMax > 0.95f) col = s.clipColor;
        else if (absMax > 0.70f) col = s.warnColor;

        float yTop, yBot;
        if (s.unipolar) {
            yTop = h - (absMax * h); yBot = h;
            if (yBot - yTop < 2.0f) yTop = h - 2.0f;
        } else {
            yTop = midY - (maxA * midY); yBot = midY - (minA * midY);
            if (std::abs(yTop - yBot) < 2.0f) { yTop = midY - 1.0f; yBot = midY + 1.0f; }
        }

        // Pre-multiply alpha for Qt Quick compositing
        float cr = col.redF() * col.alphaF();
        float cg = col.greenF() * col.alphaF();
        float cbv = col.blueF() * col.alphaF();
        float ca = col.alphaF();

        setVtx(peakV, base0, px, yTop, cr, cg, cbv, ca);
        setVtx(peakV, base1, px, yBot, cr, cg, cbv, ca);

        // RMS overlay
        if (rmsV) {
            float rmsApprox = absMax * 0.65f;
            float rTop, rBot;

            if (s.unipolar) {
                rTop = h - (rmsApprox * h); rBot = h;
                if (rBot - rTop < 1.0f) rTop = h - 1.0f;
            } else {
                rTop = midY - (rmsApprox * midY);
                rBot = midY + (rmsApprox * midY);
                if (std::abs(rTop - rBot) < 1.0f) { rTop = midY - 0.5f; rBot = midY + 0.5f; }
            }

            float rr = s.rmsColor.redF() * s.rmsColor.alphaF();
            float rg = s.rmsColor.greenF() * s.rmsColor.alphaF();
            float rb = s.rmsColor.blueF() * s.rmsColor.alphaF();
            float ra = s.rmsColor.alphaF();

            setVtx(rmsV, base0, px, rTop, rr, rg, rb, ra);
            setVtx(rmsV, base1, px, rBot, rr, rg, rb, ra);
        }
    }

    m_peakVertexCount = vtxCount;
    m_rmsVertexCount = s.showRms ? vtxCount : 0;
}

// ─── Upload Vertex Data ───

void WaveformRenderer::uploadVertexData(QRhiResourceUpdateBatch *batch) {
    QRhi *r = rhi();

    // Peak vertices
    if (m_peakVertexCount > 0) {
        qint64 dataSize = m_peakData.size() * sizeof(float);
        if (m_vertexBuffer && m_vertexBuffer->size() < dataSize) {
            delete m_vertexBuffer;
            m_vertexBuffer = nullptr;
        }
        if (!m_vertexBuffer) {
            m_vertexBuffer = r->newBuffer(QRhiBuffer::Dynamic,
                                           QRhiBuffer::VertexBuffer,
                                           dataSize);
            m_vertexBuffer->create();
        }
        batch->updateDynamicBuffer(m_vertexBuffer, 0, dataSize, m_peakData.constData());
    }

    // RMS vertices
    if (m_rmsVertexCount > 0) {
        qint64 dataSize = m_rmsData.size() * sizeof(float);
        if (m_rmsVertexBuffer && m_rmsVertexBuffer->size() < dataSize) {
            delete m_rmsVertexBuffer;
            m_rmsVertexBuffer = nullptr;
        }
        if (!m_rmsVertexBuffer) {
            m_rmsVertexBuffer = r->newBuffer(QRhiBuffer::Dynamic,
                                              QRhiBuffer::VertexBuffer,
                                              dataSize);
            m_rmsVertexBuffer->create();
        }
        batch->updateDynamicBuffer(m_rmsVertexBuffer, 0, dataSize, m_rmsData.constData());
    }

    // Clip indicators (lines)
    if (!m_clipColumns.isEmpty()) {
        const float h = float(m_state.height);
        QVector<float> clipData;
        clipData.reserve(m_clipColumns.size() * 4 * 6); // 4 verts per column (2 lines)

        // Pre-multiplied red
        const float cr = 220.0f/255.0f * (220.0f/255.0f);
        const float cg = 50.0f/255.0f  * (220.0f/255.0f);
        const float cbv = 50.0f/255.0f  * (220.0f/255.0f);
        const float ca = 220.0f/255.0f;

        for (int col : m_clipColumns) {
            float px = float(col);
            // Top tick
            clipData << px << 0.0f << cr << cg << cbv << ca;
            clipData << px << float(CLIP_TICK_PX) << cr << cg << cbv << ca;
            // Bottom tick
            clipData << px << (h - float(CLIP_TICK_PX)) << cr << cg << cbv << ca;
            clipData << px << h << cr << cg << cbv << ca;
        }

        qint64 dataSize = clipData.size() * sizeof(float);
        if (m_clipVertexBuffer && m_clipVertexBuffer->size() < dataSize) {
            delete m_clipVertexBuffer;
            m_clipVertexBuffer = nullptr;
        }
        if (!m_clipVertexBuffer) {
            m_clipVertexBuffer = r->newBuffer(QRhiBuffer::Dynamic,
                                               QRhiBuffer::VertexBuffer,
                                               dataSize);
            m_clipVertexBuffer->create();
        }
        batch->updateDynamicBuffer(m_clipVertexBuffer, 0, dataSize, clipData.constData());
        m_clipVertexCount = clipData.size() / 6;
    } else {
        m_clipVertexCount = 0;
    }
}

// ─── Envelope interpolation (same logic as WaveformItem) ───

float WaveformRenderer::envelopeGainAt(float t) const {
    const auto &data = m_state.envelopeData;
    int n = m_state.numEnvNodes;
    if (n <= 0) return 1.0f;

    float firstX = data[0], firstY = data[1];
    float lastX = data[(n - 1) * 2], lastY = data[(n - 1) * 2 + 1];

    if (t <= firstX) return firstY;
    if (t >= lastX) return lastY;

    for (int i = 0; i < n - 1; ++i) {
        float ax = data[i * 2], ay = data[i * 2 + 1];
        float bx = data[(i + 1) * 2], by = data[(i + 1) * 2 + 1];
        if (t >= ax && t <= bx) {
            float dx = bx - ax;
            if (dx < 1e-8f) return ay;
            float frac = (t - ax) / dx;
            return ay + frac * (by - ay);
        }
    }
    return lastY;
}
