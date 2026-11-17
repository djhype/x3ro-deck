// Docker endpoint resolution, and the app talking to a fake Engine API on a
// non-default socket with a non-default compose binary.

#include "AppController.h"
#include "DockerClient.h"
#include "DockerEndpoint.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
DockerEnvironment fakeEnv(const QStringList &env, const QString &home, const QSet<QString> &sockets)
{
    QProcessEnvironment e;
    for (const QString &kv : env) {
        e.insert(kv.section(QLatin1Char('='), 0, 0), kv.section(QLatin1Char('='), 1));
    }
    return {e, home, [sockets](const QString &p) { return sockets.contains(p); }};
}

void writeJson(const QString &path, const QJsonObject &o)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(o).toJson());
}

// Answers /containers/json with `containers` and holds /events open.
class FakeEngine : public QObject
{
public:
    FakeEngine(const QString &path, const QJsonArray &containers)
        : m_containers(containers)
    {
        QLocalServer::removeServer(path);
        QVERIFY(m_server.listen(path));
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket *s = m_server.nextPendingConnection()) {
                connect(s, &QLocalSocket::readyRead, s, [this, s] {
                    const QByteArray req = s->readAll();
                    requests.append(QString::fromUtf8(req.left(req.indexOf('\r'))));
                    if (req.startsWith("GET /events")) {
                        s->write("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n");
                        return;
                    }
                    const QByteArray body = req.startsWith("GET /containers/json") ? QJsonDocument(m_containers).toJson() : "{}";
                    s->write("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n" + body);
                    s->disconnectFromServer();
                });
            }
        });
    }
    QStringList requests;

private:
    QLocalServer m_server;
    QJsonArray m_containers;
};
} // namespace

class DockerEndpointTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void settingWins();
    void dockerHostWinsOverTheRest();
    void unsupportedSchemes();
    void activeContext();
    void socketCandidatesInOrder();
    void fallsBackToTheSystemSocket();
    void appUsesResolvedSocketAndComposeBinary();
};

void DockerEndpointTest::settingWins()
{
    const auto e = resolveDockerEndpoint(QStringLiteral("unix:///from/setting.sock"),
                                         fakeEnv({QStringLiteral("DOCKER_HOST=unix:///from/env.sock")}, QStringLiteral("/h"), {}));
    QCOMPARE(e.socketPath, QStringLiteral("/from/setting.sock"));
    QCOMPARE(e.source, QStringLiteral("setting"));
    QCOMPARE(resolveDockerEndpoint(QStringLiteral("/bare/path.sock"), fakeEnv({}, QStringLiteral("/h"), {})).socketPath,
             QStringLiteral("/bare/path.sock"));
}

void DockerEndpointTest::dockerHostWinsOverTheRest()
{
    QTemporaryDir home;
    writeJson(home.path() + QStringLiteral("/.docker/config.json"), {{QStringLiteral("currentContext"), QStringLiteral("other")}});
    const auto e
        = resolveDockerEndpoint(QString(),
                                fakeEnv({QStringLiteral("DOCKER_HOST=unix:///custom.sock"), QStringLiteral("XDG_RUNTIME_DIR=/run/user/1")},
                                        home.path(), {QStringLiteral("/run/user/1/docker.sock")}));
    QCOMPARE(e.socketPath, QStringLiteral("/custom.sock"));
    QCOMPARE(e.source, QStringLiteral("DOCKER_HOST"));
    QCOMPARE(e.display(), QStringLiteral("unix:///custom.sock (DOCKER_HOST)"));
}

void DockerEndpointTest::unsupportedSchemes()
{
    for (const QString &host :
         {QStringLiteral("tcp://10.0.0.1:2375"), QStringLiteral("ssh://me@box"), QStringLiteral("npipe:////./pipe/docker_engine")}) {
        const auto e = resolveDockerEndpoint(host, fakeEnv({}, QStringLiteral("/h"), {}));
        QVERIFY(e.socketPath.isEmpty());
        QVERIFY2(e.error.contains(QStringLiteral("not supported")), qPrintable(e.error));
    }
}

void DockerEndpointTest::activeContext()
{
    QTemporaryDir home;
    const QString cfg = home.path() + QStringLiteral("/.docker");
    writeJson(cfg + QStringLiteral("/config.json"), {{QStringLiteral("currentContext"), QStringLiteral("colima-dev")}});
    auto meta = [](const char *name, const char *host) {
        return QJsonObject{{QStringLiteral("Name"), QLatin1String(name)},
                           {QStringLiteral("Endpoints"),
                            QJsonObject{{QStringLiteral("docker"), QJsonObject{{QStringLiteral("Host"), QLatin1String(host)}}}}}};
    };
    writeJson(cfg + QStringLiteral("/contexts/meta/aaa/meta.json"), meta("colima-dev", "unix:///ctx/colima.sock"));
    writeJson(cfg + QStringLiteral("/contexts/meta/bbb/meta.json"), meta("rootless", "unix:///ctx/rootless.sock"));

    auto e = resolveDockerEndpoint(QString(), fakeEnv({}, home.path(), {}));
    QCOMPARE(e.socketPath, QStringLiteral("/ctx/colima.sock"));
    QCOMPARE(e.source, QStringLiteral("context colima-dev"));

    // DOCKER_CONTEXT beats currentContext; "default" means no context.
    e = resolveDockerEndpoint(QString(), fakeEnv({QStringLiteral("DOCKER_CONTEXT=rootless")}, home.path(), {}));
    QCOMPARE(e.socketPath, QStringLiteral("/ctx/rootless.sock"));
    e = resolveDockerEndpoint(QString(), fakeEnv({QStringLiteral("DOCKER_CONTEXT=default")}, home.path(), {}));
    QCOMPARE(e.source, QStringLiteral("default"));

    // DOCKER_CONFIG moves the whole directory.
    e = resolveDockerEndpoint(QString(), fakeEnv({QStringLiteral("DOCKER_CONFIG=") + cfg}, QStringLiteral("/elsewhere"), {}));
    QCOMPARE(e.socketPath, QStringLiteral("/ctx/colima.sock"));
}

