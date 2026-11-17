// x3ro-deck-sessiond end to end: the wire protocol, DaemonBackend on top of
// it, and the attach client in a real PTY. Every daemon runs on a socket in a
// private temporary directory.

#include "DaemonBackend.h"
#include "SessiondProtocol.h"

#include <QFile>
#include <QJsonArray>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <csignal>

#include <poll.h>

#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
// A protocol client that collects the output of the sessions it attached to.
class Client
{
public:
    bool connectTo(const QString &path)
    {
        sock.connectToServer(path);
        return sock.waitForConnected(2000);
    }

    QJsonObject request(QJsonObject req)
    {
        const int id = nextId++;
        req.insert(QStringLiteral("id"), id);
        sock.write(Sessiond::encodeControl(req));
        QDeadlineTimer deadline(3000);
        while (!deadline.hasExpired()) {
            pump(50);
            for (qsizetype i = 0; i < controls.size(); ++i) {
                if (controls.at(i).value(QStringLiteral("re")).toInt() == id) {
                    return controls.takeAt(i);
                }
            }
        }
        return {};
    }

    void pump(int ms)
    {
        sock.flush();
        if (sock.bytesAvailable() == 0) {
            sock.waitForReadyRead(ms);
        }
        buffer.append(sock.readAll());
        Sessiond::Frame f;
        while (Sessiond::takeFrame(buffer, f)) {
            if (f.type == Sessiond::Control) {
                controls.append(Sessiond::parseControl(f.payload));
            } else if (f.type == Sessiond::Output) {
                quint32 id = 0;
                QByteArray bytes;
                if (Sessiond::splitData(f.payload, id, bytes)) {
                    output[id].append(bytes);
                }
            }
        }
    }

    // Pumps until `session` printed `text`.
    bool waitFor(quint32 session, const QByteArray &text, int ms = 5000)
    {
        QDeadlineTimer deadline(ms);
        while (!output.value(session).contains(text)) {
            if (deadline.hasExpired()) {
                return false;
            }
            pump(50);
        }
        return true;
    }

    QJsonObject event(const QString &name, int ms = 5000)
    {
        QDeadlineTimer deadline(ms);
        while (!deadline.hasExpired()) {
            for (qsizetype i = 0; i < controls.size(); ++i) {
                if (controls.at(i).value(QStringLiteral("event")).toString() == name) {
                    return controls.takeAt(i);
                }
            }
            pump(50);
        }
        return {};
    }

    QLocalSocket sock;
    QByteArray buffer;
    QList<QJsonObject> controls;
    QHash<quint32, QByteArray> output;
    int nextId = 1;
};

QJsonObject op(const char *name, QJsonObject fields = {})
{
    fields.insert(QStringLiteral("op"), QLatin1String(name));
    return fields;
}

QJsonObject sessionNamed(const QJsonObject &list, const QString &name)
{
    for (const QJsonValue &v : list.value(QStringLiteral("sessions")).toArray()) {
        if (v.toObject().value(QStringLiteral("name")).toString() == name) {
            return v.toObject();
        }
    }
    return {};
}

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}
} // namespace

class SessiondTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void keyBytes();
    void protocol();
    void backend();
    void attachClient();

private:
    // Runs the daemon in the foreground on the test socket.
    bool startForeground();
    QString shellCommandSession(Client &c, const QString &name, quint32 *id);

    QTemporaryDir m_tmp{QStringLiteral("/tmp/xds-XXXXXX")};
    QString m_socket;
    QProcess m_daemon;
};

void SessiondTest::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    QFile::setPermissions(m_tmp.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qunsetenv("X3RO_DECK_SESSIOND_SOCKET");
    qunsetenv("ENV");
    qputenv("XDG_RUNTIME_DIR", m_tmp.path().toLocal8Bit());
    // A predictable shell without the user's rc files.
    qputenv("HOME", m_tmp.path().toLocal8Bit());
    qputenv("SHELL", "/bin/sh");
    m_socket = m_tmp.path() + QStringLiteral("/x3ro-deck-sessiond.sock");
    QCOMPARE(Sessiond::socketPath(), m_socket);
    QVERIFY(QFileInfo(DaemonBackend::daemonProgram()).isExecutable());
}

void SessiondTest::cleanup()
{
    Client c;
    if (c.connectTo(m_socket)) {
        c.request(op("shutdown"));
    }
    if (m_daemon.state() != QProcess::NotRunning) {
        QVERIFY(m_daemon.waitForFinished(6000));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(m_socket), 6000);
}

bool SessiondTest::startForeground()
{
    m_daemon.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_daemon.start(DaemonBackend::daemonProgram(), {});
    QDeadlineTimer deadline(5000);
    while (!deadline.hasExpired()) {
        QLocalSocket probe;
        probe.connectToServer(m_socket);
        if (probe.waitForConnected(200)) {
            return true;
        }
        QTest::qWait(50);
    }
    return false;
}

