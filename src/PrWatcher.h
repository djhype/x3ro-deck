#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>

class QProcess;
class QTimer;

struct PrInfo {
    int number = 0; // 0: the branch has no pull request
    QString url;
    QString title;
    QString state; // OPEN | MERGED | CLOSED
    bool draft = false;
    QString base;
    QString reviewDecision; // APPROVED | CHANGES_REQUESTED | REVIEW_REQUIRED | ""
    QString mergeable;      // MERGEABLE | CONFLICTING | UNKNOWN
    QString mergeState;     // CLEAN | BEHIND | BLOCKED | DIRTY | UNSTABLE | ...
    int additions = 0;
    int deletions = 0;
    int checksPassed = 0;
    int checksFailed = 0;
    int checksPending = 0;
    QStringList failing; // names of failed checks
    QStringList approvers;
    QStringList changesRequestedBy;
};

// Polls `gh pr view <branch>` for each checkout. Polling speeds up while
// GitHub is still computing mergeability or checks are running.
class PrWatcher : public QObject
{
    Q_OBJECT
public:
    explicit PrWatcher(QObject *parent = nullptr);
    void watch(const QString &key, const QString &dir);
    // Fetches immediately when the branch differs from the last one seen.
    void setBranch(const QString &key, const QString &branch);
    void refresh(const QString &key, int delayMs = 0);

Q_SIGNALS:
    void updated(const QString &key, const PrInfo &info);

private:
    struct Entry {
        QString dir;
        QString branch;
        QProcess *proc = nullptr;
        QTimer *timer = nullptr;
        bool again = false; // a refresh arrived while a fetch was running
    };

    void fetch(const QString &key);
    void finished(const QString &key, int exitCode);
    static PrInfo parse(const QByteArray &json);

    QHash<QString, Entry> m_entries;
};
