#pragma once

#include <QObject>
#include <QStringList>

#include <memory>

class QLocalServer;
class QLocalSocket;
class QLockFile;

// One app instance per user. The first process holds a lock file and listens
// on a local socket; a later launch hands it its arguments and activation
// token and exits. The lock decides who is first: a crashed owner's lock is
// stale (its PID is gone), and two launches at once cannot both win.
class SingleInstance : public QObject
{
    Q_OBJECT
public:
    enum class Result {
        Primary, // this process runs the app
        Forwarded, // a running instance took the request; exit
        Failed, // neither; see the error
    };

    // `socketPath` empty: instanceSocketPath() from x3ro-deck-socket.h.
    explicit SingleInstance(const QString &socketPath = QString(), QObject *parent = nullptr);
    ~SingleInstance() override;

    // How long a launch waits for a lock holder that does not answer yet
    // (it may still be starting) before giving up.
    void setForwardTimeout(int ms) { m_forwardTimeoutMs = ms; }

    Result claim(const QStringList &args, const QString &activationToken, QString *error = nullptr);
    // Gives up the lock and the socket, so a relaunch becomes the primary.
    void release();
    QString socketPath() const { return m_path; }

Q_SIGNALS:
    void activateRequested(const QStringList &args, const QString &activationToken);

private:
    bool forward(const QByteArray &message);
    void onConnection();
    void readRequest(QLocalSocket *socket);

    QString m_path;
    QString m_privateDir;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer *m_server = nullptr;
    int m_forwardTimeoutMs = 3000;
};
