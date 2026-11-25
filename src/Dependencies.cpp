// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Dependencies.h"

#include "Config.h"
#include "SessionShell.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace
{
constexpr int kProbeTimeoutMs = 5000;
const QByteArray kMarker = QByteArrayLiteral("__X3RO_DECK_PATH__");

QString agentHint(const QString &kind, const QString &binary)
{
    if (kind == QLatin1String("claude") && binary == QLatin1String("claude")) {
        return QStringLiteral("Install Claude Code: npm install -g @anthropic-ai/claude-code");
    }
    if (kind == QLatin1String("codex") && binary == QLatin1String("codex")) {
        return QStringLiteral("Install Codex: npm install -g @openai/codex");
    }
    return QStringLiteral("Install %1, or fix the profile's command in Settings → Agents.").arg(binary);
}
} // namespace

Dependencies::Dependencies(QObject *parent)
    : QObject(parent)
    , m_appPath(qEnvironmentVariable("PATH"))
{
}

QString Dependencies::commandBinary(const QString &command)
{
    static const QRegularExpression assignment(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*="));
    const QStringList words = command.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString &w : words) {
        if (!assignment.match(w).hasMatch()) {
            QString word = w;
            word.remove(QLatin1Char('"')).remove(QLatin1Char('\''));
            return word;
        }
    }
    return {};
}

QString Dependencies::resolve(const QString &binary, const QString &appPath, const QString &loginPath)
{
    if (binary.isEmpty()) {
        return {};
    }
    QString program = binary;
    if (program.startsWith(QLatin1String("~/"))) {
        program = QDir::homePath() + program.mid(1);
    }
    if (program.contains(QLatin1Char('/')) || program.contains(QLatin1Char('\\'))) {
        const QFileInfo fi(program);
        return fi.isFile() && fi.isExecutable() ? fi.absoluteFilePath() : QString();
    }
    for (const QString &path : {appPath, loginPath}) {
        // ';' on Windows, where ':' follows every drive letter.
        const QStringList dirs = path.split(QDir::listSeparator(), Qt::SkipEmptyParts);
        if (dirs.isEmpty()) {
            continue;
        }
        if (QString found = QStandardPaths::findExecutable(program, dirs); !found.isEmpty()) {
            return found;
        }
    }
    return {};
}

QStringList Dependencies::probeArgs()
{
    // Interactive as well as login: zsh and bash set most of PATH in their rc files.
    return {QStringLiteral("-l"), QStringLiteral("-i"), QStringLiteral("-c"),
            QStringLiteral("printf '%s%s%s' '%1' \"$PATH\" '%1'").arg(QString::fromLatin1(kMarker))};
}

QString Dependencies::parseProbe(const QByteArray &output)
{
    const qsizetype begin = output.indexOf(kMarker);
    if (begin < 0) {
        return {};
    }
    const qsizetype from = begin + kMarker.size();
    const qsizetype end = output.indexOf(kMarker, from);
    if (end < 0) {
        return {};
    }
    // fish joins a quoted path variable with ':' already; other shells print it as is.
    return QString::fromLocal8Bit(output.mid(from, end - from)).trimmed();
}

