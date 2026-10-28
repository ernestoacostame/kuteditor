#include "UndoManager.h"
#include "TrackModel.h"
#include "ChapterModel.h"

#include <QDebug>
#include <QSet>
#include <limits>
#include <QJSValue>

static QVector<int> parseTracks(const QVariant &trackVal, int maxTracks) {
  QVector<int> tracks;
  QVariant val = trackVal;

  if (val.typeName() && QLatin1String(val.typeName()) == "QJSValue") {
    val = val.value<QJSValue>().toVariant();
  }

  if (val.isNull() || !val.isValid()) {
    for (int i = 0; i < maxTracks; ++i) tracks.append(i);
  } else if (val.userType() == QMetaType::QVariantList) {
    QVariantList list = val.toList();
    if (list.isEmpty()) {
      for (int i = 0; i < maxTracks; ++i) tracks.append(i);
    } else {
      for (const QVariant &v : list) {
        int idx = v.toInt();
        if (idx >= 0 && idx < maxTracks) tracks.append(idx);
      }
    }
  } else {
    bool ok = false;
    int idx = val.toInt(&ok);
    if (ok && idx >= 0 && idx < maxTracks) {
      tracks.append(idx);
    } else {
      for (int i = 0; i < maxTracks; ++i) tracks.append(i);
    }
  }
  return tracks;
}

UndoManager::UndoManager(QObject *parent) : QObject(parent) {
  m_stack.setUndoLimit(30);
  connect(&m_stack, &QUndoStack::canUndoChanged, this,
          &UndoManager::stackChanged);
  connect(&m_stack, &QUndoStack::canRedoChanged, this,
          &UndoManager::stackChanged);
  connect(&m_stack, &QUndoStack::undoTextChanged, this,
          &UndoManager::stackChanged);
  connect(&m_stack, &QUndoStack::redoTextChanged, this,
          &UndoManager::stackChanged);
}

void UndoManager::undo() { m_stack.undo(); }
void UndoManager::redo() { m_stack.redo(); }
void UndoManager::clear() { m_stack.clear(); }

void UndoManager::adjustUndoLimit() {
  if (!m_trackModel) return;
  // Estimar la duración total del audio en el proyecto.
  // Si supera los 10 minutos (~28.8M frames stereo @ 48kHz), bajar
  // el undo limit a 15 para reducir la memoria de los snapshots.
  constexpr qint64 TEN_MINUTES_FRAMES = 10LL * 60 * 48000; // 28.8M
  qint64 totalFrames = 0;
  for (int i = 0; i < m_trackModel->count(); ++i) {
    totalFrames += m_trackModel->trackFrameCount(i);
    if (totalFrames > TEN_MINUTES_FRAMES) break; // early exit
  }
  const int newLimit = (totalFrames > TEN_MINUTES_FRAMES) ? 15 : 30;
  if (m_stack.undoLimit() != newLimit)
    m_stack.setUndoLimit(newLimit);
}

// ============================================================================
//  Split
// ============================================================================
// Semántica: aplicamos splitClipsAt en todas las pistas. Para deshacer, la
// forma más robusta es guardar la lista de clips *antes* de cada pista y
// restaurarlas con removeClips+insertClipSnapshots. Pero es costoso.
// Alternativa: un split se puede "un-split" buscando en cada pista pares de
// clips contiguos con mismo sourceIdx y la unión de sus rangos, y fusionarlos.
//
// De momento, usamos el método robusto: snapshot completo de los clips de
// cada pista antes, y restaurar al deshacer.
void UndoManager::splitAt(double atSec, const QVariant &trackVal) {
  if (!m_trackModel)
    return;
  const int n = m_trackModel->count();

  QVector<int> tracks = parseTracks(trackVal, n);

  // Snapshot antes (solo las pistas afectadas)
  QMap<int, QVariantList> beforeClips;
  for (int i : tracks) {
    QVariantList snaps;
    const QVariantList clips = m_trackModel->clipsOf(i);
    for (int k = 0; k < clips.size(); ++k)
      snaps.append(m_trackModel->clipSnapshot(i, k));
    beforeClips[i] = snaps;
  }

  // Redo: aplicar el split
  auto doRedo = [this, atSec, tracks]() {
    if (m_trackModel) {
      for (int i : tracks) {
        m_trackModel->splitClipsAt(i, atSec);
      }
    }
  };
  // Undo: restaurar clips de las pistas afectadas
  auto doUndo = [this, beforeClips]() {
    if (!m_trackModel)
      return;
    for (auto it = beforeClips.constBegin(); it != beforeClips.constEnd();
         ++it) {
      const int i = it.key();
      // Borrar todos los clips actuales
      const QVariantList current = m_trackModel->clipsOf(i);
      QVariantList allIdx;
      for (int k = 0; k < current.size(); ++k)
        allIdx.append(k);
      if (!allIdx.isEmpty())
        m_trackModel->removeClips(i, allIdx);
      // Reinsertar desde snapshots raw (frames, sin conversión)
      if (!it.value().isEmpty())
        m_trackModel->insertClipSnapshots(i, it.value());
    }
  };

  m_stack.push(new LambdaCommand(tr("Dividir en %1 s").arg(atSec, 0, 'f', 2),
                                 doRedo, doUndo));
}

// ============================================================================
//  Delete region
// ============================================================================
void UndoManager::deleteRegion(double startSec, double endSec,
                               bool rippleClose, const QVariant &tracksVal) {
  if (!m_trackModel)
    return;
  const int n = m_trackModel->count();

  QVector<int> tracks = parseTracks(tracksVal, n);

  QVector<QVariantList> before(n);
  for (int i : tracks)
    before[i] = m_trackModel->clipsOf(i);

  auto doRedo = [this, startSec, endSec, rippleClose, tracks]() {
    if (!m_trackModel)
      return;
    for (int i : tracks) {
      if (rippleClose) {
        // Borrar rango y cerrar brecha (clips posteriores se desplazan).
        m_trackModel->deleteRegion(i, startSec, endSec);
      } else {
        // Ripple OFF: corta los clips en la región y deja hueco,
        // sin mover los demás clips.
        m_trackModel->deleteRegionNoRipple(i, startSec, endSec);
      }
    }
  };
  auto doUndo = [this, before, tracks]() {
    if (!m_trackModel)
      return;
    for (int i : tracks) {
      const QVariantList current = m_trackModel->clipsOf(i);
      QVariantList allIdx;
      for (int k = 0; k < current.size(); ++k)
        allIdx.append(k);
      if (!allIdx.isEmpty())
        m_trackModel->removeClips(i, allIdx);

      QVariantList snaps;
      const int sr = m_trackModel->trackSampleRate(i);
      for (const QVariant &v : before[i]) {
        QVariantMap m = v.toMap();
        QVariantMap s;
        s["sourceIdx"] = m.value("sourceIdx", -1).toInt();
        s["timelineStart"] = qint64(m.value("startSec", 0).toDouble() * sr);
        s["sourceOffset"] =
            qint64(m.value("sourceOffsetSec", 0).toDouble() * sr);
        s["length"] = qint64(m.value("lengthSec", 0).toDouble() * sr);
        s["fadeInLen"] = qint64(m.value("fadeInSec", 0).toDouble() * sr);
        s["fadeOutLen"] = qint64(m.value("fadeOutSec", 0).toDouble() * sr);
        s["gain"] = m.value("gain", 1.0f).toFloat();
        s["envelope"] = m.value("envelope");
        snaps.append(s);
      }
      if (!snaps.isEmpty())
        m_trackModel->insertClipSnapshots(i, snaps);
    }
  };

  m_stack.push(new LambdaCommand(tr("Eliminar región"), doRedo, doUndo));
}

// ============================================================================
//  Delete clips (Supr sobre clips seleccionados)
// ============================================================================
void UndoManager::deleteClipsInTrack(int trackIndex,
                                     const QVariantList &clipIndices,
                                     bool rippleClose) {
  if (!m_trackModel || clipIndices.isEmpty())
    return;

  // Capturar snapshots antes de borrar. En modo ripple, los clips restantes
  // también cambian su timelineStart, así que para el undo guardamos el
  // estado completo de la pista.
  QVariantList beforeAll;
  if (rippleClose) {
    const QVariantList cur = m_trackModel->clipsOf(trackIndex);
    const int sr = m_trackModel->trackSampleRate(trackIndex);
    for (const QVariant &cv : cur) {
      QVariantMap c = cv.toMap();
      QVariantMap s;
      s["sourceIdx"] = c.value("sourceIdx", -1).toInt();
      s["timelineStart"] = qint64(c.value("startSec", 0).toDouble() * sr);
      s["sourceOffset"] = qint64(c.value("sourceOffsetSec", 0).toDouble() * sr);
      s["length"] = qint64(c.value("lengthSec", 0).toDouble() * sr);
      s["fadeInLen"] = qint64(c.value("fadeInSec", 0).toDouble() * sr);
      s["fadeOutLen"] = qint64(c.value("fadeOutSec", 0).toDouble() * sr);
      s["gain"] = c.value("gain", 1.0f).toFloat();
      s["envelope"] = c.value("envelope");
      beforeAll.append(s);
    }
  } else {
    for (const QVariant &v : clipIndices) {
      beforeAll.append(m_trackModel->clipSnapshot(trackIndex, v.toInt()));
    }
  }

  auto doRedo = [this, trackIndex, clipIndices, rippleClose]() {
    if (m_trackModel)
      m_trackModel->removeClips(trackIndex, clipIndices, rippleClose);
  };
  auto doUndo = [this, trackIndex, beforeAll, rippleClose]() {
    if (!m_trackModel)
      return;
    if (rippleClose) {
      // Restaurar el estado completo: borrar todos los clips actuales y
      // reinsertar los que teníamos antes.
      const QVariantList cur = m_trackModel->clipsOf(trackIndex);
      QVariantList allIdx;
      for (int k = 0; k < cur.size(); ++k)
        allIdx.append(k);
      if (!allIdx.isEmpty())
        m_trackModel->removeClips(trackIndex, allIdx, /*rippleClose=*/false);
      m_trackModel->insertClipSnapshots(trackIndex, beforeAll);
    } else {
      m_trackModel->insertClipSnapshots(trackIndex, beforeAll);
    }
  };

  m_stack.push(new LambdaCommand(rippleClose ? tr("Eliminar clips (ripple)")
                                             : tr("Eliminar clips"),
                                 doRedo, doUndo));
}

