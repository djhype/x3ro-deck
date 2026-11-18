// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Shortcuts.h"

#include <QMap>

namespace Shortcuts
{

const QList<Action> &actions()
{
    static const QList<Action> list = [] {
        QList<Action> out;
        // On macOS Qt's Ctrl is ⌘, and ⌥+digit types characters, so
        // checkouts switch with ⌘+digit there and Alt+digit elsewhere.
#if defined(Q_OS_MACOS)
        const Qt::Modifier selectModifier = Qt::CTRL;
#else
        const Qt::Modifier selectModifier = Qt::ALT;
#endif
        for (int i = 0; i < 9; ++i) {
            out.append({QStringLiteral("select-%1").arg(i + 1), QStringLiteral("Checkout %1").arg(i + 1),
                        QKeySequence(selectModifier | Qt::Key(Qt::Key_1 + i))});
        }
        out.append({QStringLiteral("next-checkout"), QStringLiteral("Next checkout"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_PageDown)});
        out.append(
            {QStringLiteral("previous-checkout"), QStringLiteral("Previous checkout"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_PageUp)});
        out.append({QStringLiteral("docker-tab"), QStringLiteral("Docker tab"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D)});
        out.append({QStringLiteral("agents-tab"), QStringLiteral("Agents tab"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A)});
        out.append({QStringLiteral("toggle-rail"), QStringLiteral("Show or hide the NEEDS YOU rail"),
                    QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R)});
        out.append({QStringLiteral("new-shell"), QStringLiteral("New shell tab"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T)});
        out.append({QStringLiteral("next-attention"), QStringLiteral("Open the first NEEDS YOU item"),
                    QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N)});
        out.append({QStringLiteral("settings"), QStringLiteral("Settings"), QKeySequence(Qt::CTRL | Qt::Key_Comma)});
        out.append({QStringLiteral("quit"), QStringLiteral("Quit"), QKeySequence(Qt::CTRL | Qt::Key_Q)});
        out.append({QStringLiteral("toggle-window"), QStringLiteral("Show or hide X3RO Deck (system-wide)"),
                    QKeySequence(Qt::META | Qt::ALT | Qt::Key_H), true});
        return out;
    }();
    return list;
}

const Action *action(const QString &id)
{
    for (const Action &a : actions()) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

QKeySequence binding(const QVariantMap &overrides, const QString &id)
{
    if (overrides.contains(id)) {
        return QKeySequence::fromString(overrides.value(id).toString(), QKeySequence::PortableText);
    }
    const Action *a = action(id);
    return a ? a->defaultKeys : QKeySequence();
}

QHash<QString, QKeySequence> bindings(const QVariantMap &overrides)
{
    QHash<QString, QKeySequence> out;
    for (const Action &a : actions()) {
        out.insert(a.id, binding(overrides, a.id));
    }
    return out;
}

QList<QStringList> conflicts(const QHash<QString, QKeySequence> &bindings)
{
    QMap<QString, QStringList> byKeys;
    for (const Action &a : actions()) {
        const QKeySequence keys = bindings.value(a.id);
        if (!keys.isEmpty()) {
            byKeys[keys.toString(QKeySequence::PortableText)].append(a.id);
        }
    }
    QList<QStringList> out;
    for (const QStringList &ids : std::as_const(byKeys)) {
        if (ids.size() > 1) {
            out.append(ids);
        }
    }
    return out;
}

QVariantMap hints(const QVariantMap &overrides)
{
    QVariantMap out;
    for (const Action &a : actions()) {
        out.insert(a.id, binding(overrides, a.id).toString(QKeySequence::NativeText));
    }
    return out;
}

} // namespace Shortcuts
