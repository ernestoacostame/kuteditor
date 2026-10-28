#pragma once

#include <QAbstractListModel>
#include <QMetaType>
#include <QColor>
#include <QMutex>
#include <QRecursiveMutex>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

#include "audio/TrackFxChain.h"
#include "audio/AudioMipmap.h"
#include "audio/MmapAudioBuffer.h"
#include <memory>

/**
 * Modelo de pistas con arquitectura de CLIPS.
 *
 * Cada pista tiene:
 *  - metadatos (nombre, color, mute/solo, gain, device, armada)
 *  - uno o más AudioSource (buffers reales de samples de grabaciones / imports)
 *  - una lista ordenada de Clip que referencian rangos de un source y los
 *    sitúan en la línea temporal con un offset (timelineStart).
 *
 * Esto permite: split, delete region (cerrando brecha), mover clips, etc.
 *
 * La API externa mantiene compatibilidad con el resto del código: métodos
 * como trackSamples/trackSamplesData/trackPeaks devuelven vistas aplanadas
 * como si la pista fuese un buffer continuo con silencios en las brechas.
 */
class TrackModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(int armedCount READ armedCount NOTIFY armedChanged)
  Q_PROPERTY(
      bool hasNoiseProfile READ hasNoiseProfile NOTIFY noiseProfileChanged)
  Q_PROPERTY(QVariantList noiseProfileMagnitudes READ noiseProfileMagnitudes
                 NOTIFY noiseProfileChanged)