// ============================================================================
//  Move clips (drag, incluyendo cambio de pista)
// ============================================================================
void UndoManager::moveClips(int srcTrackIndex, const QVariantList &clipIndices,
                            double deltaSec, int dstTrackIndex) {
  if (!m_trackModel || clipIndices.isEmpty())
    return;
  if (deltaSec == 0.0 && (dstTrackIndex < 0 || dstTrackIndex == srcTrackIndex))
    return;

  const int finalDst = (dstTrackIndex < 0 || dstTrackIndex == srcTrackIndex)
                           ? srcTrackIndex
                           : dstTrackIndex;
  const bool sameTrack = (finalDst == srcTrackIndex);

  // Guardar snapshots COMPLETOS de las pistas afectadas (en formato raw).
  // Esto es lo más seguro: restaurar TODO tal cual estaba.
  QVariantList srcSnapsBefore;
  {
    const QVariantList clips = m_trackModel->clipsOf(srcTrackIndex);
    for (const QVariant &v : clips) {
      QVariantMap c = v.toMap();
      srcSnapsBefore.append(m_trackModel->clipSnapshot(
          srcTrackIndex,
          srcSnapsBefore.size() < clips.size() ? srcSnapsBefore.size() : 0));
    }
    // Usar clipSnapshot que devuelve frames raw
    srcSnapsBefore.clear();
    for (int i = 0; i < clips.size(); ++i)
      srcSnapsBefore.append(m_trackModel->clipSnapshot(srcTrackIndex, i));
  }

  QVariantList dstSnapsBefore;
  if (!sameTrack) {
    const QVariantList clips = m_trackModel->clipsOf(finalDst);
    for (int i = 0; i < clips.size(); ++i)
      dstSnapsBefore.append(m_trackModel->clipSnapshot(finalDst, i));
  }

  // Calcular el rango temporal de los clips que se van a mover, para
  // desplazar los marcadores de capítulo que caigan dentro de ese rango.
  double clipRangeStart = std::numeric_limits<double>::max();
  double clipRangeEnd = 0.0;
  {
    const QVariantList allClips = m_trackModel->clipsOf(srcTrackIndex);
    for (const QVariant &ci : clipIndices) {
      const int idx = ci.toInt();
      if (idx < 0 || idx >= allClips.size()) continue;
      const QVariantMap cm = allClips[idx].toMap();
      const double s = cm.value("startSec").toDouble();
      const double e = cm.value("endSec").toDouble();
      if (s < clipRangeStart) clipRangeStart = s;
      if (e > clipRangeEnd)   clipRangeEnd = e;
    }
  }

  auto doRedo = [this, srcTrackIndex, clipIndices, deltaSec, dstTrackIndex,
                 clipRangeStart, clipRangeEnd]() {
    if (m_trackModel)
      m_trackModel->moveClips(srcTrackIndex, clipIndices, deltaSec,
                              dstTrackIndex);
    // Mover marcadores de capítulo que caen dentro del rango de clips
    if (m_chapterModel && clipRangeStart < clipRangeEnd)
      m_chapterModel->shiftChaptersInRange(clipRangeStart, clipRangeEnd, deltaSec);
  };
  auto doUndo = [this, srcTrackIndex, finalDst, sameTrack, srcSnapsBefore,
                 dstSnapsBefore, clipRangeStart, clipRangeEnd, deltaSec]() {
    if (!m_trackModel)
      return;

    // Restaurar marcadores de capítulo a sus posiciones originales
    if (m_chapterModel && clipRangeStart < clipRangeEnd) {
      const double newStart = clipRangeStart + deltaSec;
      const double newEnd   = clipRangeEnd + deltaSec;
      m_chapterModel->shiftChaptersInRange(newStart, newEnd, -deltaSec);
    }

    // Borrar todos los clips de la pista src
    {
      const QVariantList cur = m_trackModel->clipsOf(srcTrackIndex);
      QVariantList allIdx;
      for (int i = 0; i < cur.size(); ++i)
        allIdx.append(i);
      if (!allIdx.isEmpty())
        m_trackModel->removeClips(srcTrackIndex, allIdx, false);
    }
    // Restaurar src desde snapshots (usa formato raw con frames)
    m_trackModel->insertClipSnapshots(srcTrackIndex, srcSnapsBefore);

    if (!sameTrack) {
      // Borrar todos los clips de la pista dst
      const QVariantList cur = m_trackModel->clipsOf(finalDst);
      QVariantList allIdx;
      for (int i = 0; i < cur.size(); ++i)
        allIdx.append(i);
      if (!allIdx.isEmpty())
        m_trackModel->removeClips(finalDst, allIdx, false);
      // Restaurar dst
      m_trackModel->insertClipSnapshots(finalDst, dstSnapsBefore);
    }
  };

  const QString txt =
      (!sameTrack) ? tr("Mover clips a otra pista") : tr("Mover clips");
  m_stack.push(new LambdaCommand(txt, doRedo, doUndo));
}

// ============================================================================
//  Metadatos de pista
// ============================================================================

// IDs para merging. Usamos 100+trackIndex para rename, 200+trackIndex para
// gain. Ajustar a rangos que no choquen con otros comandos.
static int renameMergeId(int idx) { return 100 + idx; }
static int gainMergeId(int idx) { return 200 + idx; }

void UndoManager::renameTrack(int trackIndex, const QString &newName) {
  if (!m_trackModel)
    return;
  const QVariantMap before = m_trackModel->getTrackData(trackIndex);
  if (!before.contains("name"))
    return;
  const QString oldName = before["name"].toString();
  if (oldName == newName)
    return;

  auto doRedo = [this, trackIndex, newName]() {
    if (m_trackModel)
      m_trackModel->updateTrackName(trackIndex, newName);
  };
  auto doUndo = [this, trackIndex, oldName]() {
    if (m_trackModel)
      m_trackModel->updateTrackName(trackIndex, oldName);
  };

  m_stack.push(new LambdaCommand(tr("Renombrar pista"), doRedo, doUndo,
                                 renameMergeId(trackIndex)));
}

void UndoManager::setGain(int trackIndex, float newGain) {
  if (!m_trackModel)
    return;
  const QVariantMap before = m_trackModel->getTrackData(trackIndex);
  if (!before.contains("gain"))
    return;
  const float oldGain = before["gain"].toFloat();
  if (std::abs(oldGain - newGain) < 1e-6f)
    return;

  auto doRedo = [this, trackIndex, newGain]() {
    if (m_trackModel)
      m_trackModel->updateGain(trackIndex, newGain);
  };
  auto doUndo = [this, trackIndex, oldGain]() {
    if (m_trackModel)
      m_trackModel->updateGain(trackIndex, oldGain);
  };

  m_stack.push(new LambdaCommand(tr("Cambiar volumen"), doRedo, doUndo,
                                 gainMergeId(trackIndex)));
}

void UndoManager::setPan(int trackIndex, float newPan) {
  if (!m_trackModel)
    return;
  const float oldPan = m_trackModel->getTrackPan(trackIndex);
  if (std::abs(oldPan - newPan) < 1e-4f)
    return;

  auto doRedo = [this, trackIndex, newPan]() {
    if (m_trackModel)
      m_trackModel->updatePan(trackIndex, newPan);
  };
  auto doUndo = [this, trackIndex, oldPan]() {
    if (m_trackModel)
      m_trackModel->updatePan(trackIndex, oldPan);
  };

  // Merge ID único para pan de cada pista (offset para no colisionar con gain)
  m_stack.push(new LambdaCommand(tr("Cambiar panorama"), doRedo, doUndo,
                                 5000 + trackIndex));
}

void UndoManager::moveTrack(int from, int to) {
  if (!m_trackModel)
    return;
  if (from == to)
    return;

  auto doRedo = [this, from, to]() {
    if (m_trackModel)
      m_trackModel->moveTrack(from, to);
  };
  auto doUndo = [this, from, to]() {
    if (m_trackModel)
      m_trackModel->moveTrack(to, from);
  };

  m_stack.push(new LambdaCommand(tr("Mover pista"), doRedo, doUndo));
}

void UndoManager::toggleMute(int trackIndex) {
  if (!m_trackModel)
    return;
  auto doToggle = [this, trackIndex]() {
    if (m_trackModel)
      m_trackModel->toggleMute(trackIndex);
  };
  m_stack.push(new LambdaCommand(tr("Mute"), doToggle, doToggle));
}

void UndoManager::toggleSolo(int trackIndex) {
  if (!m_trackModel)
    return;
  auto doToggle = [this, trackIndex]() {
    if (m_trackModel)
      m_trackModel->toggleSolo(trackIndex);
  };
  m_stack.push(new LambdaCommand(tr("Solo"), doToggle, doToggle));
}

