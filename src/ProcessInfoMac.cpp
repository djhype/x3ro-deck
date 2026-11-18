// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// ProcessInfo's platform layer on macOS: libproc for the process table and
// sysctl(KERN_PROCARGS2) for argument vectors.

#include "ProcessInfo.h"

#include <QSet>

#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <vector>

namespace ProcessInfo
{

namespace
{
// argv from KERN_PROCARGS2: argc, the executable path, padding, then argv.
QStringList arguments(pid_t pid)
{
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
    size_t size = 0;
    if (sysctl(mib, 3, nullptr, &size, nullptr, 0) != 0 || size < sizeof(int)) {
        return {};
    }
    std::vector<char> buf(size);
    if (sysctl(mib, 3, buf.data(), &size, nullptr, 0) != 0 || size < sizeof(int)) {
        return {};
    }
    int argc = 0;
    memcpy(&argc, buf.data(), sizeof(int));
    size_t at = sizeof(int);
    while (at < size && buf[at] != '\0') {
        ++at; // the executable path
    }
    while (at < size && buf[at] == '\0') {
        ++at;
    }
    QStringList out;
    while (at < size && out.size() < argc) {
        const size_t start = at;
        while (at < size && buf[at] != '\0') {
            ++at;
        }
        out.append(QString::fromLocal8Bit(buf.data() + start, qsizetype(at - start)));
        ++at;
    }
    return out;
}

QList<qint64> children(pid_t pid)
{
    std::vector<pid_t> pids(256);
    for (;;) {
        const int n = proc_listchildpids(pid, pids.data(), int(pids.size() * sizeof(pid_t)));
        if (n <= 0) {
            return {};
        }
        // Some releases report a count of pids, others bytes.
        size_t count = size_t(n) > pids.size() ? size_t(n) / sizeof(pid_t) : size_t(n);
        if (count < pids.size()) {
            QList<qint64> out;
            for (size_t i = 0; i < count; ++i) {
                if (pids[i] > 0) {
                    out.append(pids[i]);
                }
            }
            return out;
        }
        pids.resize(pids.size() * 2);
    }
}
} // namespace

qint64 foregroundGroup(int ptyFd)
{
    const pid_t pg = tcgetpgrp(ptyFd);
    return pg > 0 ? pg : 0;
}

std::optional<Process> process(qint64 pid)
{
    proc_bsdinfo info{};
    if (proc_pidinfo(pid_t(pid), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != int(sizeof(info))) {
        return std::nullopt;
    }
    Process p;
    p.pid = pid;
    p.ppid = info.pbi_ppid;
    p.pgid = info.pbi_pgid;
    p.comm = QString::fromLocal8Bit(info.pbi_comm);
    p.args = arguments(pid_t(pid));
    return p;
}

QList<Process> tree(qint64 root)
{
    QList<Process> out;
    QList<qint64> queue{root};
    QSet<qint64> seen;
    while (!queue.isEmpty()) {
        const qint64 pid = queue.takeFirst();
        if (seen.contains(pid)) {
            continue;
        }
        seen.insert(pid);
        if (const std::optional<Process> p = process(pid)) {
            out.append(*p);
            queue += children(pid_t(pid));
        }
    }
    return out;
}

} // namespace ProcessInfo
