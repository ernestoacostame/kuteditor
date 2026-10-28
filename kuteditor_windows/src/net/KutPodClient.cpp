#include "KutPodClient.h"

#ifdef HAVE_KUTPOD

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrlQuery>

// ─────────────────────────────────────────────────────────────────────
//  Utils
// ─────────────────────────────────────────────────────────────────────
static QString cleanBaseUrl(QString u)
{
    u = u.trimmed();
    if (u.isEmpty()) return u;
    if (!u.startsWith("http://", Qt::CaseInsensitive) &&
        !u.startsWith("https://", Qt::CaseInsensitive))
        u.prepend("https://");
    while (u.endsWith('/')) u.chop(1);
    return u;
}

static QString mimeForFile(const QString &path)
{
    static QMimeDatabase db;
    const QString mt = db.mimeTypeForFile(path).name();
    return mt.isEmpty() ? QStringLiteral("application/octet-stream") : mt;
}

static void addFilePart(QHttpMultiPart *mp,
                        const QString &name,
                        const QString &path)
{
    if (path.isEmpty()) return;
    QString clean = path;
    if (clean.startsWith("file://")) clean = clean.mid(7);

    QFile *f = new QFile(clean);
    if (!f->open(QIODevice::ReadOnly)) {
        qWarning() << "[KutPodClient] no se pudo abrir para subir:" << clean;
        delete f;
        return;
    }
    QHttpPart part;
    const QString cd = QStringLiteral(
        "form-data; name=\"%1\"; filename=\"%2\"")
        .arg(name, QFileInfo(clean).fileName());
    part.setHeader(QNetworkRequest::ContentDispositionHeader, cd);
    part.setHeader(QNetworkRequest::ContentTypeHeader, mimeForFile(clean));
    f->setParent(mp);   // que el multipart se encargue de cerrarlo
    part.setBodyDevice(f);
    mp->append(part);
}

static void addTextPart(QHttpMultiPart *mp,
                        const QString &name,
                        const QString &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"").arg(name));
    part.setBody(value.toUtf8());
    mp->append(part);
}

// ─────────────────────────────────────────────────────────────────────
KutPodClient::KutPodClient(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    loadSession();
}

void KutPodClient::setBusy(bool b)
{
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged();
}

QUrl KutPodClient::endpoint(const QString &path) const
{
    QString p = path;
    if (!p.startsWith('/')) p.prepend('/');
    return QUrl(m_baseUrl + p);
}

QNetworkReply *KutPodClient::get(const QString &path)
{
    QNetworkRequest req(endpoint(path));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "KutEditor/0.3");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    return m_nam->get(req);
}

QNetworkReply *KutPodClient::postJson(const QString &path, const QByteArray &json)
{
    QNetworkRequest req(endpoint(path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "KutEditor/0.3");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    return m_nam->post(req, json);
}

QNetworkReply *KutPodClient::postMultipart(const QString &path, QHttpMultiPart *mp)
{
    QNetworkRequest req(endpoint(path));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "KutEditor/0.3");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    QNetworkReply *r = m_nam->post(req, mp);
    mp->setParent(r);
    return r;
}

QVariantMap KutPodClient::readJsonObject(QNetworkReply *r, bool &ok, QString &err) const
{
    ok = false;
    const QByteArray raw = r->readAll();
    if (r->error() != QNetworkReply::NoError) {
        err = r->errorString();
        // Si el server devolvió JSON con un campo "error"/"message", úsalo
        QJsonParseError je;
        const auto doc = QJsonDocument::fromJson(raw, &je);
        if (je.error == QJsonParseError::NoError && doc.isObject()) {
            const auto o = doc.object();
            if (o.contains("error"))    err = o["error"].toString();
            else if (o.contains("message")) err = o["message"].toString();
        }
        return {};
    }
    QJsonParseError je;
    const auto doc = QJsonDocument::fromJson(raw, &je);
    if (je.error != QJsonParseError::NoError) {
        err = QStringLiteral("Respuesta no es JSON válido: %1").arg(je.errorString());
        return {};
    }
    if (!doc.isObject()) {
        err = QStringLiteral("Se esperaba objeto JSON");
        return {};
    }
    ok = true;
    return doc.object().toVariantMap();
}

QVariantList KutPodClient::readJsonArray(QNetworkReply *r, bool &ok, QString &err) const
{
    ok = false;
    const QByteArray raw = r->readAll();
    if (r->error() != QNetworkReply::NoError) { err = r->errorString(); return {}; }
    QJsonParseError je;
    const auto doc = QJsonDocument::fromJson(raw, &je);
    if (je.error != QJsonParseError::NoError) {
        err = QStringLiteral("Respuesta no es JSON válido: %1").arg(je.errorString());
        return {};
    }
    if (doc.isArray()) { ok = true; return doc.array().toVariantList(); }
    if (doc.isObject()) {
        // Algunos endpoints devuelven { items:[…] }
        const auto o = doc.object();
        if (o.contains("items") && o["items"].isArray()) {
            ok = true; return o["items"].toArray().toVariantList();
        }
    }
    err = QStringLiteral("Se esperaba array JSON");
    return {};
}

