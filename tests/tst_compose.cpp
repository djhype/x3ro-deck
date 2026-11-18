// Per-checkout compose options: file detection, the commands built from them,
// and the containers a checkout with a project name tracks.
#include "AppController.h"
#include "ComposeFiles.h"
#include "Config.h"
#include "DockerClient.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class ComposeTest : public QObject
{
    Q_OBJECT

private:
    static void touch(const QString &path)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }

private Q_SLOTS:
    void detectsBaseFilesThenOverrides()
    {
        QTemporaryDir dir;
        for (const char *name : {"docker-compose.yml", "compose.yml", "docker-compose.dev.yml", "compose.override.yaml", "compose.yml.bak",
                                 "notcompose.yml", "docker-compose.ci.test.yaml"}) {
            touch(dir.filePath(QLatin1String(name)));
        }
        QCOMPARE(ComposeFiles::detect(dir.path()),
                 (QStringList{QStringLiteral("compose.yml"), QStringLiteral("docker-compose.yml"), QStringLiteral("compose.override.yaml"),
                              QStringLiteral("docker-compose.ci.test.yaml"), QStringLiteral("docker-compose.dev.yml")}));
        QTemporaryDir empty;
        QVERIFY(ComposeFiles::detect(empty.path()).isEmpty());
    }

    void buildsCommandsFromTheOptions()
    {
        CheckoutConfig cc;
        QCOMPARE(cc.composeCommand(QStringLiteral("up")), QStringLiteral("{compose} up -d"));
        cc.composeFiles = {QStringLiteral("compose.yml"), QStringLiteral("docker-compose.dev.yml")};
        cc.composeProfiles = {QStringLiteral("worker")};
        cc.composeEnvFile = QStringLiteral("my env/.env");
        cc.composeProject = QStringLiteral("p135");
        QCOMPARE(
            cc.composeCommand(QStringLiteral("up")),
            QStringLiteral("{compose} -f compose.yml -f docker-compose.dev.yml --profile worker --env-file 'my env/.env' -p p135 up -d"));
        QCOMPARE(cc.composeCommand(QStringLiteral("down")),
                 cc.composeCommand(QStringLiteral("up")).replace(QStringLiteral("up -d"), QStringLiteral("down")));
        cc.composeUp = QStringLiteral("make up");
        QCOMPARE(cc.composeCommand(QStringLiteral("up")), QStringLiteral("make up"));
        QCOMPARE(ComposeFiles::shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
    }

    void projectNamePicksTheContainers()
    {
        QTemporaryDir tmp;
        qputenv("XDG_CONFIG_HOME", tmp.path().toLocal8Bit());
        qputenv("XDG_RUNTIME_DIR", tmp.path().toLocal8Bit());
        qputenv("TMUX_TMPDIR", tmp.path().toLocal8Bit());
        QVERIFY(QDir().mkpath(tmp.filePath(QStringLiteral("x3ro-deck"))));
        const QString a = tmp.filePath(QStringLiteral("a"));
        const QString b = tmp.filePath(QStringLiteral("b"));
        QVERIFY(QDir().mkpath(a) && QDir().mkpath(b));
        touch(a + QStringLiteral("/compose.yml"));
        auto write = [&](const QString &project) {
            QJsonObject compose;
            if (!project.isEmpty()) {
                compose.insert(QStringLiteral("project"), project);
            }
            const QJsonObject config{{QStringLiteral("autostartAgents"), false},
                                     {QStringLiteral("checkouts"),
                                      QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("a")},
                                                             {QStringLiteral("path"), a},
                                                             {QStringLiteral("compose"), compose}},
                                                 QJsonObject{{QStringLiteral("name"), QStringLiteral("b")}, {QStringLiteral("path"), b}}}}};
            QVERIFY(Config::writeJson(config));
        };
        write(QString());
        AppController app;
        // One container from the folder's default project, one from "other"
        // running elsewhere.
        ContainerInfo here;
        here.id = QStringLiteral("1");
        here.project = QStringLiteral("a");
        here.service = QStringLiteral("web");
        here.workingDir = a;
        here.state = QStringLiteral("running");
        ContainerInfo other = here;
        other.id = QStringLiteral("2");
        other.project = QStringLiteral("other");
        other.service = QStringLiteral("worker");
        other.workingDir = tmp.filePath(QStringLiteral("elsewhere"));
        Q_EMIT app.docker()->containersUpdated({here, other});
        QCOMPARE(app.checkout(0).containers.size(), 1);
        QCOMPARE(app.checkout(0).containers.first().id, QStringLiteral("1"));
        QVERIFY(app.current().value(QStringLiteral("composeFound")).toBool());

        // Naming the project in Settings switches the checkout to it.
        write(QStringLiteral("other"));
        app.reloadConfig();
        QCOMPARE(app.checkout(0).containers.size(), 1);
        QCOMPARE(app.checkout(0).containers.first().id, QStringLiteral("2"));
        QCOMPARE(app.checkout(0).project, QStringLiteral("other"));

        app.select(1);
        QVERIFY(!app.current().value(QStringLiteral("composeFound")).toBool());
    }

    // Two worktrees of one repo and stacks started from a terminal: the
    // labels compose writes decide, not the project name.
    void labelsPickTheCheckout()
    {
        QTemporaryDir tmp;
        qputenv("XDG_CONFIG_HOME", tmp.path().toLocal8Bit());
        qputenv("XDG_RUNTIME_DIR", tmp.path().toLocal8Bit());
        qputenv("TMUX_TMPDIR", tmp.path().toLocal8Bit());
        QVERIFY(QDir().mkpath(tmp.filePath(QStringLiteral("x3ro-deck"))));
        const QString main = tmp.filePath(QStringLiteral("main/App"));
        const QString wt = tmp.filePath(QStringLiteral("wt/App"));
        const QString solo = tmp.filePath(QStringLiteral("Solo.Dir"));
        for (const QString &d : {main, wt, solo, main + QStringLiteral("/sub")}) {
            QVERIFY(QDir().mkpath(d));
        }
        QJsonArray checkouts;
        for (const auto &[name, path] : {std::pair{"main", main}, std::pair{"wt", wt}, std::pair{"solo", solo}}) {
            checkouts.append(QJsonObject{{QStringLiteral("name"), QLatin1String(name)}, {QStringLiteral("path"), path}});
        }
        QVERIFY(Config::writeJson(QJsonObject{{QStringLiteral("checkouts"), checkouts}}));
        AppController app;
        auto container = [](const QString &project, const QString &dir, const QStringList &files = {}) {
            ContainerInfo ci;
            ci.project = project;
            ci.workingDir = dir;
            ci.configFiles = files;
            return ci;
        };
        // Both worktrees run the default project "app"; working_dir tells them apart.
        QCOMPARE(app.checkoutForContainer(container(QStringLiteral("app"), main)), 0);
        QCOMPARE(app.checkoutForContainer(container(QStringLiteral("app"), wt)), 1);
        QCOMPARE(app.checkoutForContainer(container(QStringLiteral("app"), main + QStringLiteral("/"))), 0);
        // A project rooted in a subdirectory is not the checkout's.
        QCOMPARE(app.checkoutForContainer(container(QStringLiteral("sub"), main + QStringLiteral("/sub"))), -1);
        // --project-directory elsewhere: the compose file says where it came from.
        QCOMPARE(app.checkoutForContainer(container(QStringLiteral("x"), tmp.path(), {wt + QStringLiteral("/compose.yml")})), 1);
        QCOMPARE(app.checkoutForContainer(container(QStringLiteral("x"), tmp.path())), -1);
        // No labels at all: the project name, when only one checkout gives it.
        QCOMPARE(AppController::defaultProjectName(solo), QStringLiteral("solodir"));
        QCOMPARE(app.checkoutForContainer(container(QStringLiteral("solodir"), QString())), 2);
        QCOMPARE(app.checkoutForContainer(container(QStringLiteral("app"), QString())), -1);
    }
};

QTEST_GUILESS_MAIN(ComposeTest)
#include "tst_compose.moc"
