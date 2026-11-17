#include "TmuxManager.h"

#include "Config.h"

#include <QDir>
#include <QFile>
#include <QProcess>

namespace
{
const char *kDefaultConfig = R"(# x3ro-deck tmux server (tmux -L x3ro-deck). x3ro-deck writes this file
# only when it is missing, so edits here are kept.
set -g status off
set -g mouse on
set -g history-limit 100000
set -sg escape-time 0
set -g focus-events on
set -s extended-keys on
set -g allow-passthrough on
set -g set-clipboard on
set -g default-terminal "tmux-256color"
set -as terminal-features ",xterm-256color:RGB:clipboard"
setw -g aggressive-resize on
set -g prefix C-a
unbind C-b
bind C-a send-prefix
)";
}

TmuxManager::TmuxManager(QObject *parent)
    : SessionBackend(parent)
    , m_configPath(Config::configDir() + QStringLiteral("/tmux.conf"))
{
    // Agent terminals run `tmux attach`, which refuses to nest inside another
    // tmux client; x3ro-deck may itself be launched from a tmux pane.
    qunsetenv("TMUX");
    qunsetenv("TMUX_PANE");
    QFile conf(m_configPath);
    if (!conf.exists()) {
        QDir().mkpath(Config::configDir());
        if (conf.open(QIODevice::WriteOnly)) {
            conf.write(kDefaultConfig);
        }
    }
    connect(&m_poll, &QTimer::timeout, this, &TmuxManager::poll);
}

QStringList TmuxManager::baseArgs() const
{
    return {QStringLiteral("-L"), socketName(), QStringLiteral("-f"), m_configPath};
}

bool TmuxManager::hasSession(const QString &name) const
{
    QProcess has;
    has.start(QStringLiteral("tmux"), baseArgs() << QStringLiteral("has-session") << QStringLiteral("-t") << (QLatin1Char('=') + name));
    has.waitForFinished(2000);
    return has.exitStatus() == QProcess::NormalExit && has.exitCode() == 0;
}

bool TmuxManager::createSession(const QString &name, const QString &dir, const QString &startCommand)
{
    QProcess create;
    create.start(QStringLiteral("tmux"),
                 baseArgs() << QStringLiteral("new-session") << QStringLiteral("-d") << QStringLiteral("-s") << name << QStringLiteral("-c")
                            << dir << QStringLiteral("-x") << QStringLiteral("200") << QStringLiteral("-y") << QStringLiteral("50"));
    create.waitForFinished(3000);
    if (create.exitCode() != 0) {
        qWarning("tmux new-session %s failed: %s", qPrintable(name), create.readAllStandardError().constData());
        return false;
    }
    if (!startCommand.isEmpty()) {
        // Typed into the shell rather than passed as the session command, so the
        // shell survives the agent exiting and resuming it is one keystroke away.
        submit(name, startCommand);
    }
    return true;
}

QStringList TmuxManager::sessionsIn(const QString &dir) const
{
    QProcess list;
    list.start(QStringLiteral("tmux"),
               baseArgs() << QStringLiteral("list-sessions") << QStringLiteral("-F") << QStringLiteral("#{session_path}\t#{session_name}"));
    list.waitForFinished(2000);
    QStringList out;
    for (const QString &line : QString::fromUtf8(list.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.section(QLatin1Char('\t'), 0, 0) == dir) {
            out.append(line.section(QLatin1Char('\t'), 1));
        }
    }
    return out;
}

bool TmuxManager::renameSession(const QString &from, const QString &to)
{
    QProcess rename;
    rename.start(QStringLiteral("tmux"),
                 baseArgs() << QStringLiteral("rename-session") << QStringLiteral("-t") << (QLatin1Char('=') + from) << to);
    rename.waitForFinished(2000);
    return rename.exitStatus() == QProcess::NormalExit && rename.exitCode() == 0;
}

void TmuxManager::killSession(const QString &name)
{
    QProcess kill;
    kill.start(QStringLiteral("tmux"), baseArgs() << QStringLiteral("kill-session") << QStringLiteral("-t") << (QLatin1Char('=') + name));
    kill.waitForFinished(3000);
}

void TmuxManager::killAll()
{
    m_poll.stop();
    QProcess kill;
    kill.start(QStringLiteral("tmux"), baseArgs() << QStringLiteral("kill-server"));
    kill.waitForFinished(3000);
}

QString TmuxManager::attachHint(const QString &name) const
{
    return QStringLiteral("tmux -L %1 attach -t %2").arg(socketName(), name);
}

QStringList TmuxManager::attachCommand(const QString &name) const
{
    return QStringList{QStringLiteral("tmux")} + baseArgs()
        + QStringList{QStringLiteral("attach-session"), QStringLiteral("-t"), QLatin1Char('=') + name};
}

void TmuxManager::sendKeys(const QString &name, const QStringList &keys, bool literal)
{
    QStringList args = baseArgs() << QStringLiteral("send-keys") << QStringLiteral("-t") << (QLatin1Char('=') + name + QLatin1Char(':'));
    if (literal) {
        args << QStringLiteral("-l");
    }
    args << keys;
    QProcess::startDetached(QStringLiteral("tmux"), args);
}

void TmuxManager::submit(const QString &name, const QString &text, int enterDelayMs)
{
    if (enterDelayMs > 0) {
        sendKeys(name, {text}, true);
        QTimer::singleShot(enterDelayMs, this, [this, name] { sendKeys(name, {QStringLiteral("Enter")}); });
        return;
    }
    // One tmux invocation, so Enter cannot overtake the text.
    const QString target = QLatin1Char('=') + name + QLatin1Char(':');
    QProcess::startDetached(QStringLiteral("tmux"),
                            baseArgs() << QStringLiteral("send-keys") << QStringLiteral("-t") << target << QStringLiteral("-l") << text
                                       << QStringLiteral(";") << QStringLiteral("send-keys") << QStringLiteral("-t") << target
                                       << QStringLiteral("Enter"));
}

void TmuxManager::startPolling(int intervalMs)
{
    m_poll.start(intervalMs);
    poll();
}

void TmuxManager::poll()
{
    if (m_pollInFlight) {
        return;
    }
    m_pollInFlight = true;
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc] {
        m_pollInFlight = false;
        QHash<QString, QString> out;
        const QList<QByteArray> lines = proc->readAllStandardOutput().split('\n');
        for (const QByteArray &line : lines) {
            const QList<QByteArray> cols = line.split('\t');
            // Only the active pane of each session; a user may split a session by hand.
            if (cols.size() == 3 && cols.at(1) == "1") {
                out.insert(QString::fromUtf8(cols.at(0)), QString::fromUtf8(cols.at(2)));
            }
        }
        Q_EMIT foregroundProcesses(out);
        proc->deleteLater();
    });
    proc->start(QStringLiteral("tmux"),
                baseArgs() << QStringLiteral("list-panes") << QStringLiteral("-a") << QStringLiteral("-F")
                           << QStringLiteral("#{session_name}\t#{?#{&&:#{window_active},#{pane_active}},1,0}\t#{pane_current_command}"));
}
