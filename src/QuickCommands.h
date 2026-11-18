#pragma once

#include "CommandDetector.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;
struct QuickCommand;

// One quick command as the Docker panel shows it.
struct QuickEntry
{
    QString key; // "<source>:<label>", stable across scans
    QString label;
    QString command;
    QString group;
    QString source; // "Manual" for config.json's quickCommands
    bool pinned = false;
    bool hidden = false;
};

// Detected quick commands per checkout, re-scanned when their files change,
// plus the user's pinned and hidden entries (QSettings, per checkout name).
class QuickCommands : public QObject
{
    Q_OBJECT
public:
    explicit QuickCommands(QObject *parent = nullptr);

    // Checkout names and folders; scans them all and watches their files.
    void setCheckouts(const QStringList &names, const QStringList &dirs);
    void rescan(int checkout);
    const QList<DetectedCommand> &detected(int checkout) const;

    // Manual commands first, then detected ones by source; pinned entries
    // move to the front, keeping that order among themselves.
    QList<QuickEntry> entries(int checkout, const QList<QuickCommand> &manual) const;
    void setPinned(int checkout, const QString &key, bool on);
    void setHidden(int checkout, const QString &key, bool on);

    static QString manualSource() { return QStringLiteral("Manual"); }

Q_SIGNALS:
    void changed(int checkout);

private:
    void watch(int checkout);
    void setFlag(int checkout, const QString &list, const QString &key, bool on);
    QStringList flags(int checkout, const QString &list) const;

    QStringList m_names;
    QStringList m_dirs;
    QList<QList<DetectedCommand>> m_detected;
    QFileSystemWatcher *m_watcher;
    QTimer *m_debounce;
    QList<int> m_pending;
};