void Dependencies::setConfig(const Config &config)
{
    QList<Dependency> list;
    list.append({QStringLiteral("git"),
                 QStringLiteral("Git (git)"),
                 QStringLiteral("git"),
                 QStringLiteral("Branch and worktree status"),
                 QStringLiteral("Install git from your distribution's packages."),
                 {}});
    list.append({QStringLiteral("gh"),
                 QStringLiteral("GitHub CLI (gh)"),
                 QStringLiteral("gh"),
                 QStringLiteral("Pull request status"),
                 QStringLiteral("Install the GitHub CLI (https://cli.github.com), then run gh auth login."),
                 {}});
    const QString compose = commandBinary(config.composeBinary);
    list.append({QStringLiteral("compose"),
                 QStringLiteral("Compose (%1)").arg(compose),
                 compose,
                 QStringLiteral("Compose and quick commands"),
                 compose == QLatin1String("podman")
                     ? QStringLiteral("Install Podman and podman-compose.")
                     : QStringLiteral("Install Docker with the compose plugin, or set the compose command in Settings → Docker."),
                 {}});
    if (config.sessionBackend == QLatin1String("daemon")) {
        list.append({QStringLiteral("sessiond"),
                     QStringLiteral("Session daemon (x3ro-deck-sessiond)"),
                     QStringLiteral("x3ro-deck-sessiond"),
                     QStringLiteral("Agent sessions"),
#if defined(Q_OS_WIN)
                     QStringLiteral("Agent sessions on Windows run in x3ro-deck-sessiond, which ships with X3RO Deck: reinstall it."),
#else
                     QStringLiteral("It ships with X3RO Deck: reinstall it, or set the session backend to tmux."),
#endif
                     {}});
    }
    if (config.sessionBackend != QLatin1String("daemon")) {
        list.append({QStringLiteral("tmux"),
                     QStringLiteral("tmux"),
                     QStringLiteral("tmux"),
                     QStringLiteral("Agent sessions"),
                     QStringLiteral("Install tmux, or set the session backend to daemon."),
                     {}});
    }
    for (const AgentProfile &p : config.agents) {
        const QString binary = commandBinary(p.command);
        if (binary.isEmpty()) {
            continue;
        }
        auto same = std::find_if(list.begin(), list.end(), [&binary](const Dependency &d) { return d.id == binary; });
        if (same != list.end()) {
            same->purpose += QStringLiteral(", ") + p.label;
            continue;
        }
        list.append({binary,
                     QStringLiteral("%1 (%2)").arg(p.label, binary),
                     binary,
                     QStringLiteral("Agent: ") + p.label,
                     agentHint(p.kind, binary),
                     {}});
    }
    m_list = list;
    check();
}

void Dependencies::setLoginPath(const QString &path)
{
    m_loginPath = path;
    m_ready = true;
    check();
}

void Dependencies::probeLoginPath(const QStringList &shell)
{
    if (m_probe) {
        return;
    }
#if defined(Q_OS_WIN)
    // No login shell sets up PATH there: the app's own is the one sessions get.
    Q_UNUSED(shell)
    setLoginPath(m_appPath);
#else
    const SessionShell s = SessionShell::resolve(shell);
    m_probe = new QProcess(this);
    m_probe->setStandardInputFile(QProcess::nullDevice());
    m_probe->setProcessChannelMode(QProcess::SeparateChannels);
    auto done = [this](const QString &path) {
        m_probe->deleteLater();
        m_probe = nullptr;
        setLoginPath(path);
    };
    connect(m_probe, &QProcess::finished, this, [this, done] { done(parseProbe(m_probe->readAllStandardOutput())); });
    connect(m_probe, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning("x3ro-deck: cannot run the login shell to read its PATH");
            done(QString());
        }
    });
    QProcess *probe = m_probe;
    QTimer::singleShot(kProbeTimeoutMs, probe, [probe] { probe->kill(); });
    m_probe->start(s.program, probeArgs());
#endif
}

void Dependencies::check()
{
    for (Dependency &d : m_list) {
        d.path = resolve(d.binary, m_appPath, m_loginPath);
        // The daemon is found the way DaemonBackend starts it: the override,
        // then next to the app.
        if (d.id == QLatin1String("sessiond") && d.path.isEmpty()) {
            for (const QString &candidate : {qEnvironmentVariable("X3RO_DECK_SESSIOND_BIN"),
                                             QCoreApplication::applicationDirPath() + QStringLiteral("/x3ro-deck-sessiond")
#if defined(Q_OS_WIN)
                                                 + QStringLiteral(".exe")
#endif
                 }) {
                if (!candidate.isEmpty() && QFileInfo(candidate).isExecutable()) {
                    d.path = candidate;
                    break;
                }
            }
        }
    }
    Q_EMIT changed();
}

const Dependency *Dependencies::missing(const QString &id) const
{
    if (!m_ready) {
        return nullptr;
    }
    for (const Dependency &d : m_list) {
        if (d.id == id) {
            return d.path.isEmpty() ? &d : nullptr;
        }
    }
    return nullptr;
}
