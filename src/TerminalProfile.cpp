// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TerminalProfile.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

namespace
{
QString g_windowsShell; // config "windowsShell"
}

void setWindowsShell(const QString &choice)
{
    g_windowsShell = choice;
}

QList<WindowsShell> windowsShells(const std::function<QString(const QString &)> &find)
{
    QList<WindowsShell> out;
    if (const QString pwsh = find(QStringLiteral("pwsh")); !pwsh.isEmpty()) {
        out.append({QStringLiteral("pwsh"), QStringLiteral("PowerShell 7"), {pwsh, QStringLiteral("-NoLogo")}});
    }
    if (const QString ps = find(QStringLiteral("powershell")); !ps.isEmpty()) {
        out.append({QStringLiteral("powershell"), QStringLiteral("Windows PowerShell"), {ps, QStringLiteral("-NoLogo")}});
    }
    if (const QString cmd = find(QStringLiteral("cmd")); !cmd.isEmpty()) {
        out.append({QStringLiteral("cmd"), QStringLiteral("Command Prompt (cmd)"), {cmd}});
    }
    // Git for Windows' bash, not WSL's bash.exe in System32.
    QString bash = find(QStringLiteral("Git/bin/bash"));
    if (bash.isEmpty()) {
        const QString onPath = find(QStringLiteral("bash"));
        if (!onPath.isEmpty() && !onPath.contains(QLatin1String("System32"), Qt::CaseInsensitive)) {
            bash = onPath;
        }
    }
    if (!bash.isEmpty()) {
        out.append({QStringLiteral("bash"), QStringLiteral("Git Bash"), {bash, QStringLiteral("--login"), QStringLiteral("-i")}});
    }
    return out;
}

QStringList windowsShellArgv(const QString &choice, const std::function<QString(const QString &)> &find)
{
    const QList<WindowsShell> shells = windowsShells(find);
    for (const WindowsShell &s : shells) {
        if (s.id == choice) {
            return s.argv;
        }
    }
    // The default: PowerShell 7, else Windows PowerShell, else cmd.
    return shells.isEmpty() ? QStringList{QStringLiteral("cmd.exe")} : shells.first().argv;
}

QString findWindowsProgram(const QString &name)
{
    if (name.contains(QLatin1Char('/'))) {
        for (const QString &root : {qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramW6432")}) {
            const QString path = root + QLatin1Char('/') + name + QStringLiteral(".exe");
            if (!root.isEmpty() && QFileInfo::exists(path)) {
                return QDir::toNativeSeparators(path);
            }
        }
        return {};
    }
    return QStandardPaths::findExecutable(name);
}

QString loginShell()
{
#if defined(Q_OS_WIN)
    return windowsShellArgv(g_windowsShell, findWindowsProgram).first();
#else
    const QString env = qEnvironmentVariable("SHELL");
    if (!env.isEmpty()) {
        return env;
    }
#ifdef Q_OS_UNIX
    if (const passwd *pw = getpwuid(getuid()); pw && pw->pw_shell && *pw->pw_shell) {
        return QString::fromLocal8Bit(pw->pw_shell);
    }
#endif
    return QStringLiteral("/bin/sh");
#endif
}

TerminalProfile TerminalProfile::fallback()
{
    TerminalProfile p;
    p.appearance = TerminalAppearance::fallback();
#if defined(Q_OS_WIN)
    p.shell = windowsShellArgv(g_windowsShell, findWindowsProgram);
#else
    p.shell = {loginShell()};
#endif
    return p;
}
