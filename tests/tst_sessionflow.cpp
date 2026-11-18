// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// End-to-end agent flow through AppController and the session backend, the
// same calls the QML buttons make. It runs a private tmux server, so it never
// touches a live instance's sessions. With X3RO_DECK_TEST_BACKEND=daemon it
// runs the same flow against a private x3ro-deck-sessiond instead.

#include "AgentBus.h"
#include "AppController.h"
#include "SessionBackend.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <SessiondProtocol.h>

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
    void composeCommandsPerCheckout();
    void backendSwitchResumesAgents();
    void paneDrivesState();
    void importedShell();

private:
    static bool daemon() { return qEnvironmentVariable("X3RO_DECK_TEST_BACKEND") == QLatin1String("daemon"); }
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
    QString m_pollRepo;
    QString m_bin;
    QString m_shellRepo;
    QString m_resumeRepo;
    QString m_resumeLog;
    QString m_resumeScript;
    QString m_nvmLog;
    QString m_shellA;
    QString m_shellB;
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

    // `codex` here is sleep under another name: the pane poll sees "codex".
    m_pollRepo = m_tmp.path() + QStringLiteral("/poll-repo");
    QVERIFY(QDir().mkpath(m_pollRepo));
    m_bin = m_tmp.path() + QStringLiteral("/bin");
    QVERIFY(QDir().mkpath(m_bin));
    const QString sleep = QStandardPaths::findExecutable(QStringLiteral("sleep"));
    QVERIFY(!sleep.isEmpty());
    QVERIFY(QFile::link(sleep, m_bin + QStringLiteral("/codex")));

    // Sessions run a fixture shell in a private HOME, never the user's shell
    // or rc files: bash under names of its own, so the pane poll tells them
    // apart. Its rc file puts an nvm-style bin directory on PATH, which the
    // test's own PATH lacks, as a desktop-launched app's does.
    const QString bash = QStandardPaths::findExecutable(QStringLiteral("bash"));
    QVERIFY(!bash.isEmpty());
    m_shellA = m_bin + QStringLiteral("/deckshell-a");
    m_shellB = m_bin + QStringLiteral("/deckshell-b");
    QVERIFY(QFile::link(bash, m_shellA));
    QVERIFY(QFile::link(bash, m_shellB));
    const QString home = m_tmp.path() + QStringLiteral("/home");
    QVERIFY(QDir().mkpath(home + QStringLiteral("/nvm/bin")));
    qputenv("HOME", home.toLocal8Bit());
    qputenv("SHELL", m_shellA.toLocal8Bit());
    qunsetenv("ENV");
    qunsetenv("BASH_ENV");
    auto writeFile = [](const QString &path, const QString &text, bool exec = false) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
        f.close();
        if (exec) {
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        }
    };
    writeFile(home + QStringLiteral("/.bash_profile"), QStringLiteral(". \"$HOME/.bashrc\"\n"));
    writeFile(home + QStringLiteral("/.bashrc"), QStringLiteral("export PATH=\"$HOME/nvm/bin:$PATH\"\nPS1='$ '\n"));
    m_nvmLog = m_tmp.path() + QStringLiteral("/nvm.log");
    // The script logs and then becomes "fakeclaude" for the pane poll, which
    // reads argv[0] as tmux does.
    QVERIFY(QDir().mkpath(home + QStringLiteral("/nvm/lib")));
    QVERIFY(QFile::link(sleep, home + QStringLiteral("/nvm/lib/fakeclaude")));
    writeFile(home + QStringLiteral("/nvm/bin/fakeclaude"),
              QStringLiteral("#!/bin/sh\necho ran >> %1\nexec \"$HOME/nvm/lib/fakeclaude\" 300\n").arg(m_nvmLog), true);
    QVERIFY(QStandardPaths::findExecutable(QStringLiteral("fakeclaude")).isEmpty());
    m_shellRepo = m_tmp.path() + QStringLiteral("/shell-repo");
    QVERIFY(QDir().mkpath(m_shellRepo));
    m_resumeRepo = m_tmp.path() + QStringLiteral("/resume-repo");
    QVERIFY(QDir().mkpath(m_resumeRepo));
    m_resumeLog = m_tmp.path() + QStringLiteral("/resume.log");
    m_resumeScript = m_bin + QStringLiteral("/resumer");
    writeFile(m_resumeScript, QStringLiteral("#!/bin/sh\necho \"start $*\" >> %1\nexec cat\n").arg(m_resumeLog), true);

    // `cat` stands in for an agent: what reaches its terminal ends up in the log.
    const QJsonObject config{
        {QStringLiteral("autostartAgents"), true},
        // No import: the session shell is $SHELL, the fixture above.
        {QStringLiteral("terminalSettings"), QStringLiteral("none")},
        {QStringLiteral("sessionBackend"), daemon() ? QStringLiteral("daemon") : QStringLiteral("tmux")},
        {QStringLiteral("defaultAgent"), QStringLiteral("fake")},
        {QStringLiteral("agents"),
         QJsonArray{QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("fake")},
                        {QStringLiteral("kind"), QStringLiteral("custom")},
                        {QStringLiteral("command"), QStringLiteral("cat >> ") + m_log},
                        {QStringLiteral("permissionKeys"), QJsonObject{{QStringLiteral("allow"), QStringLiteral("Enter")}}},
                    },
                    scriptAgent("envy", {{QStringLiteral("X3RO_TEST"), QStringLiteral("${HOME}/deck|$X3RO_UNSET_VAR|$")}}),
                    scriptAgent("plain", {}),
                    // No hooks: only the pane poll tracks it.
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("poller")},
                                {QStringLiteral("kind"), QStringLiteral("custom")},
                                {QStringLiteral("command"), QStringLiteral("sleep 300")}},
                    // Found only on the PATH the shell's rc file sets.
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("nvm")},
                                {QStringLiteral("kind"), QStringLiteral("custom")},
                                {QStringLiteral("command"), QStringLiteral("fakeclaude")}},
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("cx")}, {QStringLiteral("kind"), QStringLiteral("codex")}},
                    // Logs its arguments, so a resumed start shows its continue args.
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("resumer")},
                                {QStringLiteral("kind"), QStringLiteral("custom")},
                                {QStringLiteral("command"), m_resumeScript},
                                {QStringLiteral("continueArgs"), QStringLiteral("--resumed")}}}},
        {QStringLiteral("checkouts"),
         QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("flow")}, {QStringLiteral("path"), m_repo}},
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("envy")},
                                {QStringLiteral("path"), m_envRepo},
                                {QStringLiteral("agent"), QStringLiteral("envy")},
                                {QStringLiteral("compose"), QJsonObject{{QStringLiteral("up"), QStringLiteral("make up")}}}},
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("poll")},
                                {QStringLiteral("path"), m_pollRepo},
                                {QStringLiteral("agent"), QStringLiteral("poller")}},
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("shell")},
                                {QStringLiteral("path"), m_shellRepo},
                                {QStringLiteral("agent"), QStringLiteral("nvm")}},
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("resume")},
                                {QStringLiteral("path"), m_resumeRepo},
                                {QStringLiteral("agent"), QStringLiteral("resumer")}}}},
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
    if (daemon()) {
        // The session's recent output, as an attaching client gets it replayed.
        QLocalSocket sock;
        sock.connectToServer(Sessiond::socketPath());
        if (!sock.waitForConnected(2000)) {
            return {};
        }
        sock.write(Sessiond::encodeControl(
            {{QStringLiteral("op"), QStringLiteral("attach")}, {QStringLiteral("id"), 1}, {QStringLiteral("name"), session}}));
        QByteArray buffer;
        QByteArray out;
        QDeadlineTimer deadline(500);
        while (!deadline.hasExpired()) {
            sock.waitForReadyRead(50);
            buffer.append(sock.readAll());
            Sessiond::Frame f;
            quint32 id = 0;
            QByteArray bytes;
            while (Sessiond::takeFrame(buffer, f)) {
                if (f.type == Sessiond::Output && Sessiond::splitData(f.payload, id, bytes)) {
                    out.append(bytes);
                }
            }
        }
        return QString::fromUtf8(out);
    }
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

