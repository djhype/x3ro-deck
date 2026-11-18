// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>

class QProcess;
class QTimer;

struct PrInfo
{
    int number = 0; // 0: the branch has no pull request
    QString url;
    QString title;
    QString state; // OPEN | MERGED | CLOSED
    bool draft = false;
    QString base;
    QString reviewDecision; // APPROVED | CHANGES_REQUESTED | REVIEW_REQUIRED | ""
    QString mergeable; // MERGEABLE | CONFLICTING | UNKNOWN
    QString mergeState; // CLEAN | BEHIND | BLOCKED | DIRTY | UNSTABLE | ...
    int additions = 0;
    int deletions = 0;
    int checksPassed = 0;
    int checksFailed = 0;
    int checksPending = 0;
    QStringList failing; // names of failed checks
    QStringList approvers;
    QStringList changesRequestedBy;
    // Why there is no status: gh-missing | gh-logged-out | no-github-remote, or empty.
    QString problem;
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
    // Checks gh again after it was missing or logged out (the user's Refresh).
    void recheck();

    static bool isGitHubUrl(const QString &url);
    // `git symbolic-ref refs/remotes/origin/HEAD` → the branch name.
    static QString branchFromSymbolicRef(const QByteArray &out);

Q_SIGNALS:
    void updated(const QString &key, const PrInfo &info);

private:
    struct Entry
    {
        QString dir;
        QString branch;
        QProcess *proc = nullptr;
        QTimer *timer = nullptr;
        bool again = false; // a refresh arrived while a fetch was running
        bool repoKnown = false; // origin and the default branch were read for this branch
        bool resolving = false;
        bool github = false;
        QString defaultBranch; // empty: unknown, so main/master/develop count
    };
    enum class Gh { Unknown, Checking, Ok, Missing, LoggedOut };
    using Done = std::function<void(int exitCode, const QByteArray &out)>;

    void fetch(const QString &key);
    void checkAuth();
    void resolveRepo(const QString &key);
    // Runs a short command in `dir`; exitCode is -1 when it could not start.
    void run(const QString &dir, const QString &program, const QStringList &args, const Done &done);
    void emitProblem(const QString &key, const QString &problem);
    void finished(const QString &key, int exitCode);
    static PrInfo parse(const QByteArray &json);

    QHash<QString, Entry> m_entries;
    Gh m_gh = Gh::Unknown;
    QTimer *m_authRecheck;
};
