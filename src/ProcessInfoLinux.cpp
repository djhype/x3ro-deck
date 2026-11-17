// ProcessInfo's platform layer on Linux: /proc.

#include "ProcessInfo.h"

#include <QHash>

#include <cerrno>
#include <cstdlib>

#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace
{
// A /proc file is small and changes under us, so one read() is the snapshot.
QByteArray readProcFile(const char *path)
{
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }
    QByteArray out;
    char buf[4096];
    ssize_t n = 0;
    while ((n = ::read(fd, buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR)) {
        if (n > 0) {
            out.append(buf, n);
        }
        if (out.size() > 256 * 1024) {
            break;
        }
    }
    ::close(fd);
    return out;
}

QByteArray procPath(qint64 pid, const char *file)
{
    return QByteArray("/proc/") + QByteArray::number(pid) + '/' + file;
}

// "pid (comm) state ppid pgrp …"; comm may hold spaces and parentheses.
bool readStat(qint64 pid, ProcessInfo::Process &p)
{
    const QByteArray stat = readProcFile(procPath(pid, "stat").constData());
    const qsizetype open = stat.indexOf('(');
    const qsizetype close = stat.lastIndexOf(')');
    if (open < 0 || close < open) {
        return false;
    }
    const QList<QByteArray> fields = stat.mid(close + 2).split(' ');
    if (fields.size() < 3) {
        return false;
    }
    p.pid = pid;
    p.comm = QString::fromLocal8Bit(stat.mid(open + 1, close - open - 1));
    p.ppid = fields.at(1).toLongLong();
    p.pgid = fields.at(2).toLongLong();
    return true;
}

QStringList readArgs(qint64 pid)
{
    QByteArray cmdline = readProcFile(procPath(pid, "cmdline").constData());
    if (cmdline.endsWith('\0')) {
        cmdline.chop(1);
    }
    QStringList args;
    if (!cmdline.isEmpty()) {
        for (const QByteArray &a : cmdline.split('\0')) {
            args.append(QString::fromLocal8Bit(a));
        }
    }
    return args;
}

QList<qint64> numericEntries(const char *dir)
{
    QList<qint64> out;
    DIR *d = opendir(dir);
    if (!d) {
        return out;
    }
    while (const dirent *e = readdir(d)) {
        char *end = nullptr;
        const long long n = std::strtoll(e->d_name, &end, 10);
        if (end != e->d_name && *end == '\0' && n > 0) {
            out.append(n);
        }
    }
    closedir(d);
    return out;
}

// Children of `pid` from /proc/<pid>/task/*/children. False when the kernel
// lacks CONFIG_PROC_CHILDREN.
bool childrenOf(qint64 pid, QList<qint64> &out)
{
    const QList<qint64> tasks = numericEntries(procPath(pid, "task").constData());
    bool any = false;
    for (const qint64 tid : tasks) {
        const QByteArray path = procPath(pid, "task/") + QByteArray::number(tid) + "/children";
        if (::access(path.constData(), R_OK) != 0) {
            continue;
        }
        any = true;
        for (const QByteArray &c : readProcFile(path.constData()).split(' ')) {
            if (const qint64 child = c.trimmed().toLongLong(); child > 0) {
                out.append(child);
            }
        }
    }
    return any || tasks.isEmpty();
}
} // namespace

namespace ProcessInfo
{

qint64 foregroundGroup(int ptyFd)
{
    const pid_t pg = tcgetpgrp(ptyFd);
    return pg > 0 ? pg : 0;
}

std::optional<Process> process(qint64 pid)
{
    Process p;
    if (pid <= 0 || !readStat(pid, p)) {
        return std::nullopt;
    }
    p.args = readArgs(pid);
    return p;
}

QList<Process> tree(qint64 root)
{
    QList<Process> out;
    const std::optional<Process> top = process(root);
    if (!top) {
        return out;
    }
    out.append(*top);

    QList<qint64> kids;
    if (childrenOf(root, kids)) {
        // Breadth first from the root, reading only the session's processes.
        for (qsizetype i = 0; i < out.size(); ++i) {
            QList<qint64> next;
            if (i > 0) {
                childrenOf(out.at(i).pid, next);
            } else {
                next = kids;
            }
            for (const qint64 c : std::as_const(next)) {
                Process p;
                if (readStat(c, p) && p.ppid == out.at(i).pid) {
                    p.args = readArgs(c);
                    out.append(p);
                }
            }
        }
        return out;
    }

    // No children files: one pass over every process.
    QHash<qint64, QList<Process>> byParent;
    for (const qint64 pid : numericEntries("/proc")) {
        Process p;
        if (readStat(pid, p)) {
            byParent[p.ppid].append(p);
        }
    }
    for (qsizetype i = 0; i < out.size(); ++i) {
        for (Process p : byParent.value(out.at(i).pid)) {
            p.args = readArgs(p.pid);
            out.append(p);
        }
    }
    return out;
}

} // namespace ProcessInfo
