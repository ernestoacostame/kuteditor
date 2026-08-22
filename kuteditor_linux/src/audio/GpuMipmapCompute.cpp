#include "GpuMipmapCompute.h"
#include "AudioMipmap.h"

#include <QDebug>
#include <QFile>
#include <QVulkanInstance>
#include <QElapsedTimer>

#include <rhi/qrhi.h>

// ─── Singleton ───

GpuMipmapCompute &GpuMipmapCompute::instance() {
    static GpuMipmapCompute s_instance;
    return s_instance;
}

GpuMipmapCompute::GpuMipmapCompute() {}

GpuMipmapCompute::~GpuMipmapCompute() {
    delete m_shader;
    delete m_rhi;
}

// ─── Lazy initialization ───

bool GpuMipmapCompute::initialize() {
    if (m_initAttempted) return m_available;
    m_initAttempted = true;

    qDebug() << "[GpuMipmapCompute] Initializing headless Vulkan compute...";

    // Create a dedicated QVulkanInstance for headless compute
    m_vulkanInstance = std::make_unique<QVulkanInstance>();
    m_vulkanInstance->setApiVersion(QVersionNumber(1, 1));

    const auto exts = QRhiVulkanInitParams::preferredInstanceExtensions();
    m_vulkanInstance->setExtensions(exts);

    if (!m_vulkanInstance->create()) {
        qWarning() << "[GpuMipmapCompute] Failed to create QVulkanInstance";
        return false;
    }

    // Create headless QRhi (no window — purely for compute)
    QRhiVulkanInitParams vkParams;
    vkParams.inst = m_vulkanInstance.get();

    m_rhi = QRhi::create(QRhi::Vulkan, &vkParams);
    if (!m_rhi) {
        qWarning() << "[GpuMipmapCompute] Failed to create QRhi Vulkan instance";
        return false;
    }

    if (!m_rhi->isFeatureSupported(QRhi::Compute)) {
        qWarning() << "[GpuMipmapCompute] Compute not supported";
        delete m_rhi; m_rhi = nullptr;
        return false;
    }

    // Load the compute shader (.qsb)
    QFile shaderFile(QStringLiteral(":/shaders/src/shaders/mipmap_build.comp.qsb"));
    if (!shaderFile.open(QIODevice::ReadOnly)) {
        qWarning() << "[GpuMipmapCompute] Cannot open mipmap_build.comp.qsb";
        delete m_rhi; m_rhi = nullptr;
        return false;
    }

    m_shader = new QShader(QShader::fromSerialized(shaderFile.readAll()));
    if (!m_shader->isValid()) {
        qWarning() << "[GpuMipmapCompute] Invalid compute shader";
        delete m_shader; m_shader = nullptr;
        delete m_rhi; m_rhi = nullptr;
        return false;
    }

    m_available = true;
    qDebug() << "[GpuMipmapCompute] ✓ GPU:" << m_rhi->driverInfo().deviceName
             << "| Compute ready";
    return true;
}

// ─── Build mipmap on GPU ───