public:
  enum TrackRoles {
    NameRole = Qt::UserRole + 1,
    ColorRole,
    InputDeviceRole,
    InputDeviceNameRole,
    InputChannelIndexRole,
    InputModeRole,
    GainRole,
    PanRole,
    MutedRole,
    SoloRole,
    ArmedRole,
    DurationRole,
    HasAudioRole
  };

  struct AudioSource {
    std::shared_ptr<QVector<float>> samples = std::make_shared<QVector<float>>(); // interleaved PCM (heap)
    std::shared_ptr<MmapAudioBuffer> mmapBuffer; // alternativa mmap para sources grandes
    QVector<float> peaks;   // una peak por PEAK_WINDOW_FRAMES frames
    int sampleRate = 48000;
    int channels = 2;
    std::shared_ptr<QVector<float>> processedSamples; // Cache para RNNoise (compartido)
    std::shared_ptr<AudioMipmap> mipmap;
    bool isDirty = true;

    /// Puntero a los samples (mmap o heap, transparente).
    const float *samplesConstData() const {
      if (mmapBuffer && mmapBuffer->isValid()) return mmapBuffer->constData();
      return samples ? samples->constData() : nullptr;
    }
    /// Número total de floats (interleaved).
    qint64 samplesSize() const {
      if (mmapBuffer && mmapBuffer->isValid()) return mmapBuffer->size();
      return samples ? samples->size() : 0;
    }
    /// ¿Usa mmap?
    bool isMmapped() const { return mmapBuffer && mmapBuffer->isValid(); }

    const QVector<float>& samplesVec() const { return samples ? *samples : m_emptyVec; }
    QVector<float>& samplesVec() { if (!samples) samples = std::make_shared<QVector<float>>(); return *samples; }

    const QVector<float>& processedVec() const { return processedSamples ? *processedSamples : m_emptyVec; }
    QVector<float>& processedVec() { if (!processedSamples) processedSamples = std::make_shared<QVector<float>>(); return *processedSamples; }

  private:
    static inline const QVector<float> m_emptyVec{};
  };

  struct Clip {
    int sourceIdx = -1;
    qint64 timelineStart = 0;
    qint64 sourceOffset = 0;
    qint64 length = 0;
    qint64 fadeInLen = 0;
    qint64 fadeOutLen = 0;
    float gain = 1.0f;
    bool muted = false;
    QVector<QPointF> envelope;
  };

  struct TrackAudioState {
    QVector<AudioSource> sources;
    QVector<Clip> clips;
  };

  explicit TrackModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex &index, const QVariant &value,
               int role) override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void addTrack(const QString &name);
  Q_INVOKABLE void insertTrackAt(int index, const QString &name);
  Q_INVOKABLE void removeTrack(int index);
  Q_INVOKABLE void updateTrackColor(int index, const QColor &color);
  Q_INVOKABLE void updateInputDevice(int index, const QString &deviceId,
                                     const QString &deviceName);
  Q_INVOKABLE void updateInputChannel(int index, int mode, int channelIndex);
  Q_INVOKABLE void updateGain(int index, float gain);
  Q_INVOKABLE void updatePan(int index, float pan);
  Q_INVOKABLE void toggleMute(int index);
  Q_INVOKABLE void toggleSolo(int index);
  Q_INVOKABLE void setArmed(int index, bool armed);
  Q_INVOKABLE void toggleArmed(int index);
  Q_INVOKABLE void updateTrackName(int index, const QString &name);
  Q_INVOKABLE void moveTrack(int from, int to);
  Q_INVOKABLE QVariantMap getTrackData(int index) const;
  Q_INVOKABLE void clearAudio(int index);
  Q_INVOKABLE TrackFxChain *trackFxChain(int index);

  int count() const { return m_tracks.size(); }
  int armedCount() const;

  Q_INVOKABLE QList<int> armedTrackIndices() const;
  Q_INVOKABLE QString trackDeviceId(int index) const;

  /**
   * Escribe samples en la pista, empezando en startFrame (línea temporal
   * absoluta del proyecto). Si la pista está marcada como "en grabación"
   * (beginRecording), los samples se añaden a ese source y el clip se
   * extiende. En caso contrario, se crea un source+clip nuevo para este
   * chunk (escenario no esperado en uso normal pero seguro).
   */
  void writeSamplesAt(int index, qint64 startFrame, const float *interleaved,
                      int nFrames, int sampleRate, int channels);

  /**
   * Marca el inicio de una toma de grabación en la pista.
   * Crea un AudioSource y un Clip que empieza en startFrame.
   * Devuelve el índice del clip creado, o -1 si falla.
   */
  int beginRecording(int index, qint64 startFrame, int sampleRate,
                     int channels);

  /// Marca el fin de la grabación actual en la pista (libera el "clip activo").
  void finishRecording(int index);

  /// Silencia una región temporal [startSec, endSec). Semántica actual:
  /// pone a cero los samples dentro del rango, no corta ni cierra brecha.
  /// (Se sustituirá por deleteRegion en la Ronda 2.)
  Q_INVOKABLE void clearRegion(int index, double startSec, double endSec);

  /// Parte los clips de la pista en el frame indicado. Si algún clip cubre
  /// (estrictamente contiene) ese frame, lo divide en dos. Si index < 0,
  /// aplica a todas las pistas. Idempotente: si ya hay un borde ahí, no
  /// pasa nada.
  Q_INVOKABLE void splitClipsAt(int index, double atSec);

  /// Elimina una región temporal [startSec, endSec):
  ///  - Clips totalmente dentro → se borran.
  ///  - Clips que cruzan los bordes → se recortan.
  ///  - Clips posteriores → se desplazan hacia atrás para cerrar la brecha.
  /// Si index < 0, aplica a todas las pistas no muteadas.
  /// Devuelve el número de frames de brecha eliminada (fin-inicio).
  Q_INVOKABLE qint64 deleteRegion(int index, double startSec, double endSec);

  /// Como deleteRegion pero SIN desplazar clips posteriores. Corta los
  /// clips que se solapen con la región y elimina solo la parte central,
  /// dejando un hueco del mismo tamaño. Usado con ripple OFF.
  Q_INVOKABLE qint64 deleteRegionNoRipple(int index, double startSec,
                                          double endSec);

  /// Snapshot de los clips de una pista para la UI. Cada entrada es un
  /// QVariantMap con { startSec, endSec, sourceIdx, sourceOffsetSec,
  /// lengthSec }.
  Q_INVOKABLE QVariantList clipsOf(int index) const;

  /// Peaks de un clip concreto (aplanados, una entrada cada PEAK_WINDOW_FRAMES
  /// del source). El consumidor debe mapear por posición relativa al clip.
  Q_INVOKABLE QVariantList clipPeaks(int trackIndex, int clipIndex,
                                     int pixelWidth = 0) const;
  Q_INVOKABLE QVariantList clipPeaksVisible(int trackIndex, int clipIndex,
                                            double fracStart, double fracEnd,
                                            int pixelWidth) const;

  /// Returns 1 peak per exact pixel duration, guaranteeing 1:1 mapping for live drawing.
  Q_INVOKABLE QVariantList clipPeaksLive(int trackIndex, int clipIndex,
                                         double secPerPixel) const;

  /// Elimina clips concretos por índice (sin cerrar brecha).
  /// Devuelve snapshots de los clips borrados para poder deshacer.
  Q_INVOKABLE QVariantList removeClips(int trackIndex,
                                       const QVariantList &clipIndices,
                                       bool rippleClose = false);

  /// Inserta clips descritos por snapshots en una pista. El orden final se
  /// reordena por timelineStart. Usa los mismos sources que ya existen en
  /// la pista (por sourceIdx). Si sourceIdx apunta fuera de range, ignora.
  Q_INVOKABLE void insertClipSnapshots(int trackIndex,
                                       const QVariantList &snapshots,
                                       bool ripple = false);

  /// Mueve clips (por índice) dentro de una pista y opcionalmente a otra.
  /// - deltaSec: desplazamiento temporal a aplicar a cada clip.
  /// - dstTrackIndex: pista destino (si distinta de srcTrackIndex).
  /// Si cambia de pista, los sources del clip se copian a la pista destino
  /// y los sourceIdx se reescriben para que sigan siendo consistentes.
  /// Devuelve la lista de nuevos índices de los clips movidos en la pista
  /// destino (para mantener selección tras la operación).
  Q_INVOKABLE QVariantList moveClips(int srcTrackIndex,
                                     const QVariantList &clipIndices,
                                     double deltaSec, int dstTrackIndex);

  /// Copia clips de una pista a otra (o a la misma) en C++ puro, sin
  /// pasar samples por QML. El source se duplica internamente en la pista
  /// destino. Cada clip se coloca en timelineStartSec + su offset relativo
  /// respecto al primer clip copiado.
  /// Devuelve el número de clips copiados exitosamente.
  Q_INVOKABLE int copyClipToTrack(int srcTrackIndex,
                                  const QVariantList &clipIndices,
                                  int dstTrackIndex, double timelineStartSec,
                                  bool ripple = false);

  /// Snapshot de un único clip (para undo).
  Q_INVOKABLE QVariantMap clipSnapshot(int trackIndex, int clipIndex) const;

  /// Duración total de la pista en frames (fin del clip más tardío).
  qint64 trackFrameCount(int index) const;

  /// Aplanado de la pista a un buffer continuo. Costoso; usar solo para
  /// export o acceso ocasional. Usa caché internamente.
  QVector<float> trackSamples(int index) const;
  Q_INVOKABLE QVariantList noiseProfileMagnitudes() const;
  Q_INVOKABLE bool hasNoiseProfile() const { return !m_noiseProfile.isEmpty(); }

  /// Puntero a la caché aplanada. Válido hasta la siguiente mutación.
  /// NOTA: Costoso en RAM. Usar readMixSegment() para playback en tiempo real.
  const float *trackSamplesData(int index, int *outFrames, int *outChannels,
                                int *outSampleRate) const;

  /// Lee un segmento de mezcla directamente de los clips de la pista,
  /// sin crear un flatCache intermedio. Escribe nFrames frames estéreo
  /// (interleaved) en outBuf. Los clips se mezclan con fades, gain,
  /// envelope y denoiser aplicados. outBuf debe tener espacio para
  /// nFrames * channels floats (se asume 2 canales).
  /// Esta es la ruta de bajo consumo de memoria para playback en tiempo real.
  void readMixSegment(int index, qint64 startFrame, int nFrames,
                      float *outBuf) const;

    Q_INVOKABLE int trackSampleRate(int index) const;
    Q_INVOKABLE int trackChannels(int index) const;
    Q_INVOKABLE qint64 clipCurrentLength(int trackIndex, int clipIndex) const;
    Q_INVOKABLE QVector<float> clipRawPeaks(int trackIndex, int clipIndex) const;

  struct MixerSnapshot {
    bool valid = false;
    bool muted = false;
    bool solo = false;
    bool armed = false;
    float gain = 1.0f;
    float pan = 0.0f;
  };
  MixerSnapshot mixerSnapshot(int index) const;

  bool anyTrackInSolo() const;

  static constexpr int PEAK_WINDOW_FRAMES = 1024;
  void
  appendPeak(int index,
             float peak); // legacy; equivalente a peaks del clip en grabación
  Q_INVOKABLE QVariantList trackPeaks(int index) const;
  std::shared_ptr<AudioMipmap> clipMipmap(int trackIndex, int clipIndex) const;
  void rebuildPeaks(int index);
  void clearPeaks(int index);

  void updateTrackLevel(int index, float leftLevel, float rightLevel);
  Q_INVOKABLE float getTrackLevelLeft(int index) const;
  Q_INVOKABLE float getTrackLevelRight(int index) const;
  Q_INVOKABLE float getTrackPan(int index) const;

  /// Longitud en segundos del clip actualmente en grabación en una pista,
  /// o -1 si ninguna pista está grabando. Se consulta para refrescar el
  /// ancho del rectángulo del clip en vivo durante grabación sin tener que
  /// emitir clipsChanged en cada chunk (lo que haría parpadear el Repeater).
  Q_INVOKABLE double recordingClipLengthSec(int trackIndex) const;
  /// Índice del clip activo de grabación en la pista (o -1).
  Q_INVOKABLE int recordingClipIndex(int trackIndex) const;

  /// Nº de sources en una pista.
  int trackSourceCount(int trackIndex) const;

  Q_INVOKABLE bool isSourceDirty(int trackIndex, int sourceIdx) const;
  Q_INVOKABLE void setSourceDirty(int trackIndex, int sourceIdx, bool dirty);
  Q_INVOKABLE bool isSourceRecording(int trackIndex, int sourceIdx) const;
  Q_INVOKABLE void stopRecoveryWorker();

  /// Copia del buffer de samples de un source concreto de una pista.
  /// Thread-safe (toma el mutex).
  QVector<float> trackSourceSamples(int trackIndex, int sourceIdx,
                                    int *outSampleRate, int *outChannels) const;

  /// QML-friendly version that returns sample data without output parameters.
  /// Returns an empty QVariantMap if the source doesn't exist.
  Q_INVOKABLE QVariantMap getSourceSamples(int trackIndex, int sourceIdx) const;

  /// Limpia el modelo entero (para cargar un proyecto nuevo encima).
  /// Emite todas las señales necesarias para que la UI se repinte.
  void reset();

  /// Añade una pista "vacía" configurable al cargar un proyecto. Devuelve
  /// el índice de la pista creada.
  int addTrackFromSnapshot(const QVariantMap &trackMeta);

  /// Añade un source a una pista (desde WAV cargado). Devuelve el
  /// sourceIdx creado.
  Q_INVOKABLE int addSourceToTrack(int trackIndex,
                                   const QVector<float> &samples,
                                   int sampleRate, int channels);
  /// Overload con move semantics: toma ownership del buffer sin copiarlo.
  /// Ideal para imports donde el caller ya no necesita los samples.
  int addSourceToTrack(int trackIndex, QVector<float> &&samples,
                       int sampleRate, int channels);

  /// Añade un clip a una pista dado un snapshot {sourceIdx, timelineStart,
  /// sourceOffset, length}. No ordena ni valida más allá de sourceIdx.
  Q_INVOKABLE void addClipRaw(int trackIndex, int sourceIdx,
                              qint64 timelineStart, qint64 sourceOffset,
                              qint64 length, qint64 fadeInLen = 0,
                              qint64 fadeOutLen = 0, float gain = 1.0f,
                              const QVariantList &envelope = QVariantList());

  /// Re-ordena los clips de una pista por timelineStart. Útil tras cargar.
  Q_INVOKABLE void sortClips(int trackIndex);

  /// Busca el borde de clip más cercano (inicio o fin) en todas las pistas,
  /// que esté dentro de una distancia máxima `thresholdSec`.
  /// Si encuentra uno, devuelve ese tiempo. Si no, devuelve `timeSec`.
  Q_INVOKABLE double findNearestClipEdge(double timeSec, double thresholdSec) const;


  /// Fusiona clips seleccionados de una pista en un solo clip continuo.
  /// Crea un nuevo source con los samples renderizados (fades + gain
  /// aplicados) y reemplaza los clips originales.
  /// Devuelve el número de clips resultantes.
  Q_INVOKABLE int mergeClips(int trackIndex, const QVariantList &clipIndices);

  /// Fusiona TODOS los clips de una pista en un único clip continuo.
  Q_INVOKABLE int mergeAllClips(int trackIndex);

  /// Ajusta los bordes de un clip (trimming).
  /// deltaStartSec: cuánto mover el borde izquierdo (positivo = acortar, negativo = alargar).
  /// deltaEndSec: cuánto mover el borde derecho (positivo = alargar, negativo = acortar).
  /// Si live = true, modifica el modelo y emite clipTrimmed(clipIndex) pero NO clipsChanged para evitar recargar el Repeater.
  Q_INVOKABLE bool trimClip(int trackIndex, int clipIndex, double deltaStartSec, double deltaEndSec, bool live = false);

  /// Elimina los huecos de silencio entre clips de una pista,
  /// desplazando cada clip para que empiece donde termina el anterior.
  /// Si trackIndex < 0, aplica a todas las pistas.
  /// Devuelve el número total de frames eliminados.
  Q_INVOKABLE qint64 removeGaps(int trackIndex);

  /// Define los fades (en segundos) de un clip. Valores negativos se
  /// clampan a 0; el total fadeIn+fadeOut se clampa a la longitud del clip.
  Q_INVOKABLE void setClipFades(int trackIndex, int clipIndex, double fadeInSec,
                                double fadeOutSec);
  Q_INVOKABLE double clipFadeInSec(int trackIndex, int clipIndex) const;
  Q_INVOKABLE double clipFadeOutSec(int trackIndex, int clipIndex) const;

  Q_INVOKABLE void setClipGain(int trackIndex, int clipIndex, float gain);
  Q_INVOKABLE void setClipMuted(int trackIndex, int clipIndex, bool muted);
  Q_INVOKABLE bool clipMuted(int trackIndex, int clipIndex) const;

  // ---- Envelopes (Automatización de Volumen) ----
  Q_INVOKABLE void addEnvelopeNode(int trackIndex, int clipIndex,
                                   double timeSec, float gain,
                                   bool autoAnchors = true);
  Q_INVOKABLE void removeEnvelopeNode(int trackIndex, int clipIndex,
                                      int nodeIndex);
  Q_INVOKABLE void setEnvelopeNode(int trackIndex, int clipIndex, int nodeIndex,
                                   double timeSec, float gain);
  Q_INVOKABLE QVariantList getEnvelopeNodes(int trackIndex,
                                            int clipIndex) const;

  /// Calcula el nivel RMS (dBFS) integrado de todo el audio de una pista,
  /// combinando ambos canales y contando solo los samples cubiertos por
  /// clips (ignora huecos). Devuelve -120 si la pista está vacía.
  Q_INVOKABLE double computeTrackLoudnessDb(int trackIndex) const;

  /// Aplica gain a cada pista con audio para llevar su RMS a targetDb.
  /// Pistas vacías se omiten. Clamp de gain a [0.05, 8.0] (evita extremos
  /// absurdos). Devuelve el número de pistas afectadas.
  Q_INVOKABLE int autoLevelTracks(double targetDb);

  /// "Bakea" el gain actual directamente en los samples de cada source de
  /// la pista, dejando el gain de la pista en 1.0. El waveform se redibuja
  /// con la amplitud real nueva. Útil al normalizar para que la onda
  /// refleje el cambio visualmente (como Audacity "Normalize").
  /// Devuelve el gain anterior (para undo).
  Q_INVOKABLE float bakeGainIntoSamples(int trackIndex);

  /// Calcula el LUFS integrado de un clip específico (BS.1770-4 simplificado).
  /// Devuelve un valor en LUFS (negativo). -100 si no se puede medir.
  Q_INVOKABLE double computeClipLoudnessLufs(int trackIndex, int clipIndex) const;

  /// Aplica una ganancia destructiva a un clip específico, recalculando peaks
  /// y mipmap. No modifica clip.gain; multiplica directamente los samples.
  /// Devuelve el factor aplicado.
  Q_INVOKABLE float bakeClipGain(int trackIndex, int clipIndex, float gainFactor);

  /// Devuelve el pico máximo (en dB) de un clip. -120 si no se puede medir.
  Q_INVOKABLE double computeClipPeakDb(int trackIndex, int clipIndex) const;

  /// Detecta automáticamente zumbido eléctrico (Hum) en un rango de tiempo.
  Q_INVOKABLE double detectHumFrequency(int trackIndex, double startSec, double endSec) const;

  /// Elimina los clics de boca/edición en una selección usando interpolación Hermite.
  Q_INVOKABLE int deClickSelection(int trackIndex, double startSec, double endSec, float thresholdDb, double sensitivity);

  // ---- Noise Reduction (sustracción espectral) ----
  /// Capturar perfil de ruido de una región del clip (en segundos).
  /// Si endSec <= startSec, usa los primeros 0.5s del clip.
  Q_INVOKABLE bool noiseProfileCapture(int trackIndex, int clipIndex,
                                       double startSec, double endSec);

  /// Aplicar reducción de ruido al clip usando el perfil capturado.
  /// reduction: fuerza de reducción (0-100, default 12 dB equiv).
  /// sensitivity: sensibilidad del umbral (0-24, default 6).
  Q_INVOKABLE bool noiseReduce(int trackIndex, int clipIndex, float reductionDb,
                               float sensitivity);

  // ---- Truncate Silence ----
  /// Detecta regiones de silencio y las trunca/elimina.
  /// thresholdDb: nivel por debajo del cual se considera silencio.
  /// minDurationSec: duración mínima de silencio para ser detectado.
  /// truncateToSec: duración a la que se trunca cada silencio (0 = eliminar).
  /// Retorna el número de regiones procesadas.
  Q_INVOKABLE int truncateSilence(int trackIndex, float thresholdDb,
                                  double minDurationSec, double truncateToSec);

  // ---- Guardar/restaurar estado completo de audio de una pista (para undo)
  // ----
  /// Guarda clips + sources + peaks de la pista en un QVariant opaco.
  Q_INVOKABLE QVariant saveTrackAudioState(int trackIndex) const;
  /// Restaura el estado guardado con saveTrackAudioState.
  Q_INVOKABLE void restoreTrackAudioState(int trackIndex,
                                          const QVariant &state);

  /// Importa un archivo de audio en una pista, creando un nuevo source y
  /// un nuevo clip que arranca en timelineStartSec. Acepta WAV nativo y,
  /// si ffmpeg está disponible, cualquier formato que ffmpeg decodifique.
  /// Devuelve true si se creó algo.
  Q_INVOKABLE bool importAudioFile(int trackIndex, const QString &filePath,
                                   double timelineStartSec);
  // --------------------------------------------------------------------

  /// Fuerza que todas las pistas tengan su flatCache actualizada. Debe
  /// llamarse antes de playback para que el mix no haga alocaciones en el
  /// hilo de audio.
  void prepareForPlayback();

  /// Libera el flatCache de todas las pistas para ahorrar memoria cuando no se reproduce.
  Q_INVOKABLE void freeFlatCaches();

  bool isValidIndex(int i) const { return i >= 0 && i < m_tracks.size(); }