void UndoManager::toggleArmed(int trackIndex) {
  if (!m_trackModel)
    return;
  auto doToggle = [this, trackIndex]() {
    if (m_trackModel)
      m_trackModel->toggleArmed(trackIndex);
  };
  m_stack.push(new LambdaCommand(tr("Armar"), doToggle, doToggle));
}

// ============================================================================
//  Pistas (add/remove)
// ============================================================================
void UndoManager::addTrack(const QString &name) {
  if (!m_trackModel)
    return;

  auto doRedo = [this, name]() {
    if (m_trackModel)
      m_trackModel->addTrack(name);
  };
  // Undo: quitar la última pista añadida (asumimos que es la de mayor índice).
  auto doUndo = [this]() {
    if (!m_trackModel)
      return;
    const int last = m_trackModel->count() - 1;
    if (last >= 0)
      m_trackModel->removeTrack(last);
  };

  m_stack.push(new LambdaCommand(tr("Añadir pista"), doRedo, doUndo));
}

void UndoManager::removeTrack(int trackIndex) {
  if (!m_trackModel)
    return;
  if (trackIndex < 0 || trackIndex >= m_trackModel->count())
    return;

  // Guardar estado completo de la pista para poder restaurarla
  const QVariantMap trackData = m_trackModel->getTrackData(trackIndex);
  const QVariant audioState = m_trackModel->saveTrackAudioState(trackIndex);
  const QString name = trackData.value("name").toString();
  const QColor color = trackData.value("color").value<QColor>();

  auto doRedo = [this, trackIndex]() {
    if (m_trackModel && trackIndex < m_trackModel->count())
      m_trackModel->removeTrack(trackIndex);
  };
  auto doUndo = [this, trackIndex, name, color, trackData, audioState]() {
    if (!m_trackModel)
      return;
    // Reinsertar la pista en la posición original
    m_trackModel->insertTrackAt(trackIndex, name);
    // Restaurar color y configuración
    m_trackModel->updateTrackColor(trackIndex, color);
    if (trackData.contains("inputDevice"))
      m_trackModel->updateInputDevice(trackIndex,
                                      trackData["inputDevice"].toString(),
                                      trackData["inputDeviceName"].toString());
    if (trackData.contains("inputMode"))
      m_trackModel->updateInputChannel(trackIndex,
                                       trackData["inputMode"].toInt(),
                                       trackData["inputChannelIndex"].toInt());
    if (trackData.contains("gain"))
      m_trackModel->updateGain(trackIndex, trackData["gain"].toFloat());
    if (trackData.contains("pan"))
      m_trackModel->updatePan(trackIndex, trackData["pan"].toFloat());
    // Restaurar audio (samples, clips, peaks)
    m_trackModel->restoreTrackAudioState(trackIndex, audioState);
  };

  m_stack.push(new LambdaCommand(tr("Eliminar pista"), doRedo, doUndo));
}

// ============================================================================
//  Auto-level (Hindenburg Magic)
// ============================================================================
void UndoManager::autoLevelAllTracks(double targetDb) {
  if (!m_trackModel)
    return;

  // Calcular factor necesario por pista: gain_mult = target - rms_actual.
  // Guardar los factores para poder deshacer aplicando el inverso.
  const int n = m_trackModel->count();
  QVector<float> factors(n,
                         1.0f); // factor aplicado a los samples de cada pista
  for (int i = 0; i < n; ++i) {
    const double db = m_trackModel->computeTrackLoudnessDb(i);
    if (db <= -119.0)
      continue;
    const double deltaDb = targetDb - db;
    double g = std::pow(10.0, deltaDb / 20.0);
    if (g < 0.05)
      g = 0.05;
    if (g > 8.0)
      g = 8.0;
    factors[i] = float(g);
  }

  auto doRedo = [this, factors]() {
    if (!m_trackModel)
      return;
    for (int i = 0; i < factors.size() && i < m_trackModel->count(); ++i) {
      if (std::abs(factors[i] - 1.0f) < 1e-6f)
        continue;
      // Aplicar el factor como gain temporal y bakearlo en samples.
      const QVariantMap d = m_trackModel->getTrackData(i);
      const float curGain = d.value("gain", 1.0f).toFloat();
      m_trackModel->updateGain(i, curGain * factors[i]);
      m_trackModel->bakeGainIntoSamples(i); // deja gain=1 y aplica a samples
    }
  };
  auto doUndo = [this, factors]() {
    if (!m_trackModel)
      return;
    for (int i = 0; i < factors.size() && i < m_trackModel->count(); ++i) {
      if (std::abs(factors[i] - 1.0f) < 1e-6f)
        continue;
      // Aplicar el factor inverso para restaurar los samples
      const float inv = 1.0f / factors[i];
      m_trackModel->updateGain(i, inv);
      m_trackModel->bakeGainIntoSamples(i);
    }
  };

  m_stack.push(new LambdaCommand(tr("Nivelar pistas"), doRedo, doUndo));
}

// ============================================================================
//  Fades y normalización por pista
// ============================================================================
void UndoManager::setClipFades(int trackIndex, int clipIndex, double fadeInSec,
                               double fadeOutSec) {
  if (!m_trackModel)
    return;

  const double oldIn = m_trackModel->clipFadeInSec(trackIndex, clipIndex);
  const double oldOut = m_trackModel->clipFadeOutSec(trackIndex, clipIndex);
  if (std::abs(oldIn - fadeInSec) < 1e-6 &&
      std::abs(oldOut - fadeOutSec) < 1e-6) {
    return; // nada que hacer
  }

  auto doRedo = [this, trackIndex, clipIndex, fadeInSec, fadeOutSec]() {
    if (m_trackModel)
      m_trackModel->setClipFades(trackIndex, clipIndex, fadeInSec, fadeOutSec);
  };
  auto doUndo = [this, trackIndex, clipIndex, oldIn, oldOut]() {
    if (m_trackModel)
      m_trackModel->setClipFades(trackIndex, clipIndex, oldIn, oldOut);
  };

  // ID de merge basado en trackIndex/clipIndex: así arrastrar un handle
  // varios veces seguidas se colapsa a un solo item de undo.
  const int mergeId = 300 + trackIndex * 1000 + clipIndex;
  m_stack.push(new LambdaCommand(tr("Cambiar fades"), doRedo, doUndo, mergeId));
}

void UndoManager::setClipGain(int trackIndex, int clipIndex, float gain) {
  if (!m_trackModel)
    return;

  // Obtener la ganancia antigua desde el snapshot del clip
  const QVariantMap oldSnap = m_trackModel->clipSnapshot(trackIndex, clipIndex);
  if (oldSnap.isEmpty())
    return;
  const float oldGain = oldSnap.value("gain", 1.0f).toFloat();

  if (std::abs(oldGain - gain) < 1e-4f)
    return;

  auto doRedo = [this, trackIndex, clipIndex, gain]() {
    if (m_trackModel)
      m_trackModel->setClipGain(trackIndex, clipIndex, gain);
  };
  auto doUndo = [this, trackIndex, clipIndex, oldGain]() {
    if (m_trackModel)
      m_trackModel->setClipGain(trackIndex, clipIndex, oldGain);
  };

  // ID de merge basado en track/clip para que arrastrar el slider agrupe undos.
  const int mergeId = 400 + trackIndex * 1000 + clipIndex;
  m_stack.push(
      new LambdaCommand(tr("Ganancia de clip"), doRedo, doUndo, mergeId));
}

void UndoManager::addEnvelopeNode(int trackIndex, int clipIndex, double timeSec,
                                  float gain) {
  if (!m_trackModel)
    return;
  const QVariantList envBefore =
      m_trackModel->getEnvelopeNodes(trackIndex, clipIndex);
  const double fadeInBefore = m_trackModel->clipFadeInSec(trackIndex, clipIndex);
  const double fadeOutBefore = m_trackModel->clipFadeOutSec(trackIndex, clipIndex);

  auto doRedo = [this, trackIndex, clipIndex, timeSec, gain]() {
    if (m_trackModel)
      m_trackModel->addEnvelopeNode(trackIndex, clipIndex, timeSec, gain);
  };
  auto doUndo = [this, trackIndex, clipIndex, envBefore, fadeInBefore, fadeOutBefore]() {
    if (!m_trackModel)
      return;
    QVariantList cur = m_trackModel->getEnvelopeNodes(trackIndex, clipIndex);
    for (int i = cur.size() - 1; i >= 0; --i)
      m_trackModel->removeEnvelopeNode(trackIndex, clipIndex, i);
    for (const QVariant &v : envBefore) {
      QVariantMap n = v.toMap();
      m_trackModel->addEnvelopeNode(trackIndex, clipIndex, n["x"].toDouble(),
                                    n["y"].toFloat(), /*autoAnchors=*/false);
    }
    m_trackModel->setClipFades(trackIndex, clipIndex, fadeInBefore, fadeOutBefore);
  };

  m_stack.push(
      new LambdaCommand(tr("Añadir nodo de envolvente"), doRedo, doUndo));
}

