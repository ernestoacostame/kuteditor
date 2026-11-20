#pragma once
#include <QObject>
#include <QVector>
#include <QFuture>

class RNNoiseWorker : public QObject {
    Q_OBJECT
public:
    explicit RNNoiseWorker(QObject *parent = nullptr);
    ~RNNoiseWorker();

    void processOffline(int trackIndex, int sourceIdx, const QVector<float>& inSamples, int sampleRate, int channels);
    bool isProcessing() const { return m_processing; }

signals:
    void processingStarted(int trackIndex, int sourceIdx);
    void processingFinished(int trackIndex, int sourceIdx, const QVector<float>& outSamples);
    void progressUpdated(int trackIndex, int sourceIdx, float progress);

private:
    bool m_processing = false;
    QFuture<QVector<float>> m_future;
};
