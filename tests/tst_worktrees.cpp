#include "AppController.h"
#include "Config.h"
#include "Worktrees.h"

#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace Worktrees;

class TestWorktrees : public QObject
{
    Q_OBJECT

private:
    static bool git(const QString &dir, const QStringList &args)
    {
        QProcess p;
        p.setWorkingDirectory(dir);
        p.start(QStringLiteral("git"), args);
        return p.waitForFinished(10000) && p.exitCode() == 0;
    }

    // A repository with a second worktree; empty when git is missing.
    static QStringList makeRepo(const QTemporaryDir &tmp)
    {
        const QString main = tmp.path() + QStringLiteral("/app");
        const QString linked = tmp.path() + QStringLiteral("/app-feature");
        QDir().mkpath(main);
        if (!git(main, {QStringLiteral("init"), QStringLiteral("-q")})
            || !git(main,
                    {QStringLiteral("-c"), QStringLiteral("user.name=t"), QStringLiteral("-c"), QStringLiteral("user.email=t@t"),
                     QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("--allow-empty"), QStringLiteral("-m"),
                     QStringLiteral("x")})
            || !git(main,
                    {QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("-q"), linked, QStringLiteral("-b"),
                     QStringLiteral("feature")})) {
            return {};
        }
        return {main, linked};
    }

private Q_SLOTS:
    void parsePorcelain()
    {
        const QByteArray out
            = "worktree /src/app\nHEAD 1111\nbranch refs/heads/main\n\n"
              "worktree /src/app-fix\nHEAD 2222\nbranch refs/heads/fix/login\n\n"
              "worktree /src/app-detached\nHEAD 3333\ndetached\n\n"
              "worktree /src/gone\nHEAD 4444\nbranch refs/heads/old\nprunable gitdir file points to non-existent location\n\n"
              "worktree /src/bare.git\nbare\n";
        const QList<Worktree> w = parseWorktreeList(out);
        QCOMPARE(w.size(), 3);
        QCOMPARE(w.at(0).path, QStringLiteral("/src/app"));
        QCOMPARE(w.at(0).branch, QStringLiteral("main"));
        QCOMPARE(w.at(1).branch, QStringLiteral("fix/login"));
        QVERIFY(w.at(2).branch.isEmpty());
    }

    void uniqueNames()
    {
        const QList<Candidate> c = candidates({{QStringLiteral("/a/app"), QStringLiteral("main")},
                                               {QStringLiteral("/b/app"), QStringLiteral("fix/x")},
                                               {QStringLiteral("/c/app"), QString()}});
        QCOMPARE(c.at(0).name, QStringLiteral("app"));
        QCOMPARE(c.at(1).name, QStringLiteral("app-fix-x"));
        QCOMPARE(c.at(2).name, QStringLiteral("app-2"));
    }

    void withCheckoutsKeepsOtherKeys()
    {
        QList<Candidate> c = candidates({{QStringLiteral("/a/app"), QString()}, {QStringLiteral("/a/other"), QString()}});
        c[1].checked = false;
        const QJsonObject root = withCheckouts(QJsonObject{{QStringLiteral("autostartAgents"), false}}, c);
        QCOMPARE(root.value(QStringLiteral("autostartAgents")).toBool(true), false);
        const QJsonArray list = root.value(QStringLiteral("checkouts")).toArray();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("app"));
    }

    void discoverRealRepo()
    {
        QTemporaryDir tmp;
        const QStringList repo = makeRepo(tmp);
        if (repo.isEmpty()) {
            QSKIP("git is not available");
        }
        const QList<Worktree> w = discover(repo.at(1)); // from the linked worktree
        QCOMPARE(w.size(), 2);
        QCOMPARE(QFileInfo(w.at(0).path).canonicalFilePath(), QFileInfo(repo.at(0)).canonicalFilePath());
        QCOMPARE(w.at(1).branch, QStringLiteral("feature"));
        const QList<Worktree> plain = discover(tmp.path());
        QCOMPARE(plain.size(), 1);
        QCOMPARE(plain.at(0).path, QDir::cleanPath(tmp.path()));
    }

    void welcomeFlow()
    {
        QTemporaryDir tmp, config;
        const QStringList repo = makeRepo(tmp);
        if (repo.isEmpty()) {
            QSKIP("git is not available");
        }
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        qputenv("TMUX_TMPDIR", tmp.path().toUtf8());
        {
            AppController app;
            QVERIFY(app.firstRun());
            const QByteArray before = [] {
                QFile f(Config::configPath());
                return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
            }();
            app.setWelcomeFolder(repo.at(0));
            QCOMPARE(app.welcomeCandidates().size(), 2);
            app.cancelWelcome();
            QVERIFY(app.welcomeCandidates().isEmpty());
            QFile f(Config::configPath());
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), before);

            app.setWelcomeFolder(repo.at(0));
            QSignalSpy finished(&app, &AppController::welcomeFinished);
            app.finishWelcome();
            QCOMPARE(finished.size(), 1);
        }
        AppController reloaded;
        QVERIFY(!reloaded.firstRun());
        QCOMPARE(reloaded.checkoutCount(), 2);
        QCOMPARE(reloaded.checkout(0).cfg.name, QStringLiteral("app"));
        QCOMPARE(reloaded.checkout(1).cfg.name, QStringLiteral("app-feature"));
    }
};

QTEST_GUILESS_MAIN(TestWorktrees)
#include "tst_worktrees.moc"
