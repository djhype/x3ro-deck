#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

// Owns the tmux server that keeps agent sessions alive across x3ro-deck
// restarts. It runs on its own socket (-L x3ro-deck) with a minimal config:
// x3ro-deck draws the chrome, so tmux's status bar is off.
//
// Reach a session from plain Konsole with: tmux -L x3ro-deck attach -t <checkout>
class TmuxManager : public QObject
{
    Q_OBJECT
public:
    explicit TmuxManager(QObject *parent = nullptr);

    static QString socketName() { return QStringLiteral("x3ro-deck"); }
    QString configPath() const { return m_configPath; }

    bool hasSession(const QString &name) const;
    // A new session starts `startCommand`.
    bool createSession(const QString &name, const QString &dir, const QString &startCommand);
    bool ensureSession(const QString &name, const QString &dir, const QString &startCommand);
    // Sessions started in `dir`, whatever their name.
    QStringList sessionsIn(const QString &dir) const;
    bool renameSession(const QString &from, const QString &to);
    // Ends every session on our socket, and the agents in them.
    void killServer();
    QStringList attachArgs(const QString &name) const;
    // `keys` are tmux key names (Enter, Escape, …) unless `literal` is set.
    void sendKeys(const QString &name, const QStringList &keys, bool literal = false);
    // Types `text` and presses Enter. `enterDelayMs` holds Enter back for TUIs
    // that read a fast burst of input as a paste.
    void submit(const QString &name, const QString &text, int enterDelayMs = 0);
    void startPolling(int intervalMs);

Q_SIGNALS:
    // session name → foreground command of its active pane
    void paneCommands(const QHash<QString, QString> &commands);

private:
    QStringList baseArgs() const;
    void poll();

    QString m_configPath;
    QTimer m_poll;
    bool m_pollInFlight = false;
};
