// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProcessInfo.h"

#include <algorithm>

namespace ProcessInfo
{

QString commandName(const QStringList &args, const QString &comm)
{
    QString name = args.value(0);
    if (name.isEmpty()) {
        name = comm;
    }
    // tmux's parse_window_name, which turns argv[0] into pane_current_command.
    if (name.startsWith(QLatin1Char('"'))) {
        name.remove(0, 1);
    }
    if (const qsizetype quote = name.indexOf(QLatin1Char('"')); quote >= 0) {
        name.truncate(quote);
    }
    if (name.startsWith(QLatin1String("exec "))) {
        name.remove(0, 5);
    }
    qsizetype start = 0;
    while (start < name.size() && (name.at(start) == QLatin1Char(' ') || name.at(start) == QLatin1Char('-'))) {
        ++start;
    }
    name.remove(0, start);
    if (const qsizetype space = name.indexOf(QLatin1Char(' ')); space >= 0) {
        name.truncate(space);
    }
    while (!name.isEmpty() && (name.back().isSpace() || !name.back().isPrint())) {
        name.chop(1);
    }
    // Only a path loses its directory; a name such as "kworker/0:1" keeps its slash.
    if (name.startsWith(QLatin1Char('/')) || name.startsWith(QLatin1String("./")) || name.startsWith(QLatin1String("../"))) {
        name.remove(0, name.lastIndexOf(QLatin1Char('/')) + 1);
    }
    return name;
}

QStringList foregroundNames(qint64 shellPid, int ptyFd)
{
    const qint64 group = foregroundGroup(ptyFd);
    if (group <= 0) {
        return {};
    }
    QStringList names;
    auto add = [&names](const Process &p) {
        const QString name = commandName(p);
        if (!name.isEmpty() && !names.contains(name)) {
            names.append(name);
        }
    };
    // Once the leader is gone, its pid may belong to an unrelated process.
    if (const std::optional<Process> leader = process(group); leader && leader->pgid == group) {
        add(*leader);
    }
    QList<Process> members = tree(shellPid);
    members.erase(std::remove_if(members.begin(), members.end(), [group](const Process &p) { return p.pgid != group; }), members.end());
    std::sort(members.begin(), members.end(), [](const Process &a, const Process &b) { return a.pid < b.pid; });
    for (const Process &p : std::as_const(members)) {
        add(p);
    }
    return names;
}

} // namespace ProcessInfo
