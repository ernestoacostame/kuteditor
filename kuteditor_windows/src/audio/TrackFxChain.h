#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMetaProperty>
#include <QVector>
#include <QString>

#include "fx/MasterEffect.h"
#include "fx/HighPassFilter.h"
#include "fx/LowPassFilter.h"
#include "fx/NotchFilter.h"
#include "fx/RNNoiseEffect.h"
#include "fx/NoiseGateEffect.h"
#include "fx/DeepFilterEffect.h"
#include "fx/DeEsserEffect.h"
#include "fx/ExpanderEffect.h"
#include "fx/CompressorEffect.h"
#include "fx/EqualizerEffect.h"
#include "fx/AutoGainEffect.h"
#include "fx/AutoDuckEffect.h"
#include "fx/LimiterEffect.h"
#include "fx/StereoWidener.h"
#include "fx/PhaseInvertEffect.h"
#include "fx/MonoMixerEffect.h"
#include "fx/TrimGainEffect.h"

/**
 * TrackFxChain: cadena de efectos instanciada por pista.
 *
 * Cada pista tiene su propia instancia con todos los efectos
 * disponibles. Se procesan en orden fijo:
 *   Trim → Phase → HPF → LPF → Notch → DeNoiser → Gate →
 *   Expander → Compressor → DeEsser → EQ → AutoGain →
 *   AutoDuck → Widener → Mono → Limiter
 *
 * Todos los efectos arrancan desactivados. Se expone a QML
 * para configurar desde la ventana FX.
 */
class TrackFxChain : public QObject
{
    Q_OBJECT

    Q_PROPERTY(TrimGainEffect*     trimGain      READ trimGain      CONSTANT)
    Q_PROPERTY(PhaseInvertEffect*  phaseInvert   READ phaseInvert   CONSTANT)
    Q_PROPERTY(HighPassFilter*     highPassFilter READ highPassFilter CONSTANT)
    Q_PROPERTY(LowPassFilter*      lowPassFilter  READ lowPassFilter  CONSTANT)
    Q_PROPERTY(NotchFilter*        notchFilter    READ notchFilter    CONSTANT)
    Q_PROPERTY(RNNoiseEffect*      deNoiser       READ deNoiser       CONSTANT)
    Q_PROPERTY(DeepFilterEffect*   deepFilter     READ deepFilter     CONSTANT)
    Q_PROPERTY(NoiseGateEffect*    noiseGate      READ noiseGate      CONSTANT)
    Q_PROPERTY(ExpanderEffect*     expander       READ expander       CONSTANT)
    Q_PROPERTY(CompressorEffect*   compressor     READ compressor     CONSTANT)
    Q_PROPERTY(DeEsserEffect*      deEsser        READ deEsser        CONSTANT)
    Q_PROPERTY(EqualizerEffect*    equalizer      READ equalizer      CONSTANT)
    Q_PROPERTY(AutoGainEffect*     autoGain       READ autoGain       CONSTANT)
    Q_PROPERTY(AutoDuckEffect*     autoDuck       READ autoDuck       CONSTANT)
    Q_PROPERTY(StereoWidener*      stereoWidener  READ stereoWidener  CONSTANT)
    Q_PROPERTY(MonoMixerEffect*    monoMixer      READ monoMixer      CONSTANT)
    Q_PROPERTY(LimiterEffect*      limiter        READ limiter        CONSTANT)

