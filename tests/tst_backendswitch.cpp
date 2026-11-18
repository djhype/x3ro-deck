// SwitchingBackend over a private tmux server (the old backend) and a private
// x3ro-deck-sessiond (the new one): old sessions keep working until they
// exit, new ones start in the daemon.
#include "DaemonBackend.h"
#include "SwitchingBackend.h"
#include "TmuxManager.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class BackendSwitchTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QFile::setPermissions(m_tmp.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("XDG_RUNTIME_DIR", m_tmp.path().toLocal8Bit());
        qputenv("TMUX_TMPDIR", m_tmp.path().toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", (m_tmp.path() + QStringLiteral("/config")).toLocal8Bit());
        qputenv("SHELL", "/bin/sh");
    }

    void oldSessionsFinishWhereTheyAre()
    {
        const QString dir = m_tmp.path();
        auto *tmux = new TmuxManager;
        auto *daemon = new DaemonBackend;
        const QStringList shell{QStringLiteral("/bin/sh")};
        QVERIFY(tmux->createSession(QStringLiteral("old"), dir, QString(), {}, shell));
        SwitchingBackend both(daemon, tmux);

        // The old session is still reachable, and commands reach it in tmux.
        QVERIFY(both.hasSession(QStringLiteral("old")));
        QCOMPARE(both.attachCommand(QStringLiteral("old")), tmux->attachCommand(QStringLiteral("old")));
        const QString marker = dir + QStringLiteral("/old.txt");
        both.submit(QStringLiteral("old"), QStringLiteral("echo still-here > ") + marker);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(marker), 5000);

        // New sessions start in the daemon.
        QVERIFY(both.createSession(QStringLiteral("new"), dir, QString(), {}, shell));
        QVERIFY(daemon->hasSession(QStringLiteral("new")));
        QVERIFY(!tmux->hasSession(QStringLiteral("new")));
        QVERIFY(both.attachChannel(QStringLiteral("old"), 24, 80, this) == nullptr);

        // One poll reports both backends' sessions.
        QSignalSpy panes(&both, &SessionBackend::foregroundProcesses);
        QSignalSpy drained(&both, &SwitchingBackend::drained);
        both.startPolling(200);
        using Panes = QHash<QString, QStringList>;
        auto lastSize = [&panes] {
            return panes.isEmpty() ? -1 : int(panes.last().at(0).value<Panes>().size());
        };
        QTRY_COMPARE_WITH_TIMEOUT(lastSize(), 2, 5000);

        // The old session exits: the switch is over, and the checkout's next
        // session starts in the daemon under the same name.
        both.sendKeys(QStringLiteral("old"), {QStringLiteral("exit"), QStringLiteral("Enter")});
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 5000);
        QVERIFY(!both.hasSession(QStringLiteral("old")));
        QVERIFY(both.createSession(QStringLiteral("old"), dir, QString(), {}, shell));
        QVERIFY(daemon->hasSession(QStringLiteral("old")));
        both.killAll();
    }

private:
    QTemporaryDir m_tmp;
};

QTEST_GUILESS_MAIN(BackendSwitchTest)
#include "tst_backendswitch.moc"