void UndoManager::removeEnvelopeNode(int trackIndex, int clipIndex,
                                     int nodeIndex) {
  if (!m_trackModel)
    return;
  const QVariantList envBefore =
      m_trackModel->getEnvelopeNodes(trackIndex, clipIndex);
  const double fadeInBefore = m_trackModel->clipFadeInSec(trackIndex, clipIndex);
  const double fadeOutBefore = m_trackModel->clipFadeOutSec(trackIndex, clipIndex);

  auto doRedo = [this, trackIndex, clipIndex, nodeIndex]() {
    if (m_trackModel)
      m_trackModel->removeEnvelopeNode(trackIndex, clipIndex, nodeIndex);
  };
  auto doUndo = [this, trackIndex, clipIndex, envBefore, fadeInBefore, fadeOutBefore]() {
    if (!m_trackModel)
      return;
    QVariantList cur = m_trackModel->getEnvelopeNodes(trackIndex, clipIndex);
    for (int i = cur.size() - 1; i >= 0; --i)
      m_trackModel->removeEnvelopeNode(trackIndex, clipIndex, i);
    for (const QVariant &v : envBefore) {
      QVariantMap n = v.toMap();
      m_trackModel->addEnvelopeNode(trackIndex, clipIndex, n["x"].toDouble(),
                                    n["y"].toFloat(), /*autoAnchors=*/false);
    }
    m_trackModel->setClipFades(trackIndex, clipIndex, fadeInBefore, fadeOutBefore);
  };

  m_stack.push(
      new LambdaCommand(tr("Eliminar nodo envolvente"), doRedo, doUndo));
}

void UndoManager::setEnvelopeNode(int trackIndex, int clipIndex, int nodeIndex,
                                  double timeSec, float gain) {
  if (!m_trackModel)
    return;
  const QVariantList envBefore =
      m_trackModel->getEnvelopeNodes(trackIndex, clipIndex);
  const double fadeInBefore = m_trackModel->clipFadeInSec(trackIndex, clipIndex);
  const double fadeOutBefore = m_trackModel->clipFadeOutSec(trackIndex, clipIndex);

  auto doRedo = [this, trackIndex, clipIndex, nodeIndex, timeSec, gain]() {
    if (m_trackModel)
      m_trackModel->setEnvelopeNode(trackIndex, clipIndex, nodeIndex, timeSec,
                                    gain);
  };
  auto doUndo = [this, trackIndex, clipIndex, envBefore, fadeInBefore, fadeOutBefore]() {
    if (!m_trackModel)
      return;
    QVariantList cur = m_trackModel->getEnvelopeNodes(trackIndex, clipIndex);
    for (int i = cur.size() - 1; i >= 0; --i)
      m_trackModel->removeEnvelopeNode(trackIndex, clipIndex, i);
    for (const QVariant &v : envBefore) {
      QVariantMap n = v.toMap();
      m_trackModel->addEnvelopeNode(trackIndex, clipIndex, n["x"].toDouble(),
                                    n["y"].toFloat(), /*autoAnchors=*/false);
    }
    m_trackModel->setClipFades(trackIndex, clipIndex, fadeInBefore, fadeOutBefore);
  };

  const int mergeId = 500 + trackIndex * 10000 + clipIndex * 100 + nodeIndex;
  m_stack.push(
      new LambdaCommand(tr("Modificar envolvente"), doRedo, doUndo, mergeId));
}

void UndoManager::clearClipEnvelope(int trackIndex, int clipIndex) {
  if (!m_trackModel)
    return;
  const QVariantList envBefore = m_trackModel->getEnvelopeNodes(trackIndex, clipIndex);
  if (envBefore.isEmpty())
    return;
  const double fadeInBefore = m_trackModel->clipFadeInSec(trackIndex, clipIndex);
  const double fadeOutBefore = m_trackModel->clipFadeOutSec(trackIndex, clipIndex);

  auto doRedo = [this, trackIndex, clipIndex]() {
    if (!m_trackModel)
      return;
    QVariantList cur = m_trackModel->getEnvelopeNodes(trackIndex, clipIndex);
    for (int i = cur.size() - 1; i >= 0; --i) {
      m_trackModel->removeEnvelopeNode(trackIndex, clipIndex, i);
    }
  };

  auto doUndo = [this, trackIndex, clipIndex, envBefore, fadeInBefore, fadeOutBefore]() {
    if (!m_trackModel)
      return;
    QVariantList cur = m_trackModel->getEnvelopeNodes(trackIndex, clipIndex);
    for (int i = cur.size() - 1; i >= 0; --i) {
      m_trackModel->removeEnvelopeNode(trackIndex, clipIndex, i);
    }
    for (const QVariant &v : envBefore) {
      QVariantMap n = v.toMap();
      m_trackModel->addEnvelopeNode(trackIndex, clipIndex, n["x"].toDouble(),
                                    n["y"].toFloat(), /*autoAnchors=*/false);
    }
    m_trackModel->setClipFades(trackIndex, clipIndex, fadeInBefore, fadeOutBefore);
  };

  m_stack.push(
      new LambdaCommand(tr("Quitar automatización de volumen"), doRedo, doUndo));
}

void UndoManager::clearAudio(const QVariant &trackVal) {
  if (!m_trackModel)
    return;
  const int n = m_trackModel->count();

  QVector<int> tracks = parseTracks(trackVal, n);

  // Guardar estado completo de todas las pistas antes de borrar
  QVector<QPair<int, QVariant>> statesBefore;
  for (int i : tracks) {
    statesBefore.append({i, m_trackModel->saveTrackAudioState(i)});
  }

  // Ejecutar
  for (int i : tracks) {
    m_trackModel->clearAudio(i);
  }

  auto doRedo = [this, tracks]() {
    if (m_trackModel) {
      for (int i : tracks) {
        m_trackModel->clearAudio(i);
      }
    }
  };
  auto doUndo = [this, statesBefore]() {
    if (m_trackModel) {
      for (const auto &pair : statesBefore) {
        m_trackModel->restoreTrackAudioState(pair.first, pair.second);
      }
    }
  };

  auto *cmd = new LambdaCommand(tr("Limpiar audio"), doRedo, doUndo);
  cmd->setAlreadyDone(true);
  m_stack.push(cmd);
}

void UndoManager::removeGaps(const QVariant &trackVal) {
  if (!m_trackModel)
    return;

  const int n = m_trackModel->count();
  QVector<int> tracks = parseTracks(trackVal, n);

  // Guardar snapshots de todas las pistas afectadas antes de cerrar huecos.
  QVector<QPair<int, QVariantList>> before;
  for (int i : tracks) {
    before.append({i, m_trackModel->clipsOf(i)});
  }

  auto doRedo = [this, tracks]() {
    if (m_trackModel) {
      for (int i : tracks) {
        m_trackModel->removeGaps(i);
      }
    }
  };
  auto doUndo = [this, before]() {
    if (!m_trackModel)
      return;
    // Restaurar los clips a sus posiciones originales usando snapshots.
    for (const auto &pair : before) {
      const int ti = pair.first;
      const QVariantList &clips = pair.second;
      // Leer los clips actuales y recalcular timelineStart original.
      const QVariantList cur = m_trackModel->clipsOf(ti);
      const int sr = m_trackModel->trackSampleRate(ti);
      // Borrar todos los clips y reinsertar desde snapshots.
      QVariantList allIdx;
      for (int j = 0; j < cur.size(); ++j)
        allIdx.append(j);
      if (!allIdx.isEmpty())
        m_trackModel->removeClips(ti, allIdx, false);
      for (const QVariant &v : clips) {
        QVariantMap c = v.toMap();
        m_trackModel->addClipRaw(
            ti, c["sourceIdx"].toInt(), qint64(c["startSec"].toDouble() * sr),
            qint64(c["sourceOffsetSec"].toDouble() * sr),
            qint64(c["lengthSec"].toDouble() * sr),
            qint64(c.value("fadeInSec", 0).toDouble() * sr),
            qint64(c.value("fadeOutSec", 0).toDouble() * sr),
            c.value("gain", 1.0f).toFloat(), c.value("envelope").toList());
      }
      m_trackModel->sortClips(ti);
    }
  };

  m_stack.push(new LambdaCommand(tr("Eliminar silencios"), doRedo, doUndo));
}

void UndoManager::mergeClips(int trackIndex, const QVariantList &clipIndices) {
  if (!m_trackModel || clipIndices.size() < 2)
    return;

  adjustUndoLimit();

  // Guardar estado completo antes de fusionar
  const QVariant stateBefore = m_trackModel->saveTrackAudioState(trackIndex);

  // Ejecutar
  const int result = m_trackModel->mergeClips(trackIndex, clipIndices);
  if (result == 0)
    return; // nada que deshacer

  // Redo re-ejecuta la operación en vez de guardar un snapshot stateAfter
  // (ahorra ~50% de la memoria de este comando en el undo stack).
  auto doRedo = [this, trackIndex, clipIndices]() {
    if (m_trackModel)
      m_trackModel->mergeClips(trackIndex, clipIndices);
  };
  auto doUndo = [this, trackIndex, stateBefore]() {
    if (m_trackModel)
      m_trackModel->restoreTrackAudioState(trackIndex, stateBefore);
  };

  auto *cmd = new LambdaCommand(tr("Unir clips"), doRedo, doUndo);
  cmd->setAlreadyDone(true);
  m_stack.push(cmd);
}

void UndoManager::trimClip(int trackIndex, int clipIndex, double deltaStartSec, double deltaEndSec) {
  if (!m_trackModel)
    return;

  const QVariant stateBefore = m_trackModel->saveTrackAudioState(trackIndex);
  auto doRedo = [this, trackIndex, clipIndex, deltaStartSec, deltaEndSec]() {
    if (m_trackModel)
      m_trackModel->trimClip(trackIndex, clipIndex, deltaStartSec, deltaEndSec);
  };
  auto doUndo = [this, trackIndex, stateBefore]() {
    if (m_trackModel)
      m_trackModel->restoreTrackAudioState(trackIndex, stateBefore);
  };

  m_stack.push(
      new LambdaCommand(tr("Recortar clip"), doRedo, doUndo));
}