signals:
  void countChanged();
  void armedChanged();
  void trackDeviceChanged(int index, const QString &deviceId);
  void trackArmedChanged(int index, bool armed);
  void trackPeaksUpdated(int index);
  void trackLevelsChanged();
  /// Emitida cuando la estructura de clips de una pista cambia (split,
  /// delete, move, ...). La UI la usa para repopular el Repeater.
  void clipsChanged(int index);
  void clipTrimmed(int trackIndex, int clipIndex, double newStartSec, double newLengthSec);
  /// Emitida cuando solo cambia gain o mute de un clip (sin reconstruir Repeater).
  void clipGainChanged(int trackIndex, int clipIndex);
  void noiseProfileChanged();

private slots:
  void checkRNNoiseProcessing(int trackIndex);
  void onRNNoiseFinished(int trackIndex, int sourceIdx, const QVector<float>& processedSamples);

private:
  struct Track {
    QString name;
    QColor color;
    QString inputDeviceId;
    QString inputDeviceName;
    int inputChannelIndex = -1;
    int inputMode = 0;
    float gain = 1.0f;
    float pan = 0.0f; // -1.0=L, 0=C, 1.0=R
    bool muted = false;
    bool solo = false;
    bool armed = false;

    // Cadena de efectos por pista
    TrackFxChain *fxChain = nullptr;

    QVector<AudioSource> sources;
    QVector<Clip> clips;

    // Caché del buffer aplanado para trackSamplesData.
    mutable QVector<float> flatCache;
    mutable bool flatCacheValid = false;

    // Índice del clip que está recibiendo samples ahora mismo. -1 si
    // ninguno.
    int recordingClipIdx = -1;

    float levelLeft = 0.0f;
    float levelRight = 0.0f;
  };

  // Helpers internos (asumen lock de m_bufMutex externo salvo los const).
  void splitClipsAtLocked(int index, qint64 atFrame);
  void invalidateFlatCache(Track &t);
  void rebuildFlatCache(const Track &t) const;
  mutable QRecursiveMutex m_bufMutex;

  // Mapa de workers de RNNoise activos por índice de pista
  QMap<int, class RNNoiseWorker*> m_noiseWorkers;
  class RecoveryWorker *m_recoveryWorker = nullptr;

  // Actualiza los peaks para un rango específico en coord source (frames)
  void updateSourcePeaksInRange(AudioSource &src, qint64 fromFrame,
                                qint64 toFrame);

  void emitRowChanged(int row, const QVector<int> &roles);

  QVector<Track> m_tracks;

  // Noise Reduction: perfil espectral (magnitudes promedio por bin FFT)
  QVector<float> m_noiseProfile;
  static constexpr int NR_FFT_SIZE = 2048;
};

Q_DECLARE_METATYPE(TrackModel::TrackAudioState)
