#include "PortalGlobalHotkey.h"

#include <QCoreApplication>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace
{
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString kRegistry = QStringLiteral("org.freedesktop.host.portal.Registry");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");
const QString kAppId = QStringLiteral("x3ro-deck");

QList<PortalShortcut> shortcutsFrom(const QVariant &v)
{
    QList<PortalShortcut> list;
    if (v.canConvert<QDBusArgument>()) {
        v.value<QDBusArgument>() >> list;
    }
    return list;
}
} // namespace

QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &s)
{
    arg.beginStructure();
    arg << s.id << s.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &s)
{
    arg.beginStructure();
    arg >> s.id >> s.properties;
    arg.endStructure();
    return arg;
}

GlobalHotkey *GlobalHotkey::create(QObject *parent)
{
    return new PortalGlobalHotkey(QDBusConnection::sessionBus(), parent);
}

void PortalGlobalHotkey::registerTypes()
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<QList<PortalShortcut>>();
}

PortalGlobalHotkey::PortalGlobalHotkey(const QDBusConnection &bus, QObject *parent)
    : GlobalHotkey(parent)
    , m_bus(bus)
{
    registerTypes();
}

QString PortalGlobalHotkey::portalTrigger(const QKeySequence &sequence)
{
    if (sequence.isEmpty()) {
        return QString();
    }
    const QKeyCombination combo = sequence[0];
    QStringList parts;
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    if (mods & Qt::ControlModifier) {
        parts << QStringLiteral("CTRL");
    }
    if (mods & Qt::AltModifier) {
        parts << QStringLiteral("ALT");
    }
    if (mods & Qt::ShiftModifier) {
        parts << QStringLiteral("SHIFT");
    }
    if (mods & Qt::MetaModifier) {
        parts << QStringLiteral("LOGO");
    }
    // Key names are xkb keysym names: letters are lower case, F-keys as is.
    QString key = QKeySequence(combo.key()).toString(QKeySequence::PortableText);
    if (key.size() == 1) {
        key = key.toLower();
    }
    parts << key;
    return parts.join(QLatin1Char('+'));
}

void PortalGlobalHotkey::fail(const QString &what)
{
    qWarning("x3ro-deck: global shortcuts unavailable (%s); they work only inside the window", qPrintable(what));
}

void PortalGlobalHotkey::start()
{
    if (m_hotkeys.isEmpty() || !m_bus.isConnected()) {
        return;
    }
    QDBusMessage get
        = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    get << kInterface << QStringLiteral("version");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        if (watcher->isError()) {
            fail(QStringLiteral("no GlobalShortcuts portal: %1").arg(watcher->error().message()));
            return;
        }
        // Tells the portal which app a host (unsandboxed) process is; older
        // portals lack the registry, which is fine.
        QDBusMessage reg = QDBusMessage::createMethodCall(kService, kPath, kRegistry, QStringLiteral("Register"));
        reg << kAppId << QVariantMap();
        auto *registered = new QDBusPendingCallWatcher(m_bus.asyncCall(reg), this);
        connect(registered, &QDBusPendingCallWatcher::finished, this, [this, registered] {
            registered->deleteLater();
            createSession();
        });
    });
    if (!m_bus.connect(kService, kPath, kInterface, QStringLiteral("Activated"), this, SLOT(onActivated(QDBusMessage)))) {
        fail(QStringLiteral("cannot listen for Activated"));
    }
}

void PortalGlobalHotkey::request(const QString &method, QVariantList args, const Handler &handler)
{
    // The portal answers on a Request object whose path follows from our
    // bus name and the token; subscribe before calling so no answer is lost.
    const QString token = QStringLiteral("x3rodeck%1_%2").arg(QCoreApplication::applicationPid()).arg(++m_tokens);
    QString sender = m_bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    m_pending.insert(path, handler);
    m_bus.connect(kService, path, kRequest, QStringLiteral("Response"), this, SLOT(onResponse(QDBusMessage)));

    QVariantMap options = args.takeLast().toMap();
    options.insert(QStringLiteral("handle_token"), token);
    args.append(options);
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    call.setArguments(args);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, method, path] {
        watcher->deleteLater();
        if (watcher->isError()) {
            m_pending.remove(path);
            m_bus.disconnect(kService, path, kRequest, QStringLiteral("Response"), this, SLOT(onResponse(QDBusMessage)));
            fail(QStringLiteral("%1 failed: %2").arg(method, watcher->error().message()));
        }
    });
}

void PortalGlobalHotkey::onResponse(const QDBusMessage &message)
{
    const QString path = message.path();
    const Handler handler = m_pending.take(path);
    m_bus.disconnect(kService, path, kRequest, QStringLiteral("Response"), this, SLOT(onResponse(QDBusMessage)));
    if (!handler || message.arguments().size() < 2) {
        return;
    }
    const uint response = message.arguments().at(0).toUInt();
    QVariantMap results;
    message.arguments().at(1).value<QDBusArgument>() >> results;
    handler(response, results);
}

void PortalGlobalHotkey::createSession()
{
    const QVariantMap options{
        {QStringLiteral("session_handle_token"), QStringLiteral("x3rodeck%1").arg(QCoreApplication::applicationPid())}};
    request(QStringLiteral("CreateSession"), {options}, [this](uint response, const QVariantMap &results) {
        if (response != 0) {
            fail(QStringLiteral("CreateSession was refused"));
            return;
        }
        // Spec says string; some portals send an object path.
        const QVariant handle = results.value(QStringLiteral("session_handle"));
        m_session = handle.userType() == qMetaTypeId<QDBusObjectPath>() ? handle.value<QDBusObjectPath>().path() : handle.toString();
        listShortcuts();
    });
}

void PortalGlobalHotkey::listShortcuts()
{
    request(QStringLiteral("ListShortcuts"), {QVariant::fromValue(QDBusObjectPath(m_session)), QVariantMap()},
            [this](uint response, const QVariantMap &results) {
                QStringList bound;
                if (response == 0) {
                    for (const PortalShortcut &s : shortcutsFrom(results.value(QStringLiteral("shortcuts")))) {
                        bound << s.id;
                    }
                }
                QList<PortalShortcut> missing;
                for (const Hotkey &h : std::as_const(m_hotkeys)) {
                    if (!bound.contains(h.id)) {
                        missing.append({h.id,
                                        {{QStringLiteral("description"), h.description},
                                         {QStringLiteral("preferred_trigger"), portalTrigger(h.sequence)}}});
                    }
                }
                if (!missing.isEmpty()) {
                    bindShortcuts(missing);
                }
            });
}

void PortalGlobalHotkey::bindShortcuts(const QList<PortalShortcut> &missing)
{
    request(QStringLiteral("BindShortcuts"),
            {QVariant::fromValue(QDBusObjectPath(m_session)), QVariant::fromValue(missing), QString(), QVariantMap()},
            [this](uint response, const QVariantMap &) {
                // 1 means the user dismissed the dialog; the portal asks again next launch.
                if (response == 2) {
                    fail(QStringLiteral("BindShortcuts failed"));
                }
            });
}

void PortalGlobalHotkey::onActivated(const QDBusMessage &message)
{
    const QVariantList args = message.arguments();
    if (args.size() < 4 || args.at(0).value<QDBusObjectPath>().path() != m_session) {
        return;
    }
    QVariantMap options;
    args.at(3).value<QDBusArgument>() >> options;
    Q_EMIT activated(args.at(1).toString(), options.value(QStringLiteral("activation_token")).toString());
}
