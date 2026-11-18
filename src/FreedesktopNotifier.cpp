#include "FreedesktopNotifier.h"

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>

namespace
{
const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");
const QString kInterface = QStringLiteral("org.freedesktop.Notifications");
const QString kAppId = QStringLiteral("x3ro-deck");
} // namespace

Notifier *Notifier::create(QObject *parent)
{
    return new FreedesktopNotifier(QDBusConnection::sessionBus(), parent);
}

FreedesktopNotifier::FreedesktopNotifier(const QDBusConnection &bus, QObject *parent)
    : Notifier(parent)
    , m_bus(bus)
{
    m_bus.connect(kService, kPath, kInterface, QStringLiteral("ActionInvoked"), this, SLOT(onActionInvoked(uint, QString)));
    m_bus.connect(kService, kPath, kInterface, QStringLiteral("ActivationToken"), this, SLOT(onActivationToken(uint, QString)));
    m_bus.connect(kService, kPath, kInterface, QStringLiteral("NotificationClosed"), this, SLOT(onClosed(uint, uint)));
    queryCapabilities();
}

void FreedesktopNotifier::queryCapabilities()
{
    const QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, QStringLiteral("GetCapabilities"));
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QStringList> reply = *watcher;
        if (!reply.isError()) {
            m_bodyMarkup = reply.value().contains(QStringLiteral("body-markup"));
        }
    });
}

void FreedesktopNotifier::show(const Notification &n, const QString &tag)
{
    QStringList actions;
    if (!n.defaultLabel.isEmpty()) {
        actions << defaultAction() << n.defaultLabel;
    }
    for (const Action &a : n.actions) {
        actions << a.key << a.label;
    }
    const uchar urgency = n.urgency == Urgency::Low ? 0 : n.urgency == Urgency::Critical ? 2 : 1;
    const QVariantMap hints{
        {QStringLiteral("urgency"), QVariant::fromValue(urgency)},
        {QStringLiteral("desktop-entry"), kAppId},
        {QStringLiteral("x-kde-appname"), kAppId},
    };
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, QStringLiteral("Notify"));
    call << QStringLiteral("X3RO Deck") << uint(0) << kAppId << n.title << (m_bodyMarkup ? n.body.toHtmlEscaped() : n.body) << actions
         << hints << int(-1);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, tag] {
        watcher->deleteLater();
        const QDBusPendingReply<uint> reply = *watcher;
        if (reply.isError()) {
            if (!m_warned) {
                m_warned = true;
                qWarning("x3ro-deck: cannot show notifications: %s", qPrintable(reply.error().message()));
            }
            return;
        }
        m_tags.insert(reply.value(), tag);
    });
}

void FreedesktopNotifier::onActionInvoked(uint id, const QString &key)
{
    const auto it = m_tags.constFind(id);
    if (it != m_tags.constEnd()) {
        Q_EMIT actionInvoked(*it, key);
    }
}

void FreedesktopNotifier::onActivationToken(uint id, const QString &token)
{
    const auto it = m_tags.constFind(id);
    if (it != m_tags.constEnd()) {
        Q_EMIT activationToken(*it, token);
    }
}

void FreedesktopNotifier::onClosed(uint id, uint)
{
    const QString tag = m_tags.take(id);
    if (!tag.isNull()) {
        Q_EMIT closed(tag);
    }
}