QString SessiondTest::shellCommandSession(Client &c, const QString &name, quint32 *id)
{
    const QJsonObject created = c.request(op("create",
                                             {{QStringLiteral("name"), name},
                                              {QStringLiteral("cwd"), m_tmp.path()},
                                              {QStringLiteral("command"), QJsonArray{QStringLiteral("/bin/sh")}},
                                              {QStringLiteral("env"), QJsonObject{{QStringLiteral("X3RO_T"), QStringLiteral("v1")}}},
                                              {QStringLiteral("cols"), 100},
                                              {QStringLiteral("rows"), 30}}));
    *id = quint32(created.value(QStringLiteral("session")).toInt());
    return created.value(QStringLiteral("ok")).toBool() ? QString() : created.value(QStringLiteral("error")).toString(QStringLiteral("?"));
}

void SessiondTest::keyBytes()
{
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("Enter")), QByteArray("\r"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("Escape")), QByteArray("\x1b"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("C-c")), QByteArray("\x03"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("C-d")), QByteArray("\x04"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("Up")), QByteArray("\x1b[A"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("M-x")), QByteArray("\x1bx"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("1")), QByteArray("1"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("y")), QByteArray("y"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("hello")), QByteArray("hello"));
}

void SessiondTest::protocol()
{
    QVERIFY(startForeground());
    Client c;
    QVERIFY(c.connectTo(m_socket));
    const QJsonObject hello = c.request(op("hello", {{QStringLiteral("version"), Sessiond::kProtocolVersion}}));
    QCOMPARE(hello.value(QStringLiteral("version")).toInt(), Sessiond::kProtocolVersion);
    QVERIFY(!c.request(op("frobnicate")).value(QStringLiteral("ok")).toBool());

    quint32 id = 0;
    QCOMPARE(shellCommandSession(c, QStringLiteral("p"), &id), QString());
    QVERIFY(id > 0);
    // Names are unique.
    quint32 dup = 0;
    QVERIFY(!shellCommandSession(c, QStringLiteral("p"), &dup).isEmpty());

    QVERIFY(c.request(op("attach", {{QStringLiteral("name"), QStringLiteral("p")}})).value(QStringLiteral("ok")).toBool());
    QVERIFY(c.request(op("write",
                         {{QStringLiteral("name"), QStringLiteral("p")},
                          {QStringLiteral("data"), QStringLiteral("echo \"<$X3RO_T>\"; pwd; stty size\r")}}))
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY2(c.waitFor(id, "<v1>"), c.output.value(id).constData());
    QVERIFY2(c.waitFor(id, m_tmp.path().toUtf8()), c.output.value(id).constData());
    QVERIFY2(c.waitFor(id, "30 100"), c.output.value(id).constData());

    // Resize, then input as an Input frame.
    QVERIFY(c.request(op("resize", {{QStringLiteral("session"), qint64(id)}, {QStringLiteral("cols"), 120}, {QStringLiteral("rows"), 40}}))
                .value(QStringLiteral("ok"))
                .toBool());
    c.sock.write(Sessiond::encodeData(Sessiond::Input, id, "stty size; cat\r"));
    QVERIFY2(c.waitFor(id, "40 120"), c.output.value(id).constData());

    // The list reports the session, its env and its foreground process.
    QTRY_COMPARE_WITH_TIMEOUT(sessionNamed(c.request(op("list")), QStringLiteral("p")).value(QStringLiteral("foreground")).toString(),
                              QStringLiteral("cat"), 5000);
    QJsonObject listed = sessionNamed(c.request(op("list")), QStringLiteral("p"));
    QCOMPARE(listed.value(QStringLiteral("cwd")).toString(), m_tmp.path());
    QCOMPARE(listed.value(QStringLiteral("env")).toObject().value(QStringLiteral("X3RO_T")).toString(), QStringLiteral("v1"));
    QCOMPARE(listed.value(QStringLiteral("cols")).toInt(), 120);

    // Renamed, the attached client keeps receiving output.
    QVERIFY(c.request(op("rename", {{QStringLiteral("name"), QStringLiteral("p")}, {QStringLiteral("to"), QStringLiteral("q")}}))
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(sessionNamed(c.request(op("list")), QStringLiteral("p")).isEmpty());
    c.request(op("write", {{QStringLiteral("name"), QStringLiteral("q")}, {QStringLiteral("data"), QStringLiteral("renamed\r")}}));
    QVERIFY(c.waitFor(id, "renamed"));

    // A second client attaching gets the recent output replayed.
    Client late;
    QVERIFY(late.connectTo(m_socket));
    QVERIFY(late.request(op("attach", {{QStringLiteral("name"), QStringLiteral("q")}})).value(QStringLiteral("ok")).toBool());
    QVERIFY(late.waitFor(id, "<v1>"));

    // Kill ends the session and tells the attached clients.
    QVERIFY(c.request(op("kill", {{QStringLiteral("name"), QStringLiteral("q")}})).value(QStringLiteral("ok")).toBool());
    QCOMPARE(c.event(QStringLiteral("exited")).value(QStringLiteral("session")).toInt(), int(id));
    QVERIFY(sessionNamed(c.request(op("list")), QStringLiteral("q")).isEmpty());

    // A session whose shell exits ends by itself, with its exit status.
    quint32 second = 0;
    QCOMPARE(shellCommandSession(c, QStringLiteral("r"), &second), QString());
    c.request(op("attach", {{QStringLiteral("session"), qint64(second)}}));
    c.request(op("write",
                 {{QStringLiteral("name"), QStringLiteral("r")}, {QStringLiteral("data"), QStringLiteral("echo last words; exit 3\r")}}));
    const QJsonObject exited = c.event(QStringLiteral("exited"));
    QCOMPARE(exited.value(QStringLiteral("name")).toString(), QStringLiteral("r"));
    QCOMPARE(exited.value(QStringLiteral("status")).toInt(), 3);
    QVERIFY(c.output.value(second).contains("last words"));

    // Only one daemon per socket.
    QProcess second_daemon;
    second_daemon.start(DaemonBackend::daemonProgram(), {QStringLiteral("--detach")});
    QVERIFY(second_daemon.waitForFinished(5000));
    QCOMPARE(second_daemon.exitCode(), 0);
    QVERIFY(c.request(op("hello")).value(QStringLiteral("pid")).toInteger() == m_daemon.processId());
}

void SessiondTest::backend()
{
    const QString dir = m_tmp.path() + QStringLiteral("/repo");
    QVERIFY(QDir().mkpath(dir));
    const QString log = m_tmp.path() + QStringLiteral("/keys.log");
    DaemonBackend backend;

    // Asking about sessions never starts the daemon.
    QVERIFY(!backend.hasSession(QStringLiteral("b")));
    QVERIFY(backend.sessionsIn(dir).isEmpty());
    QVERIFY(!QFile::exists(m_socket));

    // Creating one does, in the background.
    const SessionBackend::Environment env{{QStringLiteral("X3RO_T"), QStringLiteral("e1")}};
    QVERIFY(backend.createSession(QStringLiteral("b"), dir, QStringLiteral("cat -v >> ") + log, env));
    QVERIFY(QFile::exists(m_socket));
    QVERIFY(backend.hasSession(QStringLiteral("b")));
    QCOMPARE(backend.sessionEnvironment(QStringLiteral("b")), env);
    QCOMPARE(backend.sessionsIn(dir), QStringList{QStringLiteral("b")});

    QSignalSpy spy(&backend, &SessionBackend::foregroundProcesses);
    backend.startPolling(200);
    using Commands = QHash<QString, QString>;
    auto polledCommand = [&spy] {
        return spy.isEmpty() ? QString() : spy.last().at(0).value<Commands>().value(QStringLiteral("b"));
    };
    QTRY_COMPARE_WITH_TIMEOUT(polledCommand(), QStringLiteral("cat"), 5000);

    // Permission keys and prompt buttons.
    backend.sendKeys(QStringLiteral("b"), {QStringLiteral("1")});
    backend.sendKeys(QStringLiteral("b"), {QStringLiteral("Escape"), QStringLiteral("Enter")});
    QTRY_COMPARE_WITH_TIMEOUT(readFile(log), QStringLiteral("1^[\n"), 5000);
    backend.submit(QStringLiteral("b"), QStringLiteral("hello"));
    QTRY_COMPARE_WITH_TIMEOUT(readFile(log), QStringLiteral("1^[\nhello\n"), 5000);
    backend.submit(QStringLiteral("b"), QStringLiteral("later"), 300);
    QTest::qWait(100);
    QCOMPARE(readFile(log), QStringLiteral("1^[\nhello\n"));
    QTRY_COMPARE_WITH_TIMEOUT(readFile(log), QStringLiteral("1^[\nhello\nlater\n"), 5000);

    QVERIFY(backend.renameSession(QStringLiteral("b"), QStringLiteral("c")));
    QVERIFY(!backend.hasSession(QStringLiteral("b")));
    QVERIFY(backend.hasSession(QStringLiteral("c")));
    QCOMPARE(backend.attachCommand(QStringLiteral("c")).last(), QStringLiteral("c"));
    QCOMPARE(backend.attachHint(QStringLiteral("c")), QStringLiteral("x3ro-deck attach c"));

    backend.killSession(QStringLiteral("c"));
    QVERIFY(!backend.hasSession(QStringLiteral("c")));

    // Another backend (the next app start) finds the daemon's sessions.
    QVERIFY(backend.createSession(QStringLiteral("d"), dir, QString()));
    {
        DaemonBackend next;
        QVERIFY(next.hasSession(QStringLiteral("d")));
    }

    // "Stop agents and quit" ends every session and the daemon.
    backend.killAll();
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(m_socket), 3000);
    QVERIFY(!backend.hasSession(QStringLiteral("d")));
}

void SessiondTest::attachClient()
{
    QVERIFY(startForeground());
    Client c;
    QVERIFY(c.connectTo(m_socket));
    quint32 id = 0;
    QCOMPARE(shellCommandSession(c, QStringLiteral("my_checkout"), &id), QString());
    c.request(
        op("write",
           {{QStringLiteral("name"), QStringLiteral("my_checkout")}, {QStringLiteral("data"), QStringLiteral("echo before-attach\r")}}));
    QTest::qWait(300);

    // No session of that name: an error, not a hang.
    QProcess missing;
    missing.start(QStringLiteral(X3RO_DECK_ATTACH_PATH), {QStringLiteral("nope")});
    QVERIFY(missing.waitForFinished(5000));
    QCOMPARE(missing.exitCode(), 1);

    // Runs the attach client in a PTY of its own, like a terminal would.
    auto attach = [](const QString &checkout, int *master) {
        struct winsize ws{};
        ws.ws_col = 90;
        ws.ws_row = 20;
        const QByteArray program(X3RO_DECK_ATTACH_PATH);
        const QByteArray arg = checkout.toUtf8();
        const pid_t pid = forkpty(master, nullptr, nullptr, &ws);
        if (pid == 0) {
            execl(program.constData(), program.constData(), arg.constData(), static_cast<char *>(nullptr));
            _exit(127);
        }
        return pid;
    };
    auto readUntil = [](int fd, QByteArray &seen, const QByteArray &text) {
        QDeadlineTimer deadline(5000);
        while (!seen.contains(text) && !deadline.hasExpired()) {
            pollfd p{fd, POLLIN, 0};
            if (poll(&p, 1, 100) > 0) {
                char buf[4096];
                const ssize_t n = ::read(fd, buf, sizeof buf);
                if (n <= 0) {
                    break;
                }
                seen.append(buf, n);
            }
        }
        return seen.contains(text);
    };
    auto exitCode = [](pid_t pid) {
        int status = -1;
        QDeadlineTimer deadline(5000);
        while (waitpid(pid, &status, WNOHANG) == 0 && !deadline.hasExpired()) {
            QTest::qWait(20);
        }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    };

    // The checkout name maps to its session name, as in AppController.
    int master = -1;
    pid_t pid = attach(QStringLiteral("my.checkout"), &master);
    QVERIFY(pid > 0);
    QByteArray seen;
    QVERIFY2(readUntil(master, seen, "before-attach"), seen.constData());
    QTRY_COMPARE_WITH_TIMEOUT(sessionNamed(c.request(op("list")), QStringLiteral("my_checkout")).value(QStringLiteral("cols")).toInt(), 90,
                              5000);

    // Typing reaches the session.
    QCOMPARE(::write(master, "echo typed-$((40+2))\r", 21), 21);
    QVERIFY2(readUntil(master, seen, "typed-42"), seen.constData());

    // The terminal's size follows the PTY.
    struct winsize ws{};
    ws.ws_col = 70;
    ws.ws_row = 25;
    ioctl(master, TIOCSWINSZ, &ws);
    QTRY_COMPARE_WITH_TIMEOUT(sessionNamed(c.request(op("list")), QStringLiteral("my_checkout")).value(QStringLiteral("cols")).toInt(), 70,
                              5000);

    // Ctrl+\ detaches and leaves the session running.
    QCOMPARE(::write(master, "\x1c", 1), 1);
    QVERIFY2(readUntil(master, seen, "[detached from my_checkout]"), seen.constData());
    QCOMPARE(exitCode(pid), 0);
    ::close(master);
    QVERIFY(!sessionNamed(c.request(op("list")), QStringLiteral("my_checkout")).isEmpty());

    // The session ending ends the client.
    pid = attach(QStringLiteral("my_checkout"), &master);
    seen.clear();
    QVERIFY2(readUntil(master, seen, "typed-42"), seen.constData());
    QCOMPARE(::write(master, "exit\r", 5), 5);
    QVERIFY2(readUntil(master, seen, "[session my_checkout ended]"), seen.constData());
    QCOMPARE(exitCode(pid), 0);
    ::close(master);
}

QTEST_GUILESS_MAIN(SessiondTest)
#include "tst_sessiond.moc"