// ─────────────────────────────────────────────────────────────────────
//  Sesión
// ─────────────────────────────────────────────────────────────────────
void KutPodClient::login(const QString &baseUrl, const QString &user, const QString &password)
{
    const QString b = cleanBaseUrl(baseUrl);
    if (b.isEmpty()) {
        emit loginFailed(tr("URL inválida"));
        return;
    }
    m_baseUrl = b;
    m_user    = user;
    m_token.clear();

    QJsonObject body;
    body["user"]     = user;
    body["password"] = password;
    const QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);

    setBusy(true);
    QNetworkReply *r = postJson("/api/auth/login", json);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        bool ok; QString err;
        const QVariantMap m = readJsonObject(r, ok, err);
        r->deleteLater();
        setBusy(false);
        if (!ok) {
            m_lastError = err;
            emit loginFailed(err.isEmpty() ? tr("No se pudo iniciar sesión") : err);
            return;
        }
        m_token = m.value("token").toString();
        if (m.contains("user")) {
            const QVariantMap u = m["user"].toMap();
            if (u.contains("user"))      m_user = u["user"].toString();
            else if (u.contains("name")) m_user = u["name"].toString();
        }
        if (m_token.isEmpty()) {
            emit loginFailed(tr("El servidor no devolvió token"));
            return;
        }
        saveSession();
        emit sessionChanged();
        emit loginSuccess(m_user);
    });
}

void KutPodClient::logout()
{
    m_token.clear();
    m_podcasts.clear();
    emit podcastsChanged();
    saveSession();
    emit sessionChanged();
}

void KutPodClient::restoreSession()
{
    loadSession();
    emit sessionChanged();
}

void KutPodClient::saveSession() const
{
    QSettings s;
    s.beginGroup("KutPod");
    s.setValue("baseUrl", m_baseUrl);
    s.setValue("user",    m_user);
    s.setValue("token",   m_token);
    s.endGroup();
}

void KutPodClient::loadSession()
{
    QSettings s;
    s.beginGroup("KutPod");
    m_baseUrl = s.value("baseUrl").toString();
    m_user    = s.value("user").toString();
    m_token   = s.value("token").toString();
    s.endGroup();
}

// ─────────────────────────────────────────────────────────────────────
//  Listado de podcasts
// ─────────────────────────────────────────────────────────────────────
void KutPodClient::fetchPodcasts()
{
    if (!loggedIn()) { emit podcastsFailed(tr("No has iniciado sesión")); return; }
    setBusy(true);
    QNetworkReply *r = get("/api/podcasts");
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        bool ok; QString err;
        QVariantList items = readJsonArray(r, ok, err);
        r->deleteLater();
        setBusy(false);
        if (!ok) { m_lastError = err; emit podcastsFailed(err); return; }
        m_podcasts = items;
        emit podcastsChanged();
        emit podcastsLoaded(items);
    });
}

// ─────────────────────────────────────────────────────────────────────
//  Listado de episodios paginado + búsqueda
// ─────────────────────────────────────────────────────────────────────
void KutPodClient::fetchEpisodes(const QString &podcastId,
                                 int page, int perPage,
                                 const QString &query)
{
    if (!loggedIn()) { emit episodesFailed(podcastId, tr("No has iniciado sesión")); return; }

    QString path = QStringLiteral("/api/podcasts/%1/episodes").arg(podcastId);
    QUrlQuery q;
    q.addQueryItem("page", QString::number(qMax(1, page)));
    q.addQueryItem("per_page", QString::number(qMax(1, perPage)));
    if (!query.trimmed().isEmpty()) q.addQueryItem("q", query.trimmed());
    path += "?" + q.toString(QUrl::FullyEncoded);

    setBusy(true);
    QNetworkReply *r = get(path);
    connect(r, &QNetworkReply::finished, this, [this, r, podcastId]() {
        const QByteArray raw = r->readAll();
        const auto netErr = r->error();
        r->deleteLater();
        setBusy(false);
        if (netErr != QNetworkReply::NoError) {
            emit episodesFailed(podcastId, tr("Error de red: %1").arg(r->errorString()));
            return;
        }
        QJsonParseError je;
        const auto doc = QJsonDocument::fromJson(raw, &je);
        if (je.error != QJsonParseError::NoError) {
            emit episodesFailed(podcastId, tr("Respuesta no es JSON: %1").arg(je.errorString()));
            return;
        }
        QVariantList items;
        int total = 0, pg = 1, pages = 1;
        if (doc.isArray()) {
            items = doc.array().toVariantList();
            total = items.size(); pages = 1;
        } else if (doc.isObject()) {
            const auto o = doc.object();
            items = o.value("items").toArray().toVariantList();
            total = o.value("total").toInt(items.size());
            pg    = o.value("page").toInt(1);
            pages = o.value("pages").toInt(1);
        }
        emit episodesLoaded(podcastId, items, total, pg, pages);
    });
}

