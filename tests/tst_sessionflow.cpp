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

private:
    QString readLog() const;
    void sendHook(const QJsonObject &payload) const;
    int permissionRows(AppController &app) const;

    // Short path: the hook socket lives here and must stay under 108 bytes.
    QTemporaryDir m_tmp{QStringLiteral("/tmp/xdt-XXXXXX")};
    QString m_repo;
    QString m_log;
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
         }}},
        {QStringLiteral("checkouts"),
         QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("flow")}, {QStringLiteral("path"), m_repo}}}},
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

QString SessionFlowTest::readLog() const
{
    QFile f(m_log);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
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

QTEST_MAIN(SessionFlowTest)
#include "tst_sessionflow.moc"
