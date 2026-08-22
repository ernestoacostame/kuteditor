#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVector>
#include <algorithm>

#include "TrackModel.h"

/**
 * ChapterModel: modelo de capítulos para podcasting.
 *
 * Compatible con:
 *   - ID3v2.3 CHAP frames (vía FFmpeg FFMETADATA1)
 *   - Podcasting 2.0 chapters JSON
 *   - Formato interno de proyecto (.podcastproj)
 *
 * El endTime del último capítulo siempre coincide con la duración total
 * del episodio (actualizada dinámicamente desde AudioEngine).
 */
class ChapterModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(qint64 episodeDurationMs READ episodeDurationMs
               WRITE setEpisodeDurationMs NOTIFY episodeDurationMsChanged)

public:
    enum Roles {
        StartTimeRole = Qt::UserRole + 1,
        EndTimeRole,
        TitleRole,
        LinkRole,
        ArtworkRole,
        StartSecRole,
        EndSecRole,
    };

    struct Chapter {
        qint64  startMs = 0;
        QString title;
        QUrl    link;
        QString artworkPath;   // ruta a imagen de portada del capítulo
    };

    explicit ChapterModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    // ── QAbstractListModel ──────────────────────────────────────────
    int rowCount(const QModelIndex & = {}) const override { return m_chapters.size(); }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= m_chapters.size()) return {};
        const Chapter &c = m_chapters[index.row()];
        switch (role) {
        case StartTimeRole: return c.startMs;
        case EndTimeRole:   return endTimeMs(index.row());
        case TitleRole:     return c.title;
        case LinkRole:      return c.link;
        case ArtworkRole:   return c.artworkPath;
        case StartSecRole:  return c.startMs / 1000.0;
        case EndSecRole:    return endTimeMs(index.row()) / 1000.0;
        default: return {};
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override {
        if (!index.isValid() || index.row() >= m_chapters.size()) return false;
        Chapter &c = m_chapters[index.row()];
        switch (role) {
        case TitleRole:     c.title = value.toString(); break;
        case LinkRole:      c.link = value.toUrl(); break;
        case ArtworkRole:   c.artworkPath = value.toString(); break;
        case StartTimeRole: c.startMs = value.toLongLong(); sortAndNotify(); return true;
        default: return false;
        }
        emit dataChanged(index, index, {role});
        return true;
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {StartTimeRole, "startTime"},
            {EndTimeRole,   "endTime"},
            {TitleRole,     "title"},
            {LinkRole,      "link"},
            {ArtworkRole,   "artwork"},
            {StartSecRole,  "startSec"},
            {EndSecRole,    "endSec"},
        };
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override {
        if (!index.isValid()) return Qt::NoItemFlags;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    }

    // ── Propiedades ─────────────────────────────────────────────────
    int count() const { return m_chapters.size(); }

    qint64 episodeDurationMs() const { return m_episodeDurationMs; }
    void setEpisodeDurationMs(qint64 v) {
        if (v == m_episodeDurationMs) return;
        m_episodeDurationMs = v;
        emit episodeDurationMsChanged();
        if (!m_chapters.isEmpty()) {
            const int last = m_chapters.size() - 1;
            emit dataChanged(index(last), index(last), {EndTimeRole, EndSecRole});
        }
    }

    // ── API Q_INVOKABLE ─────────────────────────────────────────────

    Q_INVOKABLE void addChapter(qint64 startMs, const QString &title,
                                const QString &link = {}) {
        Chapter ch;
        ch.startMs = startMs;
        ch.title = title.isEmpty() ? tr("Capítulo %1").arg(m_chapters.size() + 1) : title;
        if (!link.isEmpty()) ch.link = QUrl(link);

        int pos = 0;
        while (pos < m_chapters.size() && m_chapters[pos].startMs < startMs) ++pos;
        beginInsertRows({}, pos, pos);
        m_chapters.insert(pos, ch);
        endInsertRows();
        emit countChanged();
    }

    Q_INVOKABLE void addChapterAtSec(double sec, const QString &title = {},
                                     const QString &link = {}) {
        addChapter(qint64(sec * 1000.0), title, link);
    }

    Q_INVOKABLE void removeChapter(int idx) {
        if (idx < 0 || idx >= m_chapters.size()) return;
        beginRemoveRows({}, idx, idx);
        m_chapters.removeAt(idx);
        endRemoveRows();
        emit countChanged();
    }

    Q_INVOKABLE void moveChapter(int idx, qint64 newStartMs) {
        if (idx < 0 || idx >= m_chapters.size()) return;
        m_chapters[idx].startMs = newStartMs;
        sortAndNotify();
    }

    Q_INVOKABLE void moveChapterToSec(int idx, double sec) {
        moveChapter(idx, qint64(sec * 1000.0));
    }

    Q_INVOKABLE void renameChapter(int idx, const QString &title) {
        if (idx < 0 || idx >= m_chapters.size()) return;
        m_chapters[idx].title = title;
        emit dataChanged(index(idx), index(idx), {TitleRole});
    }

    Q_INVOKABLE void setChapterLink(int idx, const QString &link) {
        if (idx < 0 || idx >= m_chapters.size()) return;
        m_chapters[idx].link = QUrl(link);
        emit dataChanged(index(idx), index(idx), {LinkRole});
    }

    Q_INVOKABLE void setChapterArtwork(int idx, const QString &path) {
        if (idx < 0 || idx >= m_chapters.size()) return;
        m_chapters[idx].artworkPath = path;
        emit dataChanged(index(idx), index(idx), {ArtworkRole});
    }

    Q_INVOKABLE QString chapterArtwork(int idx) const {
        if (idx < 0 || idx >= m_chapters.size()) return {};
        return m_chapters[idx].artworkPath;
    }

    Q_INVOKABLE double chapterStartSec(int idx) const {
        if (idx < 0 || idx >= m_chapters.size()) return 0;
        return m_chapters[idx].startMs / 1000.0;
    }

    Q_INVOKABLE QString chapterTitle(int idx) const {
        if (idx < 0 || idx >= m_chapters.size()) return {};
        return m_chapters[idx].title;
    }

    Q_INVOKABLE QString chapterLink(int idx) const {
        if (idx < 0 || idx >= m_chapters.size()) return {};
        return m_chapters[idx].link.toString();
    }

    Q_INVOKABLE qint64 snapToClipEdge(qint64 timeMs, qint64 thresholdMs) const {
        if (!m_trackModel) return timeMs;
        const double sec = timeMs / 1000.0;
        const double threshSec = thresholdMs / 1000.0;
        const double snapped = m_trackModel->findNearestClipEdge(sec, threshSec);
        return qint64(snapped * 1000.0);
    }

    Q_INVOKABLE double snapToClipEdgeSec(double sec, double threshSec) const {
        if (!m_trackModel) return sec;
        return m_trackModel->findNearestClipEdge(sec, threshSec);
    }

    /// Shift all chapters within [startSec, endSec) by deltaSec.
    /// Returns list of {oldMs, newMs} pairs for undo support.
    Q_INVOKABLE QVariantList shiftChaptersInRange(double startSec, double endSec,
                                                   double deltaSec) {
        QVariantList moved;
        if (m_chapters.isEmpty() || qFuzzyIsNull(deltaSec)) return moved;
        const qint64 startMs = qint64(startSec * 1000.0);
        const qint64 endMs   = qint64(endSec * 1000.0);
        const qint64 deltaMs = qint64(deltaSec * 1000.0);
        for (auto &ch : m_chapters) {
            if (ch.startMs >= startMs && ch.startMs < endMs) {
                QVariantMap pair;
                pair["oldMs"] = ch.startMs;
                const qint64 newMs = qMax(qint64(0), ch.startMs + deltaMs);
                pair["newMs"] = newMs;
                moved.append(pair);
                ch.startMs = newMs;
            }
        }
        if (!moved.isEmpty()) sortAndNotify();
        return moved;
    }

    /// Restore chapters from a list of {oldMs, newMs} pairs (undo of shift).
    Q_INVOKABLE void restoreChapterPositions(const QVariantList &moves) {
        if (moves.isEmpty()) return;
        for (const QVariant &v : moves) {
            const QVariantMap pair = v.toMap();
            const qint64 newMs = pair["newMs"].toLongLong();
            const qint64 oldMs = pair["oldMs"].toLongLong();
            for (auto &ch : m_chapters) {
                if (ch.startMs == newMs) {
                    ch.startMs = oldMs;
                    break;
                }
            }
        }
        sortAndNotify();
    }

    Q_INVOKABLE void clear() {
        if (m_chapters.isEmpty()) return;
        beginResetModel();
        m_chapters.clear();
        endResetModel();
        emit countChanged();
    }

    // ── Serialización ───────────────────────────────────────────────

    /// Podcasting 2.0 JSON con soporte de chapter images.
    Q_INVOKABLE QJsonArray toJson(bool forExport = false) const {
        QJsonArray arr;
        for (int i = 0; i < m_chapters.size(); ++i) {
            const Chapter &c = m_chapters[i];
            QJsonObject obj;
            obj["startTime"] = c.startMs / 1000.0;
            obj["title"] = c.title;
            if (!c.link.isEmpty())
                obj["url"] = c.link.toString();
            if (!c.artworkPath.isEmpty()) {
                if (forExport) {
                    if (c.artworkPath.startsWith("http://") || c.artworkPath.startsWith("https://")) {
                        obj["img"] = c.artworkPath;
                    }
                } else {
                    obj["img"] = c.artworkPath;
                }
            }
            arr.append(obj);
        }
        return arr;
    }

    Q_INVOKABLE void fromJson(const QJsonArray &arr) {
        beginResetModel();
        m_chapters.clear();
        for (const auto &v : arr) {
            const QJsonObject obj = v.toObject();
            Chapter ch;
            ch.startMs = qint64(obj["startTime"].toDouble() * 1000.0);
            ch.title = obj["title"].toString();
            if (obj.contains("url"))
                ch.link = QUrl(obj["url"].toString());
            if (obj.contains("img"))
                ch.artworkPath = obj["img"].toString();
            if (obj.contains("artwork"))
                ch.artworkPath = obj["artwork"].toString();
            m_chapters.append(ch);
        }
        std::sort(m_chapters.begin(), m_chapters.end(),
                  [](const Chapter &a, const Chapter &b) { return a.startMs < b.startMs; });
        endResetModel();
        emit countChanged();
    }

    /// FFMETADATA1 para FFmpeg. El endTime del último capítulo siempre
    /// es la duración total del episodio para cumplir el estándar ID3.
    QString toFfmetadata() const {
        QStringList lines;
        lines << ";FFMETADATA1";
        for (int i = 0; i < m_chapters.size(); ++i) {
            const Chapter &c = m_chapters[i];
            const qint64 endMs = endTimeMs(i);
            lines << ""
                  << "[CHAPTER]"
                  << "TIMEBASE=1/1000"
                  << QString("START=%1").arg(c.startMs)
                  << QString("END=%1").arg(endMs)
                  << QString("title=%1").arg(c.title);
        }
        return lines.join("\n") + "\n";
    }

    // ── Wiring ──────────────────────────────────────────────────────
    void setTrackModel(TrackModel *m) { m_trackModel = m; }
    const QVector<Chapter> &chapters() const { return m_chapters; }

signals:
    void countChanged();
    void episodeDurationMsChanged();

private:
    /// El endTime del último capítulo SIEMPRE es la duración total del episodio.
    /// Esto cumple con el estándar ID3 y evita "huecos" en reproductores.
    qint64 endTimeMs(int idx) const {
        if (idx + 1 < m_chapters.size())
            return m_chapters[idx + 1].startMs;
        // Último capítulo: usar duración del episodio (obligatorio para ID3)
        if (m_episodeDurationMs > 0)
            return m_episodeDurationMs;
        // Fallback si no se ha seteado la duración
        return m_chapters[idx].startMs + 60000;
    }

    void sortAndNotify() {
        std::sort(m_chapters.begin(), m_chapters.end(),
                  [](const Chapter &a, const Chapter &b) { return a.startMs < b.startMs; });
        emit dataChanged(index(0), index(m_chapters.size() - 1));
    }

    QVector<Chapter> m_chapters;
    qint64 m_episodeDurationMs = 0;
    TrackModel *m_trackModel = nullptr;
};
