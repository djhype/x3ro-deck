#include "SingleInstance.h"

#include "x3ro-deck-socket.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QThread>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
constexpr int kMaxRequest = 64 * 1024;
const QByteArray kOk = QByteArrayLiteral("ok\n");
} // namespace

SingleInstance::SingleInstance(const QString &socketPath, QObject *parent)
    : QObject(parent)
    , m_path(socketPath)
{
    if (m_path.isEmpty()) {
        const x3ro_deck::SocketPath sock = x3ro_deck::instanceSocketPath();
        m_path = QString::fromStdString(sock.path);
        m_privateDir = QString::fromStdString(sock.privateDir);
    }
}

SingleInstance::~SingleInstance()
{
    release();
}

SingleInstance::Result SingleInstance::claim(const QStringList &args, const QString &activationToken, QString *error)
{
    auto fail = [error](const QString &text) {
        if (error) {
            *error = text;
        }
        return Result::Failed;
    };
#ifdef Q_OS_UNIX
    if (!m_privateDir.isEmpty()) {
        // /tmp is shared: a directory someone else made could take our launches.
        const QByteArray dir = QFile::encodeName(m_privateDir);
        mkdir(dir.constData(), 0700);
        struct stat st{};
        if (lstat(dir.constData(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 077) != 0) {
            return fail(QStringLiteral("%1 is not a private directory owned by this user").arg(m_privateDir));
        }
    }
#endif

    const QByteArray message = QJsonDocument(QJsonObject{
                                                 {QStringLiteral("op"), QStringLiteral("activate")},
                                                 {QStringLiteral("args"), QJsonArray::fromStringList(args)},
                                                 {QStringLiteral("token"), activationToken},
                                             })
                                   .toJson(QJsonDocument::Compact)
        + '\n';

    m_lock = std::make_unique<QLockFile>(m_path + QStringLiteral(".lock"));
    // Only a dead owner makes the lock stale, never its age.
    m_lock->setStaleLockTime(0);
    const QDeadlineTimer deadline(m_forwardTimeoutMs);
    for (;;) {
        // A live instance answers first. That also covers one whose binary
        // `cmake --install` replaced: QLockFile compares the owner's executable
        // name, which then ends in " (deleted)", and would call its lock stale.
        if (forward(message)) {
            m_lock.reset();
            return Result::Forwarded;
        }
        if (m_lock->tryLock(0)) {
            // Holding the lock, any socket left at the path is a crashed instance's.
            QLocalServer::removeServer(m_path);
            m_server = new QLocalServer(this);
            m_server->setSocketOptions(QLocalServer::UserAccessOption);
            if (!m_server->listen(m_path)) {
                const QString text = m_server->errorString();
                release();
                return fail(QStringLiteral("Could not listen on %1: %2").arg(m_path, text));
            }
            connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onConnection);
            return Result::Primary;
        }
        if (m_lock->error() != QLockFile::LockFailedError) {
            const QString dir = QFileInfo(m_path).absolutePath();
            m_lock.reset();
            return fail(QStringLiteral("Could not create the instance lock in %1").arg(dir));
        }
        if (deadline.hasExpired()) {
            m_lock.reset();
            return fail(QStringLiteral("Another X3RO Deck holds %1.lock but does not answer on its socket").arg(m_path));
        }
        QThread::msleep(100);
    }
}

bool SingleInstance::forward(const QByteArray &message)
{
    QLocalSocket socket;
    socket.connectToServer(m_path);
    if (!socket.waitForConnected(500)) {
        return false;
    }
    socket.write(message);
    if (!socket.waitForBytesWritten(1000)) {
        return false;
    }
    QByteArray reply;
    while (!reply.contains('\n') && socket.waitForReadyRead(2000)) {
        reply += socket.readAll();
    }
    return reply == kOk;
}

void SingleInstance::release()
{
    if (m_server) {
        m_server->close(); // removes the socket file
        delete m_server;
        m_server = nullptr;
    }
    if (m_lock) {
        m_lock->unlock();
        m_lock.reset();
    }
}

void SingleInstance::onConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readRequest(socket); });
        readRequest(socket);
    }
}

void SingleInstance::readRequest(QLocalSocket *socket)
{
    if (!socket->canReadLine()) {
        if (socket->bytesAvailable() > kMaxRequest) {
            socket->abort();
        }
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(socket->readLine(kMaxRequest)).object();
    if (o.value(QStringLiteral("op")).toString() != QLatin1String("activate")) {
        socket->abort();
        return;
    }
    socket->write(kOk);
    socket->flush();
    socket->disconnectFromServer();
    QStringList args;
    for (const QJsonValue &v : o.value(QStringLiteral("args")).toArray()) {
        args.append(v.toString());
    }
    Q_EMIT activateRequested(args, o.value(QStringLiteral("token")).toString());
}
