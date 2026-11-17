#pragma once

#include "SessionBackend.h"

#include <QJsonObject>
#include <QTimer>

class QLocalSocket;

// Runs agent sessions in x3ro-deck-sessiond (config "sessionBackend":
// "daemon"). Calls block on the daemon's reply, as TmuxManager's block on
// tmux. Only createSession() starts the daemon; everything else treats a
// missing daemon as having no sessions.
//
// Reach a session from any terminal with: x3ro-deck attach <checkout>
class DaemonBackend : public SessionBackend
{
    Q_OBJECT
public:
    explicit DaemonBackend(QObject *parent = nullptr);
    ~DaemonBackend() override;

    // $X3RO_DECK_SESSIOND_BIN, else next to the app, else on PATH.
    static QString daemonProgram();
    // x3ro-deck-attach next to the app, else on PATH.
    static QString attachProgram();
    // The bytes a key name (tmux's vocabulary) sends: "Enter" → "\r",
    // "C-c" → "\x03". Anything else is typed as is, as tmux does.
    static QByteArray keyBytes(const QString &key);

    bool hasSession(const QString &name) const override;
    bool createSession(const QString &name, const QString &dir, const QString &startCommand, const Environment &env = {}) override;
    Environment sessionEnvironment(const QString &name) const override;
    QStringList sessionsIn(const QString &dir) const override;
    bool renameSession(const QString &from, const QString &to) override;
    void killSession(const QString &name) override;
    void killAll() override;
    QStringList attachCommand(const QString &name) const override;
    QString attachHint(const QString &name) const override;
    void sendKeys(const QString &name, const QStringList &keys, bool literal = false) override;
    void submit(const QString &name, const QString &text, int enterDelayMs = 0) override;
    void startPolling(int intervalMs) override;

    // Sends one request and waits for its reply. A failed call returns an
    // object with "ok": false.
    QJsonObject request(QJsonObject req, bool startDaemon = false) const;

private:
    bool connectDaemon(bool start) const;
    bool startDaemon() const;
    QJsonObject find(const QString &name) const;
    QJsonObject write(const QString &name, const QByteArray &bytes) const;
    void poll();

    QLocalSocket *m_sock;
    mutable QByteArray m_buffer;
    mutable int m_nextId = 1;
    QTimer m_poll;
};
