#pragma once

#include <QObject>
#include <QUndoStack>
#include <QUndoCommand>
#include <functional>
#include <QString>
#include <QVariant>

/**
 * UndoManager: fachada QML del QUndoStack.
 *
 * Modo de uso desde QML:
 *   1. Construir un objeto de operación (map o directamente llamando a un
 *      método invocable que internamente construye un UndoableOp).
 *   2. El modelo ejecuta la operación y envuelve redo/undo en un command.
 *
 * En este proyecto, el TrackModel expone operaciones "atómicas" no undoables
 * (ej: splitClipsAt, deleteRegion, moveClips, ...). Los wrappers undoables
 * viven en UndoManager: calcula el "antes", ejecuta, y guarda en el stack
 * una pareja redo/undo en forma de lambdas.
 *
 * Para esta primera iteración implementamos:
 *   - split
 *   - deleteRegion
 *   - moveClips
 *   - removeClips (incluye Supr sobre clips seleccionados)
 *   - renameTrack
 *   - setGain (con merging por pista)
 *   - toggleMute / toggleSolo / toggleArmed
 *   - addTrack / removeTrack
 */

class TrackModel;

class UndoManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stackChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY stackChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY stackChanged)
public:
    explicit UndoManager(QObject *parent = nullptr);

    void setTrackModel(TrackModel *model) { m_trackModel = model; }
    void setChapterModel(class ChapterModel *model) { m_chapterModel = model; }

    bool canUndo() const { return m_stack.canUndo(); }
    bool canRedo() const { return m_stack.canRedo(); }
    QString undoText() const { return m_stack.undoText(); }
    QString redoText() const { return m_stack.redoText(); }

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clear();

    // Operaciones undoables. Cada una hace: capturar estado previo, aplicar
    // cambio al modelo, y pushear un command al stack.
    Q_INVOKABLE void splitAt(double atSec, const QVariant &trackVal = QVariant());
    Q_INVOKABLE void deleteRegion(double startSec, double endSec,
                                  bool rippleClose = true, const QVariant &tracksVal = QVariant());

    /// Borra las regiones marcadas por-pista (Shift+drag). regions es una
    /// lista de QVariantMap con { track, start, end }. Reversible.
    Q_INVOKABLE void deleteTrackRegions(const QVariantList &regions,
                                        bool rippleClose = false);
    Q_INVOKABLE void deleteClipsInTrack(int trackIndex, const QVariantList &clipIndices,
                                        bool rippleClose = false);
    Q_INVOKABLE void moveClips(int srcTrackIndex, const QVariantList &clipIndices,
                               double deltaSec, int dstTrackIndex);
    Q_INVOKABLE void moveMultipleClips(const QVariantList &selectedClips,
                                       double deltaSec, int deltaTrack);
    Q_INVOKABLE void deleteMultipleClips(const QVariantList &selectedClips,
                                         bool rippleClose = false);
    Q_INVOKABLE void pasteClips(int trackIndex, const QVariantList &snapshots, bool ripple = false);
    Q_INVOKABLE void copyClipToTrack(int srcTrackIndex, const QVariantList &clipIndices,
                                     int dstTrackIndex, double timelineStartSec, bool ripple = false);
    Q_INVOKABLE void copyMultipleClipsToTrack(const QVariantList &clipsToCopy,
                                              int targetTrackIndex, double timelineStartSec,
                                              bool ripple = false);
    Q_INVOKABLE void pasteMultipleClipSnapshots(int targetTrackIndex,
                                                const QVariantList &snapshotsWithTrack,
                                                double timelineStartSec,
                                                bool ripple = false);

    Q_INVOKABLE void renameTrack(int trackIndex, const QString &newName);
    Q_INVOKABLE void setGain(int trackIndex, float newGain);
    Q_INVOKABLE void setPan(int trackIndex, float newPan);
    Q_INVOKABLE void moveTrack(int from, int to);
    Q_INVOKABLE void toggleMute(int trackIndex);
    Q_INVOKABLE void toggleSolo(int trackIndex);
    Q_INVOKABLE void toggleArmed(int trackIndex);

    Q_INVOKABLE void addTrack(const QString &name);
    Q_INVOKABLE void removeTrack(int trackIndex);

    /// Auto-nivelado de todas las pistas al parar grabación (Hindenburg Magic):
    /// ajusta el gain de cada pista para llevarla al targetDb. Reversible
    /// con undo (restaura los gains originales).
    Q_INVOKABLE void autoLevelAllTracks(double targetDb);

    /// Normaliza una pista concreta al targetDb (mismo mecanismo que
    /// autoLevelAllTracks pero sobre una sola pista). Reversible.
    Q_INVOKABLE void normalizeTrack(int trackIndex, double targetDb);
    Q_INVOKABLE void normalizeClipPeak(int trackIndex, int clipIndex, double targetDb);
    Q_INVOKABLE void normalizeTrackPeak(int trackIndex, double targetDb);

    /// Normalización dinámica estilo Hindenburg: analiza LUFS por clip
    /// y aplica ganancia destructiva para llevar cada clip al LUFS objetivo.
    Q_INVOKABLE void dynamicNormalizeAllTracks(double targetLufs);
    Q_INVOKABLE void dynamicNormalizeTrack(int trackIndex, double targetLufs);
    Q_INVOKABLE void dynamicNormalizeTracks(const QVariantList &tracks, double targetLufs);
    Q_INVOKABLE void dynamicNormalizeClip(int trackIndex, int clipIndex, double targetLufs);

    /// Cambia los fades in/out de un clip (en segundos). Reversible.
    Q_INVOKABLE void setClipFades(int trackIndex, int clipIndex,
                                  double fadeInSec, double fadeOutSec);

    /// Cambia la ganancia de un clip. Reversible.
    Q_INVOKABLE void setClipGain(int trackIndex, int clipIndex, float gain);

    // ---- Envelopes ----
    Q_INVOKABLE void addEnvelopeNode(int trackIndex, int clipIndex, double timeSec, float gain);
    Q_INVOKABLE void removeEnvelopeNode(int trackIndex, int clipIndex, int nodeIndex);
    Q_INVOKABLE void setEnvelopeNode(int trackIndex, int clipIndex, int nodeIndex, double timeSec, float gain);
    Q_INVOKABLE void clearClipEnvelope(int trackIndex, int clipIndex);

    /// Elimina los huecos de silencio entre clips. Si trackIndex < 0,
    /// aplica a todas las pistas. Reversible.
    Q_INVOKABLE void removeGaps(const QVariant &trackVal = QVariant());

    /// Fusiona clips seleccionados en uno solo. Reversible.
    Q_INVOKABLE void mergeClips(int trackIndex, const QVariantList &clipIndices);

    /// Fusiona todos los clips de una pista en uno solo. Reversible.
    Q_INVOKABLE void mergeAllClips(int trackIndex);

    /// Ajusta los bordes de un clip. Reversible.
    Q_INVOKABLE void trimClip(int trackIndex, int clipIndex, double deltaStartSec, double deltaEndSec);

    /// Limpiar audio de una pista (undoable)
    Q_INVOKABLE void clearAudio(const QVariant &trackVal);

    /// Truncar silencio (undoable)
    Q_INVOKABLE int truncateSilence(int trackIndex, float thresholdDb,
                                     double minDurationSec, double truncateToSec);

    /// Noise Reduction (undoable)
    Q_INVOKABLE bool noiseReduce(int trackIndex, int clipIndex,
                                  float reductionDb, float sensitivity);

    // ---- Capítulos (undoable) ----
    Q_INVOKABLE void addChapterAtSec(double sec, const QString &title = {});
    Q_INVOKABLE void removeChapter(int idx);
    Q_INVOKABLE void moveChapterToSec(int idx, double sec);
    Q_INVOKABLE void renameChapter(int idx, const QString &title);
    Q_INVOKABLE void clearChapters();

