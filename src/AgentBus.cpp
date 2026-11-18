#include "AgentBus.h"

#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>

#include <memory>

#include <x3ro-deck-socket.h>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

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
    return QString::fromStdString(x3ro_deck::hookSocketPath().path);
}

bool AgentBus::listen(QString *error)
{
    auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    const x3ro_deck::SocketPath sock = x3ro_deck::hookSocketPath();
    if (sock.tooLong()) {
        return fail(
            QStringLiteral("socket path is %1 bytes, over the platform limit of %2").arg(sock.path.size()).arg(x3ro_deck::kMaxSocketPath));
    }
#ifdef Q_OS_UNIX
    if (!sock.privateDir.empty()) {
        // /tmp is shared: a directory someone else made could hand them our hooks.
        const char *dir = sock.privateDir.c_str();
        mkdir(dir, 0700);
        struct stat st{};
        if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 077) != 0) {
            return fail(QStringLiteral("%1 is not a private directory owned by this user").arg(QString::fromStdString(sock.privateDir)));
        }
    }
#endif
    // A stale socket from a crashed instance would make listen() fail. The
    // single-instance guard in main() guarantees no live instance owns it.
    const QString path = QString::fromStdString(sock.path);
    QLocalServer::removeServer(path);
    if (!m_server->listen(path)) {
        return fail(m_server->errorString());
    }
    return true;
}
