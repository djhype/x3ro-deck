#pragma once

#include <QJsonObject>
#include <QObject>

class QLocalServer;

// Receives agent hook payloads (Claude Code and Codex share one format)
// forwarded by x3ro-deck-hook over $XDG_RUNTIME_DIR/x3ro-deck.sock. One
// connection carries one JSON payload.
class AgentBus : public QObject
{
    Q_OBJECT
public:
    explicit AgentBus(QObject *parent = nullptr);
    ~AgentBus() override;

    bool listen(QString *error);
    static QString socketPath();

Q_SIGNALS:
    void hookEvent(const QJsonObject &payload);

private:
    QLocalServer *m_server;
};
