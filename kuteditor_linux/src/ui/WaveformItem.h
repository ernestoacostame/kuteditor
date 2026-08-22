#pragma once

#include <QQuickItem>
#include <QColor>
#include <QPointer>
#include "TrackModel.h"

/**
 * WaveformItem — High-performance waveform visualization.
 *
 * Renders audio waveforms using the Qt Scene Graph (QSGGeometryNode)
 * with GPU rasterization via triangle strip geometry.
 *
 * Features:
 *   - Viewport culling (only visible columns generate geometry)
 *   - RMS overlay (inner "body" of the waveform, darker shade)
 *   - Clip indicators (red ticks at top/bottom for columns > 0dB)
 *   - Sub-pixel precision for smooth scrolling
 *   - Envelope (volume automation) support
 */
class WaveformItem : public QQuickItem {
    Q_OBJECT

    Q_PROPERTY(TrackModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int trackIndex READ trackIndex WRITE setTrackIndex NOTIFY trackIndexChanged)
    Q_PROPERTY(int clipIndex READ clipIndex WRITE setClipIndex NOTIFY clipIndexChanged)

    Q_PROPERTY(double timelineScrollX READ timelineScrollX WRITE setTimelineScrollX NOTIFY viewportChanged)
    Q_PROPERTY(double timelineZoom READ timelineZoom WRITE setTimelineZoom NOTIFY viewportChanged)
    Q_PROPERTY(double secPerPixel READ secPerPixel WRITE setSecPerPixel NOTIFY secPerPixelChanged)

    Q_PROPERTY(double clipTimelineStart READ clipTimelineStart WRITE setClipTimelineStart NOTIFY viewportChanged)
    Q_PROPERTY(double clipSourceOffset READ clipSourceOffset WRITE setClipSourceOffset NOTIFY viewportChanged)
    Q_PROPERTY(double clipLength READ clipLength WRITE setClipLength NOTIFY viewportChanged)

    Q_PROPERTY(double visibleStartPx READ visibleStartPx WRITE setVisibleStartPx NOTIFY viewportChanged)
    Q_PROPERTY(double visibleEndPx READ visibleEndPx WRITE setVisibleEndPx NOTIFY viewportChanged)

    Q_PROPERTY(float gain READ gain WRITE setGain NOTIFY gainChanged)
    Q_PROPERTY(QVariantList envelope READ envelope WRITE setEnvelope NOTIFY envelopeChanged)
    Q_PROPERTY(bool liveMode READ liveMode WRITE setLiveMode NOTIFY liveModeChanged)
    Q_PROPERTY(bool unipolar READ unipolar WRITE setUnipolar NOTIFY unipolarChanged)
    Q_PROPERTY(double sampleRate READ sampleRate WRITE setSampleRate NOTIFY sampleRateChanged)

    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor warnColor READ warnColor WRITE setWarnColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor clipColor READ clipColor WRITE setClipColor NOTIFY colorsChanged)

    Q_PROPERTY(QColor rmsColor READ rmsColor WRITE setRmsColor NOTIFY colorsChanged)
    Q_PROPERTY(bool showRms READ showRms WRITE setShowRms NOTIFY colorsChanged)

public:
    explicit WaveformItem(QQuickItem *parent = nullptr);
    ~WaveformItem() override;

    // ── Property accessors (QML API) ──
    TrackModel* model() const { return m_model; }
    void setModel(TrackModel* m);
    int trackIndex() const { return m_trackIndex; }
    void setTrackIndex(int i);
    int clipIndex() const { return m_clipIndex; }
    void setClipIndex(int i);
    double timelineScrollX() const { return m_timelineScrollX; }
    void setTimelineScrollX(double x);
    double timelineZoom() const { return m_timelineZoom; }
    void setTimelineZoom(double z);
    double secPerPixel() const { return m_secPerPixel; }
    void setSecPerPixel(double s);
    double clipTimelineStart() const { return m_clipTimelineStart; }
    void setClipTimelineStart(double s);
    double clipSourceOffset() const { return m_clipSourceOffset; }
    void setClipSourceOffset(double o);
    double clipLength() const { return m_clipLength; }
    void setClipLength(double l);
    double visibleStartPx() const { return m_visibleStartPx; }
    void setVisibleStartPx(double v);
    double visibleEndPx() const { return m_visibleEndPx; }
    void setVisibleEndPx(double v);
    float gain() const { return m_gain; }
    void setGain(float g);
    QVariantList envelope() const { return m_envelopeList; }
    void setEnvelope(const QVariantList &env);
    bool liveMode() const { return m_liveMode; }
    void setLiveMode(bool l);
    bool unipolar() const { return m_unipolar; }
    void setUnipolar(bool u);
    double sampleRate() const { return m_sampleRate; }
    void setSampleRate(double sr);
    QColor fillColor() const { return m_fillColor; }
    void setFillColor(const QColor &c);
    QColor warnColor() const { return m_warnColor; }
    void setWarnColor(const QColor &c);
    QColor clipColor() const { return m_clipColor; }
    void setClipColor(const QColor &c);
    QColor rmsColor() const { return m_rmsColor; }
    void setRmsColor(const QColor &c);
    bool showRms() const { return m_showRms; }
    void setShowRms(bool v);

signals:
    void modelChanged();
    void trackIndexChanged();
    void clipIndexChanged();
    void viewportChanged();
    void secPerPixelChanged();
    void gainChanged();
    void envelopeChanged();
    void liveModeChanged();
    void unipolarChanged();
    void sampleRateChanged();
    void colorsChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeo, const QRectF &oldGeo) override;

private:
    void updateMipmapConnection();
    float envelopeGainAt(float t) const;

    std::shared_ptr<AudioMipmap> m_currentMipmap;

    TrackModel *m_model = nullptr;
    int m_trackIndex = -1;
    int m_clipIndex = -1;

    double m_timelineScrollX = 0;
    double m_timelineZoom = 1.0;
    double m_secPerPixel = 0.0;
    double m_clipTimelineStart = 0;
    double m_clipSourceOffset = 0;
    double m_clipLength = 0;
    double m_visibleStartPx = 0;
    double m_visibleEndPx = 1e9;

    float m_gain = 1.0f;
    QVariantList m_envelopeList;
    double m_sampleRate = 48000.0;
    bool m_liveMode = false;
    bool m_unipolar = true;

    QColor m_fillColor{255, 255, 255, 140};
    QColor m_warnColor{250, 209, 56, 160};
    QColor m_clipColor{242, 77, 61, 180};
    QColor m_rmsColor{180, 210, 255, 120};
    bool m_showRms = true;

    bool m_geometryDirty = true;

    // Envelope cache
    QVector<float> m_envData;
    int m_numEnvNodes = 0;
};
