// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>

// System-wide shortcuts that work while another app has focus. One backend
// per OS; create() picks it. add() every shortcut, then start().
class GlobalHotkey : public QObject
{
    Q_OBJECT
public:
    struct Hotkey
    {
        QString id;
        QString description;
        QKeySequence sequence; // the default; the desktop may let the user change it
    };

    // The platform's backend, or one that never fires.
    static GlobalHotkey *create(QObject *parent = nullptr);

    using QObject::QObject;
    void add(const QString &id, const QString &description, const QKeySequence &sequence) { m_hotkeys.append({id, description, sequence}); }
    virtual void start() = 0;

Q_SIGNALS:
    // `activationToken` lets the window take focus on Wayland; may be empty.
    void activated(const QString &id, const QString &activationToken);

protected:
    QList<Hotkey> m_hotkeys;
};
