#include "Config.h"
#include "Dependencies.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestDependencies : public QObject
{
    Q_OBJECT

private:
    static void tool(const QString &dir, const QString &name)
    {
        QFile f(dir + QLatin1Char('/') + name);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n");
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }

private Q_SLOTS:
    void commandBinary()
    {
        QCOMPARE(Dependencies::commandBinary(QStringLiteral("codex --yolo")), QStringLiteral("codex"));
        QCOMPARE(Dependencies::commandBinary(QStringLiteral("  FOO=1 BAR=x claude")), QStringLiteral("claude"));
        QCOMPARE(Dependencies::commandBinary(QStringLiteral("docker compose")), QStringLiteral("docker"));
        QCOMPARE(Dependencies::commandBinary(QString()), QString());
    }

    void resolveSearchesBothPaths()
    {
        QTemporaryDir app, login;
        tool(app.path(), QStringLiteral("gh"));
        tool(login.path(), QStringLiteral("codex"));
        QCOMPARE(Dependencies::resolve(QStringLiteral("gh"), app.path(), login.path()), app.path() + QStringLiteral("/gh"));
        QCOMPARE(Dependencies::resolve(QStringLiteral("codex"), app.path(), login.path()), login.path() + QStringLiteral("/codex"));
        QVERIFY(Dependencies::resolve(QStringLiteral("claude"), app.path(), login.path()).isEmpty());
        QCOMPARE(Dependencies::resolve(login.path() + QStringLiteral("/codex"), QString(), QString()),
                 login.path() + QStringLiteral("/codex"));
    }

    void parseProbe()
    {
        QCOMPARE(Dependencies::parseProbe("Welcome!\n__X3RO_DECK_PATH__/a:/b__X3RO_DECK_PATH__"), QStringLiteral("/a:/b"));
        QVERIFY(Dependencies::parseProbe("no markers").isEmpty());
        QVERIFY(Dependencies::parseProbe("__X3RO_DECK_PATH__/a").isEmpty());
    }

    // The daemon backend needs x3ro-deck-sessiond: found through its
    // override, else reported missing (as on Windows for now).
    void sessionDaemon()
    {
        Config config;
        config.sessionBackend = QStringLiteral("daemon");
        qunsetenv("X3RO_DECK_SESSIOND_BIN");
        Dependencies deps;
        deps.setAppPath(QString());
        deps.setConfig(config);
        deps.setLoginPath(QString());
        QVERIFY(deps.missing(QStringLiteral("sessiond")));
        QTemporaryDir dir;
        const QString bin = dir.filePath(QStringLiteral("x3ro-deck-sessiond"));
        QFile f(bin);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("X3RO_DECK_SESSIOND_BIN", bin.toLocal8Bit());
        deps.setConfig(config);
        QVERIFY(!deps.missing(QStringLiteral("sessiond")));
        qunsetenv("X3RO_DECK_SESSIOND_BIN");
    }

    void listFollowsConfig()
    {
        QTemporaryDir app;
        tool(app.path(), QStringLiteral("gh"));
        tool(app.path(), QStringLiteral("claude"));
        Config config;
        config.composeBinary = QStringLiteral("podman compose");
        config.sessionBackend = QStringLiteral("daemon");
        AgentProfile claude;
        claude.id = claude.label = QStringLiteral("Claude");
        claude.kind = QStringLiteral("claude");
        claude.command = QStringLiteral("claude");
        AgentProfile codex;
        codex.id = codex.label = QStringLiteral("Codex");
        codex.kind = QStringLiteral("codex");
        codex.command = QStringLiteral("codex --yolo");
        config.agents = {claude, codex};

        Dependencies deps;
        deps.setAppPath(app.path());
        deps.setConfig(config);
        QStringList ids;
        for (const Dependency &d : deps.list()) {
            ids.append(d.id);
        }
        QCOMPARE(ids,
                 (QStringList{QStringLiteral("git"), QStringLiteral("gh"), QStringLiteral("compose"), QStringLiteral("sessiond"),
                              QStringLiteral("claude"), QStringLiteral("codex")}));
        QCOMPARE(deps.list().at(2).binary, QStringLiteral("podman"));
        // Nothing counts as missing until the login shell's PATH is known.
        QVERIFY(!deps.missing(QStringLiteral("codex")));
        deps.setLoginPath(QString());
        QVERIFY(deps.missing(QStringLiteral("codex")));
        QCOMPARE(deps.missing(QStringLiteral("codex"))->name, QStringLiteral("Codex (codex)"));
        QVERIFY(deps.missing(QStringLiteral("codex"))->hint.contains(QLatin1String("@openai/codex")));
        QVERIFY(!deps.missing(QStringLiteral("gh")));
        QVERIFY(!deps.missing(QStringLiteral("claude")));
    }

    void probeReadsTheLoginShellsPath()
    {
        QTemporaryDir dir, login;
        tool(login.path(), QStringLiteral("codex"));
        // A shell whose rc files print a banner before the probe's output.
        const QString shell = dir.path() + QStringLiteral("/fakesh");
        QFile f(shell);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("#!/bin/sh\necho 'motd banner'\nPATH='%1'\nshift 3\nexec /bin/sh -c \"$1\"\n").arg(login.path()).toUtf8());
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        Config config;
        AgentProfile codex;
        codex.id = codex.label = QStringLiteral("Codex");
        codex.command = QStringLiteral("codex");
        config.agents = {codex};
        Dependencies deps;
        deps.setAppPath(dir.path());
        deps.setConfig(config);
        QSignalSpy changed(&deps, &Dependencies::changed);
        deps.probeLoginPath({shell});
        QVERIFY(changed.wait(5000));
        QVERIFY(deps.ready());
        QVERIFY(!deps.missing(QStringLiteral("codex")));
        QVERIFY(deps.missing(QStringLiteral("gh")));
    }
};

QTEST_GUILESS_MAIN(TestDependencies)
#include "tst_dependencies.moc"
