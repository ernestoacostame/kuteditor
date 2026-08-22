#pragma once

#include "MasterEffect.h"
#include <vector>
#include <cstring>
#include <mutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <algorithm>

#ifdef HAVE_DEEPFILTERNET
#include "../../../3rdparty/deepfilternet/include/deep_filter.h"
#endif

/**
 * DeepFilterEffect: reducción de ruido mediante IA (DeepFilterNet).
 *
 * IMPORTANTE: df_process_frame() es stateful (STFT interno).
 * Cada canal necesita su propia instancia DFState.
 * El frame_length típico es 480 samples (10ms @ 48kHz).
 *
 * El modelo se descarga automáticamente la primera vez que el usuario
 * activa el efecto (lazy init).
 */
class DeepFilterEffect : public MasterEffect
{
    Q_OBJECT
    Q_PROPERTY(float reductionDb READ reductionDb WRITE setReductionDb NOTIFY changed)
    Q_PROPERTY(bool isReady READ isReady NOTIFY readyChanged)
    Q_PROPERTY(float downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)

public:
    explicit DeepFilterEffect(QObject *parent = nullptr) : MasterEffect(parent) {
        m_manager = new QNetworkAccessManager(this);
    }

    ~DeepFilterEffect() {
#ifdef HAVE_DEEPFILTERNET
        for (int ch = 0; ch < 2; ++ch) {
            if (m_dfState[ch]) df_free(m_dfState[ch]);
        }
#endif
    }

    float reductionDb() const { return m_reductionDb; }
    Q_INVOKABLE void setReductionDb(float v) {
        m_reductionDb = std::clamp(v, 0.0f, 100.0f);
#ifdef HAVE_DEEPFILTERNET
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int ch = 0; ch < 2; ++ch) {
            if (m_dfState[ch]) df_set_atten_lim(m_dfState[ch], m_reductionDb);
        }
#endif
        emit changed();
    }

    bool isReady() const { return m_isReady; }
    float downloadProgress() const { return m_downloadProgress; }

    Q_INVOKABLE void retryDownload() {
#ifdef HAVE_DEEPFILTERNET
        if (m_isReady || m_downloading) return;
        QString path = modelFilePath();
        if (QFile::exists(path)) {
            qDebug() << "[DeepFilter] Borrando modelo corrupto/incompleto:" << path;
            QFile::remove(path);
        }
        m_downloadProgress = 0.0f;
        emit downloadProgressChanged();
        initModel();
#endif
    }

    void setEnabled(bool v) override {
        MasterEffect::setEnabled(v);
#ifdef HAVE_DEEPFILTERNET
        if (v && !m_isReady && !m_initAttempted) {
            m_initAttempted = true;
            initModel();
        }
#endif
    }

    void process(float *buffer, int nFrames, int channels, int sampleRate) override
    {
#ifndef HAVE_DEEPFILTERNET
        Q_UNUSED(buffer); Q_UNUSED(nFrames); Q_UNUSED(channels); Q_UNUSED(sampleRate);
        return;
#else
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isReady || !m_dfState[0]) return;

        const int nCh = std::min(channels, 2);

        for (int ch = 0; ch < nCh; ++ch) {
            if (!m_dfState[ch]) continue;

            // De-interleave: extraer canal 'ch' al buffer de entrada
            for (int f = 0; f < nFrames; ++f) {
                m_inRing[ch][m_inWrite[ch]] = buffer[f * channels + ch];
                m_inWrite[ch] = (m_inWrite[ch] + 1) % RING_SIZE;
                m_inCount[ch]++;
            }

            // Procesar todos los bloques completos que tengamos
            while (m_inCount[ch] >= m_frameLength) {
                // Copiar un bloque del ring buffer de entrada
                for (int i = 0; i < m_frameLength; ++i) {
                    m_block[i] = m_inRing[ch][m_inRead[ch]];
                    m_inRead[ch] = (m_inRead[ch] + 1) % RING_SIZE;
                }
                m_inCount[ch] -= m_frameLength;

                // Procesar con DeepFilterNet (cada canal tiene su propio state)
                df_process_frame(m_dfState[ch], m_block, m_outBlock);

                // Copiar resultado al ring buffer de salida
                for (int i = 0; i < m_frameLength; ++i) {
                    m_outRing[ch][m_outWrite[ch]] = m_outBlock[i];
                    m_outWrite[ch] = (m_outWrite[ch] + 1) % RING_SIZE;
                    m_outCount[ch]++;
                }
            }

            // Re-interleave: escribir del buffer de salida al buffer interleaved
            for (int f = 0; f < nFrames; ++f) {
                if (m_outCount[ch] > 0) {
                    buffer[f * channels + ch] = m_outRing[ch][m_outRead[ch]];
                    m_outRead[ch] = (m_outRead[ch] + 1) % RING_SIZE;
                    m_outCount[ch]--;
                }
                // Si no hay output aún (latencia inicial), dejamos el sample original
            }
        }
#endif
    }

    void reset() override {
#ifdef HAVE_DEEPFILTERNET
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int ch = 0; ch < 2; ++ch) {
            m_inRead[ch] = m_inWrite[ch] = m_inCount[ch] = 0;
            m_outRead[ch] = m_outWrite[ch] = m_outCount[ch] = 0;
        }