    // Lista de nombres de efectos activos (para mostrar en UI)
    Q_PROPERTY(QStringList activeEffectNames READ activeEffectNames NOTIFY changed)

public:
    explicit TrackFxChain(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_trimGain      = new TrimGainEffect(this);
        m_phaseInvert   = new PhaseInvertEffect(this);
        m_highPassFilter = new HighPassFilter(this);
        m_lowPassFilter  = new LowPassFilter(this);
        m_notchFilter    = new NotchFilter(this);
        m_deNoiser       = new RNNoiseEffect(this);
        m_deepFilter     = new DeepFilterEffect(this);
        m_noiseGate      = new NoiseGateEffect(this);
        m_expander       = new ExpanderEffect(this);
        m_compressor     = new CompressorEffect(this);
        m_deEsser        = new DeEsserEffect(this);
        m_equalizer      = new EqualizerEffect(this);
        m_autoGain       = new AutoGainEffect(this);
        m_autoDuck       = new AutoDuckEffect(this);
        m_stereoWidener  = new StereoWidener(this);
        m_monoMixer      = new MonoMixerEffect(this);
        m_limiter        = new LimiterEffect(this);

        m_chain = {
            m_trimGain, m_phaseInvert, m_highPassFilter, m_lowPassFilter,
            m_notchFilter, m_deNoiser, m_deepFilter, m_noiseGate, m_expander,
            m_compressor, m_deEsser, m_equalizer, m_autoGain,
            m_autoDuck, m_stereoWidener, m_monoMixer, m_limiter
        };

        m_names = QStringList({
            "Ganancia (Trim)",
            "Inversión de fase",
            "Filtro paso alto",
            "Filtro paso bajo",
            "Filtro Notch",
            "Reductor de ruido",
            "Deep Denoise",
            "Puerta de ruido",
            "Expansor",
            "Compresor",
            "De-esser (Sibilantes)",
            "Ecualizador",
            "Ganancia automática",
            "Auto Duck",
            "Ensanchador estéreo",
            "Mezcla mono",
            "Limitador"
        });

        // Conectar changed de cada efecto
        for (auto *fx : m_chain)
            connect(fx, &MasterEffect::changed, this, &TrackFxChain::changed);
    }

    // Accesores
    TrimGainEffect*     trimGain()      const { return m_trimGain; }
    PhaseInvertEffect*  phaseInvert()   const { return m_phaseInvert; }
    HighPassFilter*     highPassFilter() const { return m_highPassFilter; }
    LowPassFilter*      lowPassFilter()  const { return m_lowPassFilter; }
    NotchFilter*        notchFilter()    const { return m_notchFilter; }
    RNNoiseEffect*      deNoiser()       const { return m_deNoiser; }
    DeepFilterEffect*   deepFilter()     const { return m_deepFilter; }
    NoiseGateEffect*    noiseGate()      const { return m_noiseGate; }
    ExpanderEffect*     expander()       const { return m_expander; }
    CompressorEffect*   compressor()     const { return m_compressor; }
    DeEsserEffect*      deEsser()        const { return m_deEsser; }
    EqualizerEffect*    equalizer()      const { return m_equalizer; }
    AutoGainEffect*     autoGain()       const { return m_autoGain; }
    AutoDuckEffect*     autoDuck()       const { return m_autoDuck; }
    StereoWidener*      stereoWidener()  const { return m_stereoWidener; }
    MonoMixerEffect*    monoMixer()      const { return m_monoMixer; }
    LimiterEffect*      limiter()        const { return m_limiter; }

    /// Procesar buffer estéreo in-place. Llamado desde AudioEngine.
    void process(float *buffer, int nFrames, int channels, int sampleRate)
    {
        for (auto *fx : m_chain) {
            if (fx->enabled())
                fx->process(buffer, nFrames, channels, sampleRate);
        }
    }

    /// Resetear todos los efectos (al iniciar playback).
    void resetAll()
    {
        for (auto *fx : m_chain) fx->reset();
    }

    /// ¿Hay algún efecto activo?
    Q_INVOKABLE bool hasActiveEffects() const
    {
        for (auto *fx : m_chain)
            if (fx->enabled()) return true;
        return false;
    }

    /// Número de efectos activos
    Q_INVOKABLE int activeCount() const
    {
        int c = 0;
        for (auto *fx : m_chain)
            if (fx->enabled()) c++;
        return c;
    }

    /// Lista de nombres de efectos activos
    QStringList activeEffectNames() const
    {
        QStringList out;
        for (int i = 0; i < m_chain.size(); ++i)
            if (m_chain[i]->enabled()) out << m_names[i];
        return out;
    }

    /// Acceso por índice (para UI tipo lista)
    Q_INVOKABLE int effectCount() const { return m_chain.size(); }
    Q_INVOKABLE QString effectName(int i) const {
        return (i >= 0 && i < m_names.size()) ? m_names[i] : QString();
    }
    Q_INVOKABLE bool effectEnabled(int i) const {
        return (i >= 0 && i < m_chain.size()) ? m_chain[i]->enabled() : false;
    }
    Q_INVOKABLE void setEffectEnabled(int i, bool v) {
        if (i >= 0 && i < m_chain.size()) m_chain[i]->setEnabled(v);
    }
    Q_INVOKABLE QObject* effectAt(int i) const {
        return (i >= 0 && i < m_chain.size()) ? m_chain[i] : nullptr;
    }