void UndoManager::mergeAllClips(int trackIndex) {
  if (!m_trackModel)
    return;

  adjustUndoLimit();

  // Guardar estado completo antes
  const QVariant stateBefore = m_trackModel->saveTrackAudioState(trackIndex);

  // Ejecutar
  const int result = m_trackModel->mergeAllClips(trackIndex);
  if (result == 0)
    return;

  // Redo re-ejecuta la operación (ahorra snapshot stateAfter).
  auto doRedo = [this, trackIndex]() {
    if (m_trackModel)
      m_trackModel->mergeAllClips(trackIndex);
  };
  auto doUndo = [this, trackIndex, stateBefore]() {
    if (m_trackModel)
      m_trackModel->restoreTrackAudioState(trackIndex, stateBefore);
  };

  auto *cmd = new LambdaCommand(tr("Unir toda la pista"), doRedo, doUndo);
  cmd->setAlreadyDone(true);
  m_stack.push(cmd);
}

int UndoManager::truncateSilence(int trackIndex, float thresholdDb,
                                 double minDurationSec, double truncateToSec) {
  if (!m_trackModel)
    return 0;

  adjustUndoLimit();

  // Guardar estado completo antes de truncar
  const QVariant stateBefore = m_trackModel->saveTrackAudioState(trackIndex);

  // Ejecutar el truncate
  const int result = m_trackModel->truncateSilence(
      trackIndex, thresholdDb, minDurationSec, truncateToSec);
  if (result == 0)
    return 0; // nada que deshacer

  // Redo re-ejecuta con los parámetros originales (ahorra snapshot stateAfter).
  auto doRedo = [this, trackIndex, thresholdDb, minDurationSec, truncateToSec]() {
    if (m_trackModel)
      m_trackModel->truncateSilence(trackIndex, thresholdDb, minDurationSec, truncateToSec);
  };
  auto doUndo = [this, trackIndex, stateBefore]() {
    if (m_trackModel)
      m_trackModel->restoreTrackAudioState(trackIndex, stateBefore);
  };

  // Pushear un comando que ya fue ejecutado (no llama doRedo al crear)
  auto *cmd = new LambdaCommand(tr("Truncar silencio"), doRedo, doUndo);
  cmd->setAlreadyDone(true);
  m_stack.push(cmd);

  return result;
}

bool UndoManager::noiseReduce(int trackIndex, int clipIndex, float reductionDb,
                              float sensitivity) {
  if (!m_trackModel)
    return false;

  adjustUndoLimit();

  // Guardar estado completo antes
  const QVariant stateBefore = m_trackModel->saveTrackAudioState(trackIndex);

  // Ejecutar noise reduction
  const bool ok = m_trackModel->noiseReduce(trackIndex, clipIndex, reductionDb,
                                            sensitivity);
  if (!ok)
    return false;

  // Redo re-ejecuta con los parámetros originales (ahorra snapshot stateAfter).
  auto doRedo = [this, trackIndex, clipIndex, reductionDb, sensitivity]() {
    if (m_trackModel)
      m_trackModel->noiseReduce(trackIndex, clipIndex, reductionDb, sensitivity);
  };
  auto doUndo = [this, trackIndex, stateBefore]() {
    if (m_trackModel)
      m_trackModel->restoreTrackAudioState(trackIndex, stateBefore);
  };

  auto *cmd = new LambdaCommand(tr("Noise Reduction"), doRedo, doUndo);
  cmd->setAlreadyDone(true);
  m_stack.push(cmd);

  return true;
}

void UndoManager::normalizeTrack(int trackIndex, double targetDb) {
  if (!m_trackModel)
    return;

  // Factor a aplicar basado en loudness actual.
  const double db = m_trackModel->computeTrackLoudnessDb(trackIndex);
  if (db <= -119.0)
    return; // pista vacía
  const double deltaDb = targetDb - db;
  double g = std::pow(10.0, deltaDb / 20.0);
  if (g < 0.05)
    g = 0.05;
  if (g > 8.0)
    g = 8.0;
  const float factor = float(g);
  if (std::abs(factor - 1.0f) < 1e-6f)
    return; // ya está al target

  auto doRedo = [this, trackIndex, factor]() {
    if (!m_trackModel)
      return;
    const QVariantMap d = m_trackModel->getTrackData(trackIndex);
    const float curGain = d.value("gain", 1.0f).toFloat();
    m_trackModel->updateGain(trackIndex, curGain * factor);
    m_trackModel->bakeGainIntoSamples(trackIndex);
  };
  auto doUndo = [this, trackIndex, factor]() {
    if (!m_trackModel)
      return;
    const float inv = 1.0f / factor;
    m_trackModel->updateGain(trackIndex, inv);
    m_trackModel->bakeGainIntoSamples(trackIndex);
  };

  m_stack.push(new LambdaCommand(tr("Normalizar pista"), doRedo, doUndo));
}

void UndoManager::normalizeClipPeak(int trackIndex, int clipIndex, double targetDb) {
  if (!m_trackModel) return;
  const double peakDb = m_trackModel->computeClipPeakDb(trackIndex, clipIndex);
  if (peakDb <= -119.0) return;
  const double deltaDb = targetDb - peakDb;
  if (std::abs(deltaDb) < 1e-4) return;
  double g = std::pow(10.0, deltaDb / 20.0);
  g = std::clamp(g, 0.05, 8.0);
  const float factor = float(g);

  auto doRedo = [this, trackIndex, clipIndex, factor]() {
    if (m_trackModel) {
      m_trackModel->bakeClipGain(trackIndex, clipIndex, factor);
    }
  };
  auto doUndo = [this, trackIndex, clipIndex, factor]() {
    if (m_trackModel) {
      m_trackModel->bakeClipGain(trackIndex, clipIndex, 1.0f / factor);
    }
  };

  m_stack.push(new LambdaCommand(tr("Normalizar clip a pico"), doRedo, doUndo));
}

void UndoManager::normalizeTrackPeak(int trackIndex, double targetDb) {
  if (!m_trackModel) return;
  const QVariantList clips = m_trackModel->clipsOf(trackIndex);
  double maxPeakDb = -120.0;
  for (int c = 0; c < clips.size(); ++c) {
    double peak = m_trackModel->computeClipPeakDb(trackIndex, c);
    if (peak > maxPeakDb)
      maxPeakDb = peak;
  }
  if (maxPeakDb <= -119.0) return;
  const double deltaDb = targetDb - maxPeakDb;
  if (std::abs(deltaDb) < 1e-4) return;
  double g = std::pow(10.0, deltaDb / 20.0);
  g = std::clamp(g, 0.05, 8.0);
  const float factor = float(g);

  auto doRedo = [this, trackIndex, factor]() {
    if (!m_trackModel) return;
    const int count = m_trackModel->clipsOf(trackIndex).size();
    for (int c = 0; c < count; ++c) {
      m_trackModel->bakeClipGain(trackIndex, c, factor);
    }
  };
  auto doUndo = [this, trackIndex, factor]() {
    if (!m_trackModel) return;
    const int count = m_trackModel->clipsOf(trackIndex).size();
    for (int c = 0; c < count; ++c) {
      m_trackModel->bakeClipGain(trackIndex, c, 1.0f / factor);
    }
  };

  m_stack.push(new LambdaCommand(tr("Normalizar pista a pico"), doRedo, doUndo));
}

// ============================================================================
//  Normalización dinámica por clip (estilo Hindenburg Auto Level)
// ============================================================================
void UndoManager::dynamicNormalizeAllTracks(double targetLufs) {
  if (!m_trackModel) return;
  const int n = m_trackModel->count();
  struct ClipInfo { int track; int clip; float factor; };
  QVector<ClipInfo> allClips;
  for (int t = 0; t < n; ++t) {
    const QVariantList clips = m_trackModel->clipsOf(t);
    for (int c = 0; c < clips.size(); ++c) {
      const double lufs = m_trackModel->computeClipLoudnessLufs(t, c);
      if (lufs <= -99.0) continue;
      const double deltaDb = targetLufs - lufs;
      if (std::abs(deltaDb) < 0.01) continue;
      double g = std::pow(10.0, deltaDb / 20.0);
      g = std::clamp(g, 0.05, 8.0);

      // Limitar ganancia para que el pico resultante no supere -1 dBTP.
      // Obtenemos el pico actual del clip y calculamos el máximo gain seguro.
      const double peakDb = m_trackModel->computeClipPeakDb(t, c);
      if (peakDb > -120.0) {
        // Si peak + gain > -1 dBTP, reducir el gain
        const double maxSafeDb = -1.0 - peakDb;
        const double maxSafeGain = std::pow(10.0, maxSafeDb / 20.0);
        if (g > maxSafeGain && maxSafeGain > 0.05)
          g = maxSafeGain;
      }

      allClips.append({t, c, float(g)});
    }
  }
  if (allClips.isEmpty()) return;

  auto doRedo = [this, allClips]() {
    if (!m_trackModel) return;
    for (const auto &ci : allClips) {
      m_trackModel->bakeClipGain(ci.track, ci.clip, ci.factor);
      const double db = 20.0 * std::log10(std::max(double(ci.factor), 1e-8));
      emit dynNormClipApplied(ci.track, ci.clip, db);
    }
  };
  auto doUndo = [this, allClips]() {
    if (!m_trackModel) return;
    for (const auto &ci : allClips) {
      m_trackModel->bakeClipGain(ci.track, ci.clip, 1.0f / ci.factor);
    }
  };

  m_stack.push(new LambdaCommand(tr("Normalización dinámica"), doRedo, doUndo));
}

