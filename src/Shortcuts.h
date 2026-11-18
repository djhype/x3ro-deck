#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QVariantMap>

// The app's keyboard shortcuts: ids, labels and defaults. config.json's
// "shortcuts" object holds the user's bindings by id ("" unbinds one); ids it
// leaves out keep their defaults.
namespace Shortcuts
{
struct Action
{
    QString id;
    QString label;
    QKeySequence defaultKeys;
    bool global = false; // also registered system-wide (GlobalHotkey)
};

const QList<Action> &actions();
const Action *action(const QString &id);
// `overrides` is config.json's "shortcuts".
QKeySequence binding(const QVariantMap &overrides, const QString &id);
QHash<QString, QKeySequence> bindings(const QVariantMap &overrides);
// Ids whose sequence another id also uses, grouped: each inner list shares one.
QList<QStringList> conflicts(const QHash<QString, QKeySequence> &bindings);
// What QML shows: id → native text ("Ctrl+Alt+D").
QVariantMap hints(const QVariantMap &overrides);
} // namespace Shortcuts
