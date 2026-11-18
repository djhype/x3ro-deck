// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Worktrees.h"

#include "Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

namespace Worktrees
{

QList<Worktree> parseWorktreeList(const QByteArray &porcelain)
{
    QList<Worktree> out;
    Worktree current;
    bool skip = false;
    auto flush = [&] {
        if (!current.path.isEmpty() && !skip) {
            out.append(current);
        }
        current = {};
        skip = false;
    };
    for (const QByteArray &raw : porcelain.split('\n')) {
        const QString line = QString::fromUtf8(raw);
        if (line.isEmpty()) {
            flush();
        } else if (line.startsWith(QLatin1String("worktree "))) {
            flush();
            current.path = line.mid(9);
        } else if (line.startsWith(QLatin1String("branch "))) {
            current.branch = line.mid(7);
            current.branch.remove(QRegularExpression(QStringLiteral("^refs/heads/")));
        } else if (line == QLatin1String("bare") || line.startsWith(QLatin1String("prunable"))) {
            skip = true;
        }
    }
    flush();
    return out;
}

QList<Worktree> discover(const QString &dir)
{
    QProcess git;
    git.setWorkingDirectory(dir);
    git.start(QStringLiteral("git"), {QStringLiteral("worktree"), QStringLiteral("list"), QStringLiteral("--porcelain")});
    QList<Worktree> found;
    if (git.waitForFinished(5000) && git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0) {
        found = parseWorktreeList(git.readAllStandardOutput());
    } else {
        git.kill();
        git.waitForFinished(1000);
    }
    if (found.isEmpty()) {
        found.append({QDir::cleanPath(dir), QString()});
    }
    return found;
}

QList<Candidate> candidates(const QList<Worktree> &worktrees)
{
    QList<Candidate> out;
    QSet<QString> taken;
    for (const Worktree &w : worktrees) {
        QString name = QFileInfo(w.path).fileName();
        if (name.isEmpty()) {
            name = QStringLiteral("checkout");
        }
        if (taken.contains(name) && !w.branch.isEmpty()) {
            name += QLatin1Char('-') + QString(w.branch).replace(QLatin1Char('/'), QLatin1Char('-'));
        }
        const QString base = name;
        for (int n = 2; taken.contains(name); ++n) {
            name = base + QLatin1Char('-') + QString::number(n);
        }
        taken.insert(name);
        out.append({name, w.path, w.branch, true});
    }
    return out;
}

QJsonObject withCheckouts(QJsonObject root, const QList<Candidate> &picked)
{
    QJsonArray checkouts;
    for (const Candidate &c : picked) {
        if (c.checked) {
            checkouts.append(QJsonObject{{QStringLiteral("name"), c.name}, {QStringLiteral("path"), Paths::tilde(c.path)}});
        }
    }
    root.insert(QStringLiteral("checkouts"), checkouts);
    return root;
}

} // namespace Worktrees
