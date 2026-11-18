#include "QuickCommands.h"

#include "Config.h"
#include "Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QTimer>

#include <algorithm>

namespace
{
constexpr int kDebounceMs = 150;

QString settingsKey(const QString &checkout, const QString &list)
{
    return QStringLiteral("quick/%1/%2").arg(checkout, list);
}
} // namespace

QuickCommands::QuickCommands(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    auto touched = [this](const QString &path) {
        for (int i = 0; i < m_dirs.size(); ++i) {
            if (Paths::same(path, m_dirs.at(i)) || Paths::same(QFileInfo(path).absolutePath(), m_dirs.at(i))) {
                if (!m_pending.contains(i)) {
                    m_pending.append(i);
                }
            }
        }
        m_debounce->start();
    };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, touched);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, touched);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        const QList<int> pending = std::exchange(m_pending, {});
        for (int i : pending) {
            rescan(i);
        }
    });
}

void QuickCommands::setCheckouts(const QStringList &names, const QStringList &dirs)
{
    if (const QStringList watched = m_watcher->files() + m_watcher->directories(); !watched.isEmpty()) {
        m_watcher->removePaths(watched);
    }
    m_names = names;
    m_dirs = dirs;
    m_detected = QList<QList<DetectedCommand>>(dirs.size());
    m_pending.clear();
    for (int i = 0; i < dirs.size(); ++i) {
        m_detected[i] = CommandDetector::detectAll(dirs.at(i));
        watch(i);
    }
}

void QuickCommands::watch(int checkout)
{
    const QString dir = m_dirs.at(checkout);
    if (!QFileInfo(dir).isDir()) {
        return;
    }
    // The folder catches files that appear, and editors that save by
    // replacing the file, which drops it from the watcher.
    QStringList paths{dir};
    for (const QString &name : CommandDetector::watchedFiles()) {
        const QString path = QDir(dir).filePath(name);
        if (QFileInfo::exists(path)) {
            paths.append(path);
        }
    }
    const QStringList current = m_watcher->files() + m_watcher->directories();
    paths.removeIf([&](const QString &p) { return current.contains(p); });
    if (!paths.isEmpty()) {
        m_watcher->addPaths(paths);
    }
}

void QuickCommands::rescan(int checkout)
{
    if (checkout < 0 || checkout >= m_dirs.size()) {
        return;
    }
    watch(checkout);
    QList<DetectedCommand> next = CommandDetector::detectAll(m_dirs.at(checkout));
    const auto same = [](const DetectedCommand &a, const DetectedCommand &b) {
        return a.label == b.label && a.command == b.command && a.group == b.group && a.source == b.source;
    };
    if (std::equal(next.begin(), next.end(), m_detected.at(checkout).begin(), m_detected.at(checkout).end(), same)) {
        return;
    }
    m_detected[checkout] = std::move(next);
    Q_EMIT changed(checkout);
}

const QList<DetectedCommand> &QuickCommands::detected(int checkout) const
{
    static const QList<DetectedCommand> none;
    return checkout >= 0 && checkout < m_detected.size() ? m_detected.at(checkout) : none;
}

QStringList QuickCommands::flags(int checkout, const QString &list) const
{
    if (checkout < 0 || checkout >= m_names.size()) {
        return {};
    }
    return QSettings().value(settingsKey(m_names.at(checkout), list)).toStringList();
}

void QuickCommands::setFlag(int checkout, const QString &list, const QString &key, bool on)
{
    if (checkout < 0 || checkout >= m_names.size()) {
        return;
    }
    QStringList keys = flags(checkout, list);
    if (on == keys.contains(key)) {
        return;
    }
    if (on) {
        keys.append(key);
    } else {
        keys.removeAll(key);
    }
    QSettings settings;
    if (keys.isEmpty()) {
        settings.remove(settingsKey(m_names.at(checkout), list));
    } else {
        settings.setValue(settingsKey(m_names.at(checkout), list), keys);
    }
    Q_EMIT changed(checkout);
}

void QuickCommands::setPinned(int checkout, const QString &key, bool on)
{
    setFlag(checkout, QStringLiteral("pinned"), key, on);
}

void QuickCommands::setHidden(int checkout, const QString &key, bool on)
{
    setFlag(checkout, QStringLiteral("hidden"), key, on);
}

QList<QuickEntry> QuickCommands::entries(int checkout, const QList<QuickCommand> &manual) const
{
    const QStringList pinned = flags(checkout, QStringLiteral("pinned"));
    const QStringList hidden = flags(checkout, QStringLiteral("hidden"));
    QList<QuickEntry> out;
    auto add = [&](const QString &label, const QString &command, const QString &group, const QString &source) {
        QuickEntry e;
        e.key = source + QLatin1Char(':') + label;
        e.label = label;
        e.command = command;
        e.group = group;
        e.source = source;
        e.pinned = pinned.contains(e.key);
        e.hidden = hidden.contains(e.key);
        out.append(e);
    };
    for (const QuickCommand &q : manual) {
        add(q.label, q.command, q.group, manualSource());
    }
    for (const DetectedCommand &d : detected(checkout)) {
        add(d.label, d.command, d.group, d.source);
    }
    std::stable_partition(out.begin(), out.end(), [](const QuickEntry &e) { return e.pinned; });
    return out;
}
