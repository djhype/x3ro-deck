#pragma once

#include "TerminalProfile.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// One terminal emulator whose settings the built-in terminal can import.
// TerminalSettings holds the sources (TerminalSettings::builtinSources) and
// re-imports when a watched path changes. Implementations read the files
// themselves, without linking the terminal's libraries.
class TerminalSettingsSource
{
public:
    struct ProfileRef
    {
        QString id; // what load() and the config key terminalProfile take
        QString name; // what the Settings picker shows
    };

    virtual ~TerminalSettingsSource() = default;

    virtual QString id() const = 0; // config value of terminalSettings, e.g. "konsole"
    virtual QString displayName() const = 0;
    // Desktop entry ids and executable names that name this terminal as the
    // desktop's default (kdeglobals, xdg-terminals.list).
    virtual QStringList desktopEntries() const = 0;
    // XDG_CURRENT_DESKTOP values whose own terminal this is.
    virtual QStringList nativeDesktops() const { return {}; }
    // Whether the terminal is installed or has left settings behind.
    virtual bool detect() const = 0;
    virtual QList<ProfileRef> profiles() const = 0;
    virtual QString defaultProfile() const = 0;
    // The profile with this id; an empty id is the default profile. Nothing
    // when it does not exist.
    virtual std::optional<TerminalProfile> load(const QString &profile) const = 0;
    // Files and directories whose changes call for another load(). The
    // directories catch files that appear later or are replaced on save.
    virtual QStringList watchedPaths(const QString &profile) const = 0;
};

// KConfig-style INI: [Group] headers, key=value lines, # comments. Keys keep
// their case; localized keys (Name[de]) and $-flags are dropped, and KConfig's
// escapes (\s \t \n \\) are decoded. Commas stay part of the value, unlike
// with QSettings.
using IniGroups = QHash<QString, QHash<QString, QString>>;
IniGroups readIni(const QString &path, bool *ok = nullptr);
IniGroups parseIni(const QByteArray &text);
