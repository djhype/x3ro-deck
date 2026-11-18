// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ListeningPorts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#elif defined(Q_OS_MACOS)
#include <arpa/inet.h>
#include <libproc.h>
#include <sys/proc_info.h>
#include <unistd.h>

#include <vector>
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
#elif defined(Q_OS_MACOS)
    // libproc: every process of ours, its socket descriptors, and the TCP
    // ones in LISTEN.
    const uid_t uid = ::getuid();
    std::vector<pid_t> pids(4096);
    const int bytes = proc_listallpids(pids.data(), int(pids.size() * sizeof(pid_t)));
    const int count = bytes <= 0 ? 0 : std::min<int>(bytes, int(pids.size()));
    for (int i = 0; i < count; ++i) {
        const pid_t pid = pids[size_t(i)];
        proc_bsdinfo info{};
        if (pid <= 0 || proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != int(sizeof(info)) || info.pbi_uid != uid) {
            continue;
        }
        const int fdBytes = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
        if (fdBytes <= 0) {
            continue;
        }
        std::vector<proc_fdinfo> fds(size_t(fdBytes) / sizeof(proc_fdinfo));
        const int got = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(), fdBytes);
        QList<int> found;
        for (int f = 0; f < got / int(sizeof(proc_fdinfo)); ++f) {
            if (fds[size_t(f)].proc_fdtype != PROX_FDTYPE_SOCKET) {
                continue;
            }
            socket_fdinfo si{};
            if (proc_pidfdinfo(pid, fds[size_t(f)].proc_fd, PROC_PIDFDSOCKETINFO, &si, sizeof(si)) != int(sizeof(si))) {
                continue;
            }
            if (si.psi.soi_kind == SOCKINFO_TCP && si.psi.soi_proto.pri_tcp.tcpsi_state == TSI_S_LISTEN) {
                const int port = ntohs(static_cast<uint16_t>(si.psi.soi_proto.pri_tcp.tcpsi_ini.insi_lport));
                if (port > 0 && !found.contains(port)) {
                    found.append(port);
                }
            }
        }
        if (found.isEmpty()) {
            continue;
        }
        proc_vnodepathinfo paths{};
        const QString cwd = proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &paths, sizeof(paths)) == int(sizeof(paths))
            ? QString::fromLocal8Bit(paths.pvi_cdir.vip_path)
            : QString();
        char name[2 * MAXCOMLEN + 1] = {};
        proc_name(pid, name, sizeof(name));
        for (int port : std::as_const(found)) {
            out.append({int(pid), QString::fromLocal8Bit(name), cwd, port});
        }
    }
#endif
    return out;
}

} // namespace ListeningPorts
