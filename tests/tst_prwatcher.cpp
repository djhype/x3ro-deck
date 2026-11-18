#include "PrWatcher.h"

#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// PrWatcher against real repositories and a fake gh that logs its calls.
class TestPrWatcher : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_bin;
    QString m_log;
    QByteArray m_path;

    static bool git(const QString &dir, const QStringList &args)
    {
        QProcess p;
        p.setWorkingDirectory(dir);
        p.start(QStringLiteral("git"), args);
        return p.waitForFinished(10000) && p.exitCode() == 0;
    }

    QString repo(const QString &name, const QString &origin, const QString &originHead)
    {
        const QString dir = m_tmp.path() + QLatin1Char('/') + name;
        QDir().mkpath(dir);
        const bool ok = git(dir, {QStringLiteral("init"), QStringLiteral("-q")})
            && (origin.isEmpty() || git(dir, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), origin}))
            && (originHead.isEmpty()
                || git(dir,
                       {QStringLiteral("symbolic-ref"), QStringLiteral("refs/remotes/origin/HEAD"),
                        QStringLiteral("refs/remotes/origin/") + originHead}));
        return ok ? dir : QString();
    }

    QStringList calls() const
    {
        QFile f(m_log);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    // The last update for `key` within `ms`.
    static PrInfo settle(QSignalSpy &spy, int ms = 1500)
    {
        QTest::qWait(ms);
        return spy.isEmpty() ? PrInfo{} : spy.last().at(1).value<PrInfo>();
    }

private Q_SLOTS:
    void initTestCase()
    {
        qRegisterMetaType<PrInfo>();
        QProcess probe;
        probe.start(QStringLiteral("git"), {QStringLiteral("--version")});
        if (!probe.waitForFinished(5000)) {
            QSKIP("git is not available");
        }
        m_bin = m_tmp.path() + QStringLiteral("/bin");
        m_log = m_tmp.path() + QStringLiteral("/gh.log");
        QDir().mkpath(m_bin);
        QFile gh(m_bin + QStringLiteral("/gh"));
        QVERIFY(gh.open(QIODevice::WriteOnly));
        gh.write("#!/bin/sh\n"
                 "echo \"$*\" >> \"$FAKE_GH_LOG\"\n"
                 "case \"$1 $2\" in\n"
                 "  'auth status') exit \"${FAKE_GH_AUTH:-0}\" ;;\n"
                 "  'repo view') echo trunk ;;\n"
                 "  'pr view') echo 'no pull requests found for branch' >&2; exit 1 ;;\n"
                 "esac\n");
        gh.close();
        gh.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        m_path = qgetenv("PATH");
        qputenv("FAKE_GH_LOG", m_log.toUtf8());
    }

    void init()
    {
        QFile::remove(m_log);
        qputenv("PATH", m_bin.toUtf8() + ':' + m_path);
        qunsetenv("FAKE_GH_AUTH");
    }

    void cleanupTestCase() { qputenv("PATH", m_path); }

    void parsers()
    {
        QVERIFY(PrWatcher::isGitHubUrl(QStringLiteral("https://github.com/o/r.git")));
        QVERIFY(PrWatcher::isGitHubUrl(QStringLiteral("git@github.com:o/r.git")));
        QVERIFY(PrWatcher::isGitHubUrl(QStringLiteral("ssh://git@github.example.com/o/r")));
        QVERIFY(!PrWatcher::isGitHubUrl(QStringLiteral("git@gitlab.com:o/r.git")));
        QVERIFY(!PrWatcher::isGitHubUrl(QStringLiteral("https://codeberg.org/github/r")));
        QCOMPARE(PrWatcher::branchFromSymbolicRef("refs/remotes/origin/trunk\n"), QStringLiteral("trunk"));
        QCOMPARE(PrWatcher::branchFromSymbolicRef("refs/remotes/origin/release/2.x\n"), QStringLiteral("release/2.x"));
        QVERIFY(PrWatcher::branchFromSymbolicRef("").isEmpty());
    }

    void skipsTheDefaultBranch()
    {
        const QString dir = repo(QStringLiteral("trunk"), QStringLiteral("git@github.com:o/r.git"), QStringLiteral("trunk"));
        QVERIFY(!dir.isEmpty());
        PrWatcher w;
        QSignalSpy spy(&w, &PrWatcher::updated);
        w.watch(QStringLiteral("a"), dir);
        w.setBranch(QStringLiteral("a"), QStringLiteral("trunk"));
        QVERIFY(settle(spy).problem.isEmpty());
        QVERIFY2(!calls().join(QLatin1Char('|')).contains(QLatin1String("pr view")), qPrintable(calls().join(QLatin1Char('|'))));
        // `main` is not this repository's default branch, so it gets a lookup.
        w.setBranch(QStringLiteral("a"), QStringLiteral("main"));
        settle(spy);
        QVERIFY(calls().contains(QLatin1String("pr view main --json ")
                                 + QStringLiteral("number,url,title,state,isDraft,baseRefName,reviewDecision,mergeable,mergeStateStatus,"
                                                  "additions,deletions,statusCheckRollup,latestReviews")));
    }

    void asksGitHubWithoutOriginHead()
    {
        const QString dir = repo(QStringLiteral("nohead"), QStringLiteral("https://github.com/o/r"), QString());
        QVERIFY(!dir.isEmpty());
        PrWatcher w;
        QSignalSpy spy(&w, &PrWatcher::updated);
        w.watch(QStringLiteral("a"), dir);
        w.setBranch(QStringLiteral("a"), QStringLiteral("trunk"));
        settle(spy);
        const QString log = calls().join(QLatin1Char('|'));
        QVERIFY2(log.contains(QLatin1String("repo view")), qPrintable(log));
        QVERIFY2(!log.contains(QLatin1String("pr view")), qPrintable(log));
    }

    void loggedOut()
    {
        qputenv("FAKE_GH_AUTH", "1");
        const QString dir = repo(QStringLiteral("out"), QStringLiteral("git@github.com:o/r.git"), QStringLiteral("main"));
        PrWatcher w;
        QSignalSpy spy(&w, &PrWatcher::updated);
        w.watch(QStringLiteral("a"), dir);
        w.setBranch(QStringLiteral("a"), QStringLiteral("feature"));
        QCOMPARE(settle(spy).problem, QStringLiteral("gh-logged-out"));
        QCOMPARE(calls(), QStringList{QStringLiteral("auth status")});
        // Logging in and pressing Refresh picks it up.
        qunsetenv("FAKE_GH_AUTH");
        w.recheck();
        QVERIFY(settle(spy).problem.isEmpty());
        QVERIFY(calls().join(QLatin1Char('|')).contains(QLatin1String("pr view feature")));
    }

    void ghMissing()
    {
        const QString dir = repo(QStringLiteral("missing"), QStringLiteral("git@github.com:o/r.git"), QStringLiteral("main"));
        QVERIFY(!dir.isEmpty());
        qputenv("PATH", "/nonexistent");
        PrWatcher w;
        QSignalSpy spy(&w, &PrWatcher::updated);
        w.watch(QStringLiteral("a"), dir);
        w.setBranch(QStringLiteral("a"), QStringLiteral("feature"));
        QCOMPARE(settle(spy).problem, QStringLiteral("gh-missing"));
        const qsizetype updates = spy.size();
        w.refresh(QStringLiteral("a")); // a hook-triggered refresh doesn't poll
        QCOMPARE(settle(spy, 500).problem, QStringLiteral("gh-missing"));
        QCOMPARE(spy.size(), updates + 1);
        QVERIFY(calls().isEmpty());
    }

    void noGitHubRemote()
    {
        const QString dir = repo(QStringLiteral("lab"), QStringLiteral("git@gitlab.com:o/r.git"), QString());
        PrWatcher w;
        QSignalSpy spy(&w, &PrWatcher::updated);
        w.watch(QStringLiteral("a"), dir);
        w.setBranch(QStringLiteral("a"), QStringLiteral("feature"));
        QCOMPARE(settle(spy).problem, QStringLiteral("no-github-remote"));
        QCOMPARE(calls(), QStringList{QStringLiteral("auth status")});
    }
};

QTEST_GUILESS_MAIN(TestPrWatcher)
#include "tst_prwatcher.moc"
