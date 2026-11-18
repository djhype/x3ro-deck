#include "FreedesktopNotifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

// Stands in for the desktop's notification server.
class FakeServer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    struct Call
    {
        QString appName;
        QString icon;
        QString summary;
        QString body;
        QStringList actions;
        QVariantMap hints;
    };
    QList<Call> calls;
    QStringList capabilities{QStringLiteral("actions"), QStringLiteral("body-markup")};
    uint nextId = 41;

public Q_SLOTS:
    QStringList GetCapabilities() { return capabilities; }
    uint Notify(const QString &appName, uint, const QString &icon, const QString &summary, const QString &body, const QStringList &actions,
                const QVariantMap &hints, int)
    {
        calls.append({appName, icon, summary, body, actions, hints});
        return ++nextId;
    }
};

class NotifierTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void permissionCarriesActionsAndHints();
    void signalsMapBackToTheTag();
    void plainBodyWithoutMarkup();
    void missingServerIsQuiet();

private:
    void emitSignal(const QString &name, const QVariantList &args);

    QDBusConnection m_serverBus{QString()};
    FakeServer m_server;
};

void NotifierTest::initTestCase()
{
    if (!QDBusConnection::sessionBus().isConnected()) {
        QSKIP("needs a session bus (run under dbus-run-session)");
    }
    m_serverBus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-server"));
    QVERIFY(m_serverBus.isConnected());
    QVERIFY(m_serverBus.registerService(QStringLiteral("org.freedesktop.Notifications")));
    QVERIFY(m_serverBus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), &m_server, QDBusConnection::ExportAllSlots));
}

void NotifierTest::emitSignal(const QString &name, const QVariantList &args)
{
    QDBusMessage sig = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/Notifications"),
                                                  QStringLiteral("org.freedesktop.Notifications"), name);
    sig.setArguments(args);
    QVERIFY(m_serverBus.send(sig));
}

void NotifierTest::permissionCarriesActionsAndHints()
{
    m_server.calls.clear();
    FreedesktopNotifier notifier;
    Notifier::Notification n;
    n.title = QStringLiteral("deck · Claude · Bash");
    n.body = QStringLiteral("rm -rf <build>");
    n.urgency = Notifier::Urgency::Critical;
    n.actions = {{QStringLiteral("allow"), QStringLiteral("Allow")}, {QStringLiteral("deny"), QStringLiteral("Deny")}};
    notifier.show(n, QStringLiteral("perm:1"));
    QTRY_COMPARE(m_server.calls.size(), 1);
    const FakeServer::Call &c = m_server.calls.first();
    QCOMPARE(c.appName, QStringLiteral("X3RO Deck"));
    QCOMPARE(c.icon, QStringLiteral("x3ro-deck"));
    QCOMPARE(c.summary, n.title);
    QCOMPARE(c.body, QStringLiteral("rm -rf &lt;build&gt;"));
    QCOMPARE(c.actions,
             (QStringList{QStringLiteral("default"), QStringLiteral("Open"), QStringLiteral("allow"), QStringLiteral("Allow"),
                          QStringLiteral("deny"), QStringLiteral("Deny")}));
    QCOMPARE(c.hints.value(QStringLiteral("urgency")).toInt(), 2);
    QCOMPARE(c.hints.value(QStringLiteral("desktop-entry")).toString(), QStringLiteral("x3ro-deck"));
    QCOMPARE(c.hints.value(QStringLiteral("x-kde-appname")).toString(), QStringLiteral("x3ro-deck"));

    // A docker item: Open only, normal urgency.
    Notifier::Notification d;
    d.title = QStringLiteral("deck · web unhealthy");
    notifier.show(d, QStringLiteral("docker:web"));
    QTRY_COMPARE(m_server.calls.size(), 2);
    QCOMPARE(m_server.calls.at(1).actions, (QStringList{QStringLiteral("default"), QStringLiteral("Open")}));
    QCOMPARE(m_server.calls.at(1).hints.value(QStringLiteral("urgency")).toInt(), 1);
}

void NotifierTest::signalsMapBackToTheTag()
{
    FreedesktopNotifier notifier;
    QSignalSpy actions(&notifier, &Notifier::actionInvoked);
    QSignalSpy tokens(&notifier, &Notifier::activationToken);
    QSignalSpy closed(&notifier, &Notifier::closed);
    const qsizetype before = m_server.calls.size();
    notifier.show({}, QStringLiteral("perm:7"));
    QTRY_COMPARE(m_server.calls.size(), before + 1);
    const uint id = m_server.nextId;
    // The reply has to land before the signals can map.
    QTest::qWait(50);

    emitSignal(QStringLiteral("ActivationToken"), {id, QStringLiteral("tok")});
    emitSignal(QStringLiteral("ActionInvoked"), {id, QStringLiteral("allow")});
    emitSignal(QStringLiteral("ActionInvoked"), {id + 100, QStringLiteral("allow")}); // someone else's
    emitSignal(QStringLiteral("NotificationClosed"), {id, 2u});
    QTRY_COMPARE(closed.size(), 1);
    QCOMPARE(tokens.size(), 1);
    QCOMPARE(tokens.at(0), (QVariantList{QStringLiteral("perm:7"), QStringLiteral("tok")}));
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions.at(0), (QVariantList{QStringLiteral("perm:7"), QStringLiteral("allow")}));
    QCOMPARE(closed.at(0).at(0).toString(), QStringLiteral("perm:7"));

    // Closed notifications are forgotten.
    emitSignal(QStringLiteral("ActionInvoked"), {id, QStringLiteral("deny")});
    QTest::qWait(50);
    QCOMPARE(actions.size(), 1);
}

void NotifierTest::plainBodyWithoutMarkup()
{
    m_server.capabilities = {QStringLiteral("actions")};
    FreedesktopNotifier notifier;
    QTest::qWait(100); // the capabilities reply
    Notifier::Notification n;
    n.body = QStringLiteral("a < b");
    notifier.show(n, QStringLiteral("x"));
    QTRY_VERIFY(!m_server.calls.isEmpty() && m_server.calls.last().body == QStringLiteral("a < b"));
    m_server.capabilities = {QStringLiteral("actions"), QStringLiteral("body-markup")};
}

namespace
{
int g_warnings = 0;
QtMessageHandler g_previous = nullptr;
void countWarnings(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (type == QtWarningMsg && msg.contains(QStringLiteral("cannot show notifications"))) {
        ++g_warnings;
        return;
    }
    g_previous(type, ctx, msg);
}
} // namespace

void NotifierTest::missingServerIsQuiet()
{
    m_serverBus.unregisterService(QStringLiteral("org.freedesktop.Notifications"));
    g_previous = qInstallMessageHandler(countWarnings);
    {
        FreedesktopNotifier notifier;
        notifier.show({}, QStringLiteral("a"));
        notifier.show({}, QStringLiteral("b"));
        // The bus may try to activate a server first, so the errors can take a while.
        QTRY_VERIFY_WITH_TIMEOUT(g_warnings > 0, 30000);
        QTest::qWait(200);
    }
    qInstallMessageHandler(g_previous);
    QCOMPARE(g_warnings, 1);
    QVERIFY(m_serverBus.registerService(QStringLiteral("org.freedesktop.Notifications")));
}

QTEST_GUILESS_MAIN(NotifierTest)
#include "tst_notifier.moc"