void UndoManager::dynamicNormalizeTrack(int trackIndex, double targetLufs) {
  if (!m_trackModel) return;
  const QVariantList clips = m_trackModel->clipsOf(trackIndex);
  struct ClipInfo { int clip; float factor; };
  QVector<ClipInfo> normClips;
  for (int c = 0; c < clips.size(); ++c) {
    const double lufs = m_trackModel->computeClipLoudnessLufs(trackIndex, c);
    if (lufs <= -99.0) continue;
    const double deltaDb = targetLufs - lufs;
    if (std::abs(deltaDb) < 0.01) continue;
    double g = std::pow(10.0, deltaDb / 20.0);
    g = std::clamp(g, 0.05, 8.0);

    // Limitar ganancia para no superar -1 dBTP
    const double peakDb = m_trackModel->computeClipPeakDb(trackIndex, c);
    if (peakDb > -120.0) {
      const double maxSafeDb = -1.0 - peakDb;
      const double maxSafeGain = std::pow(10.0, maxSafeDb / 20.0);
      if (g > maxSafeGain && maxSafeGain > 0.05)
        g = maxSafeGain;
    }

    normClips.append({c, float(g)});
  }
  if (normClips.isEmpty()) return;

  const int ti = trackIndex;
  auto doRedo = [this, ti, normClips]() {
    if (!m_trackModel) return;
    for (const auto &ci : normClips) {
      m_trackModel->bakeClipGain(ti, ci.clip, ci.factor);
      const double db = 20.0 * std::log10(std::max(double(ci.factor), 1e-8));
      emit dynNormClipApplied(ti, ci.clip, db);
    }
  };
  auto doUndo = [this, ti, normClips]() {
    if (!m_trackModel) return;
    for (const auto &ci : normClips) {
      m_trackModel->bakeClipGain(ti, ci.clip, 1.0f / ci.factor);
    }
  };

  m_stack.push(new LambdaCommand(tr("Normalización dinámica"), doRedo, doUndo));
}

void UndoManager::dynamicNormalizeTracks(const QVariantList &tracks, double targetLufs) {
  if (!m_trackModel || tracks.isEmpty()) return;
  struct ClipInfo { int track; int clip; float factor; };
  QVector<ClipInfo> allClips;
  for (int i = 0; i < tracks.size(); ++i) {
    bool ok = false;
    int t = tracks[i].toInt(&ok);
    if (!ok || t < 0 || t >= m_trackModel->count()) continue;
    const QVariantList clips = m_trackModel->clipsOf(t);
    for (int c = 0; c < clips.size(); ++c) {
      const double lufs = m_trackModel->computeClipLoudnessLufs(t, c);
      if (lufs <= -99.0) continue;
      const double deltaDb = targetLufs - lufs;
      if (std::abs(deltaDb) < 0.01) continue;
      double g = std::pow(10.0, deltaDb / 20.0);
      g = std::clamp(g, 0.05, 8.0);

      const double peakDb = m_trackModel->computeClipPeakDb(t, c);
      if (peakDb > -120.0) {
        const double maxSafeDb = -1.0 - peakDb;
        const double maxSafeGain = std::pow(10.0, maxSafeDb / 20.0);
        if (g > maxSafeGain && maxSafeGain > 0.05)
          g = maxSafeGain;
      }

      allClips.append({t, c, float(g)});
    }
  }
  if (allClips.isEmpty()) return;

  auto doRedo = [this, allClips]() {
    if (!m_trackModel) return;
    for (const auto &ci : allClips) {
      m_trackModel->bakeClipGain(ci.track, ci.clip, ci.factor);
      const double db = 20.0 * std::log10(std::max(double(ci.factor), 1e-8));
      emit dynNormClipApplied(ci.track, ci.clip, db);
    }
  };
  auto doUndo = [this, allClips]() {
    if (!m_trackModel) return;
    for (const auto &ci : allClips) {
      m_trackModel->bakeClipGain(ci.track, ci.clip, 1.0f / ci.factor);
    }
  };

  m_stack.push(new LambdaCommand(tr("Normalización dinámica"), doRedo, doUndo));
}

void UndoManager::dynamicNormalizeClip(int trackIndex, int clipIndex, double targetLufs) {
  if (!m_trackModel) return;
  const double lufs = m_trackModel->computeClipLoudnessLufs(trackIndex, clipIndex);
  if (lufs <= -99.0) return;
  const double deltaDb = targetLufs - lufs;
  if (std::abs(deltaDb) < 0.01) return;
  double g = std::pow(10.0, deltaDb / 20.0);
  g = std::clamp(g, 0.05, 8.0);

  // Limitar ganancia para no superar -1 dBTP
  const double peakDb = m_trackModel->computeClipPeakDb(trackIndex, clipIndex);
  if (peakDb > -120.0) {
    const double maxSafeDb = -1.0 - peakDb;
    const double maxSafeGain = std::pow(10.0, maxSafeDb / 20.0);
    if (g > maxSafeGain && maxSafeGain > 0.05)
      g = maxSafeGain;
  }

  const float factor = float(g);
  auto doRedo = [this, trackIndex, clipIndex, factor]() {
    if (!m_trackModel) return;
    m_trackModel->bakeClipGain(trackIndex, clipIndex, factor);
    const double db = 20.0 * std::log10(std::max(double(factor), 1e-8));
    emit dynNormClipApplied(trackIndex, clipIndex, db);
  };
  auto doUndo = [this, trackIndex, clipIndex, factor]() {
    if (!m_trackModel) return;
    m_trackModel->bakeClipGain(trackIndex, clipIndex, 1.0f / factor);
  };

  m_stack.push(new LambdaCommand(tr("Normalización dinámica de clip"), doRedo, doUndo));
}

// ============================================================================
//  deleteTrackRegions — borrado por-pista (Shift+drag) reversible
// ============================================================================
void UndoManager::deleteTrackRegions(const QVariantList &regions,
                                     bool rippleClose) {
  if (!m_trackModel || regions.isEmpty())
    return;

  // Reunir las pistas afectadas y guardar un snapshot completo de sus
  // clips antes de borrar. Al deshacer, restauramos ese snapshot.
  QSet<int> trackSet;
  for (const QVariant &v : regions) {
    const QVariantMap m = v.toMap();
    trackSet.insert(m.value("track").toInt());
  }
  QVector<int> affectedTracks;
  for (int t : trackSet)
    affectedTracks.append(t);

  QVector<QVariantList> before(affectedTracks.size());
  for (int i = 0; i < affectedTracks.size(); ++i) {
    before[i] = m_trackModel->clipsOf(affectedTracks[i]);
  }

  auto doRedo = [this, regions, rippleClose]() {
    if (!m_trackModel)
      return;
    for (const QVariant &v : regions) {
      const QVariantMap m = v.toMap();
      const int track = m.value("track").toInt();
      const double s = m.value("start").toDouble();
      const double e = m.value("end").toDouble();
      if (rippleClose)
        m_trackModel->deleteRegion(track, s, e);
      else
        m_trackModel->deleteRegionNoRipple(track, s, e);
    }
  };
  auto doUndo = [this, affectedTracks, before]() {
    if (!m_trackModel)
      return;
    for (int i = 0; i < affectedTracks.size(); ++i) {
      const int trackIdx = affectedTracks[i];
      // Vaciar los clips actuales de esta pista.
      const QVariantList cur = m_trackModel->clipsOf(trackIdx);
      QVariantList allIdx;
      for (int k = 0; k < cur.size(); ++k)
        allIdx.append(k);
      if (!allIdx.isEmpty())
        m_trackModel->removeClips(trackIdx, allIdx, /*rippleClose=*/false);

      // Reinsertar el estado anterior.
      const int sr = m_trackModel->trackSampleRate(trackIdx);
      QVariantList snaps;
      for (const QVariant &v : before[i]) {
        const QVariantMap m = v.toMap();
        QVariantMap s;
        s["sourceIdx"] = m.value("sourceIdx", -1).toInt();
        s["timelineStart"] = qint64(m.value("startSec", 0).toDouble() * sr);
        s["sourceOffset"] =
            qint64(m.value("sourceOffsetSec", 0).toDouble() * sr);
        s["length"] = qint64(m.value("lengthSec", 0).toDouble() * sr);
        s["fadeInLen"] = qint64(m.value("fadeInSec", 0).toDouble() * sr);
        s["fadeOutLen"] = qint64(m.value("fadeOutSec", 0).toDouble() * sr);
        s["gain"] = m.value("gain", 1.0f).toFloat();
        s["envelope"] = m.value("envelope");
        snaps.append(s);
      }
      if (!snaps.isEmpty())
        m_trackModel->insertClipSnapshots(trackIdx, snaps);
    }
  };

  m_stack.push(new LambdaCommand(tr("Borrar regiones"), doRedo, doUndo));
}

void UndoManager::pasteClips(int trackIndex, const QVariantList &snapshots,
                             bool ripple) {
  if (!m_trackModel || snapshots.isEmpty())
    return;

  // Snapshot antes de la pista afectada
  const QVariant stateBefore = m_trackModel->saveTrackAudioState(trackIndex);

  auto doRedo = [this, trackIndex, snapshots, ripple]() {
    if (m_trackModel)
      m_trackModel->insertClipSnapshots(trackIndex, snapshots, ripple);
  };
  auto doUndo = [this, trackIndex, stateBefore]() {
    if (m_trackModel)
      m_trackModel->restoreTrackAudioState(trackIndex, stateBefore);
  };

  m_stack.push(new LambdaCommand(
      ripple ? tr("Pegar clips (ripple)") : tr("Pegar clips"), doRedo, doUndo));
}

