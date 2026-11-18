#include "PortalGlobalHotkey.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace
{
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kSession = QStringLiteral("/org/freedesktop/portal/desktop/session/1_1/s");
} // namespace

// Stands in for xdg-desktop-portal's GlobalShortcuts.
class FakePortal : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.GlobalShortcuts")
    Q_PROPERTY(uint version READ version)
public:
    explicit FakePortal(QDBusConnection bus)
        : m_bus(bus)
    {
    }
    uint version() const { return 2; }

    QStringList bound; // remembered across sessions, like the real portal
    QList<PortalShortcut> lastBind;
    int bindCalls = 0;

public Q_SLOTS:
    QDBusObjectPath CreateSession(const QVariantMap &options) { return respond(options, {{QStringLiteral("session_handle"), kSession}}); }
    QDBusObjectPath ListShortcuts(const QDBusObjectPath &, const QVariantMap &options)
    {
        QList<PortalShortcut> list;
        for (const QString &id : std::as_const(bound)) {
            list.append({id, {}});
        }
        return respond(options, {{QStringLiteral("shortcuts"), QVariant::fromValue(list)}});
    }
    QDBusObjectPath BindShortcuts(const QDBusObjectPath &, const QList<PortalShortcut> &shortcuts, const QString &,
                                  const QVariantMap &options)
    {
        ++bindCalls;
        lastBind = shortcuts;
        for (const PortalShortcut &s : shortcuts) {
            bound << s.id;
        }
        return respond(options, {});
    }

private:
    QDBusObjectPath respond(const QVariantMap &options, const QVariantMap &results)
    {
        QString sender = message().service().mid(1);
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
                                 .arg(sender, options.value(QStringLiteral("handle_token")).toString());
        QTimer::singleShot(0, this, [this, path, results] {
            QDBusMessage sig
                = QDBusMessage::createSignal(path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"));
            sig << uint(0) << results;
            m_bus.send(sig);
        });
        return QDBusObjectPath(path);
    }

    QDBusConnection m_bus;
};

class GlobalHotkeyTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void triggerFormat();
    void firstRunBindsThenActivates();
    void boundShortcutIsNotBoundAgain();
    void missingPortalWarnsOnce();

private:
    void activate(const QString &session, const QString &id, const QString &token);

    QDBusConnection m_portalBus{QString()};
    std::unique_ptr<FakePortal> m_portal;
};

void GlobalHotkeyTest::initTestCase()
{
    if (!QDBusConnection::sessionBus().isConnected()) {
        QSKIP("needs a session bus (run under dbus-run-session)");
    }
    PortalGlobalHotkey::registerTypes();
    m_portalBus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-portal"));
    m_portal = std::make_unique<FakePortal>(m_portalBus);
    QVERIFY(m_portalBus.registerService(kService));
    QVERIFY(m_portalBus.registerObject(kPath, m_portal.get(), QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
}

void GlobalHotkeyTest::activate(const QString &session, const QString &id, const QString &token)
{
    QDBusMessage sig
        = QDBusMessage::createSignal(kPath, QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), QStringLiteral("Activated"));
    sig << QVariant::fromValue(QDBusObjectPath(session)) << id << qulonglong(1234)
        << QVariantMap{{QStringLiteral("activation_token"), token}};
    QVERIFY(m_portalBus.send(sig));
}

void GlobalHotkeyTest::triggerFormat()
{
    QCOMPARE(PortalGlobalHotkey::portalTrigger(QKeySequence(Qt::META | Qt::ALT | Qt::Key_H)), QStringLiteral("ALT+LOGO+h"));
    QCOMPARE(PortalGlobalHotkey::portalTrigger(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F5)), QStringLiteral("CTRL+SHIFT+F5"));
    QCOMPARE(PortalGlobalHotkey::portalTrigger(QKeySequence()), QString());
}

void GlobalHotkeyTest::firstRunBindsThenActivates()
{
    m_portal->bound.clear();
    m_portal->bindCalls = 0;
    PortalGlobalHotkey hotkeys;
    hotkeys.add(QStringLiteral("toggle-window"), QStringLiteral("Show/hide X3RO Deck"), QKeySequence(Qt::META | Qt::ALT | Qt::Key_H));
    QSignalSpy spy(&hotkeys, &GlobalHotkey::activated);
    // The registry is missing on this fake portal; that must not matter.
    hotkeys.start();
    QTRY_COMPARE(m_portal->bindCalls, 1);
    QCOMPARE(m_portal->lastBind.size(), 1);
    QCOMPARE(m_portal->lastBind.at(0).id, QStringLiteral("toggle-window"));
    QCOMPARE(m_portal->lastBind.at(0).properties.value(QStringLiteral("description")).toString(), QStringLiteral("Show/hide X3RO Deck"));
    QCOMPARE(m_portal->lastBind.at(0).properties.value(QStringLiteral("preferred_trigger")).toString(), QStringLiteral("ALT+LOGO+h"));

    activate(QStringLiteral("/some/other/session"), QStringLiteral("toggle-window"), QString());
    activate(kSession, QStringLiteral("toggle-window"), QStringLiteral("tok-9"));
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0), (QVariantList{QStringLiteral("toggle-window"), QStringLiteral("tok-9")}));
}

void GlobalHotkeyTest::boundShortcutIsNotBoundAgain()
{
    m_portal->bound = {QStringLiteral("toggle-window")};
    m_portal->bindCalls = 0;
    PortalGlobalHotkey hotkeys;
    hotkeys.add(QStringLiteral("toggle-window"), QStringLiteral("Show/hide X3RO Deck"), QKeySequence(Qt::META | Qt::ALT | Qt::Key_H));
    QSignalSpy spy(&hotkeys, &GlobalHotkey::activated);
    hotkeys.start();
    // Once the session exists, activations arrive; no bind was needed.
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
            activate(kSession, QStringLiteral("toggle-window"), QString());
            QTest::qWait(20);
            return !spy.isEmpty();
        }(),
        5000);
    QCOMPARE(m_portal->bindCalls, 0);
}

namespace
{
int g_warnings = 0;
QtMessageHandler g_previous = nullptr;
void countWarnings(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (type == QtWarningMsg && msg.contains(QStringLiteral("global shortcuts unavailable"))) {
        ++g_warnings;
        return;
    }
    g_previous(type, ctx, msg);
}
} // namespace

void GlobalHotkeyTest::missingPortalWarnsOnce()
{
    // The bus name stays (so nothing gets activated), but nobody serves it.
    m_portalBus.unregisterObject(kPath);
    g_previous = qInstallMessageHandler(countWarnings);
    {
        PortalGlobalHotkey hotkeys;
        hotkeys.add(QStringLiteral("toggle-window"), QString(), QKeySequence(Qt::META | Qt::ALT | Qt::Key_H));
        hotkeys.start();
        QTRY_VERIFY_WITH_TIMEOUT(g_warnings > 0, 10000);
        QTest::qWait(200);
    }
    qInstallMessageHandler(g_previous);
    QCOMPARE(g_warnings, 1);
}

QTEST_GUILESS_MAIN(GlobalHotkeyTest)
#include "tst_globalhotkey.moc"
