#include "WaveformItem.h"
#include "audio/AudioMipmap.h"
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <QSGVertexColorMaterial>
#include <QQuickWindow>
#include <QDebug>
#include <QtMath>
#include <cmath>
#include <algorithm>

static constexpr int VIEWPORT_MARGIN = 32;
static constexpr int CLIP_TICK_PX = 3;

// ─── Constructor / Destructor ───

WaveformItem::WaveformItem(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
}

WaveformItem::~WaveformItem() {}

// ─── Setters ───

void WaveformItem::setModel(TrackModel *m) {
    if (m_model == m) return;
    m_model = m;
    if (m_model) {
        setSampleRate(m_model->trackSampleRate(m_trackIndex));
        connect(m_model, &TrackModel::trackPeaksUpdated, this, [this](int idx){
            if (idx == m_trackIndex) {
                setSampleRate(m_model->trackSampleRate(m_trackIndex));
                m_geometryDirty = true;
                update();
            }
        });
    }
    updateMipmapConnection();
    emit modelChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setTrackIndex(int i) {
    if (m_trackIndex == i) return;
    m_trackIndex = i;
    if (m_model) setSampleRate(m_model->trackSampleRate(m_trackIndex));
    updateMipmapConnection();
    emit trackIndexChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setClipIndex(int i) {
    if (m_clipIndex == i) return;
    m_clipIndex = i;
    updateMipmapConnection();
    emit clipIndexChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setSampleRate(double sr) {
    if (qAbs(m_sampleRate - sr) < 0.1) return;
    m_sampleRate = sr;
    m_geometryDirty = true;
    emit sampleRateChanged();
    update();
}

void WaveformItem::setUnipolar(bool u) {
    if (m_unipolar == u) return;
    m_unipolar = u;
    m_geometryDirty = true;
    emit unipolarChanged();
    update();
}

void WaveformItem::updateMipmapConnection() {
    if (!m_model) return;
    auto mipmap = m_model->clipMipmap(m_trackIndex, m_clipIndex);
    if (mipmap == m_currentMipmap) return;
    if (m_currentMipmap)
        disconnect(m_currentMipmap.get(), &AudioMipmap::ready, this, nullptr);
    m_currentMipmap = mipmap;
    if (m_currentMipmap)
        connect(m_currentMipmap.get(), &AudioMipmap::ready, this,
                [this]() { m_geometryDirty = true; update(); },
                Qt::QueuedConnection);
    m_geometryDirty = true;
}

void WaveformItem::setTimelineScrollX(double x) {
    if (qFuzzyCompare(x, m_timelineScrollX)) return;
    m_timelineScrollX = x;
    emit viewportChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setTimelineZoom(double z) {
    if (qFuzzyCompare(z, m_timelineZoom)) return;
    m_timelineZoom = z;
    emit viewportChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setSecPerPixel(double s) {
    if (qFuzzyCompare(s, m_secPerPixel)) return;
    m_secPerPixel = s;
    emit secPerPixelChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setClipTimelineStart(double s) {
    if (qFuzzyCompare(s, m_clipTimelineStart)) return;
    m_clipTimelineStart = s;
    emit viewportChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setClipSourceOffset(double o) {
    if (qFuzzyCompare(o, m_clipSourceOffset)) return;
    m_clipSourceOffset = o;
    emit viewportChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setClipLength(double l) {
    if (qFuzzyCompare(l, m_clipLength)) return;
    m_clipLength = l;
    emit viewportChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setVisibleStartPx(double v) {
    if (qFuzzyCompare(v, m_visibleStartPx)) return;
    m_visibleStartPx = v;
    emit viewportChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setVisibleEndPx(double v) {
    if (qFuzzyCompare(v, m_visibleEndPx)) return;
    m_visibleEndPx = v;
    emit viewportChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setGain(float g) {
    if (qFuzzyCompare(g, m_gain)) return;
    m_gain = g;
    emit gainChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setEnvelope(const QVariantList &env) {
    m_envelopeList = env;
    // Build envelope cache
    m_envData.clear();
    m_envData.reserve(env.size() * 2);
    for (const auto &v : env) {
        auto m = v.toMap();
        m_envData.append(m.value("x", 0.0f).toFloat());
        m_envData.append(m.value("y", 1.0f).toFloat());
    }
    m_numEnvNodes = env.size();
    emit envelopeChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setLiveMode(bool l) {
    if (m_liveMode == l) return;
    m_liveMode = l;
    emit liveModeChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setFillColor(const QColor &c) {
    if (m_fillColor == c) return;
    m_fillColor = c;
    emit colorsChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setWarnColor(const QColor &c) {
    if (m_warnColor == c) return;
    m_warnColor = c;
    emit colorsChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setClipColor(const QColor &c) {
    if (m_clipColor == c) return;
    m_clipColor = c;
    emit colorsChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setRmsColor(const QColor &c) {
    if (m_rmsColor == c) return;
    m_rmsColor = c;
    emit colorsChanged();
    m_geometryDirty = true;
    update();
}

void WaveformItem::setShowRms(bool v) {
    if (m_showRms == v) return;
    m_showRms = v;
    emit colorsChanged();
    m_geometryDirty = true;
    update();
}

// ─── geometryChange ───

void WaveformItem::geometryChange(const QRectF &newGeo, const QRectF &oldGeo) {
    QQuickItem::geometryChange(newGeo, oldGeo);
    if (newGeo.size() != oldGeo.size()) {
        m_geometryDirty = true;
        update();
    }
}

// ─── Envelope interpolation ───

float WaveformItem::envelopeGainAt(float t) const {
    if (m_numEnvNodes <= 0) return 1.0f;

    float firstX = m_envData[0], firstY = m_envData[1];
    float lastX = m_envData[(m_numEnvNodes - 1) * 2];
    float lastY = m_envData[(m_numEnvNodes - 1) * 2 + 1];

    if (t <= firstX) return firstY;
    if (t >= lastX) return lastY;

    for (int i = 0; i < m_numEnvNodes - 1; ++i) {
        float ax = m_envData[i * 2], ay = m_envData[i * 2 + 1];
        float bx = m_envData[(i + 1) * 2], by = m_envData[(i + 1) * 2 + 1];
        if (t >= ax && t <= bx) {
            float dx = bx - ax;
            if (dx < 1e-8f) return ay;
            float frac = (t - ax) / dx;
            return ay + frac * (by - ay);
        }
    }
    return lastY;
}

// ─── updatePaintNode — Scene Graph rendering ───

// Sequential envelope lookup helper class
class SequentialEnvelopeLookup {
public:
    SequentialEnvelopeLookup(const QVector<float> &envData, int numNodes)
        : m_envData(envData), m_numNodes(numNodes), m_currentIndex(0) {}

    float gainAt(float t) {
        if (m_numNodes <= 0) return 1.0f;

        float firstX = m_envData[0], firstY = m_envData[1];
        float lastX = m_envData[(m_numNodes - 1) * 2];
        float lastY = m_envData[(m_numNodes - 1) * 2 + 1];

        if (t <= firstX) return firstY;
        if (t >= lastX) return lastY;

        while (m_currentIndex < m_numNodes - 1) {
            float bx = m_envData[(m_currentIndex + 1) * 2];
            if (t <= bx) {
                float ax = m_envData[m_currentIndex * 2], ay = m_envData[m_currentIndex * 2 + 1];
                float by = m_envData[(m_currentIndex + 1) * 2 + 1];
                float dx = bx - ax;
                if (dx < 1e-8f) return ay;
                float frac = (t - ax) / dx;
                return ay + frac * (by - ay);
            }
            m_currentIndex++;
        }
        return lastY;
    }

private:
    const QVector<float> &m_envData;
    int m_numNodes;
    int m_currentIndex;
};

// ─── updatePaintNode — Scene Graph rendering ───

QSGNode *WaveformItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
    const float W = float(width());
    const float H = float(height());

    double clipStartSec = m_clipTimelineStart;
    double clipSourceOffset = m_clipSourceOffset;
    double clipLength = m_clipLength;

    if (m_model) {
        QVariantMap snap = m_model->clipSnapshot(m_trackIndex, m_clipIndex);
        if (!snap.isEmpty()) {
            clipStartSec = snap.value("startSec").toDouble();
            clipSourceOffset = snap.value("sourceOffsetSec").toDouble();
            clipLength = snap.value("lengthSec").toDouble();
        }
    }

    if (W <= 0 || H <= 0 || clipLength <= 0 || !m_model) {
        delete oldNode;
        return nullptr;
    }

    // Get mipmap
    auto mipmap = m_model->clipMipmap(m_trackIndex, m_clipIndex);
    bool hasMipmap = mipmap && mipmap->isReady();

    // Viewport culling
    int renderStart = qMax(0, int(std::floor(m_visibleStartPx)) - VIEWPORT_MARGIN);
    int renderEnd   = qMin(int(std::ceil(W)), int(std::ceil(m_visibleEndPx)) + VIEWPORT_MARGIN);
    if (renderEnd <= renderStart) {
        delete oldNode;
        return nullptr;
    }

    const int cols = renderEnd - renderStart;
    const int vtxCount = (cols + 1) * 2;

    // ── Peak waveform node ──
    QSGGeometryNode *peakNode = nullptr;
    QSGGeometry *peakGeo = nullptr;

    if (oldNode) {
        peakNode = static_cast<QSGGeometryNode *>(oldNode);
        peakGeo = peakNode->geometry();
        if (peakGeo->vertexCount() != vtxCount) {
            peakGeo->allocate(vtxCount);
        }
    } else {
        peakGeo = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), vtxCount);
        peakGeo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        peakNode = new QSGGeometryNode;
        peakNode->setGeometry(peakGeo);
        peakNode->setFlag(QSGNode::OwnsGeometry);
        auto *mat = new QSGVertexColorMaterial;
        peakNode->setMaterial(mat);
        peakNode->setFlag(QSGNode::OwnsMaterial);
    }

    // ── Generate vertices ──
    QSGGeometry::ColoredPoint2D *v = peakGeo->vertexDataAsColoredPoint2D();

    const float h = H;
    const float midY = h * 0.5f;
    double secPerPx = m_secPerPixel;
    double baseTime = clipSourceOffset;
    // globalOrigin: the absolute timeline pixel position of waveCanvas local x=0.
    // Used to snap sample ranges to integer timeline pixels so that waveform
    // peaks are identical before and after a split.
    double globalOrigin = 0.0;
    if (secPerPx > 0.0) {
        baseTime = clipSourceOffset + x() * secPerPx;
        globalOrigin = (clipStartSec / secPerPx) + x();
    } else {
        secPerPx = clipLength / W;
    }
    const double clipEndT = clipSourceOffset + clipLength;

    // Prepare lists for batching
    QVector<qint64> starts(cols + 1);
    QVector<qint64> ends(cols + 1);
    QVector<AudioPeak> batchPeaks(cols + 1, AudioPeak{0.0f, 0.0f});

    for (int i = 0; i <= cols; ++i) {
        const int colX = renderStart + i;
        const float px = float(colX);

        double t0;
        if (m_secPerPixel > 0.0) {
            double globalX = globalOrigin + px;
            double alignedX = std::floor(globalX);
            double pxFromClipStart = alignedX - (clipStartSec / secPerPx);
            t0 = clipSourceOffset + pxFromClipStart * m_secPerPixel;
        } else {
            t0 = clipSourceOffset + px * secPerPx;
        }

        if (t0 >= clipEndT) {
            starts[i] = 0;
            ends[i] = 0;
            continue;
        }

        double t1 = t0 + (m_secPerPixel > 0.0 ? m_secPerPixel : secPerPx);
        const double sf0 = t0 * m_sampleRate;
        const double sf1 = t1 * m_sampleRate;
        starts[i] = qMax(qint64(0), qint64(sf0));
        ends[i] = qMax(starts[i] + 1, qint64(std::ceil(sf1)));
    }

    if (hasMipmap) {
        mipmap->getPeaksBatch(starts.constData(), ends.constData(), cols + 1, 0, batchPeaks.data());
    }

    // Set up sequential envelope lookups
    SequentialEnvelopeLookup envLookup(m_envData, m_numEnvNodes);
    SequentialEnvelopeLookup rmsEnvLookup(m_envData, m_numEnvNodes);

    for (int i = 0; i <= cols; ++i) {
        const int colX = renderStart + i;
        const float px = float(colX);
        const int vi = i * 2;

        double t0;
        float vx = px;
        if (m_secPerPixel > 0.0) {
            double globalX = globalOrigin + px;
            double alignedX = std::floor(globalX);
            vx = float(alignedX - globalOrigin);
            double pxFromClipStart = alignedX - (clipStartSec / secPerPx);
            t0 = clipSourceOffset + pxFromClipStart * m_secPerPixel;
        } else {
            t0 = clipSourceOffset + px * secPerPx;
        }

        // Default: transparent
        v[vi].set(vx, h, 0, 0, 0, 0);
        v[vi + 1].set(vx, h, 0, 0, 0, 0);

        if (t0 >= clipEndT) continue;

        AudioPeak peak = batchPeaks[i];
        bool hasData = hasMipmap;

        if (!hasData) {
            // Minimal placeholder
            uchar fr = uchar(m_fillColor.red());
            uchar fg = uchar(m_fillColor.green());
            uchar fb = uchar(m_fillColor.blue());
            uchar fa = 100;
            if (m_unipolar) {
                v[vi].set(vx, h - 2, fr, fg, fb, fa);
                v[vi + 1].set(vx, h, fr, fg, fb, fa);
            } else {
                v[vi].set(vx, midY - 1, fr, fg, fb, fa);
                v[vi + 1].set(vx, midY + 1, fr, fg, fb, fa);
            }
            continue;
        }

        float g = m_gain;
        if (m_numEnvNodes > 0) {
            g *= envLookup.gainAt(float(t0 - m_clipSourceOffset));
        }

        float maxA = std::clamp(peak.max * g, -1.0f, 1.0f);
        float minA = std::clamp(peak.min * g, -1.0f, 1.0f);
        float absMax = std::max(std::abs(maxA), std::abs(minA));

        // Color selection
        QColor col = m_fillColor;
        if (absMax > 0.95f) col = m_clipColor;
        else if (absMax > 0.70f) col = m_warnColor;

        float yTop, yBot;
        if (m_unipolar) {
            yTop = h - (absMax * h);
            yBot = h;
            if (yBot - yTop < 2.0f) yTop = h - 2.0f;
        } else {
            yTop = midY - (maxA * midY);
            yBot = midY - (minA * midY);
            if (std::abs(yTop - yBot) < 2.0f) {
                yTop = midY - 1.0f;
                yBot = midY + 1.0f;
            }
        }

        v[vi].set(vx, yTop, col.red(), col.green(), col.blue(), col.alpha());
        v[vi + 1].set(vx, yBot, col.red(), col.green(), col.blue(), col.alpha());
    }

    peakNode->markDirty(QSGNode::DirtyGeometry);
    m_geometryDirty = false;

    // ── RMS overlay (child node) ──
    QSGGeometryNode *rmsNode = nullptr;
    if (m_showRms && hasMipmap) {
        if (peakNode->childCount() > 0) {
            rmsNode = static_cast<QSGGeometryNode *>(peakNode->childAtIndex(0));
            QSGGeometry *rmsGeo = rmsNode->geometry();
            if (rmsGeo->vertexCount() != vtxCount) {
                rmsGeo->allocate(vtxCount);
            }
        } else {
            auto *rmsGeo = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), vtxCount);
            rmsGeo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            rmsNode = new QSGGeometryNode;
            rmsNode->setGeometry(rmsGeo);
            rmsNode->setFlag(QSGNode::OwnsGeometry);
            auto *rmsMat = new QSGVertexColorMaterial;
            rmsNode->setMaterial(rmsMat);
            rmsNode->setFlag(QSGNode::OwnsMaterial);
            peakNode->appendChildNode(rmsNode);
        }

        QSGGeometry::ColoredPoint2D *rv = rmsNode->geometry()->vertexDataAsColoredPoint2D();

        const uchar rr = uchar(m_rmsColor.red());
        const uchar rg = uchar(m_rmsColor.green());
        const uchar rb = uchar(m_rmsColor.blue());
        const uchar ra = uchar(m_rmsColor.alpha());

        for (int i = 0; i <= cols; ++i) {
            const int colX = renderStart + i;
            const float px = float(colX);
            const int vi = i * 2;

            double t0;
            float vx = px;
            if (m_secPerPixel > 0.0) {
                double globalX = globalOrigin + px;
                double alignedX = std::floor(globalX);
                vx = float(alignedX - globalOrigin);
                double pxFromClipStart = alignedX - (m_clipTimelineStart / secPerPx);
                t0 = m_clipSourceOffset + pxFromClipStart * m_secPerPixel;
            } else {
                t0 = m_clipSourceOffset + px * secPerPx;
            }

            if (t0 >= clipEndT) {
                rv[vi].set(vx, h, 0, 0, 0, 0);
                rv[vi + 1].set(vx, h, 0, 0, 0, 0);
                continue;
            }

            AudioPeak peak = batchPeaks[i];

            float g = m_gain;
            if (m_numEnvNodes > 0) {
                g *= rmsEnvLookup.gainAt(float(t0 - m_clipSourceOffset));
            }

            float absMax = std::max(std::abs(peak.max * g), std::abs(peak.min * g));
            absMax = std::clamp(absMax, 0.0f, 1.0f);
            float rmsApprox = absMax * 0.65f;

            float rTop, rBot;
            if (m_unipolar) {
                rTop = h - (rmsApprox * h);
                rBot = h;
                if (rBot - rTop < 1.0f) rTop = h - 1.0f;
            } else {
                rTop = midY - (rmsApprox * midY);
                rBot = midY + (rmsApprox * midY);
                if (std::abs(rTop - rBot) < 1.0f) {
                    rTop = midY - 0.5f;
                    rBot = midY + 0.5f;
                }
            }

            rv[vi].set(vx, rTop, rr, rg, rb, ra);
            rv[vi + 1].set(vx, rBot, rr, rg, rb, ra);
        }

        rmsNode->markDirty(QSGNode::DirtyGeometry);
    } else {
        while (peakNode->childCount() > 0) {
            peakNode->removeChildNode(peakNode->childAtIndex(0));
        }
    }

    return peakNode;
}
