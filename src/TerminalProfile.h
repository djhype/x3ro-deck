// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "TerminalAppearance.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Settings imported from a terminal emulator's profile (see
// TerminalSettingsSource), or the app's own fallback.
struct TerminalProfile
{
    QString source; // the source's id, empty for the fallback
    QString name; // the profile's display name
    TerminalAppearance appearance;
    QStringList shell; // argv of the shell or command a terminal runs; never empty
    // History lines; -1 is unlimited, unset leaves the app's terminalScrollback.
    std::optional<int> scrollback;

    bool operator==(const TerminalProfile &) const = default;

    // The login shell, the system monospace font and the app's palette.
    static TerminalProfile fallback();
};

// $SHELL, else the passwd entry's shell, else /bin/sh. On Windows, the
// program of the configured Windows shell (see windowsShellArgv).
QString loginShell();

// Windows shells the Settings picker offers, as far as `find` (a program name,
// or "Git/bin/bash" under Program Files, to its path) finds them.
struct WindowsShell
{
    QString id; // config "windowsShell": pwsh | powershell | cmd | bash
    QString label;
    QStringList argv;
};
QList<WindowsShell> windowsShells(const std::function<QString(const QString &)> &find);
// The argv for config "windowsShell"; empty or unknown picks PowerShell 7,
// else Windows PowerShell, else cmd.
QStringList windowsShellArgv(const QString &choice, const std::function<QString(const QString &)> &find);
QString findWindowsProgram(const QString &name);
// Set from the config; the fallback profile and loginShell() use it on Windows.
void setWindowsShell(const QString &choice);
