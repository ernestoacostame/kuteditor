#pragma once

#include <QObject>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QStringList>
#include <QDateTime>

/**
 * Helper de filesystem para QML.
 * Registrado como singleton "FileHelper".
 */
class FileHelper : public QObject
{
    Q_OBJECT
public:
    explicit FileHelper(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE bool exists(const QString &path) const {
        return QFileInfo::exists(path);
    }

    Q_INVOKABLE bool fileExists(const QString &path) const {
        return QFile::exists(path);
    }

    Q_INVOKABLE QString homeDir() const {
        return QDir::homePath();
    }

    Q_INVOKABLE QString configDir() const {
        return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }

    Q_INVOKABLE QStringList listDir(const QString &path) const {
        QDir d(path);
        if (!d.exists()) return {};
        return d.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    }

    Q_INVOKABLE bool ensureDir(const QString &path) const {
        return QDir().mkpath(path);
    }

    Q_INVOKABLE QStringList listDirs(const QString &path) const {
        QDir dir(path);
        if (!dir.exists()) return {};
        return dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    }

    Q_INVOKABLE QStringList listFiles(const QString &path, const QString &filter = "") const {
        QDir dir(path);
        if (!dir.exists()) return {};
        if (filter.isEmpty())
            return dir.entryList(QDir::Files, QDir::Name);
        return dir.entryList(QStringList() << filter, QDir::Files, QDir::Name);
    }

    Q_INVOKABLE QString readTextFile(const QString &path) const {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        QTextStream in(&f);
        return in.readAll();
    }

    Q_INVOKABLE qint64 lastModified(const QString &path) const {
        QFileInfo fi(path);
        if (!fi.exists()) return 0;
        return fi.lastModified().toMSecsSinceEpoch();
    }

    Q_INVOKABLE bool writeTextFile(const QString &path, const QString &content) const {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&f);
        out << content;
        return true;
    }

    Q_INVOKABLE bool removeFile(const QString &path) const {
        return QFile::remove(path);
    }

    Q_INVOKABLE bool removeDir(const QString &path) const {
        return QDir(path).removeRecursively();
    }

    Q_INVOKABLE bool copyFile(const QString &src, const QString &dst) const {
        if (QFile::exists(dst)) QFile::remove(dst);
        return QFile::copy(src, dst);
    }

    Q_INVOKABLE QString fileName(const QString &path) const {
        return QFileInfo(path).fileName();
    }

    Q_INVOKABLE QString baseName(const QString &path) const {
        return QFileInfo(path).baseName();
    }

    Q_INVOKABLE bool renameDir(const QString &oldPath, const QString &newName) const {
        QDir dir(oldPath);
        if (!dir.exists()) return false;
        // Renombrar la carpeta (no la ruta completa, solo el nombre final)
        QDir parent = dir;
        parent.cdUp();
        return parent.rename(QFileInfo(oldPath).fileName(), newName);
    }
};
