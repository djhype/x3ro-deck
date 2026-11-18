#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

// Finds the checkouts of a repository for the first-run welcome screen.
namespace Worktrees
{
struct Worktree
{
    QString path;
    QString branch; // empty when detached
};

struct Candidate
{
    QString name; // unique among the candidates
    QString path;
    QString branch;
    bool checked = true;
};

// `git worktree list --porcelain`, without bare and prunable entries. The
// main worktree comes first.
QList<Worktree> parseWorktreeList(const QByteArray &porcelain);
// The worktrees of the repository `dir` belongs to, or `dir` alone when it
// is not in one (or git is missing).
QList<Worktree> discover(const QString &dir);
// Names each worktree after its folder, adding the branch (or a number)
// where two folders share a name.
QList<Candidate> candidates(const QList<Worktree> &worktrees);
// `root` with `checkouts` set to the checked candidates; other keys are kept.
QJsonObject withCheckouts(QJsonObject root, const QList<Candidate> &picked);
} // namespace Worktrees