// ─────────────────────────────────────────────────────────────────────
//  Publicación de episodio
// ─────────────────────────────────────────────────────────────────────
void KutPodClient::publishEpisode(const QString &podcastId, const QVariantMap &data)
{
    if (!loggedIn()) {
        emit publishFinished(false, QString(), tr("No has iniciado sesión"));
        return;
    }
    const QString audio = data.value("audioPath").toString();
    if (audio.isEmpty() || !QFile::exists(audio.startsWith("file://") ? audio.mid(7) : audio)) {
        emit publishFinished(false, QString(), tr("Archivo de audio no encontrado"));
        return;
    }

    auto *mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // Campos texto
    addTextPart(mp, "title",         data.value("title").toString());
    addTextPart(mp, "description",   data.value("description").toString());
    addTextPart(mp, "author",        data.value("author").toString());
    if (data.contains("episodeNumber"))
        addTextPart(mp, "episode", QString::number(data.value("episodeNumber").toInt()));
    if (data.contains("seasonNumber"))
        addTextPart(mp, "season",  QString::number(data.value("seasonNumber").toInt()));
    if (data.contains("episodeType"))
        addTextPart(mp, "episode_type", data.value("episodeType").toString());
    if (data.contains("explicit"))
        addTextPart(mp, "explicit", data.value("explicit").toBool() ? "1" : "0");
    if (data.contains("publishAt") && !data.value("publishAt").toString().isEmpty())
        addTextPart(mp, "publish_at", data.value("publishAt").toString());

    // Audio
    addFilePart(mp, "audio", audio);

    // Cover (opcional)
    const QString cover = data.value("coverPath").toString();
    if (!cover.isEmpty()) addFilePart(mp, "cover", cover);

    // Transcripción (opcional, .srt/.vtt)
    const QString transcript = data.value("transcriptPath").toString();
    if (!transcript.isEmpty()) addFilePart(mp, "transcript", transcript);

    // Capítulos: JSON + imágenes asociadas
    const QVariantList chapters = data.value("chapters").toList();
    if (!chapters.isEmpty()) {
        QJsonArray jarr;
        int idx = 0;
        for (const QVariant &v : chapters) {
            const QVariantMap c = v.toMap();
            QJsonObject obj;
            obj["startTime"] = c.value("startTime").toDouble();
            obj["title"]     = c.value("title").toString();
            if (c.contains("url") && !c.value("url").toString().isEmpty())
                obj["url"] = c.value("url").toString();
            // Imagen del capítulo: si es local, se sube como campo separado
            // y el JSON referencia su nombre lógico que el server resolverá.
            const QString img = c.value("img").toString();
            if (!img.isEmpty()) {
                if (img.startsWith("http://") || img.startsWith("https://")) {
                    obj["img"] = img;
                } else {
                    const QString partName = QStringLiteral("chapter_img_%1").arg(idx);
                    addFilePart(mp, partName, img);
                    obj["img_ref"] = partName;   // el server reescribe esto a URL final
                }
            }
            jarr.append(obj);
            ++idx;
        }
        addTextPart(mp, "chapters", QString::fromUtf8(
            QJsonDocument(jarr).toJson(QJsonDocument::Compact)));
    }

    setBusy(true);
    emit publishProgress(0);

    const QString path = QStringLiteral("/api/podcasts/%1/episodes").arg(podcastId);
    QNetworkReply *r = postMultipart(path, mp);

    connect(r, &QNetworkReply::uploadProgress, this,
            [this](qint64 sent, qint64 total) {
        if (total <= 0) return;
        const int pct = int((sent * 100) / total);
        emit publishProgress(qBound(0, pct, 100));
    });

    connect(r, &QNetworkReply::finished, this, [this, r]() {
        bool ok; QString err;
        const QVariantMap m = readJsonObject(r, ok, err);
        r->deleteLater();
        setBusy(false);
        if (!ok) {
            m_lastError = err;
            emit publishFinished(false, QString(),
                                 err.isEmpty() ? tr("Error al publicar") : err);
            return;
        }
        QString epUrl = m.value("url").toString();
        if (epUrl.isEmpty()) {
            const QString slug = m.value("slug").toString();
            const QString podcastSlug = m.value("podcast_slug").toString();
            if (!slug.isEmpty() && !podcastSlug.isEmpty())
                epUrl = QStringLiteral("%1/@%2/%3").arg(m_baseUrl, podcastSlug, slug);
        }
        emit publishProgress(100);
        emit publishFinished(true, epUrl, QString());
        // Refrescar listado de episodios en background
    });
}