signals:
    void stackChanged();
    void dynNormClipApplied(int trackIndex, int clipIndex, double gainDb);

private:
    /// Ajusta dinámicamente el undo limit según la duración total del audio.
    /// Si el proyecto tiene más de 10 minutos de audio, baja a 15 para
    /// reducir presión de memoria con snapshots pesados.
    void adjustUndoLimit();

    QUndoStack m_stack;
    TrackModel *m_trackModel = nullptr;
    ChapterModel *m_chapterModel = nullptr;
};

/**
 * Command de propósito general: guarda 2 lambdas (redo/undo) y un texto.
 * Si mergeKey está presente, dos commands consecutivos con la misma clave se
 * fusionan (útil para sliders de gain / renombres que escriben letra a letra).
 */
class LambdaCommand : public QUndoCommand
{
public:
    LambdaCommand(const QString &text,
                  std::function<void()> redoFn,
                  std::function<void()> undoFn,
                  int mergeId = -1,
                  QUndoCommand *parent = nullptr)
        : QUndoCommand(text, parent)
        , m_redo(std::move(redoFn))
        , m_undo(std::move(undoFn))
        , m_mergeId(mergeId)
    {}

    /// Marcar que la operación ya fue ejecutada antes de push.
    /// El primer redo() será un no-op.
    void setAlreadyDone(bool v) { m_skipFirstRedo = v; }

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_redo) m_redo();
    }
    void undo() override { if (m_undo) m_undo(); }

    int id() const override { return m_mergeId; }

    bool mergeWith(const QUndoCommand *other) override
    {
        // Estrategia de merge simple: el comando más reciente mantiene su
        // función redo, y el undo original se conserva (el primero).
        const LambdaCommand *o = dynamic_cast<const LambdaCommand*>(other);
        if (!o || o->m_mergeId != m_mergeId || m_mergeId < 0) return false;
        m_redo = o->m_redo;  // el último valor es el que se conserva al redo
        return true;
    }

private:
    std::function<void()> m_redo;
    std::function<void()> m_undo;
    int m_mergeId = -1;
    bool m_skipFirstRedo = false;
};
