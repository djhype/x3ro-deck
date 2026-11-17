#pragma once

#include <QStringList>

// What a session backend runs for a shell argv from the imported terminal
// settings (TerminalProfile::shell).
struct SessionShell
{
    QString program; // absolute path
    QStringList argv; // argv[0] is the program as given, without the login dash
    // A shell given alone runs as a login shell (argv0 "-name"), as tmux and
    // login(1) start one: on a PTY it is interactive as well, so its rc files
    // set PATH before the agent command is typed in. An argv with arguments
    // runs exactly as given.
    bool login = false;

    // Falls back to $SHELL, the passwd entry's shell, then /bin/sh when
    // `shell` is empty or its program is missing or not executable. `path`
    // is the PATH a program without a slash is searched in.
    static SessionShell resolve(const QStringList &shell, const QString &path = qEnvironmentVariable("PATH"));
};
