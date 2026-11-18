// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

struct Config;
class QProcess;

// One external program the app runs.
struct Dependency
{
    QString id; // "git", "gh", "compose", "tmux", or the agent command's binary
    QString name; // "GitHub CLI (gh)"
    QString binary;
    QString purpose;
    QString hint; // how to install it
    QString path; // resolved; empty when missing
};

// Finds the programs the app needs on PATH and on the login shell's PATH.
// Agent commands are typed into a login shell, so a CLI installed through
// nvm or into ~/.local/bin counts as found even when the app's own PATH
// (from the desktop session) lacks it.
class Dependencies : public QObject
{
    Q_OBJECT
public:
    explicit Dependencies(QObject *parent = nullptr);

    // Rebuilds the list from the config and re-resolves it.
    void setConfig(const Config &config);
    // Reads the login shell's PATH in the background, then re-resolves.
    void probeLoginPath(const QStringList &shell);

    const QList<Dependency> &list() const { return m_list; }
    // Until the login shell's PATH is known, nothing counts as missing.
    bool ready() const { return m_ready; }
    // The entry for `id`, when it is known and missing.
    const Dependency *missing(const QString &id) const;

    // The program a shell command line starts: its first word, after any
    // leading NAME=value assignments.
    static QString commandBinary(const QString &command);
    // An absolute path for `binary`, searching `appPath` then `loginPath`.
    static QString resolve(const QString &binary, const QString &appPath, const QString &loginPath);
    // The PATH between the probe's markers in a shell's output.
    static QString parseProbe(const QByteArray &output);
    static QStringList probeArgs();

    // For tests: PATH as the app sees it.
    void setAppPath(const QString &path) { m_appPath = path; }
    void setLoginPath(const QString &path);

Q_SIGNALS:
    void changed();

private:
    void check();

    QList<Dependency> m_list;
    QString m_appPath;
    QString m_loginPath;
    bool m_ready = false;
    QProcess *m_probe = nullptr;
};
