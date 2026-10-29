#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>

struct GitInfo {
    QString branch;
    QString commit; // "<short sha> <subject>"
};

// Watches each checkout's HEAD and reflog so branch switches and commits show
// up immediately, with a slow timer as a safety net for missed events.
class GitWatcher : public QObject
{
    Q_OBJECT
public:
    explicit GitWatcher(QObject *parent = nullptr);
    void watch(const QString &key, const QString &dir);

Q_SIGNALS:
    void updated(const QString &key, const GitInfo &info);

private:
    void scheduleRead(const QString &key);
    void read(const QString &key);
    void rewatch(const QString &key);
    static QString gitDir(const QString &dir);

    QFileSystemWatcher m_watcher;
    QHash<QString, QString> m_dirs;     // key → work tree
    QHash<QString, QString> m_fileKeys; // watched path → key
    QHash<QString, QTimer *> m_debounce;
    QTimer m_safety;
};