#endif
    }

signals:
    void readyChanged();
    void downloadProgressChanged();
    void errorOccurred(const QString &msg);

private:
    QString modelFilePath() const {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + "/models/DeepFilterNet3_onnx.tar.gz";
    }

#ifdef HAVE_DEEPFILTERNET
    void initModel() {
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
        QDir().mkpath(dataPath);
        QString modelPath = modelFilePath();

        if (QFile::exists(modelPath)) {
            QFileInfo fi(modelPath);
            if (fi.size() < 1000) {
                qDebug() << "[DeepFilter] Archivo de modelo demasiado pequeño (" << fi.size() << " bytes), borrando...";
                QFile::remove(modelPath);
                downloadModel(modelPath);
                return;
            }
            loadModel(modelPath);
        } else {
            qDebug() << "[DeepFilter] Modelo no encontrado, iniciando descarga...";
            downloadModel(modelPath);
        }
    }

    bool loadModel(const QString &path) {
        qDebug() << "[DeepFilter] Cargando modelo desde:" << path;

        // Crear una instancia por canal
        for (int ch = 0; ch < 2; ++ch) {
            m_dfState[ch] = df_create(path.toUtf8().constData(), m_reductionDb);
            if (!m_dfState[ch]) {
                qDebug() << "[DeepFilter] ✗ df_create falló para canal" << ch;
                // Limpiar lo que se haya creado
                for (int j = 0; j <= ch; ++j) {
                    if (m_dfState[j]) { df_free(m_dfState[j]); m_dfState[j] = nullptr; }
                }
                return false;
            }
        }

        m_frameLength = (int)df_get_frame_length(m_dfState[0]);
        m_isReady = true;
        m_downloadProgress = 1.0f;

        // Inicializar ring buffers
        reset();

        qDebug() << "[DeepFilter] ✓ Modelo cargado. Frame length:" << m_frameLength
                 << "(" << (m_frameLength / 48.0) << "ms @ 48kHz)";
        emit readyChanged();
        emit downloadProgressChanged();
        return true;
    }

    void downloadModel(const QString &targetPath) {
        if (m_downloading) {
            qDebug() << "[DeepFilter] Descarga ya en curso, ignorando petición duplicada.";
            return;
        }
        m_downloading = true;
        m_downloadProgress = 0.01f;
        emit downloadProgressChanged();

        QNetworkRequest req(QUrl("https://github.com/Rikorose/DeepFilterNet/raw/main/models/DeepFilterNet3_onnx.tar.gz"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply *reply = m_manager->get(req);
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
            if (bytesTotal > 0) {
                m_downloadProgress = (float)bytesReceived / bytesTotal;
                emit downloadProgressChanged();
            }
        });

        connect(reply, &QNetworkReply::finished, this, [this, reply, targetPath]() {
            m_downloading = false;
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray data = reply->readAll();
                qDebug() << "[DeepFilter] Descarga completa:" << data.size() << "bytes";

                if (data.size() < 1000) {
                    qDebug() << "[DeepFilter] ✗ Descarga demasiado pequeña.";
                    m_downloadProgress = 0.0f;
                    emit downloadProgressChanged();
                    emit errorOccurred("La descarga falló (archivo demasiado pequeño). Intenta de nuevo.");
                    reply->deleteLater();
                    return;
                }

                QFile f(targetPath);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(data);
                    f.close();
                    if (!loadModel(targetPath)) {
                        qDebug() << "[DeepFilter] ✗ loadModel falló tras descarga.";
                        QFile::remove(targetPath);
                        m_downloadProgress = 0.0f;
                        emit downloadProgressChanged();
                        emit errorOccurred("El archivo descargado está corrupto o no se pudo cargar.");
                    }
                }
            } else {
                qDebug() << "[DeepFilter] ✗ Error de red:" << reply->errorString();
                m_downloadProgress = 0.0f;
                emit downloadProgressChanged();
                emit errorOccurred("Error de red: " + reply->errorString());
            }
            reply->deleteLater();
        });
    }

    // --- Estado DeepFilterNet ---
    // Una instancia por canal (L, R) — el STFT interno es stateful
    DFState* m_dfState[2] = { nullptr, nullptr };
    int m_frameLength = 480;
    std::mutex m_mutex;

    // Ring buffers estáticos para evitar allocations en el hilo de audio.
    // 48000 samples = 1 segundo de buffer, más que suficiente.
    static constexpr int RING_SIZE = 48000;
    float m_inRing[2][RING_SIZE] = {};
    int m_inRead[2] = {0, 0};
    int m_inWrite[2] = {0, 0};
    int m_inCount[2] = {0, 0};

    float m_outRing[2][RING_SIZE] = {};
    int m_outRead[2] = {0, 0};
    int m_outWrite[2] = {0, 0};
    int m_outCount[2] = {0, 0};

    // Bloques temporales para process (evitar allocation por frame)
    float m_block[480] = {};
    float m_outBlock[480] = {};

    bool m_downloading = false;
    bool m_initAttempted = false;
#endif

    QNetworkAccessManager *m_manager = nullptr;
    float m_reductionDb = 40.0f;
    bool m_isReady = false;
    float m_downloadProgress = 0.0f;
};
