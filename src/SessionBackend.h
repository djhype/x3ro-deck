#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

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

    virtual bool hasSession(const QString &name) const = 0;
    // A new session runs a shell in `dir` and types `startCommand` into it, so
    // the shell survives the agent exiting.
    virtual bool createSession(const QString &name, const QString &dir, const QString &startCommand) = 0;
    bool ensureSession(const QString &name, const QString &dir, const QString &startCommand)
    {
        return hasSession(name) || createSession(name, dir, startCommand);
    }
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

    // Sends key names, or literal text when `literal` is set.
    virtual void sendKeys(const QString &name, const QStringList &keys, bool literal = false) = 0;
    // Types `text` and presses Enter. `enterDelayMs` holds Enter back for TUIs
    // that read a fast burst of input as a paste.
    virtual void submit(const QString &name, const QString &text, int enterDelayMs = 0) = 0;

    // Starts reporting each session's foreground process every `intervalMs`.
    virtual void startPolling(int intervalMs) = 0;

Q_SIGNALS:
    // session name → foreground command of the session
    void foregroundProcesses(const QHash<QString, QString> &commands);
};
