#include "PrWatcher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

namespace
{
const QString kFields = QStringLiteral("number,url,title,state,isDraft,baseRefName,reviewDecision,mergeable,mergeStateStatus,"
                                       "additions,deletions,statusCheckRollup,latestReviews");

constexpr int kPollFast = 30 * 1000; // mergeability unknown or checks running
constexpr int kPollNormal = 2 * 60 * 1000; // open PR, settled
constexpr int kPollIdle = 3 * 60 * 1000; // no PR yet, or merged/closed
constexpr int kTimeout = 30 * 1000;

bool isDefaultBranch(const QString &branch)
{
    return branch.isEmpty() || branch == QLatin1String("main") || branch == QLatin1String("master") || branch == QLatin1String("develop");
}
} // namespace

PrWatcher::PrWatcher(QObject *parent)
    : QObject(parent)
{
}

void PrWatcher::watch(const QString &key, const QString &dir)
{
    Entry &e = m_entries[key];
    e.dir = dir;
    if (!e.timer) {
        e.timer = new QTimer(this);
        e.timer->setSingleShot(true);
        connect(e.timer, &QTimer::timeout, this, [this, key] { fetch(key); });
    }
}

void PrWatcher::setBranch(const QString &key, const QString &branch)
{
    auto it = m_entries.find(key);
    if (it == m_entries.end() || it->branch == branch) {
        return;
    }
    it->branch = branch;
    Q_EMIT updated(key, PrInfo{}); // drop the previous branch's PR right away
    refresh(key);
}

void PrWatcher::refresh(const QString &key, int delayMs)
{
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return;
    }
    if (it->proc) {
        it->again = true;
        return;
    }
    it->timer->start(delayMs);
}

void PrWatcher::fetch(const QString &key)
{
    Entry &e = m_entries[key];
    if (e.proc) {
        e.again = true;
        return;
    }
    if (isDefaultBranch(e.branch)) {
        Q_EMIT updated(key, PrInfo{});
        return; // the next setBranch() restarts polling
    }

    auto *proc = new QProcess(this);
    e.proc = proc;
    proc->setWorkingDirectory(e.dir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GH_PROMPT_DISABLED"), QStringLiteral("1"));
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    proc->setProcessEnvironment(env);
    const QString branch = e.branch;
    connect(proc, &QProcess::finished, this, [this, key, proc, branch](int code, QProcess::ExitStatus status) {
        Entry &en = m_entries[key];
        en.proc = nullptr;
        proc->deleteLater();
        if (en.branch != branch) {
            refresh(key); // branch switched mid-flight; this result is stale
            return;
        }
        const QByteArray out = proc->readAllStandardOutput();
        const QByteArray err = proc->readAllStandardError();
        int next = kPollIdle;
        if (status == QProcess::NormalExit && code == 0) {
            const PrInfo info = parse(out);
            if (info.state == QLatin1String("OPEN")) {
                next = (info.mergeable == QLatin1String("UNKNOWN") || info.checksPending > 0) ? kPollFast : kPollNormal;
            }
            Q_EMIT updated(key, info);
        } else if (err.contains("no pull requests found")) {
            Q_EMIT updated(key, PrInfo{});
        } else {
            // Network or auth trouble: keep showing the last known state.
            qWarning("x3ro-deck: gh pr view failed for %s: %s", qPrintable(key), err.trimmed().constData());
            next = kPollNormal;
        }
        if (en.again) {
            en.again = false;
            next = 0;
        }
        en.timer->start(next);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, key, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return; // finished() follows for every other error
        }
        Entry &en = m_entries[key];
        en.proc = nullptr;
        proc->deleteLater();
        qWarning("x3ro-deck: cannot run gh; pull request status disabled");
    });
    QTimer::singleShot(kTimeout, proc, [proc] { proc->kill(); });
    proc->start(QStringLiteral("gh"), {QStringLiteral("pr"), QStringLiteral("view"), e.branch, QStringLiteral("--json"), kFields});
}

PrInfo PrWatcher::parse(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    PrInfo pr;
    pr.number = o.value(QStringLiteral("number")).toInt();
    pr.url = o.value(QStringLiteral("url")).toString();
    pr.title = o.value(QStringLiteral("title")).toString();
    pr.state = o.value(QStringLiteral("state")).toString();
    pr.draft = o.value(QStringLiteral("isDraft")).toBool();
    pr.base = o.value(QStringLiteral("baseRefName")).toString();
    pr.reviewDecision = o.value(QStringLiteral("reviewDecision")).toString();
    pr.mergeable = o.value(QStringLiteral("mergeable")).toString();
    pr.mergeState = o.value(QStringLiteral("mergeStateStatus")).toString();
    pr.additions = o.value(QStringLiteral("additions")).toInt();
    pr.deletions = o.value(QStringLiteral("deletions")).toInt();

    // CheckRuns carry status + conclusion; legacy StatusContexts carry a single state.
    static const QStringList failed{QStringLiteral("FAILURE"),   QStringLiteral("ERROR"),           QStringLiteral("TIMED_OUT"),
                                    QStringLiteral("CANCELLED"), QStringLiteral("ACTION_REQUIRED"), QStringLiteral("STARTUP_FAILURE")};
    static const QStringList ignored{QStringLiteral("SKIPPED"), QStringLiteral("NEUTRAL"), QStringLiteral("STALE")};
    for (const QJsonValue &v : o.value(QStringLiteral("statusCheckRollup")).toArray()) {
        const QJsonObject c = v.toObject();
        const bool isRun = c.value(QStringLiteral("__typename")).toString() == QLatin1String("CheckRun");
        const QString name = c.value(isRun ? QStringLiteral("name") : QStringLiteral("context")).toString();
        if (isRun && c.value(QStringLiteral("status")).toString() != QLatin1String("COMPLETED")) {
            ++pr.checksPending;
            continue;
        }
        const QString result = c.value(isRun ? QStringLiteral("conclusion") : QStringLiteral("state")).toString();
        if (result == QLatin1String("PENDING") || result == QLatin1String("EXPECTED")) {
            ++pr.checksPending;
        } else if (failed.contains(result)) {
            ++pr.checksFailed;
            pr.failing.append(name);
        } else if (!ignored.contains(result)) {
            ++pr.checksPassed;
        }
    }

    for (const QJsonValue &v : o.value(QStringLiteral("latestReviews")).toArray()) {
        const QJsonObject r = v.toObject();
        const QString login = r.value(QStringLiteral("author")).toObject().value(QStringLiteral("login")).toString();
        const QString state = r.value(QStringLiteral("state")).toString();
        if (state == QLatin1String("APPROVED")) {
            pr.approvers.append(login);
        } else if (state == QLatin1String("CHANGES_REQUESTED")) {
            pr.changesRequestedBy.append(login);
        }
    }
    return pr;
}
