// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QMap>
#include <QObject>
#include <QStringList>

class TerminalChannel;

// Keeps agent sessions alive outside the app, so agents survive restarts.
// AppController and MainWindow talk only to this interface; TmuxManager is the
// first implementation.
//
// Sessions are addressed by name. Key names follow tmux's vocabulary (Enter,
// Escape, or a single character); other backends translate them.
class SessionBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    using Environment = QMap<QString, QString>;

    virtual bool hasSession(const QString &name) const = 0;
    // A new session runs `shell` (an argv, resolved by SessionShell) in `dir`
    // with `env` added to the app's environment, and types `startCommand` into
    // it, so the shell survives the agent exiting.
    virtual bool createSession(const QString &name, const QString &dir, const QString &startCommand, const Environment &env = {},
                               const QStringList &shell = {}) = 0;
    bool ensureSession(const QString &name, const QString &dir, const QString &startCommand, const Environment &env = {},
                       const QStringList &shell = {})
    {
        return hasSession(name) || createSession(name, dir, startCommand, env, shell);
    }
    // The `env` the session was created with.
    virtual Environment sessionEnvironment(const QString &name) const = 0;
    // The `shell` the session was created with, as requested rather than as
    // resolved; empty for a session from a build that did not record it.
    virtual QStringList sessionShell(const QString &name) const = 0;
    // Sessions started in `dir`, whatever their name.
    virtual QStringList sessionsIn(const QString &dir) const = 0;
    virtual bool renameSession(const QString &from, const QString &to) = 0;
    virtual void killSession(const QString &name) = 0;
    // Ends every session, and the agents in them.
    virtual void killAll() = 0;

    // Program and arguments that attach a terminal to the session.
    virtual QStringList attachCommand(const QString &name) const = 0;
    // The same as a line a user can type into any terminal.
    virtual QString attachHint(const QString &name) const = 0;
    // A built-in terminal's direct stream to the session, owned by `parent`,
    // or null when the backend has none and the terminal runs
    // attachCommand() in a local PTY instead.
    virtual TerminalChannel *attachChannel(const QString &name, int rows, int cols, QObject *parent) const
    {
        Q_UNUSED(name)
        Q_UNUSED(rows)
        Q_UNUSED(cols)
        Q_UNUSED(parent)
        return nullptr;
    }

    // Sends key names, or literal text when `literal` is set.
    virtual void sendKeys(const QString &name, const QStringList &keys, bool literal = false) = 0;
    // Types `text` and presses Enter. `enterDelayMs` holds Enter back for TUIs
    // that read a fast burst of input as a paste.
    virtual void submit(const QString &name, const QString &text, int enterDelayMs = 0) = 0;

    // Starts reporting each session's foreground process every `intervalMs`.
    virtual void startPolling(int intervalMs) = 0;

Q_SIGNALS:
    // session name → names of the session's foreground job. The first is
    // tmux's pane_current_command; a backend that sees the whole job adds the
    // other processes in it, so an agent behind a wrapper is found as well.
    void foregroundProcesses(const QHash<QString, QStringList> &commands);
};
