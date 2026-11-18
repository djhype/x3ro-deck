#include "ListeningPorts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace ListeningPorts
{

QList<QPair<int, quint64>> parseNetTcp(const QByteArray &table, uint uid)
{
    QList<QPair<int, quint64>> out;
    const QList<QByteArray> lines = table.split('\n');
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> f = lines.at(i).simplified().split(' ');
        // sl local rem st tx:rx tr:when retrnsmt uid timeout inode
        if (f.size() < 10 || f.at(3) != "0A" || f.at(7).toUInt() != uid) {
            continue;
        }
        bool ok = false;
        const int port = f.at(1).mid(f.at(1).lastIndexOf(':') + 1).toInt(&ok, 16);
        const quint64 inode = f.at(9).toULongLong();
        if (ok && port > 0 && inode > 0) {
            out.append({port, inode});
        }
    }
    return out;
}

QList<Listener> scan()
{
    QList<Listener> out;
#if defined(Q_OS_LINUX)
    const uint uid = ::getuid();
    QHash<quint64, int> ports; // socket inode → port
    for (const char *table : {"/proc/net/tcp", "/proc/net/tcp6"}) {
        QFile f{QString::fromLatin1(table)};
        if (f.open(QIODevice::ReadOnly)) {
            for (const auto &[port, inode] : parseNetTcp(f.readAll(), uid)) {
                ports.insert(inode, port);
            }
        }
    }
    if (ports.isEmpty()) {
        return out;
    }
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool numeric = false;
        const int p = pid.toInt(&numeric);
        if (!numeric || QFileInfo(QStringLiteral("/proc/") + pid).ownerId() != uid) {
            continue;
        }
        const QString fdDir = QStringLiteral("/proc/%1/fd").arg(pid);
        QList<int> found;
        for (const QFileInfo &fd : QDir(fdDir).entryInfoList(QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
            const QString target = QFile::symLinkTarget(fd.filePath());
            // "socket:[12345]", made absolute by Qt, so find it in the tail.
            const qsizetype at = target.indexOf(QLatin1String("socket:["));
            if (at < 0) {
                continue;
            }
            const quint64 inode = target.mid(at + 8).chopped(1).toULongLong();
            if (const auto it = ports.constFind(inode); it != ports.cend() && !found.contains(*it)) {
                found.append(*it);
            }
        }
        if (found.isEmpty()) {
            continue;
        }
        QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
        const QString name = comm.open(QIODevice::ReadOnly) ? QString::fromUtf8(comm.readAll()).trimmed() : QString();
        const QString cwd = QFile::symLinkTarget(QStringLiteral("/proc/%1/cwd").arg(pid));
        for (int port : std::as_const(found)) {
            out.append({p, name, cwd, port});
        }
    }
#endif
    return out;
}

} // namespace ListeningPorts
