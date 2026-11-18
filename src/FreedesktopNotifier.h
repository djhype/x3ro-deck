// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Notifier.h"

#include <QDBusConnection>
#include <QHash>

// org.freedesktop.Notifications over D-Bus: Plasma, GNOME and most other
// Linux desktops.
class FreedesktopNotifier : public Notifier
{
    Q_OBJECT
public:
    explicit FreedesktopNotifier(const QDBusConnection &bus = QDBusConnection::sessionBus(), QObject *parent = nullptr);

    void show(const Notification &notification, const QString &tag) override;

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &key);
    void onActivationToken(uint id, const QString &token);
    void onClosed(uint id, uint reason);

private:
    void queryCapabilities();

    QDBusConnection m_bus;
    QHash<uint, QString> m_tags;
    // Plasma and GNOME render markup; assumed until the server says otherwise.
    bool m_bodyMarkup = true;
    bool m_warned = false;
};
