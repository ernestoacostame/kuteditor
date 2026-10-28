#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QPointer>
#include <QHash>

#ifdef HAVE_KUTPOD

class QNetworkAccessManager;
class QNetworkReply;
class QHttpMultiPart;

/**
 * KutPodClient: cliente HTTP para hablar con una instancia de KutPod.
 *
 * Endpoints que consume (todos relativos al "baseUrl" que pasa el usuario):
 *   POST /api/auth/login              { user, password }    → { token, user{...} }
 *   GET  /api/podcasts                                       → [ { id, name, slug, cover, author, ... } ]
 *   GET  /api/podcasts/{id}/episodes?page=N&per_page=10&q=…  → { items:[…], total, page, pages }
 *   POST /api/podcasts/{id}/episodes  (multipart)            → { id, slug, audio_url, ... }
 *
 * El cliente persiste sesión (baseUrl + token + user) en QSettings para que
 * el usuario no tenga que reescribir credenciales al reabrir KutEditor.
 *
 * Diseño:
 *   - Singleton inyectado a QML como "KutPod".
 *   - Todas las llamadas son async, emiten señales con resultado.
 *   - El publish soporta progreso (publishProgress) y subida de cover,
 *     audio, capítulos JSON, imágenes de capítulos y transcripción.
 */
class KutPodClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString baseUrl     READ baseUrl     NOTIFY sessionChanged)
    Q_PROPERTY(QString currentUser READ currentUser NOTIFY sessionChanged)
    Q_PROPERTY(bool    loggedIn    READ loggedIn    NOTIFY sessionChanged)
    Q_PROPERTY(bool    busy        READ busy        NOTIFY busyChanged)
    Q_PROPERTY(QVariantList podcasts READ podcasts  NOTIFY podcastsChanged)

public:
    explicit KutPodClient(QObject *parent = nullptr);

    QString baseUrl()     const { return m_baseUrl; }
    QString currentUser() const { return m_user; }
    bool    loggedIn()    const { return !m_token.isEmpty(); }
    bool    busy()        const { return m_busy; }
    QVariantList podcasts() const { return m_podcasts; }

    // ── Sesión ─────────────────────────────────────────────────────
    Q_INVOKABLE void login(const QString &baseUrl,
                           const QString &user,
                           const QString &password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void restoreSession();   // intenta recuperar de QSettings

    // ── Listado ────────────────────────────────────────────────────
    Q_INVOKABLE void fetchPodcasts();
    /** Devuelve los podcasts ya cargados, sin volver a pedirlos. */
    Q_INVOKABLE QVariantList cachedPodcasts() const { return m_podcasts; }

    /**
     * Pide episodios de un podcast con paginación + búsqueda.
     * Emite episodesLoaded(podcastId, items[], total, page, pages).
     * @param page       1-indexado
     * @param perPage    por defecto 10
     * @param query      texto de búsqueda (vacío = sin filtro)
     */
    Q_INVOKABLE void fetchEpisodes(const QString &podcastId,
                                   int page = 1,
                                   int perPage = 10,
                                   const QString &query = QString());

    // ── Publicación ────────────────────────────────────────────────
    /**
     * Publica un episodio en KutPod.
     *
     * data esperado (todas las claves opcionales salvo audioPath):
     *   {
     *     "title": "...",            // requerido
     *     "description": "...",
     *     "episodeNumber": 12,
     *     "seasonNumber": 1,
     *     "author": "...",
     *     "audioPath": "/abs/path/episode.mp3",  // REQUERIDO
     *     "coverPath": "/abs/path/cover.jpg",
     *     "transcriptPath": "/abs/path/transcript.srt",
     *     "chapters": [
     *        { "startTime": 0.0, "title": "Intro",
     *          "img": "/abs/path/ch0.png",  // se sube
     *          "url": "https://..." },
     *        ...
     *     ]
     *   }
     *
     * Emite:
     *   publishProgress(0..100)
     *   publishFinished(success, episodeUrl, errorMessage)
     */
    Q_INVOKABLE void publishEpisode(const QString &podcastId,
                                    const QVariantMap &data);

    /**
     * Actualiza un episodio en KutPod.
     * Igual que publishEpisode, pero envía POST a /episodes/{episodeId}
     */
    Q_INVOKABLE void updateEpisode(const QString &podcastId,
                                   const QString &episodeId,
                                   const QVariantMap &data);

    Q_INVOKABLE QString lastError() const { return m_lastError; }

signals:
    void sessionChanged();
    void busyChanged();
    void podcastsChanged();

    void loginSuccess(QString user);
    void loginFailed(QString reason);

    void podcastsLoaded(QVariantList items);
    void podcastsFailed(QString reason);

    void episodesLoaded(QString podcastId, QVariantList items,
                        int total, int page, int pages);
    void episodesFailed(QString podcastId, QString reason);

    void publishProgress(int percent);
    void publishFinished(bool success, QString episodeUrl, QString errorMessage);

    void episodeUpdateFinished(bool success, QString episodeUrl, QString errorMessage);

private:
    void setBusy(bool b);
    void saveSession() const;
    void loadSession();

    QUrl endpoint(const QString &path) const;
    QNetworkReply *get(const QString &path);
    QNetworkReply *postJson(const QString &path, const QByteArray &json);
    QNetworkReply *postMultipart(const QString &path, QHttpMultiPart *mp);

    /// Lee un JSON {} desde reply; rellena ok/err.
    QVariantMap readJsonObject(QNetworkReply *r, bool &ok, QString &err) const;
    QVariantList readJsonArray(QNetworkReply *r, bool &ok, QString &err) const;

    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl;
    QString m_user;
    QString m_token;
    QString m_lastError;
    bool    m_busy = false;
    QVariantList m_podcasts;
};

#endif // HAVE_KUTPOD
