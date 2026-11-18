// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "GlobalHotkey.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QVariantMap>

#include <functional>

// One entry of the portal's a(sa{sv}) shortcut lists.
struct PortalShortcut
{
    QString id;
    QVariantMap properties;
};
Q_DECLARE_METATYPE(PortalShortcut)
QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &s);
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &s);

// org.freedesktop.portal.GlobalShortcuts (Plasma 6, GNOME 48+). The portal
// asks the user to confirm new shortcuts once and remembers them per app.
class PortalGlobalHotkey : public GlobalHotkey
{
    Q_OBJECT
public:
    explicit PortalGlobalHotkey(const QDBusConnection &bus = QDBusConnection::sessionBus(), QObject *parent = nullptr);

    void start() override;

    // "Meta+Alt+H" → "LOGO+ALT+h", the XDG shortcut trigger format.
    static QString portalTrigger(const QKeySequence &sequence);
    static void registerTypes();

private Q_SLOTS:
    void onResponse(const QDBusMessage &message);
    // Takes the raw message: Qt 6.4 cannot connect a slot typed (o, s, t, a{sv}).
    void onActivated(const QDBusMessage &message);

private:
    using Handler = std::function<void(uint response, const QVariantMap &results)>;
    // Calls a portal method that answers through a Request object.
    void request(const QString &method, QVariantList args, const Handler &handler);
    void createSession();
    void listShortcuts();
    void bindShortcuts(const QList<PortalShortcut> &missing);
    void fail(const QString &what);

    QDBusConnection m_bus;
    QString m_session;
    QHash<QString, Handler> m_pending; // request path → handler
    int m_tokens = 0;
};
