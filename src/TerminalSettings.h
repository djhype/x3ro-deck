#pragma once

#include "TerminalSettingsSource.h"

#include <QObject>

#include <memory>
#include <vector>

class QFileSystemWatcher;
class QTimer;

// Picks a TerminalSettingsSource and profile, imports it, and imports again
// when the source's files change. current() is always usable: without a
// source it is TerminalProfile::fallback().
class TerminalSettings : public QObject
{
    Q_OBJECT
public:
    using Sources = std::vector<std::unique_ptr<TerminalSettingsSource>>;

    // Every source the app knows, reading the real config locations. A new
    // terminal is added here.
    static Sources builtinSources();
    // The config value of terminalSettings that skips importing.
    static QString none() { return QStringLiteral("none"); }

    explicit TerminalSettings(QObject *parent = nullptr);
    // `configDir` holds kdeglobals and xdg-terminals.list.
    TerminalSettings(Sources sources, const QString &configDir, QObject *parent = nullptr);
    ~TerminalSettings() override;

    QList<const TerminalSettingsSource *> sources() const;
    const TerminalSettingsSource *source(const QString &id) const;
    // The detected source the desktop names as its default terminal, else
    // the one native to the desktop, else the first detected; null if none.
    const TerminalSettingsSource *desktopDefault() const;

    // `source` is a source id, empty for the desktop default, or none().
    // An empty `profile` is the source's default profile.
    void select(const QString &source, const QString &profile);
    const TerminalProfile &current() const { return m_current; }

Q_SIGNALS:
    void changed(const TerminalProfile &profile);

private:
    void reload();

    Sources m_sources;
    QString m_configDir;
    QString m_source;
    QString m_profile;
    TerminalProfile m_current;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce;
};
