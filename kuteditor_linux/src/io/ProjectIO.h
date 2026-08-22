#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QPointer>
#include <memory>

class TrackModel;
class TranscriptionManager;

/**
 * ProjectIO: serialización y carga de proyectos a formato .podcastproj.
 *
 * Formato del archivo: tarball (uncompressed) con:
 *   project.json                 -> esquema completo del proyecto
 *   sources/track{N}_src{M}.wav  -> un WAV float32 por AudioSource
 *
 * project.json incluye:
 *   - version del formato
 *   - sampleRate global
 *   - metadata (título, podcaster, podcast, descripción, episodio, temporada,
 *     ruta relativa a portada si existe)
 *   - tracks[]: cada uno con metadatos de pista + sources[] + clips[]
 *
 * Funciona como singleton inyectado al QML.
 */
class ProjectIO : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(QString currentDisplayName READ currentDisplayName NOTIFY currentPathChanged)
    Q_PROPERTY(bool isDirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(QVariantMap metadata READ metadata WRITE setMetadata NOTIFY metadataChanged)
public:
    explicit ProjectIO(QObject *parent = nullptr);
    ~ProjectIO();

    void setTrackModel(TrackModel *m) { m_trackModel = m; }
    void setTranscriptionManager(TranscriptionManager *t) { m_transcription = t; }
    void setChapterModel(class ChapterModel *c) { m_chapterModel = c; }

    QString currentPath() const { return m_currentPath; }
    QString currentDisplayName() const;
    bool isDirty() const { return m_dirty; }
    QVariantMap metadata() const { return m_metadata; }
    void setMetadata(const QVariantMap &m);

    // API desde QML / main.qml

    /// Guarda el proyecto en path. Si está vacío, usa currentPath.
    Q_INVOKABLE bool save(const QString &path = QString());

    /// Abre un proyecto desde path. Limpia el modelo actual antes.
    Q_INVOKABLE bool open(const QString &path);

    /// Reinicia el proyecto a vacío (sin path, sin pistas).
    Q_INVOKABLE void newProject();

    Q_INVOKABLE void markDirty();

    /// Último error legible (si el return de save/open fue false).
    Q_INVOKABLE QString lastError() const { return m_lastError; }

    Q_INVOKABLE bool hasRecoveryProject() const;
    Q_INVOKABLE bool recoverProject();
    Q_INVOKABLE void discardRecovery();
    Q_INVOKABLE bool updateProjectPathAndTitle(const QString &oldProjPath, const QString &newProjPath, const QString &newTitle);

signals:
    void currentPathChanged();
    void dirtyChanged();
    void metadataChanged();
    void projectLoaded();
    void projectSaved();

private slots:
    void onModelMutated();
    void saveRecoveryBackup();

private:
    QPointer<TrackModel> m_trackModel = nullptr;
    QPointer<TranscriptionManager> m_transcription = nullptr;
    QPointer<ChapterModel> m_chapterModel = nullptr;
    QString m_currentPath;
    QString m_lastError;
    QVariantMap m_metadata;
    bool m_dirty = false;
    bool m_suspendDirty = false;
    class QTimer *m_recoveryTimer = nullptr;
    QString recoveryDir() const;
    QByteArray buildProjectJsonForRecovery() const;

    // Tempdir que mantiene vivos los recursos extraídos del .podcastproj
    // (portada y WAVs) mientras el proyecto esté cargado. Se destruye al
    // cargar otro proyecto o hacer newProject().
    std::unique_ptr<class QTemporaryDir> m_extractDir;

    void setCurrentPath(const QString &p);
    void setDirty(bool d);

    // Serialización JSON
    QByteArray buildProjectJson() const;
    bool applyProjectJson(const QByteArray &json, const QString &extractDir, bool isRecovery = false);

    // Tar I/O
    bool writeTarball(const QString &path,
                      const QByteArray &projectJson,
                      const QList<QPair<QString, QByteArray>> &files) const;
    bool readTarball(const QString &path,
                     QByteArray &projectJson,
                     const QString &sourcesDir) const;

    // WAV helpers (locales a este módulo para no depender de ExportManager)
    static bool writeWavFloat32(const QString &path,
                                const float *data, qint64 frames,
                                int sampleRate, int channels);
    static bool readWavFloat32(const QString &path,
                               QVector<float> &outSamples,
                               int &outSampleRate, int &outChannels);
};
