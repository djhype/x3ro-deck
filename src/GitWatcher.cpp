#include "GitWatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

GitWatcher::GitWatcher(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        const QString key = m_fileKeys.value(path);
        if (!key.isEmpty()) {
            scheduleRead(key);
        }
    });
    connect(&m_safety, &QTimer::timeout, this, [this] {
        for (auto it = m_dirs.cbegin(); it != m_dirs.cend(); ++it) {
            read(it.key());
        }
    });
    m_safety.start(60000);
}

QString GitWatcher::gitDir(const QString &dir)
{
    const QFileInfo dotGit(dir + QStringLiteral("/.git"));
    if (dotGit.isDir()) {
        return dotGit.absoluteFilePath();
    }
    // Worktrees and submodules: .git is a file holding "gitdir: <path>".
    QFile f(dotGit.absoluteFilePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QLatin1String("gitdir: "))) {
            return QDir(dir).absoluteFilePath(line.mid(8));
        }
    }
    return {};
}

void GitWatcher::watch(const QString &key, const QString &dir)
{
    m_dirs.insert(key, dir);
    auto *t = new QTimer(this);
    t->setSingleShot(true);
    t->setInterval(300);
    connect(t, &QTimer::timeout, this, [this, key] { read(key); });
    m_debounce.insert(key, t);
    rewatch(key);
    read(key);
}

void GitWatcher::rewatch(const QString &key)
{
    const QString gd = gitDir(m_dirs.value(key));
    if (gd.isEmpty()) {
        return;
    }
    // Git replaces HEAD by renaming a lock file over it, which drops inotify
    // watches, so every change re-adds the paths.
    for (const QString &p : {gd + QStringLiteral("/HEAD"), gd + QStringLiteral("/logs/HEAD")}) {
        if (QFileInfo::exists(p) && !m_watcher.files().contains(p)) {
            m_watcher.addPath(p);
            m_fileKeys.insert(p, key);
        }
    }
}

void GitWatcher::scheduleRead(const QString &key)
{
    if (QTimer *t = m_debounce.value(key)) {
        t->start();
    }
}

void GitWatcher::read(const QString &key)
{
    const QString dir = m_dirs.value(key);
    rewatch(key);
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc, key](int code) {
        proc->deleteLater();
        if (code != 0) {
            return;
        }
        // Output: "<branch>\n<sha> <subject>\n"
        const QStringList lines = QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'));
        GitInfo info;
        info.branch = lines.value(0).trimmed();
        info.commit = lines.value(1).trimmed();
        Q_EMIT updated(key, info);
    });
    proc->start(QStringLiteral("sh"),
                {QStringLiteral("-c"), QStringLiteral("git -C \"$1\" branch --show-current && git -C \"$1\" log -1 --format='%h %s'"),
                 QStringLiteral("sh"), dir});
}
