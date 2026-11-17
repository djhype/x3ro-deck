// The hook socket path shared by AgentBus and x3ro-deck-hook, and the hook
// binary talking to it.

#include "AgentBus.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <x3ro-deck-socket.h>

class SocketPathTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void overrideWins();
    void runtimeDir();
    void fallbackWithoutRuntimeDir();
    void overLongOverride();
    void hookDeliversPayload();
    void hookIsSilentOnOverLongPath();

private:
    QProcess *runHook(const QByteArray &payload);
};

void SocketPathTest::init()
{
    qunsetenv("X3RO_DECK_SOCKET");
    qunsetenv("TMPDIR");
    qputenv("XDG_RUNTIME_DIR", "/run/user/1000");
}

void SocketPathTest::overrideWins()
{
    qputenv("X3RO_DECK_SOCKET", "/tmp/elsewhere.sock");
    QCOMPARE(x3ro_deck::hookSocketPath().path, std::string("/tmp/elsewhere.sock"));
    QCOMPARE(AgentBus::socketPath(), QStringLiteral("/tmp/elsewhere.sock"));
}

void SocketPathTest::runtimeDir()
{
#if defined(__APPLE__)
    QSKIP("macOS resolves from $TMPDIR");
#endif
    qputenv("XDG_RUNTIME_DIR", "/run/user/1000/");
    const x3ro_deck::SocketPath p = x3ro_deck::hookSocketPath();
    QCOMPARE(p.path, std::string("/run/user/1000/x3ro-deck.sock"));
    QVERIFY(p.privateDir.empty());
}

void SocketPathTest::fallbackWithoutRuntimeDir()
{
    qunsetenv("XDG_RUNTIME_DIR");
    const std::string dir = "/tmp/x3ro-deck-" + std::to_string(getuid());
    QCOMPARE(x3ro_deck::hookSocketPath().privateDir, dir);
    QCOMPARE(x3ro_deck::hookSocketPath().path, dir + "/x3ro-deck.sock");

    // A runtime dir too long for sun_path falls back the same way.
    qputenv("XDG_RUNTIME_DIR", QByteArray("/") + QByteArray(120, 'r'));
    QCOMPARE(x3ro_deck::hookSocketPath().privateDir, dir);
}

void SocketPathTest::overLongOverride()
{
    qputenv("X3RO_DECK_SOCKET", QByteArray("/tmp/") + QByteArray(120, 'x'));
    QVERIFY(x3ro_deck::hookSocketPath().tooLong());
    AgentBus bus;
    QString error;
    QVERIFY(!bus.listen(&error));
    QVERIFY2(error.contains(QStringLiteral("platform limit")), qPrintable(error));
}

QProcess *SocketPathTest::runHook(const QByteArray &payload)
{
    auto *hook = new QProcess(this);
    hook->start(QStringLiteral(X3RO_DECK_HOOK_PATH), {});
    hook->write(payload);
    hook->closeWriteChannel();
    return hook;
}

void SocketPathTest::hookDeliversPayload()
{
    QTemporaryDir dir(QStringLiteral("/tmp/xdh-XXXXXX"));
    QVERIFY(dir.isValid());
    qputenv("X3RO_DECK_SOCKET", (dir.path() + QStringLiteral("/hook.sock")).toLocal8Bit());

    AgentBus bus;
    QString error;
    QVERIFY2(bus.listen(&error), qPrintable(error));
    QSignalSpy spy(&bus, &AgentBus::hookEvent);

    QProcess *hook = runHook(R"({"hook_event_name":"Stop","cwd":"/x"})");
    QVERIFY(hook->waitForFinished(3000));
    QCOMPARE(hook->exitCode(), 0);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toJsonObject().value(QStringLiteral("hook_event_name")).toString(), QStringLiteral("Stop"));
}

void SocketPathTest::hookIsSilentOnOverLongPath()
{
    qputenv("X3RO_DECK_SOCKET", QByteArray("/tmp/") + QByteArray(120, 'x'));
    QProcess *hook = runHook(R"({"hook_event_name":"Stop"})");
    QVERIFY(hook->waitForFinished(3000));
    QCOMPARE(hook->exitStatus(), QProcess::NormalExit);
    QCOMPARE(hook->exitCode(), 0);
    QVERIFY(hook->readAllStandardOutput().isEmpty());
    QVERIFY(hook->readAllStandardError().isEmpty());
}

QTEST_GUILESS_MAIN(SocketPathTest)
#include "tst_socketpath.moc"
