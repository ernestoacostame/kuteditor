#pragma once
// ============================================================================
// KutEditor · Carpeta de trabajo para datos temporales grandes
// ----------------------------------------------------------------------------
// El audio largo (decodificado y mapeado en memoria) pesa varios GB. /tmp suele ser un tmpfs
// (RAM, p. ej. Bazzite/Fedora: la mitad de la RAM): se llena y el programa muere con SIGBUS.
// Por eso los temporales van a la caché del usuario (disco), o a $KUTEDITOR_SCRATCH.
// ============================================================================
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QString>

namespace KutScratch {

inline QString dir()
{
    QString d = qEnvironmentVariable("KUTEDITOR_SCRATCH");
    if (d.isEmpty()) {
        d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (d.isEmpty()) d = QDir::tempPath();
        d += QStringLiteral("/scratch");
    }
    QDir().mkpath(d);
    return d;
}

/// Bytes libres en la carpeta de trabajo (-1 si no se puede saber).
inline qint64 freeBytes()
{
    const QStorageInfo si(dir());
    return si.isValid() ? si.bytesAvailable() : -1;
}

/// Nombre de archivo único para este proceso: <prefijo>_<pid>_<n>.<ext>
inline QString uniqueFile(const QString &prefix, const QString &ext)
{
    static std::atomic<int> counter{0};
    return dir() + QStringLiteral("/%1_%2_%3.%4").arg(prefix).arg(QCoreApplication::applicationPid())
                       .arg(counter.fetch_add(1)).arg(ext);
}

/// Borra los restos de ejecuciones anteriores que murieron (el nombre lleva el PID de su dueño).
inline int cleanupStale()
{
    int removed = 0;
    const QDir d(dir());
    const auto files = d.entryInfoList({QStringLiteral("kut_*_*_*.*"), QStringLiteral("import_*_*_*.*")}, QDir::Files);
    for (const QFileInfo &fi : files) {
        const QStringList parts = fi.baseName().split(QLatin1Char('_'));
        bool ok = false;
        // kut_mmap_<pid>_<n>  /  import_<pid>_<n>
        const qint64 pid = parts.size() >= 3 ? parts[parts.size() - 2].toLongLong(&ok) : 0;
        if (!ok || pid <= 0) continue;
        if (pid == QCoreApplication::applicationPid()) continue;
        if (QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid))) continue;   // sigue vivo
        if (QFile::remove(fi.absoluteFilePath())) ++removed;
    }
    return removed;
}

} // namespace KutScratch
