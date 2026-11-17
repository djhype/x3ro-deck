#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// What runs in a session's terminal, for the pane poll and detected links.
// QtCore only. The platform layer (foregroundGroup, process, tree) lives in
// one file per OS: ProcessInfoLinux.cpp reads /proc; ProcessInfoNone.cpp
// stands in where nothing is implemented yet.
namespace ProcessInfo
{

struct Process
{
    qint64 pid = 0;
    qint64 ppid = 0;
    qint64 pgid = 0;
    QString comm; // the kernel's short name
    QStringList args; // empty for zombies and kernel threads
};

// ── platform layer ──

// The terminal's foreground process group, or 0.
qint64 foregroundGroup(int ptyFd);
std::optional<Process> process(qint64 pid);
// `root` and its descendants, parents before children; empty when `root` is gone.
QList<Process> tree(qint64 root);

// ── shared ──

// What tmux shows as pane_current_command: argv[0], or `comm` when that is
// empty, cut at the first space, without a leading "exec " or '-', and
// without the directory of a path. tmux keeps a relative one ("./x"); this
// drops it, the same as a custom profile's default `process`.
QString commandName(const QStringList &args, const QString &comm);
inline QString commandName(const Process &p)
{
    return commandName(p.args, p.comm);
}

// Names of the terminal's foreground job: the group leader first (tmux's
// pane_current_command), then the other members of its group under
// `shellPid`, in pid order and without duplicates. An agent started through a
// wrapper (`node codex.js`, a shell script) is then still found by name.
QStringList foregroundNames(qint64 shellPid, int ptyFd);

} // namespace ProcessInfo