void UndoManager::copyClipToTrack(int srcTrackIndex,
                                  const QVariantList &clipIndices,
                                  int dstTrackIndex, double timelineStartSec,
                                  bool ripple) {
  if (!m_trackModel || clipIndices.isEmpty())
    return;

  // Snapshot antes de la pista destino (donde se pega)
  const QVariant stateBefore = m_trackModel->saveTrackAudioState(dstTrackIndex);

  auto doRedo = [this, srcTrackIndex, clipIndices, dstTrackIndex,
                 timelineStartSec, ripple]() {
    if (m_trackModel)
      m_trackModel->copyClipToTrack(srcTrackIndex, clipIndices, dstTrackIndex,
                                    timelineStartSec, ripple);
  };
  auto doUndo = [this, dstTrackIndex, stateBefore]() {
    if (m_trackModel)
      m_trackModel->restoreTrackAudioState(dstTrackIndex, stateBefore);
  };

  m_stack.push(new LambdaCommand(ripple ? tr("Copiar y pegar clips (ripple)")
                                        : tr("Copiar y pegar clips"),
                                 doRedo, doUndo));
}

// ============================================================================
//  Capítulos (undoable)
// ============================================================================

void UndoManager::addChapterAtSec(double sec, const QString &title) {
    if (!m_chapterModel) return;
    const qint64 ms = qint64(sec * 1000.0);
    const QString t = title.isEmpty()
        ? tr("Capítulo %1").arg(m_chapterModel->count() + 1) : title;
    auto doRedo = [this, ms, t]() {
        if (m_chapterModel) m_chapterModel->addChapter(ms, t);
    };
    auto doUndo = [this, ms]() {
        if (!m_chapterModel) return;
        // Buscar el capítulo con ese startMs y eliminarlo
        for (int i = 0; i < m_chapterModel->count(); ++i) {
            if (m_chapterModel->chapterStartSec(i) * 1000.0 >= ms - 1
             && m_chapterModel->chapterStartSec(i) * 1000.0 <= ms + 1) {
                m_chapterModel->removeChapter(i);
                return;
            }
        }
    };
    m_stack.push(new LambdaCommand(tr("Añadir capítulo"), doRedo, doUndo));
}

void UndoManager::removeChapter(int idx) {
    if (!m_chapterModel || idx < 0 || idx >= m_chapterModel->count()) return;
    const qint64 ms = qint64(m_chapterModel->chapterStartSec(idx) * 1000.0);
    const QString title = m_chapterModel->chapterTitle(idx);
    const QString link = m_chapterModel->chapterLink(idx);
    const QString artwork = m_chapterModel->chapterArtwork(idx);
    auto doRedo = [this, ms]() {
        if (!m_chapterModel) return;
        for (int i = 0; i < m_chapterModel->count(); ++i) {
            if (qint64(m_chapterModel->chapterStartSec(i) * 1000.0) == ms) {
                m_chapterModel->removeChapter(i);
                return;
            }
        }
    };
    auto doUndo = [this, ms, title, link, artwork]() {
        if (!m_chapterModel) return;
        m_chapterModel->addChapter(ms, title, link);
        // Restaurar artwork
        if (!artwork.isEmpty()) {
            for (int i = 0; i < m_chapterModel->count(); ++i) {
                if (qint64(m_chapterModel->chapterStartSec(i) * 1000.0) == ms) {
                    m_chapterModel->setChapterArtwork(i, artwork);
                    break;
                }
            }
        }
    };
    m_stack.push(new LambdaCommand(tr("Eliminar capítulo"), doRedo, doUndo));
}

void UndoManager::moveChapterToSec(int idx, double sec) {
    if (!m_chapterModel || idx < 0 || idx >= m_chapterModel->count()) return;
    const double oldSec = m_chapterModel->chapterStartSec(idx);
    if (qAbs(oldSec - sec) < 0.001) return;
    const qint64 oldMs = qint64(oldSec * 1000.0);
    const qint64 newMs = qint64(sec * 1000.0);
    auto doRedo = [this, oldMs, newMs]() {
        if (!m_chapterModel) return;
        for (int i = 0; i < m_chapterModel->count(); ++i) {
            if (qint64(m_chapterModel->chapterStartSec(i) * 1000.0) == oldMs) {
                m_chapterModel->moveChapter(i, newMs);
                return;
            }
        }
    };
    auto doUndo = [this, oldMs, newMs]() {
        if (!m_chapterModel) return;
        for (int i = 0; i < m_chapterModel->count(); ++i) {
            if (qint64(m_chapterModel->chapterStartSec(i) * 1000.0) == newMs) {
                m_chapterModel->moveChapter(i, oldMs);
                return;
            }
        }
    };
    m_stack.push(new LambdaCommand(tr("Mover capítulo"), doRedo, doUndo));
}

void UndoManager::renameChapter(int idx, const QString &title) {
    if (!m_chapterModel || idx < 0 || idx >= m_chapterModel->count()) return;
    const QString oldTitle = m_chapterModel->chapterTitle(idx);
    if (oldTitle == title) return;
    const qint64 ms = qint64(m_chapterModel->chapterStartSec(idx) * 1000.0);
    auto doRedo = [this, ms, title]() {
        if (!m_chapterModel) return;
        for (int i = 0; i < m_chapterModel->count(); ++i) {
            if (qint64(m_chapterModel->chapterStartSec(i) * 1000.0) == ms) {
                m_chapterModel->renameChapter(i, title);
                return;
            }
        }
    };
    auto doUndo = [this, ms, oldTitle]() {
        if (!m_chapterModel) return;
        for (int i = 0; i < m_chapterModel->count(); ++i) {
            if (qint64(m_chapterModel->chapterStartSec(i) * 1000.0) == ms) {
                m_chapterModel->renameChapter(i, oldTitle);
                return;
            }
        }
    };
    m_stack.push(new LambdaCommand(tr("Renombrar capítulo"), doRedo, doUndo));
}

void UndoManager::clearChapters() {
    if (!m_chapterModel || m_chapterModel->count() == 0) return;
    const QJsonArray backup = m_chapterModel->toJson();
    auto doRedo = [this]() {
        if (m_chapterModel) m_chapterModel->clear();
    };
    auto doUndo = [this, backup]() {
        if (m_chapterModel) m_chapterModel->fromJson(backup);
    };
    m_stack.push(new LambdaCommand(tr("Eliminar todos los capítulos"), doRedo, doUndo));
}

void UndoManager::moveMultipleClips(const QVariantList &selectedClips,
                                    double deltaSec, int deltaTrack) {
  if (!m_trackModel || selectedClips.isEmpty())
    return;
  if (deltaSec == 0.0 && deltaTrack == 0)
    return;

  // Group selected clips by track index
  QMap<int, QVariantList> clipsByTrack;
  for (const QVariant &v : selectedClips) {
    QVariantMap m = v.toMap();
    int track = m.value("track").toInt();
    int clip = m.value("clip").toInt();
    clipsByTrack[track].append(clip);
  }

  // Find all affected tracks (both sources and their destinations)
  QSet<int> affectedTracks;
  QMap<int, int> srcToDst;
  for (auto it = clipsByTrack.constBegin(); it != clipsByTrack.constEnd(); ++it) {
    int srcTrack = it.key();
    int dstTrack = std::max(0, std::min(m_trackModel->count() - 1, srcTrack + deltaTrack));
    affectedTracks.insert(srcTrack);
    affectedTracks.insert(dstTrack);
    srcToDst[srcTrack] = dstTrack;
  }

  // Save snapshots of all affected tracks BEFORE doing anything
  QMap<int, QVariantList> trackSnapsBefore;
  for (int tIdx : affectedTracks) {
    QVariantList snaps;
    const QVariantList clips = m_trackModel->clipsOf(tIdx);
    for (int i = 0; i < clips.size(); ++i)
      snaps.append(m_trackModel->clipSnapshot(tIdx, i));
    trackSnapsBefore[tIdx] = snaps;
  }

  // Sort source track indices to avoid overwriting clips during movement
  // if deltaTrack > 0, process tracks from highest index to lowest
  // if deltaTrack <= 0, process tracks from lowest to highest
  QList<int> sortedSrcTracks = clipsByTrack.keys();
  if (deltaTrack > 0) {
    std::sort(sortedSrcTracks.begin(), sortedSrcTracks.end(), std::greater<int>());
  } else {
    std::sort(sortedSrcTracks.begin(), sortedSrcTracks.end(), std::less<int>());
  }

  auto doRedo = [this, sortedSrcTracks, clipsByTrack, deltaSec, srcToDst]() {
    if (!m_trackModel)
      return;
    for (int srcTrack : sortedSrcTracks) {
      const QVariantList &indices = clipsByTrack[srcTrack];
      int dstTrack = srcToDst[srcTrack];
      m_trackModel->moveClips(srcTrack, indices, deltaSec, dstTrack);
    }
  };

  auto doUndo = [this, affectedTracks, trackSnapsBefore]() {
    if (!m_trackModel)
      return;
    // Restore all affected tracks in any order
    for (int tIdx : affectedTracks) {
      const QVariantList cur = m_trackModel->clipsOf(tIdx);
      QVariantList allIdx;
      for (int i = 0; i < cur.size(); ++i)
        allIdx.append(i);
      if (!allIdx.isEmpty())
        m_trackModel->removeClips(tIdx, allIdx, false);
      m_trackModel->insertClipSnapshots(tIdx, trackSnapsBefore[tIdx]);
    }
  };

  const QString txt = (deltaTrack != 0) ? tr("Mover múltiples clips a otras pistas")
                                        : tr("Mover múltiples clips");
  m_stack.push(new LambdaCommand(txt, doRedo, doUndo));
}

