#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>
#include <memory>

#include <sys/types.h>

class QLocalServer;
class QLocalSocket;
class QSocketNotifier;
class SessionScreen;

// x3ro-deck-sessiond: owns agent PTYs so agents outlive the GUI. Clients speak
// the protocol in SessiondProtocol.h over a local socket.
class Daemon : public QObject
{
    Q_OBJECT
public:
    explicit Daemon(const QString &socketPath, QObject *parent = nullptr);
    ~Daemon() override;

    enum class ListenResult {
        Listening,
        Running, // another daemon holds the socket's lock
        Failed,
    };
    ListenResult listen(QString *error);

private:
    struct Session
    {
        quint32 id = 0;
        QString name;
        QString cwd;
        QJsonObject env;
        QJsonArray shell;
        pid_t pid = -1;
        int fd = -1;
        int cols = 0;
        int rows = 0;
        QSocketNotifier *reader = nullptr;
        QSocketNotifier *writer = nullptr;
        QByteArray pending; // input the PTY has not taken yet
        std::unique_ptr<SessionScreen> screen; // what the session shows, redrawn on attach
    };
    struct Client
    {
        QByteArray buffer;
        QSet<quint32> attached;
    };

    void onNewConnection();
    void onClientData(QLocalSocket *sock);
    void onChildSignal();
    void handle(QLocalSocket *sock, const QJsonObject &req);
    QJsonObject create(const QJsonObject &req, QString *error);

    Session *find(const QJsonObject &req) const;
    // Returns the number of bytes read; 0 at EOF or when nothing was ready.
    qsizetype readPty(Session *s);
    void writePty(Session *s, const QByteArray &bytes);
    void flushPty(Session *s);
    void resize(Session *s, int cols, int rows);
    // Closes the PTY and drops the session; `status` < 0 means it was killed.
    void endSession(Session *s, int status);
    void killSession(Session *s);
    void shutdown();
    void checkIdle();
    void send(QLocalSocket *sock, const QByteArray &frame);

    QString m_path;
    QLocalServer *m_server;
    QHash<quint32, Session *> m_sessions;
    QHash<QLocalSocket *, Client> m_clients;
    quint32 m_nextId = 1;
    QSocketNotifier *m_childNotifier = nullptr;
    int m_lockFd = -1;
    QTimer m_idle;
    QElapsedTimer m_idleFor;
    ino_t m_socketInode = 0;
    bool m_shuttingDown = false;
};