void DockerEndpointTest::socketCandidatesInOrder()
{
    const QStringList env{QStringLiteral("XDG_RUNTIME_DIR=/run/user/1")};
    const QString rootless = QStringLiteral("/run/user/1/docker.sock");
    const QString podman = QStringLiteral("/run/user/1/podman/podman.sock");
    const QString colima = QStringLiteral("/h/.colima/default/docker.sock");
    const QString orb = QStringLiteral("/h/.orbstack/run/docker.sock");

    QCOMPARE(resolveDockerEndpoint(QString(), fakeEnv(env, QStringLiteral("/h"), {rootless, podman, colima, orb})).socketPath, rootless);
    QCOMPARE(resolveDockerEndpoint(QString(), fakeEnv(env, QStringLiteral("/h"), {podman, colima, orb})).socketPath, podman);
    QCOMPARE(resolveDockerEndpoint(QString(), fakeEnv(env, QStringLiteral("/h"), {colima, orb})).source, QStringLiteral("Colima"));
    QCOMPARE(resolveDockerEndpoint(QString(), fakeEnv(env, QStringLiteral("/h"), {orb})).source, QStringLiteral("OrbStack"));
}

void DockerEndpointTest::fallsBackToTheSystemSocket()
{
    const auto e = resolveDockerEndpoint(QString(), fakeEnv({}, QStringLiteral("/h"), {}));
    QCOMPARE(e.socketPath, QStringLiteral("/var/run/docker.sock"));
    QCOMPARE(e.source, QStringLiteral("default"));
}

void DockerEndpointTest::appUsesResolvedSocketAndComposeBinary()
{
    QTemporaryDir tmp(QStringLiteral("/tmp/xdd-XXXXXX"));
    QVERIFY(tmp.isValid());
    const QString repo = tmp.path() + QStringLiteral("/repo");
    QVERIFY(QDir().mkpath(repo));
    const QString sock = tmp.path() + QStringLiteral("/podman.sock");
    qputenv("XDG_CONFIG_HOME", (tmp.path() + QStringLiteral("/config")).toLocal8Bit());
    qputenv("XDG_RUNTIME_DIR", tmp.path().toLocal8Bit());
    qputenv("TMUX_TMPDIR", tmp.path().toLocal8Bit());
    qputenv("DOCKER_HOST", "unix:///nonexistent/should-lose.sock");
    writeJson(tmp.path() + QStringLiteral("/config/x3ro-deck/config.json"),
              {{QStringLiteral("autostartAgents"), false},
               {QStringLiteral("dockerHost"), QStringLiteral("unix://") + sock},
               {QStringLiteral("composeBinary"), QStringLiteral("podman compose")},
               {QStringLiteral("checkouts"),
                QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("web")}, {QStringLiteral("path"), repo}}}}});

    const QJsonArray containers{QJsonObject{
        {QStringLiteral("Id"), QStringLiteral("abc123")},
        {QStringLiteral("Names"), QJsonArray{QStringLiteral("/web-db-1")}},
        {QStringLiteral("State"), QStringLiteral("running")},
        {QStringLiteral("Status"), QStringLiteral("Up 5 minutes")},
        {QStringLiteral("Labels"),
         QJsonObject{{QStringLiteral("com.docker.compose.project"), QStringLiteral("web")},
                     {QStringLiteral("com.docker.compose.service"), QStringLiteral("db")},
                     {QStringLiteral("com.docker.compose.project.working_dir"), repo}}},
    }};
    FakeEngine engine(sock, containers);

    AppController app;
    QSignalSpy terminal(&app, &AppController::runInTerminal);
    app.start();
    QTRY_VERIFY_WITH_TIMEOUT(app.dockerConnected(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(app.serviceModel()->rowCount(), 1, 5000);
    QCOMPARE(app.serviceModel()->row(0).value(QStringLiteral("name")).toString(), QStringLiteral("db"));
    QVERIFY(engine.requests.contains(
        QStringLiteral("GET /containers/json?all=1&filters=%7B%22label%22%3A%5B%22com.docker.compose.project%22%5D%7D HTTP/1.0")));

    app.runCompose(QStringLiteral("up"));
    app.runCompose(QStringLiteral("down"));
    QCOMPARE(terminal.count(), 2);
    QCOMPARE(terminal.at(0).at(2).toString(), QStringLiteral("podman compose up -d"));
    QCOMPARE(terminal.at(1).at(2).toString(), QStringLiteral("podman compose down"));
    qunsetenv("DOCKER_HOST");
}

QTEST_MAIN(DockerEndpointTest)
#include "tst_dockerendpoint.moc"