    /// Serializar estado a JSON (para presets y guardado de proyecto)
    Q_INVOKABLE QJsonObject toJson() const
    {
        QJsonObject obj;
        for (int i = 0; i < m_chain.size(); ++i) {
            QJsonObject fx;
            fx["enabled"] = m_chain[i]->enabled();
            // Serializar todas las Q_PROPERTYs del efecto
            const QMetaObject *mo = m_chain[i]->metaObject();
            for (int p = mo->propertyOffset(); p < mo->propertyCount(); ++p) {
                QMetaProperty prop = mo->property(p);
                if (QString(prop.name()) == "objectName") continue;
                if (QString(prop.name()) == "enabled") continue;
                if (QString(prop.name()) == "gainReduction") continue;
                if (QString(prop.name()) == "currentGainDb") continue;
                QVariant val = prop.read(m_chain[i]);
                fx[prop.name()] = QJsonValue::fromVariant(val);
            }
            obj[m_names[i]] = fx;
        }
        return obj;
    }

    /// Restaurar estado desde JSON
    Q_INVOKABLE void fromJson(const QJsonObject &obj)
    {
        for (int i = 0; i < m_chain.size(); ++i) {
            if (!obj.contains(m_names[i])) continue;
            QJsonObject fx = obj[m_names[i]].toObject();
            if (fx.contains("enabled"))
                m_chain[i]->setEnabled(fx["enabled"].toBool());
            const QMetaObject *mo = m_chain[i]->metaObject();
            for (int p = mo->propertyOffset(); p < mo->propertyCount(); ++p) {
                QMetaProperty prop = mo->property(p);
                if (!prop.isWritable()) continue;
                QString name = prop.name();
                if (name == "objectName" || name == "enabled") continue;
                if (fx.contains(name)) {
                    prop.write(m_chain[i], fx[name].toVariant());
                }
            }
        }
        emit changed();
    }

    /// Merge: solo aplica efectos habilitados del preset, sin tocar los demás.
    /// Permite cargar varios presets en secuencia y acumular efectos activos.
    Q_INVOKABLE void mergeJson(const QJsonObject &obj)
    {
        for (int i = 0; i < m_chain.size(); ++i) {
            if (!obj.contains(m_names[i])) continue;
            QJsonObject fx = obj[m_names[i]].toObject();
            // Solo aplicar si el efecto está habilitado en el preset
            if (!fx.value("enabled").toBool(false)) continue;
            m_chain[i]->setEnabled(true);
            const QMetaObject *mo = m_chain[i]->metaObject();
            for (int p = mo->propertyOffset(); p < mo->propertyCount(); ++p) {
                QMetaProperty prop = mo->property(p);
                if (!prop.isWritable()) continue;
                QString name = prop.name();
                if (name == "objectName" || name == "enabled") continue;
                if (fx.contains(name)) {
                    prop.write(m_chain[i], fx[name].toVariant());
                }
            }
        }
        emit changed();
    }

signals:
    void changed();

private:
    QVector<MasterEffect*> m_chain;
    QStringList m_names;

    TrimGainEffect     *m_trimGain = nullptr;
    PhaseInvertEffect  *m_phaseInvert = nullptr;
    HighPassFilter     *m_highPassFilter = nullptr;
    LowPassFilter      *m_lowPassFilter = nullptr;
    NotchFilter        *m_notchFilter = nullptr;
    RNNoiseEffect      *m_deNoiser = nullptr;
    DeepFilterEffect   *m_deepFilter = nullptr;
    NoiseGateEffect    *m_noiseGate = nullptr;
    ExpanderEffect     *m_expander = nullptr;
    CompressorEffect   *m_compressor = nullptr;
    DeEsserEffect      *m_deEsser = nullptr;
    EqualizerEffect    *m_equalizer = nullptr;
    AutoGainEffect     *m_autoGain = nullptr;
    AutoDuckEffect     *m_autoDuck = nullptr;
    StereoWidener      *m_stereoWidener = nullptr;
    MonoMixerEffect    *m_monoMixer = nullptr;
    LimiterEffect      *m_limiter = nullptr;
};