// After "Restart agents now", a checkout whose agent ran starts its new
// session with the continue line, once, autostart or not.
void SessionFlowTest::backendSwitchResumesAgents()
{
    AppController app;
    const int i = 4;
    QCOMPARE(app.checkout(i).cfg.name, QStringLiteral("resume"));
    QSettings().setValue(AppController::kResumeKey, QStringList{QStringLiteral("resume"), QStringLiteral("elsewhere")});
    QVERIFY(app.ensureAgentSession(i));
    QTRY_COMPARE_WITH_TIMEOUT(readFile(m_resumeLog), QStringLiteral("start --resumed\n"), 5000);
    QCOMPARE(QSettings().value(AppController::kResumeKey).toStringList(), QStringList{QStringLiteral("elsewhere")});
    QSettings().remove(AppController::kResumeKey);

    // A fresh session afterwards starts normally.
    app.sessions()->killSession(app.checkout(i).session);
    QTRY_VERIFY_WITH_TIMEOUT(!app.sessions()->hasSession(app.checkout(i).session), 5000);
    QVERIFY(app.ensureAgentSession(i));
    QTRY_COMPARE_WITH_TIMEOUT(readFile(m_resumeLog), QStringLiteral("start --resumed\nstart \n"), 5000);
    app.sessions()->killSession(app.checkout(i).session);
}

