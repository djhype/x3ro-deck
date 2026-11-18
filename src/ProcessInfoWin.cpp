// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// ProcessInfo's platform layer on Windows: a Toolhelp32 snapshot of the
// process table. There are no terminal process groups; the daemon asks for
// the session's tree and takes its deepest process as the foreground one.

#include "ProcessInfo.h"

#include <QHash>

#include <windows.h>

#include <tlhelp32.h>

namespace ProcessInfo
{

namespace
{
QList<Process> snapshot()
{
    QList<Process> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return out;
    }
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) {
        Process p;
        p.pid = qint64(e.th32ProcessID);
        p.ppid = qint64(e.th32ParentProcessID);
        QString exe = QString::fromWCharArray(e.szExeFile);
        if (exe.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
            exe.chop(4);
        }
        p.comm = exe;
        p.args = {exe};
        out.append(p);
    }
    CloseHandle(snap);
    return out;
}
} // namespace

qint64 foregroundGroup(int)
{
    return 0;
}

std::optional<Process> process(qint64 pid)
{
    for (const Process &p : snapshot()) {
        if (p.pid == pid) {
            return p;
        }
    }
    return std::nullopt;
}

QList<Process> tree(qint64 root)
{
    const QList<Process> all = snapshot();
    QHash<qint64, QList<Process>> children;
    std::optional<Process> top;
    for (const Process &p : all) {
        children[p.ppid].append(p);
        if (p.pid == root) {
            top = p;
        }
    }
    QList<Process> out;
    if (!top) {
        return out;
    }
    QList<Process> queue{*top};
    while (!queue.isEmpty()) {
        const Process p = queue.takeFirst();
        out.append(p);
        // Windows reuses pids; a "child" older than its parent is not one.
        for (const Process &c : children.value(p.pid)) {
            if (c.pid != p.pid) {
                queue.append(c);
            }
        }
    }
    return out;
}

} // namespace ProcessInfo
