// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PrWatcher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace
{
const QString kFields = QStringLiteral("number,url,title,state,isDraft,baseRefName,reviewDecision,mergeable,mergeStateStatus,"
                                       "additions,deletions,statusCheckRollup,latestReviews");

constexpr int kPollFast = 30 * 1000; // mergeability unknown or checks running
constexpr int kPollNormal = 2 * 60 * 1000; // open PR, settled
constexpr int kPollIdle = 3 * 60 * 1000; // no PR yet, or merged/closed
constexpr int kTimeout = 30 * 1000;

constexpr int kGitTimeout = 10 * 1000;

bool isLegacyDefault(const QString &branch)
{
    return branch == QLatin1String("main") || branch == QLatin1String("master") || branch == QLatin1String("develop");
}

QProcessEnvironment ghEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GH_PROMPT_DISABLED"), QStringLiteral("1"));
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    return env;
}
} // namespace

PrWatcher::PrWatcher(QObject *parent)
    : QObject(parent)
    , m_authRecheck(new QTimer(this))
{
    m_authRecheck->setSingleShot(true);
    m_authRecheck->setInterval(kPollIdle);
    connect(m_authRecheck, &QTimer::timeout, this, &PrWatcher::recheck);
}

bool PrWatcher::isGitHubUrl(const QString &url)
{
    // https://github.com/o/r(.git), git@github.com:o/r.git, ssh://git@github.com/o/r, and GitHub Enterprise hosts named *github*.
    static const QRegularExpression re(QStringLiteral(R"(^(?:[a-z+]+://)?(?:[^@/]+@)?([^/:]+))"));
    const QRegularExpressionMatch m = re.match(url.trimmed());
    return m.hasMatch() && m.captured(1).contains(QLatin1String("github"), Qt::CaseInsensitive);
}

QString PrWatcher::branchFromSymbolicRef(const QByteArray &out)
{
    QString ref = QString::fromUtf8(out).trimmed();
    if (ref.startsWith(QLatin1String("refs/remotes/"))) {
        ref = ref.mid(13);
    }
    const qsizetype slash = ref.indexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : ref.mid(slash + 1);
}

void PrWatcher::run(const QString &dir, const QString &program, const QStringList &args, const Done &done)
{
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(dir);
    proc->setProcessEnvironment(ghEnvironment());
    proc->setStandardInputFile(QProcess::nullDevice());
    connect(proc, &QProcess::finished, this, [proc, done](int code, QProcess::ExitStatus status) {
        proc->deleteLater();
        done(status == QProcess::NormalExit ? code : -2, proc->readAllStandardOutput());
    });
    connect(proc, &QProcess::errorOccurred, this, [proc, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            proc->deleteLater();
            done(-1, {});
        }
    });
    QTimer::singleShot(kGitTimeout, proc, [proc] { proc->kill(); });
    proc->start(program, args);
}

void PrWatcher::emitProblem(const QString &key, const QString &problem)
{
    PrInfo info;
    info.problem = problem;
    Q_EMIT updated(key, info);
}

void PrWatcher::recheck()
{
    if (m_gh == Gh::Missing || m_gh == Gh::LoggedOut) {
        m_gh = Gh::Unknown;
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            refresh(it.key());
        }
    }
}

void PrWatcher::checkAuth()
{
    if (m_gh == Gh::Checking) {
        return;
    }
    m_gh = Gh::Checking;
    run(QString(), QStringLiteral("gh"), {QStringLiteral("auth"), QStringLiteral("status")}, [this](int code, const QByteArray &) {
        if (code == -1) {
            m_gh = Gh::Missing;
            qWarning("x3ro-deck: cannot run gh; pull request status disabled");
        } else if (code != 0) {
            m_gh = Gh::LoggedOut;
            m_authRecheck->start();
        } else {
            m_gh = Gh::Ok;
        }
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            if (!it->branch.isEmpty()) {
                fetch(it.key());
            }
        }
    });
}

void PrWatcher::resolveRepo(const QString &key)
{
    Entry &e = m_entries[key];
    if (e.resolving) {
        return;
    }
    e.resolving = true;
    const QString dir = e.dir;
    const QString branch = e.branch;
    auto finish = [this, key, branch](bool github, const QString &defaultBranch) {
        Entry &en = m_entries[key];
        en.resolving = false;
        if (en.branch != branch) {
            fetch(key); // switched meanwhile; resolve again for the new branch
            return;
        }
        en.repoKnown = true;
        en.github = github;
        en.defaultBranch = defaultBranch;
        fetch(key);
    };
    run(dir, QStringLiteral("git"), {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")},
        [this, dir, finish](int code, const QByteArray &url) {
            if (code != 0 || !isGitHubUrl(QString::fromUtf8(url))) {
                finish(false, QString());
                return;
            }
            run(dir, QStringLiteral("git"), {QStringLiteral("symbolic-ref"), QStringLiteral("refs/remotes/origin/HEAD")},
                [this, dir, finish](int code, const QByteArray &out) {
                    if (const QString b = branchFromSymbolicRef(out); code == 0 && !b.isEmpty()) {
                        finish(true, b);
                        return;
                    }
                    // origin/HEAD is only set by clone; ask GitHub instead.
                    run(dir, QStringLiteral("gh"),
                        {QStringLiteral("repo"), QStringLiteral("view"), QStringLiteral("--json"), QStringLiteral("defaultBranchRef"),
                         QStringLiteral("-q"), QStringLiteral(".defaultBranchRef.name")},
                        [finish](int code, const QByteArray &out) {
                            finish(true, code == 0 ? QString::fromUtf8(out).trimmed() : QString());
                        });
                });
        });
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
    it->repoKnown = false;
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
    // Every early return leaves the timer stopped; setBranch(), refresh() or
    // recheck() starts the next lookup.
    if (e.branch.isEmpty()) {
        Q_EMIT updated(key, PrInfo{});
        return;
    }
    switch (m_gh) {
    case Gh::Missing:
        emitProblem(key, QStringLiteral("gh-missing"));
        return;
    case Gh::LoggedOut:
        emitProblem(key, QStringLiteral("gh-logged-out"));
        return;
    case Gh::Unknown:
    case Gh::Checking:
        checkAuth(); // fetches every checkout once it knows
        return;
    case Gh::Ok:
        break;
    }
    if (!e.repoKnown) {
        resolveRepo(key);
        return;
    }
    if (!e.github) {
        emitProblem(key, QStringLiteral("no-github-remote"));
        return;
    }
    if (e.defaultBranch.isEmpty() ? isLegacyDefault(e.branch) : e.branch == e.defaultBranch) {
        Q_EMIT updated(key, PrInfo{});
        return;
    }

    auto *proc = new QProcess(this);
    e.proc = proc;
    proc->setWorkingDirectory(e.dir);
    proc->setProcessEnvironment(ghEnvironment());
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
        m_gh = Gh::Missing; // removed while running
        emitProblem(key, QStringLiteral("gh-missing"));
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
