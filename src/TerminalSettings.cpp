#include "TerminalSettings.h"

#include "DconfTerminalSettings.h"
#include "DesktopTerminalSettings.h"
#include "GpuTerminalSettings.h"
#include "KonsoleSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>

namespace
{
// "org.kde.konsole.desktop", "/usr/bin/konsole --foo" → "org.kde.konsole", "konsole"
QString entryId(const QString &value)
{
    QString id = value.trimmed().section(QLatin1Char(' '), 0, 0);
    id = QFileInfo(id).fileName();
    if (id.endsWith(QLatin1String(".desktop"))) {
        id.chop(8);
    }
    return id;
}

// The desktop's preferred terminals, most preferred first.
QStringList preferredTerminals(const QString &configDir)
{
    QStringList ids;
    const QHash<QString, QString> kde = readIni(QDir(configDir).filePath(QStringLiteral("kdeglobals"))).value(QStringLiteral("General"));
    for (const QString &key : {QStringLiteral("TerminalService"), QStringLiteral("TerminalApplication")}) {
        if (const QString id = entryId(kde.value(key)); !id.isEmpty()) {
            ids.append(id);
        }
    }
    // xdg-terminal-exec's list: one desktop entry per line, '-' excludes one.
    QStringList lists;
    const QByteArray desktops = qgetenv("XDG_CURRENT_DESKTOP");
    for (const QByteArray &d : desktops.split(':')) {
        if (!d.isEmpty()) {
            lists.append(QString::fromUtf8(d).toLower() + QStringLiteral("-xdg-terminals.list"));
        }
    }
    lists.append(QStringLiteral("xdg-terminals.list"));
    for (const QString &name : std::as_const(lists)) {
        QFile file(QDir(configDir).filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        for (const QByteArray &raw : file.readAll().split('\n')) {
            const QString line = QString::fromUtf8(raw).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('-'))) {
                continue;
            }
            ids.append(entryId(line.startsWith(QLatin1Char('+')) ? line.mid(1) : line));
        }
    }
    return ids;
}
} // namespace

TerminalSettings::Sources TerminalSettings::builtinSources()
{
    Sources sources;
    sources.push_back(std::make_unique<KonsoleSettings>());
    sources.push_back(std::make_unique<KittySettings>());
    sources.push_back(std::make_unique<GhosttySettings>());
    sources.push_back(std::make_unique<AlacrittySettings>());
    sources.push_back(std::make_unique<GnomeConsoleSettings>());
    sources.push_back(std::make_unique<GnomeTerminalSettings>());
    sources.push_back(std::make_unique<PtyxisSettings>());
    sources.push_back(std::make_unique<MateTerminalSettings>());
    sources.push_back(std::make_unique<XfceTerminalSettings>());
    sources.push_back(std::make_unique<CosmicTerminalSettings>());
    return sources;
}

TerminalSettings::TerminalSettings(QObject *parent)
    : TerminalSettings(builtinSources(), QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), parent)
{
}

TerminalSettings::TerminalSettings(Sources sources, const QString &configDir, QObject *parent)
    : QObject(parent)
    , m_sources(std::move(sources))
    , m_configDir(configDir)
    , m_current(TerminalProfile::fallback())
    , m_debounce(new QTimer(this))
{
    // Editors and KConfig save in bursts (write, rename, touch the directory).
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, &TerminalSettings::reload);
}

TerminalSettings::~TerminalSettings() = default;

QList<const TerminalSettingsSource *> TerminalSettings::sources() const
{
    QList<const TerminalSettingsSource *> list;
    for (const auto &s : m_sources) {
        list.append(s.get());
    }
    return list;
}

const TerminalSettingsSource *TerminalSettings::source(const QString &id) const
{
    for (const auto &s : m_sources) {
        if (s->id() == id) {
            return s.get();
        }
    }
    return nullptr;
}

const TerminalSettingsSource *TerminalSettings::desktopDefault() const
{
    QList<const TerminalSettingsSource *> detected;
    for (const auto &s : m_sources) {
        if (s->detect()) {
            detected.append(s.get());
        }
    }
    for (const QString &id : preferredTerminals(m_configDir)) {
        for (const TerminalSettingsSource *s : std::as_const(detected)) {
            if (s->desktopEntries().contains(id, Qt::CaseInsensitive)) {
                return s;
            }
        }
    }
    const QStringList desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const TerminalSettingsSource *s : std::as_const(detected)) {
        for (const QString &d : desktops) {
            if (s->nativeDesktops().contains(d, Qt::CaseInsensitive)) {
                return s;
            }
        }
    }
    return detected.value(0);
}

void TerminalSettings::select(const QString &source, const QString &profile)
{
    m_source = source;
    m_profile = profile;
    reload();
}

void TerminalSettings::reload()
{
    const TerminalSettingsSource *src = nullptr;
    if (m_source.isEmpty()) {
        src = desktopDefault();
    } else if (m_source != none()) {
        src = source(m_source);
        if (src && !src->detect()) {
            src = nullptr;
        }
    }
    TerminalProfile next = TerminalProfile::fallback();
    QStringList watched;
    if (src) {
        // A profile that went away leaves the source's default.
        QString profile = m_profile;
        std::optional<TerminalProfile> loaded = src->load(profile);
        if (!loaded && !profile.isEmpty()) {
            profile.clear();
            loaded = src->load(profile);
        }
        if (loaded) {
            next = *loaded;
        }
        watched = src->watchedPaths(profile);
    }

    // Saving by rename drops the old file's watch, so every reload starts over.
    delete m_watcher;
    m_watcher = nullptr;
    QStringList existing;
    for (const QString &path : std::as_const(watched)) {
        if (QFileInfo::exists(path) && !existing.contains(path)) {
            existing.append(path);
        }
    }
    if (!existing.isEmpty()) {
        m_watcher = new QFileSystemWatcher(existing, this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, m_debounce, qOverload<>(&QTimer::start));
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_debounce, qOverload<>(&QTimer::start));
    }

    if (next == m_current) {
        return;
    }
    m_current = next;
    Q_EMIT changed(m_current);
}