void KutPodClient::updateEpisode(const QString &podcastId, const QString &episodeId, const QVariantMap &data)
{
    if (!loggedIn()) {
        emit episodeUpdateFinished(false, QString(), tr("No has iniciado sesión"));
        return;
    }

    auto *mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // Campos texto
    if (data.contains("title"))
        addTextPart(mp, "title",         data.value("title").toString());
    if (data.contains("description"))
        addTextPart(mp, "description",   data.value("description").toString());
    if (data.contains("author"))
        addTextPart(mp, "author",        data.value("author").toString());
    if (data.contains("episodeNumber"))
        addTextPart(mp, "episode", QString::number(data.value("episodeNumber").toInt()));
    if (data.contains("seasonNumber"))
        addTextPart(mp, "season",  QString::number(data.value("seasonNumber").toInt()));
    if (data.contains("episodeType"))
        addTextPart(mp, "episode_type", data.value("episodeType").toString());
    if (data.contains("explicit"))
        addTextPart(mp, "explicit", data.value("explicit").toBool() ? "1" : "0");
    if (data.contains("publishAt") && !data.value("publishAt").toString().isEmpty())
        addTextPart(mp, "publish_at", data.value("publishAt").toString());

    // Audio (opcional en update)
    const QString audio = data.value("audioPath").toString();
    if (!audio.isEmpty() && QFile::exists(audio.startsWith("file://") ? audio.mid(7) : audio)) {
        addFilePart(mp, "audio", audio);
    }

    // Cover (opcional)
    const QString cover = data.value("coverPath").toString();
    if (!cover.isEmpty()) addFilePart(mp, "cover", cover);

    // Transcripción (opcional, .srt/.vtt)
    const QString transcript = data.value("transcriptPath").toString();
    if (!transcript.isEmpty()) addFilePart(mp, "transcript", transcript);

    // Capítulos: JSON + imágenes asociadas
    const QVariantList chapters = data.value("chapters").toList();
    if (!chapters.isEmpty()) {
        QJsonArray jarr;
        int idx = 0;
        for (const QVariant &v : chapters) {
            const QVariantMap c = v.toMap();
            QJsonObject obj;
            obj["startTime"] = c.value("startTime").toDouble();
            obj["title"]     = c.value("title").toString();
            if (c.contains("url") && !c.value("url").toString().isEmpty())
                obj["url"] = c.value("url").toString();
            const QString img = c.value("img").toString();
            if (!img.isEmpty()) {
                if (img.startsWith("http://") || img.startsWith("https://")) {
                    obj["img"] = img;
                } else {
                    const QString partName = QStringLiteral("chapter_img_%1").arg(idx);
                    addFilePart(mp, partName, img);
                    obj["img_ref"] = partName;
                }
            }
            jarr.append(obj);
            ++idx;
        }
        addTextPart(mp, "chapters", QString::fromUtf8(
            QJsonDocument(jarr).toJson(QJsonDocument::Compact)));
    }

    setBusy(true);
    emit publishProgress(0);

    const QString path = QStringLiteral("/api/podcasts/%1/episodes/%2").arg(podcastId, episodeId);
    QNetworkReply *r = postMultipart(path, mp);

    connect(r, &QNetworkReply::uploadProgress, this,
            [this](qint64 sent, qint64 total) {
        if (total <= 0) return;
        const int pct = int((sent * 100) / total);
        emit publishProgress(qBound(0, pct, 100));
    });

    connect(r, &QNetworkReply::finished, this, [this, r]() {
        bool ok; QString err;
        const QVariantMap m = readJsonObject(r, ok, err);
        r->deleteLater();
        setBusy(false);
        if (!ok) {
            m_lastError = err;
            emit episodeUpdateFinished(false, QString(),
                                 err.isEmpty() ? tr("Error al actualizar") : err);
            return;
        }
        QString epUrl = m.value("url").toString();
        if (epUrl.isEmpty()) {
            const QString slug = m.value("slug").toString();
            const QString podcastSlug = m.value("podcast_slug").toString();
            if (!slug.isEmpty() && !podcastSlug.isEmpty())
                epUrl = QStringLiteral("%1/@%2/%3").arg(m_baseUrl, podcastSlug, slug);
        }
        emit publishProgress(100);
        emit episodeUpdateFinished(true, epUrl, QString());
    });
}

#endif // HAVE_KUTPOD