void UndoManager::deleteMultipleClips(const QVariantList &selectedClips,
                                      bool rippleClose) {
  if (!m_trackModel || selectedClips.isEmpty())
    return;

  // Group by track
  QMap<int, QVariantList> clipsByTrack;
  for (const QVariant &v : selectedClips) {
    QVariantMap m = v.toMap();
    int track = m.value("track").toInt();
    int clip = m.value("clip").toInt();
    clipsByTrack[track].append(clip);
  }

  // Save snapshots/states of all affected tracks
  QMap<int, QVariantList> trackSnapsBefore;
  for (auto it = clipsByTrack.constBegin(); it != clipsByTrack.constEnd(); ++it) {
    int tIdx = it.key();
    QVariantList snaps;
    if (rippleClose) {
      const QVariantList cur = m_trackModel->clipsOf(tIdx);
      const int sr = m_trackModel->trackSampleRate(tIdx);
      for (const QVariant &cv : cur) {
        QVariantMap c = cv.toMap();
        QVariantMap s;
        s["sourceIdx"] = c.value("sourceIdx", -1).toInt();
        s["timelineStart"] = qint64(c.value("startSec", 0).toDouble() * sr);
        s["sourceOffset"] = qint64(c.value("sourceOffsetSec", 0).toDouble() * sr);
        s["length"] = qint64(c.value("lengthSec", 0).toDouble() * sr);
        s["fadeInLen"] = qint64(c.value("fadeInSec", 0).toDouble() * sr);
        s["fadeOutLen"] = qint64(c.value("fadeOutSec", 0).toDouble() * sr);
        s["gain"] = c.value("gain", 1.0f).toFloat();
        s["envelope"] = c.value("envelope");
        snaps.append(s);
      }
    } else {
      const QVariantList &indices = it.value();
      for (const QVariant &v : indices) {
        snaps.append(m_trackModel->clipSnapshot(tIdx, v.toInt()));
      }
    }
    trackSnapsBefore[tIdx] = snaps;
  }

  auto doRedo = [this, clipsByTrack, rippleClose]() {
    if (!m_trackModel)
      return;
    for (auto it = clipsByTrack.constBegin(); it != clipsByTrack.constEnd(); ++it) {
      m_trackModel->removeClips(it.key(), it.value(), rippleClose);
    }
  };

  auto doUndo = [this, clipsByTrack, trackSnapsBefore, rippleClose]() {
    if (!m_trackModel)
      return;
    for (auto it = clipsByTrack.constBegin(); it != clipsByTrack.constEnd(); ++it) {
      int tIdx = it.key();
      if (rippleClose) {
        const QVariantList cur = m_trackModel->clipsOf(tIdx);
        QVariantList allIdx;
        for (int k = 0; k < cur.size(); ++k)
          allIdx.append(k);
        if (!allIdx.isEmpty())
          m_trackModel->removeClips(tIdx, allIdx, false);
        m_trackModel->insertClipSnapshots(tIdx, trackSnapsBefore[tIdx]);
      } else {
        m_trackModel->insertClipSnapshots(tIdx, trackSnapsBefore[tIdx]);
      }
    }
  };

  m_stack.push(new LambdaCommand(rippleClose ? tr("Eliminar múltiples clips (ripple)")
                                             : tr("Eliminar múltiples clips"),
                                 doRedo, doUndo));
}

void UndoManager::copyMultipleClipsToTrack(const QVariantList &clipsToCopy,
                                           int targetTrackIndex, double timelineStartSec,
                                           bool ripple) {
  if (!m_trackModel || clipsToCopy.isEmpty())
    return;
  if (targetTrackIndex < 0 || targetTrackIndex >= m_trackModel->count())
    return;

  // 1. Find min source track index to calculate relative offsets
  int minSrcTrack = std::numeric_limits<int>::max();
  for (const QVariant &v : clipsToCopy) {
    int srcTrack = v.toMap().value("sourceTrack").toInt();
    if (srcTrack < minSrcTrack)
      minSrcTrack = srcTrack;
  }

  // 2. Group clips by source track
  QMap<int, QVariantList> clipsBySrcTrack;
  for (const QVariant &v : clipsToCopy) {
    QVariantMap m = v.toMap();
    int srcTrack = m.value("sourceTrack").toInt();
    int clipIdx = m.value("clipIndex").toInt();
    clipsBySrcTrack[srcTrack].append(clipIdx);
  }

  // 3. Map destination tracks and save snapshots of destination tracks
  QMap<int, int> srcToDst;
  QSet<int> affectedDstTracks;
  for (auto it = clipsBySrcTrack.constBegin(); it != clipsBySrcTrack.constEnd(); ++it) {
    int srcTrack = it.key();
    int dstTrack = std::min(m_trackModel->count() - 1, targetTrackIndex + (srcTrack - minSrcTrack));
    srcToDst[srcTrack] = dstTrack;
    affectedDstTracks.insert(dstTrack);
  }

  // Save states of all destination tracks before pasting
  QMap<int, QVariant> dstStatesBefore;
  for (int dstTrack : affectedDstTracks) {
    dstStatesBefore[dstTrack] = m_trackModel->saveTrackAudioState(dstTrack);
  }

  auto doRedo = [this, clipsBySrcTrack, srcToDst, timelineStartSec, ripple]() {
    if (!m_trackModel)
      return;
    // Copy clips track-by-track
    for (auto it = clipsBySrcTrack.constBegin(); it != clipsBySrcTrack.constEnd(); ++it) {
      int srcTrack = it.key();
      int dstTrack = srcToDst[srcTrack];
      m_trackModel->copyClipToTrack(srcTrack, it.value(), dstTrack, timelineStartSec, ripple);
    }
  };

  auto doUndo = [this, affectedDstTracks, dstStatesBefore]() {
    if (!m_trackModel)
      return;
    // Restore original track audio states
    for (int dstTrack : affectedDstTracks) {
      m_trackModel->restoreTrackAudioState(dstTrack, dstStatesBefore[dstTrack]);
    }
  };

  m_stack.push(new LambdaCommand(ripple ? tr("Copiar y pegar múltiples clips (ripple)")
                                        : tr("Copiar y pegar múltiples clips"),
                                 doRedo, doUndo));
}

void UndoManager::pasteMultipleClipSnapshots(int targetTrackIndex,
                                             const QVariantList &snapshotsWithTrack,
                                             double timelineStartSec,
                                             bool ripple) {
  if (!m_trackModel || snapshotsWithTrack.isEmpty())
    return;
  if (targetTrackIndex < 0 || targetTrackIndex >= m_trackModel->count())
    return;

  // 1. Find min source track index to calculate relative offsets
  int minSrcTrack = std::numeric_limits<int>::max();
  for (const QVariant &v : snapshotsWithTrack) {
    int srcTrack = v.toMap().value("sourceTrack").toInt();
    if (srcTrack < minSrcTrack)
      minSrcTrack = srcTrack;
  }

  // 2. Group snapshots by destination track index
  QMap<int, QVariantList> snapsByDstTrack;
  for (const QVariant &v : snapshotsWithTrack) {
    QVariantMap m = v.toMap();
    int srcTrack = m.value("sourceTrack").toInt();
    QVariantMap snap = m.value("snapshot").toMap();
    int dstTrack = std::min(m_trackModel->count() - 1, targetTrackIndex + (srcTrack - minSrcTrack));
    snapsByDstTrack[dstTrack].append(snap);
  }

  // Save states of all affected destination tracks
  QMap<int, QVariant> dstStatesBefore;
  for (auto it = snapsByDstTrack.constBegin(); it != snapsByDstTrack.constEnd(); ++it) {
    dstStatesBefore[it.key()] = m_trackModel->saveTrackAudioState(it.key());
  }

  // Redo: paste snapshots to each destination track
  auto doRedo = [this, snapsByDstTrack, timelineStartSec, ripple]() {
    if (!m_trackModel)
      return;

    for (auto it = snapsByDstTrack.constBegin(); it != snapsByDstTrack.constEnd(); ++it) {
      int dstTrack = it.key();
      const QVariantList &snaps = it.value();

      // Find earliest timelineStart in frames for these snaps to align with timelineStartSec
      qint64 minStart = std::numeric_limits<qint64>::max();
      for (const QVariant &v : snaps) {
        qint64 start = v.toMap().value("timelineStart", 0).toLongLong();
        if (start < minStart)
          minStart = start;
      }

      int sr = m_trackModel->trackSampleRate(dstTrack);
      if (sr <= 0) sr = 48000;
      qint64 pasteFrame = std::round(timelineStartSec * sr);

      // Adjust timelineStart for each snapshot relative to pasteFrame
      QVariantList adjustedSnaps;
      for (const QVariant &v : snaps) {
        QVariantMap snap = v.toMap();
        qint64 origStart = snap.value("timelineStart", 0).toLongLong();
        snap["timelineStart"] = pasteFrame + (origStart - minStart);
        adjustedSnaps.append(snap);
      }

      m_trackModel->insertClipSnapshots(dstTrack, adjustedSnaps, ripple);
    }
  };

  auto doUndo = [this, dstStatesBefore]() {
    if (!m_trackModel)
      return;
    for (auto it = dstStatesBefore.constBegin(); it != dstStatesBefore.constEnd(); ++it) {
      m_trackModel->restoreTrackAudioState(it.key(), it.value());
    }
  };

  m_stack.push(new LambdaCommand(ripple ? tr("Pegar múltiples clips (ripple)")
                                        : tr("Pegar múltiples clips"),
                                 doRedo, doUndo));
}