void SessionFlowTest::composeCommandsPerCheckout()
{
    AppController app;
    QList<QPair<int, QString>> ran;
    connect(&app, &AppController::runInTerminal, this,
            [&](int checkout, const QString &, const QString &command) { ran.append({checkout, command}); });
    app.select(0);
    app.runCompose(QStringLiteral("up"));
    app.select(1);
    app.runCompose(QStringLiteral("up"));
    app.runCompose(QStringLiteral("down"));
    QCOMPARE(ran.size(), 3);
    QCOMPARE(ran.at(0), qMakePair(0, QStringLiteral("docker compose up -d")));
    QCOMPARE(ran.at(1), qMakePair(1, QStringLiteral("make up")));
    QCOMPARE(ran.at(2), qMakePair(1, QStringLiteral("docker compose down")));
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

// The pane poll alone, with no hook events: an agent starting and exiting in
// the session moves the checkout between ready and offline, and an agent of
// another profile started by hand switches the checkout to that profile.
void SessionFlowTest::paneDrivesState()
{
    AppController app;
    app.start();
    const int i = 2;
    const QString session = app.checkout(i).session;
    QCOMPARE(app.profile(i).id, QStringLiteral("poller"));
    QCOMPARE(app.checkout(i).agent.state, QStringLiteral("offline"));

    QVERIFY(app.ensureAgentSession(i));
    QTRY_VERIFY_WITH_TIMEOUT(app.checkout(i).agent.running, 10000);
    QCOMPARE(app.checkout(i).agent.state, QStringLiteral("ready"));

    // The agent exits; the shell stays.
    app.sessions()->sendKeys(session, {QStringLiteral("C-c")});
    QTRY_VERIFY_WITH_TIMEOUT(!app.checkout(i).agent.running, 10000);
    QCOMPARE(app.checkout(i).agent.state, QStringLiteral("offline"));
    QVERIFY(app.sessions()->hasSession(session));

    // Codex started by hand in the session.
    app.sessions()->submit(session, m_bin + QStringLiteral("/codex 300"));
    QTRY_VERIFY_WITH_TIMEOUT(app.checkout(i).agent.running, 10000);
    QCOMPARE(app.profile(i).id, QStringLiteral("cx"));
    QCOMPARE(app.checkout(i).agent.state, QStringLiteral("ready"));
    app.sessions()->sendKeys(session, {QStringLiteral("C-c")});
    QTRY_VERIFY_WITH_TIMEOUT(!app.checkout(i).agent.running, 10000);
    QCOMPARE(app.checkout(i).agent.state, QStringLiteral("offline"));

    // The same agent behind a wrapper, as `node codex.js` or a launcher
    // script runs it. tmux sees only the wrapper.
    if (daemon()) {
        app.select(i);
        app.setAgentProfile(QStringLiteral("poller"));
        QCOMPARE(app.profile(i).id, QStringLiteral("poller"));
        app.sessions()->submit(session, QStringLiteral("sh -c '%1/codex 300; true'").arg(m_bin));
        QTRY_VERIFY_WITH_TIMEOUT(app.checkout(i).agent.running, 10000);
        QCOMPARE(app.profile(i).id, QStringLiteral("cx"));
        app.sessions()->sendKeys(session, {QStringLiteral("C-c")});
        QTRY_VERIFY_WITH_TIMEOUT(!app.checkout(i).agent.running, 10000);
        QCOMPARE(app.checkout(i).agent.state, QStringLiteral("offline"));
    }
    app.sessions()->killSession(session);
}

// Sessions run the imported shell as an interactive login shell, so its rc
// files set PATH before the agent command is typed in. A changed shell
// recreates an idle session on Start, and a missing one falls back to $SHELL.
void SessionFlowTest::importedShell()
{
    AppController app;
    app.start();
    const int i = 3;
    const QString session = app.checkout(i).session;
    QSignalSpy panes(app.sessions(), &SessionBackend::foregroundProcesses);
    auto foreground = [&panes](const QString &name) {
        return panes.isEmpty() ? QStringList() : panes.last().at(0).value<QHash<QString, QStringList>>().value(name);
    };

    // The agent exists only on the rc file's PATH, and it runs.
    QCOMPARE(app.terminalSettings()->current().shell, QStringList{m_shellA});
    QVERIFY(app.ensureAgentSession(i));
    QCOMPARE(app.sessions()->sessionShell(session), QStringList{m_shellA});
    QTRY_COMPARE_WITH_TIMEOUT(readFile(m_nvmLog), QStringLiteral("ran\n"), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(app.checkout(i).agent.running, 10000);

    // Idle, the pane poll reports the shell.
    app.sessions()->sendKeys(session, {QStringLiteral("C-c")});
    QTRY_VERIFY_WITH_TIMEOUT(!app.checkout(i).agent.running, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(foreground(session).value(0), QStringLiteral("deckshell-a"), 10000);

    // A new shell: Start recreates the idle session in it.
    app.sessions()->submit(session, QStringLiteral("export KEPT=1"));
    QTRY_VERIFY_WITH_TIMEOUT(paneText(session).contains(QStringLiteral("export KEPT=1")), 5000);
    qputenv("SHELL", m_shellB.toLocal8Bit());
    app.reloadConfig();
    QCOMPARE(app.terminalSettings()->current().shell, QStringList{m_shellB});
    app.select(i);
    app.agentAction(QStringLiteral("start"));
    QCOMPARE(app.sessions()->sessionShell(session), QStringList{m_shellB});
    QTRY_VERIFY2_WITH_TIMEOUT(readFile(m_nvmLog) == QStringLiteral("ran\nran\n"), qPrintable(paneText(session)), 10000);
    QVERIFY(!paneText(session).contains(QStringLiteral("export KEPT=1")));
    QTRY_VERIFY_WITH_TIMEOUT(app.checkout(i).agent.running, 10000);
    app.sessions()->sendKeys(session, {QStringLiteral("C-c")});
    QTRY_VERIFY_WITH_TIMEOUT(!app.checkout(i).agent.running, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(foreground(session).value(0), QStringLiteral("deckshell-b"), 10000);
    app.sessions()->killSession(session);

    // A missing shell falls back to $SHELL; the session still records the request.
    const QString missing = QStringLiteral("missing-shell");
    const QStringList gone{m_tmp.path() + QStringLiteral("/no/fish")};
    QVERIFY(app.sessions()->createSession(missing, m_shellRepo, QString(), {}, gone));
    QCOMPARE(app.sessions()->sessionShell(missing), gone);
    QTRY_COMPARE_WITH_TIMEOUT(foreground(missing).value(0), QStringLiteral("deckshell-b"), 10000);
    app.sessions()->killSession(missing);

    // A shell with arguments runs exactly as given: --norc keeps the rc
    // file's PATH out.
    const QString norc = QStringLiteral("norc-shell");
    QVERIFY(app.sessions()->createSession(norc, m_shellRepo, QStringLiteral("type fakeclaude || echo NOT-\"\"FOUND"), {},
                                          {m_shellA, QStringLiteral("--norc"), QStringLiteral("-i")}));
    QTRY_COMPARE_WITH_TIMEOUT(foreground(norc).value(0), QStringLiteral("deckshell-a"), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(paneText(norc).contains(QStringLiteral("NOT-FOUND")), 5000);
    app.sessions()->killSession(norc);
    qputenv("SHELL", m_shellA.toLocal8Bit());
}

QTEST_MAIN(SessionFlowTest)
#include "tst_sessionflow.moc"
