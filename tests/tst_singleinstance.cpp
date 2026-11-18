#include "SingleInstance.h"

#include <QDeadlineTimer>
#include <QFile>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>

class SingleInstanceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init();
    void firstLaunchIsPrimary();
    void secondLaunchForwardsArgsAndToken();
    void releaseLetsARelaunchTakeOver();
    void crashedInstanceDoesNotBlock();
    void unwritableDirectoryFails();

private:
    // Claims from another thread, so this one's event loop can answer.
    SingleInstance::Result claimElsewhere(const QStringList &args, const QString &token);
    QString socketPath() const { return m_tmp->path() + QStringLiteral("/inst.sock"); }

    std::unique_ptr<QTemporaryDir> m_tmp;
};

void SingleInstanceTest::init()
{
    m_tmp = std::make_unique<QTemporaryDir>(QStringLiteral("/tmp/xsi.XXXXXX"));
    QVERIFY(m_tmp->isValid());
}

SingleInstance::Result SingleInstanceTest::claimElsewhere(const QStringList &args, const QString &token)
{
    SingleInstance::Result result = SingleInstance::Result::Failed;
    QThread *thread = QThread::create([&] {
        SingleInstance second(socketPath());
        second.setForwardTimeout(2000);
        result = second.claim(args, token);
    });
    thread->start();
    QDeadlineTimer deadline(10000);
    while (!thread->isFinished() && !deadline.hasExpired()) {
        QTest::qWait(10);
    }
    thread->wait();
    delete thread;
    return result;
}

void SingleInstanceTest::firstLaunchIsPrimary()
{
    SingleInstance first(socketPath());
    QString error;
    QCOMPARE(first.claim({}, {}, &error), SingleInstance::Result::Primary);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFile::exists(socketPath()));
}

void SingleInstanceTest::secondLaunchForwardsArgsAndToken()
{
    SingleInstance first(socketPath());
    QCOMPARE(first.claim({}, {}), SingleInstance::Result::Primary);
    QSignalSpy spy(&first, &SingleInstance::activateRequested);

    QCOMPARE(claimElsewhere({QStringLiteral("--tab"), QStringLiteral("docker")}, QStringLiteral("tok-123")),
             SingleInstance::Result::Forwarded);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toStringList(), (QStringList{QStringLiteral("--tab"), QStringLiteral("docker")}));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("tok-123"));

    // The primary keeps serving.
    QCOMPARE(claimElsewhere({}, {}), SingleInstance::Result::Forwarded);
    QTRY_COMPARE(spy.size(), 2);
}

void SingleInstanceTest::releaseLetsARelaunchTakeOver()
{
    SingleInstance first(socketPath());
    QCOMPARE(first.claim({}, {}), SingleInstance::Result::Primary);
    first.release();
    QVERIFY(!QFile::exists(socketPath()));

    SingleInstance relaunch(socketPath());
    QCOMPARE(relaunch.claim({}, {}), SingleInstance::Result::Primary);
}

void SingleInstanceTest::crashedInstanceDoesNotBlock()
{
    // A child takes the lock, binds the socket and dies without cleaning up.
    const QByteArray sock = QFile::encodeName(socketPath());
    const pid_t pid = fork();
    QVERIFY(pid >= 0);
    if (pid == 0) {
        QLockFile lock(socketPath() + QStringLiteral(".lock"));
        if (!lock.tryLock(0)) {
            _exit(2);
        }
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, sock.constData(), sizeof(addr.sun_path) - 1);
        if (fd < 0 || ::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || ::listen(fd, 1) != 0) {
            _exit(3);
        }
        _exit(0);
    }
    int status = 0;
    QCOMPARE(waitpid(pid, &status, 0), pid);
    QVERIFY(WIFEXITED(status));
    QCOMPARE(WEXITSTATUS(status), 0);
    QVERIFY(QFile::exists(socketPath()));
    QVERIFY(QFile::exists(socketPath() + QStringLiteral(".lock")));

    SingleInstance next(socketPath());
    next.setForwardTimeout(1000);
    QString error;
    QCOMPARE(next.claim({}, {}, &error), SingleInstance::Result::Primary);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void SingleInstanceTest::unwritableDirectoryFails()
{
    SingleInstance inst(QStringLiteral("/proc/x3ro-deck-no-such-dir/inst.sock"));
    inst.setForwardTimeout(200);
    QString error;
    QCOMPARE(inst.claim({}, {}, &error), SingleInstance::Result::Failed);
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(SingleInstanceTest)
#include "tst_singleinstance.moc"
