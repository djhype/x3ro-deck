#pragma once

#include "TerminalAppearance.h"

#include <QString>
#include <QStringList>

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

// $SHELL, else the passwd entry's shell, else /bin/sh.
QString loginShell();
