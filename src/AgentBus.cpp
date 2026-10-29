#include "AgentBus.h"

#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

#include <memory>

AgentBus::AgentBus(QObject *parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *sock = m_server->nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            connect(sock, &QLocalSocket::readyRead, sock, [sock, buffer] { buffer->append(sock->readAll()); });
            connect(sock, &QLocalSocket::disconnected, this, [this, sock, buffer] {
                buffer->append(sock->readAll());
                const QJsonDocument doc = QJsonDocument::fromJson(*buffer);
                if (doc.isObject()) {
                    Q_EMIT hookEvent(doc.object());
                }
                sock->deleteLater();
            });
        }
    });
}

AgentBus::~AgentBus()
{
    m_server->close();
}

QString AgentBus::socketPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/x3ro-deck.sock");
}

bool AgentBus::listen(QString *error)
{
    // A stale socket from a crashed instance would make listen() fail. The
    // single-instance guard in main() guarantees no live instance owns it.
    QLocalServer::removeServer(socketPath());
    if (!m_server->listen(socketPath())) {
        if (error) {
            *error = m_server->errorString();
        }
        return false;
    }
    return true;
}
