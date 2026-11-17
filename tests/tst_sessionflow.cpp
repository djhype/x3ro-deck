// End-to-end agent flow through AppController and the session backend, the
// same calls the QML buttons make. It runs a private tmux server, so it never
// touches a live instance's sessions.

#include "AgentBus.h"
#include "AppController.h"
#include "SessionBackend.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class SessionFlowTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void startPermissionPromptRestartStop();
    void envAtSpawnAndProfileSwitch();
    void hookCwdMapsToCheckout();

private:
    static QString readFile(const QString &path);
    QString readLog() const { return readFile(m_log); }
    QString paneText(const QString &session) const;
    void sendHook(const QJsonObject &payload) const;
    int permissionRows(AppController &app) const;

    // Short path: the hook socket lives here and must stay under 108 bytes.
    QTemporaryDir m_tmp{QStringLiteral("/tmp/xdt-XXXXXX")};
    QString m_repo;
    QString m_log;
    QString m_envRepo;
    QString m_envLog;
};

void SessionFlowTest::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    QFile::setPermissions(m_tmp.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("XDG_RUNTIME_DIR", m_tmp.path().toLocal8Bit());
    qputenv("TMUX_TMPDIR", m_tmp.path().toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", (m_tmp.path() + QStringLiteral("/config")).toLocal8Bit());

    m_repo = m_tmp.path() + QStringLiteral("/repo");
    m_log = m_tmp.path() + QStringLiteral("/agent.log");
    QVERIFY(QDir().mkpath(m_repo));
    m_envRepo = m_tmp.path() + QStringLiteral("/env-repo");
    m_envLog = m_tmp.path() + QStringLiteral("/env.log");
    QVERIFY(QDir().mkpath(m_envRepo));

    // Logs X3RO_TEST as the agent sees it, then waits like an agent would.
    const QString script = m_tmp.path() + QStringLiteral("/agent.sh");
    QFile agent(script);
    QVERIFY(agent.open(QIODevice::WriteOnly));
    agent.write(QStringLiteral("#!/bin/sh\n"
                               "if [ -n \"${X3RO_TEST+x}\" ]; then printf '%s\\n' \"$X3RO_TEST\"; else echo unset; fi >> %1\n"
                               "exec cat\n")
                    .arg(m_envLog)
                    .toUtf8());
    agent.close();
    agent.setPermissions(agent.permissions() | QFileDevice::ExeOwner);
    auto scriptAgent = [&script](const char *id, const QJsonObject &env) {
        return QJsonObject{{QStringLiteral("id"), QLatin1String(id)},
                           {QStringLiteral("kind"), QStringLiteral("custom")},
                           {QStringLiteral("command"), script},
                           {QStringLiteral("process"), QStringLiteral("cat")},
                           {QStringLiteral("env"), env}};
    };
    QVERIFY(QDir().mkpath(m_tmp.path() + QStringLiteral("/config/x3ro-deck")));

    // `cat` stands in for an agent: what reaches its terminal ends up in the log.
    const QJsonObject config{
        {QStringLiteral("autostartAgents"), true},
        {QStringLiteral("defaultAgent"), QStringLiteral("fake")},
        {QStringLiteral("agents"),
         QJsonArray{QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("fake")},
                        {QStringLiteral("kind"), QStringLiteral("custom")},
                        {QStringLiteral("command"), QStringLiteral("cat >> ") + m_log},
                        {QStringLiteral("permissionKeys"), QJsonObject{{QStringLiteral("allow"), QStringLiteral("Enter")}}},
                    },
                    scriptAgent("envy", {{QStringLiteral("X3RO_TEST"), QStringLiteral("${HOME}/deck|$X3RO_UNSET_VAR|$")}}),
                    scriptAgent("plain", {})}},
        {QStringLiteral("checkouts"),
         QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("flow")}, {QStringLiteral("path"), m_repo}},
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("envy")},
                                {QStringLiteral("path"), m_envRepo},
                                {QStringLiteral("agent"), QStringLiteral("envy")}}}},
    };
    QFile f(m_tmp.path() + QStringLiteral("/config/x3ro-deck/config.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(config).toJson());
}

void SessionFlowTest::cleanupTestCase()
{
    // Whatever happened, leave no tmux server behind.
    AppController app;
    app.sessions()->killAll();
}

QString SessionFlowTest::readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString SessionFlowTest::paneText(const QString &session) const
{
    QProcess capture;
    capture.start(QStringLiteral("tmux"),
                  {QStringLiteral("-L"), QStringLiteral("x3ro-deck"), QStringLiteral("capture-pane"), QStringLiteral("-p"),
                   QStringLiteral("-t"), QLatin1Char('=') + session + QLatin1Char(':')});
    capture.waitForFinished(2000);
    return QString::fromUtf8(capture.readAllStandardOutput());
}

void SessionFlowTest::sendHook(const QJsonObject &payload) const
{
    QLocalSocket sock;
    sock.connectToServer(AgentBus::socketPath());
    QVERIFY(sock.waitForConnected(2000));
    sock.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QVERIFY(sock.waitForBytesWritten(2000));
    sock.disconnectFromServer();
}

int SessionFlowTest::permissionRows(AppController &app) const
{
    int n = 0;
    for (int i = 0; i < app.attentionModel()->rowCount(); ++i) {
        n += app.attentionModel()->row(i).value(QStringLiteral("kind")) == QLatin1String("permission");
    }
    return n;
}

void SessionFlowTest::startPermissionPromptRestartStop()
{
    QString session;
    {
        AppController app;
        app.start();
        session = app.checkout(0).session;

        // Start: the session appears and the agent runs in it.
        QVERIFY(app.ensureAgentSession(0));
        QVERIFY(app.sessions()->hasSession(session));
        QTRY_VERIFY_WITH_TIMEOUT(app.checkout(0).agent.running, 10000);

        // A permission prompt lands in NEEDS YOU; Allow sends the profile's key.
        sendHook({{QStringLiteral("hook_event_name"), QStringLiteral("PermissionRequest")},
                  {QStringLiteral("cwd"), m_repo},
                  {QStringLiteral("tool_name"), QStringLiteral("Bash")},
                  {QStringLiteral("tool_input"), QJsonObject{{QStringLiteral("command"), QStringLiteral("ls")}}}});
        QTRY_COMPARE_WITH_TIMEOUT(permissionRows(app), 1, 5000);
        QString id;
        for (int i = 0; i < app.attentionModel()->rowCount(); ++i) {
            if (app.attentionModel()->row(i).value(QStringLiteral("kind")) == QLatin1String("permission")) {
                id = app.attentionModel()->row(i).value(QStringLiteral("id")).toString();
            }
        }
        app.attentionAction(id, QStringLiteral("allow"));
        QCOMPARE(permissionRows(app), 0);

        // The allow key is Enter, so it shows up in the log on its own.
        QTRY_COMPARE_WITH_TIMEOUT(readLog(), QStringLiteral("\n"), 5000);

        // A prompt button types the text and presses Enter.
        app.sendToAgent(QStringLiteral("hello"));
        QTRY_COMPARE_WITH_TIMEOUT(readLog(), QStringLiteral("\nhello\n"), 5000);
    }

    // Restart: a new controller finds the session and its agent still running.
    {
        AppController app;
        app.start();
        QVERIFY(app.sessions()->hasSession(session));
        QTRY_VERIFY_WITH_TIMEOUT(app.checkout(0).agent.running, 10000);
        app.sendToAgent(QStringLiteral("again"));
        QTRY_COMPARE_WITH_TIMEOUT(readLog(), QStringLiteral("\nhello\nagain\n"), 5000);

        // "Stop agents and quit".
        app.sessions()->killAll();
        QVERIFY(!app.sessions()->hasSession(session));
    }
}

void SessionFlowTest::envAtSpawnAndProfileSwitch()
{
    AppController app;
    app.start();
    const QString session = app.checkout(1).session;
    const QString expected = QDir::homePath() + QStringLiteral("/deck||$\n");

    // The profile's env reaches the agent, expanded against the app's
    // environment, and is never typed into the pane.
    QVERIFY(app.ensureAgentSession(1));
    QCOMPARE(app.profile(1).id, QStringLiteral("envy"));
    QTRY_COMPARE_WITH_TIMEOUT(readFile(m_envLog), expected, 5000);
    QVERIFY2(!paneText(session).contains(QStringLiteral("X3RO_TEST")), qPrintable(paneText(session)));
    QTRY_VERIFY_WITH_TIMEOUT(app.checkout(1).agent.running, 10000);

    // End the agent, switch the checkout to a profile without env and start
    // it: the session is recreated, so the variable is gone.
    app.select(1);
    app.sessions()->sendKeys(session, {QStringLiteral("C-d")});
    QTRY_VERIFY_WITH_TIMEOUT(!app.checkout(1).agent.running, 10000);
    app.setAgentProfile(QStringLiteral("plain"));
    QCOMPARE(app.profile(1).id, QStringLiteral("plain"));
    app.agentAction(QStringLiteral("start"));
    QTRY_COMPARE_WITH_TIMEOUT(readFile(m_envLog), expected + QStringLiteral("unset\n"), 5000);
    QVERIFY(app.sessions()->sessionEnvironment(session).isEmpty());

    // Starting the same profile again keeps the session.
    app.sessions()->sendKeys(session, {QStringLiteral("C-d")});
    QTRY_VERIFY_WITH_TIMEOUT(!app.checkout(1).agent.running, 10000);
    app.sessions()->submit(session, QStringLiteral("export KEPT=1"));
    app.agentAction(QStringLiteral("start"));
    QTRY_COMPARE_WITH_TIMEOUT(readFile(m_envLog), expected + QStringLiteral("unset\nunset\n"), 5000);
    QVERIFY(paneText(session).contains(QStringLiteral("export KEPT=1")));
}

void SessionFlowTest::hookCwdMapsToCheckout()
{
    AppController app;
    app.start();
    const QString sibling = m_repo + QStringLiteral("x");
    QVERIFY(QDir().mkpath(sibling));
    QVERIFY(QDir().mkpath(m_repo + QStringLiteral("/src/deep")));
    auto ask = [this](const QString &cwd) {
        sendHook({{QStringLiteral("hook_event_name"), QStringLiteral("PermissionRequest")},
                  {QStringLiteral("cwd"), cwd},
                  {QStringLiteral("tool_name"), QStringLiteral("Bash")},
                  {QStringLiteral("tool_input"), QJsonObject{{QStringLiteral("command"), QStringLiteral("ls")}}}});
    };

    // A sibling whose name merely starts with the checkout's is not inside it.
    ask(sibling);
    QTest::qWait(300);
    QCOMPARE(permissionRows(app), 0);

    // A subdirectory, spelled with a trailing slash and a "..", maps to the checkout.
    ask(m_repo + QStringLiteral("/src/deep/../deep/"));
    QTRY_COMPARE_WITH_TIMEOUT(permissionRows(app), 1, 5000);
    QCOMPARE(app.checkout(0).agent.state, QStringLiteral("permission"));
}

QTEST_MAIN(SessionFlowTest)
#include "tst_sessionflow.moc"