bool GpuMipmapCompute::buildMipmap(const float *samples, qint64 totalFrames,
                                    int channels, int /*sampleRate*/,
                                    QVector<MipmapLevel> &outLevels) {
    QMutexLocker lock(&m_mutex);

    if (!initialize()) return false;
    if (!samples || totalFrames <= 0 || channels <= 0) return false;

    // Skip GPU for tiny files (overhead not worth it)
    if (totalFrames < 4096) return false;

    QElapsedTimer timer;
    timer.start();

    // ── Compute level layout ──
    struct LevelLayout {
        int blockSize;
        qint64 numBlocks;
        qint64 offset; // offset in packed mipmap buffer (vec2 units)
    };
    QVector<LevelLayout> layouts;
    qint64 totalMipmapEntries = 0;

    for (int l = 0; l < MAX_LEVELS; ++l) {
        int blockSize = BASE_BLOCK;
        for (int i = 0; i < l; ++i) blockSize *= LOD_RATIO;
        if (blockSize >= totalFrames / 2 && l > 0) break;

        LevelLayout lay;
        lay.blockSize = blockSize;
        lay.numBlocks = (totalFrames + blockSize - 1) / blockSize;
        lay.offset = totalMipmapEntries;
        layouts.append(lay);
        totalMipmapEntries += lay.numBlocks;
    }

    const qint64 samplesBytes = totalFrames * channels * qint64(sizeof(float));
    const qint64 mipmapBytes  = totalMipmapEntries * 2 * qint64(sizeof(float)); // vec2

    // Safety: cap at 2GB upload
    if (samplesBytes > qint64(2) * 1024 * 1024 * 1024) {
        qDebug() << "[GpuMipmapCompute] File too large, falling back to CPU";
        return false;
    }

    // ── Create GPU buffers (recreated each call for simplicity) ──
    std::unique_ptr<QRhiBuffer> samplesSSBO(
        m_rhi->newBuffer(QRhiBuffer::Immutable,
                         QRhiBuffer::StorageBuffer,
                         quint32(samplesBytes)));
    if (!samplesSSBO->create()) {
        qWarning() << "[GpuMipmapCompute] samplesSSBO create failed";
        return false;
    }

    std::unique_ptr<QRhiBuffer> mipmapSSBO(
        m_rhi->newBuffer(QRhiBuffer::Immutable,
                         QRhiBuffer::StorageBuffer,
                         quint32(mipmapBytes)));
    if (!mipmapSSBO->create()) {
        qWarning() << "[GpuMipmapCompute] mipmapSSBO create failed";
        return false;
    }

    // UBO for dispatch params (std140: 9 uint = 36 bytes, aligned to 48)
    // std140 packs uint as 4 bytes each, no special padding for scalars
    static constexpr quint32 UBO_SIZE = 48; // 9 * 4 = 36, rounded to 16-byte boundary
    std::unique_ptr<QRhiBuffer> paramsUBO(
        m_rhi->newBuffer(QRhiBuffer::Dynamic,
                         QRhiBuffer::UniformBuffer,
                         UBO_SIZE));
    if (!paramsUBO->create()) {
        qWarning() << "[GpuMipmapCompute] paramsUBO create failed";
        return false;
    }

    // ── Prepare output ──
    outLevels.clear();
    outLevels.resize(layouts.size());
    for (int l = 0; l < layouts.size(); ++l) {
        outLevels[l].blockSize = layouts[l].blockSize;
        outLevels[l].peaks.resize(layouts[l].numBlocks * channels);
    }

    // ── Process each channel ──
    for (int ch = 0; ch < channels; ++ch) {

        // Begin offscreen frame
        QRhiCommandBuffer *cb = nullptr;
        if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
            qWarning() << "[GpuMipmapCompute] beginOffscreenFrame failed";
            return false;
        }

        // Upload samples (once per channel — same data, different channel param)
        if (ch == 0) {
            QRhiResourceUpdateBatch *upload = m_rhi->nextResourceUpdateBatch();
            upload->uploadStaticBuffer(samplesSSBO.get(), 0, quint32(samplesBytes), samples);
            cb->resourceUpdate(upload);
        }

        // Dispatch compute for each level
        for (int l = 0; l < layouts.size(); ++l) {
            const auto &lay = layouts[l];

            // Update UBO
            quint32 uboData[12] = {}; // padded to 48 bytes
            uboData[0] = quint32(totalFrames);
            uboData[1] = quint32(channels);
            uboData[2] = quint32(lay.blockSize);
            uboData[3] = quint32(ch);
            uboData[4] = (l == 0) ? 0u : 1u;
            uboData[5] = (l > 0) ? quint32(layouts[l-1].blockSize) : 0u;
            uboData[6] = quint32(lay.numBlocks);
            uboData[7] = quint32(lay.offset);
            uboData[8] = (l > 0) ? quint32(layouts[l-1].offset) : 0u;

            QRhiResourceUpdateBatch *uboUpdate = m_rhi->nextResourceUpdateBatch();
            uboUpdate->updateDynamicBuffer(paramsUBO.get(), 0, UBO_SIZE, uboData);
            cb->resourceUpdate(uboUpdate);

            // Create SRB
            std::unique_ptr<QRhiShaderResourceBindings> srb(
                m_rhi->newShaderResourceBindings());
            srb->setBindings({
                QRhiShaderResourceBinding::bufferLoad(
                    0, QRhiShaderResourceBinding::ComputeStage,
                    samplesSSBO.get()),
                QRhiShaderResourceBinding::bufferLoadStore(
                    1, QRhiShaderResourceBinding::ComputeStage,
                    mipmapSSBO.get()),
                QRhiShaderResourceBinding::uniformBuffer(
                    2, QRhiShaderResourceBinding::ComputeStage,
                    paramsUBO.get()),
            });
            if (!srb->create()) {
                qWarning() << "[GpuMipmapCompute] SRB failed level" << l;
                m_rhi->endOffscreenFrame();
                return false;
            }

            // Create compute pipeline
            std::unique_ptr<QRhiComputePipeline> pipeline(m_rhi->newComputePipeline());
            pipeline->setShaderStage(QRhiShaderStage(QRhiShaderStage::Compute, *m_shader));
            pipeline->setShaderResourceBindings(srb.get());
            if (!pipeline->create()) {
                qWarning() << "[GpuMipmapCompute] Pipeline failed level" << l;
                m_rhi->endOffscreenFrame();
                return false;
            }

            // Dispatch
            // In both phases, the compute shader uses gl_WorkGroupID.x as blockIdx
            // and writes exactly one output block per workgroup.
            int numWorkgroups = int(lay.numBlocks);

            cb->beginComputePass();
            cb->setComputePipeline(pipeline.get());
            cb->setShaderResources(srb.get());
            cb->dispatch(numWorkgroups, 1, 1);
            cb->endComputePass();
        }

        // Read back mipmap data
        QRhiReadbackResult readResult;
        bool readCompleted = false;
        readResult.completed = [&readCompleted]() { readCompleted = true; };

        QRhiResourceUpdateBatch *readBatch = m_rhi->nextResourceUpdateBatch();
        readBatch->readBackBuffer(mipmapSSBO.get(), 0, quint32(mipmapBytes), &readResult);
        cb->resourceUpdate(readBatch);

        // End frame — this submits and waits for completion
        m_rhi->endOffscreenFrame();

        if (!readCompleted || readResult.data.size() < mipmapBytes) {
            qWarning() << "[GpuMipmapCompute] Readback failed ch" << ch
                       << "completed:" << readCompleted
                       << "size:" << readResult.data.size()
                       << "expected:" << mipmapBytes;
            return false;
        }

        // Parse readback into output levels
        const float *mipmapData = reinterpret_cast<const float *>(readResult.data.constData());
        for (int l = 0; l < layouts.size(); ++l) {
            const auto &lay = layouts[l];
            for (qint64 b = 0; b < lay.numBlocks; ++b) {
                qint64 idx = (lay.offset + b) * 2;
                AudioPeak peak;
                peak.max = mipmapData[idx];
                peak.min = mipmapData[idx + 1];
                outLevels[l].peaks[b * channels + ch] = peak;
            }
        }
    }

    qint64 elapsed = timer.elapsed();
    qDebug() << "[GpuMipmapCompute] ✓ GPU mipmap:" << elapsed << "ms |"
             << totalFrames << "frames |" << layouts.size() << "levels |"
             << channels << "ch";

    return true;
}
