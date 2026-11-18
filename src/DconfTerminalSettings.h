// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "TerminalSettingsSource.h"

#include <functional>

// GNOME-family terminals, which keep their settings in dconf. They read it
// with `dconf dump` (no GLib link dependency) and re-import when the user's
// dconf database file changes.
class DconfSource : public TerminalSettingsSource
{
public:
    // `dconf dump <dir>` output for a dconf directory such as "/org/gnome/terminal/".
    using Dumper = std::function<QByteArray(const QString &dir)>;
    struct Locations
    {
        Dumper dump;
        QString dconfDb; // ~/.config/dconf/user, watched for changes
        QString executable; // the terminal, empty when not installed
        static Locations system(const QString &program);
    };

    explicit DconfSource(Locations locations)
        : m_loc(std::move(locations))
    {
    }
    QStringList watchedPaths(const QString &profile) const override;

    // GVariant text as dconf prints it.
    static QString gString(const QString &value);
    static QStringList gList(const QString &value);
    // 'rgb(1,2,3)', 'rgba(…)', '#rgb', '#rrggbb' or '#rrrrggggbbbb'.
    static std::optional<QColor> gColor(const QString &value);
    // Pango's "Family Name Bold 11".
    static QFont pangoFont(const QString &description);

protected:
    IniGroups dump(const QString &dir) const;
    // The desktop's monospace font and whether it prefers dark.
    QFont systemMonospace() const;
    bool prefersDark() const;

    Locations m_loc;
};

// GNOME Console (kgx): the system or its own font, light or dark by its
// theme setting and the desktop's color scheme. One profile.
class GnomeConsoleSettings : public DconfSource
{
public:
    explicit GnomeConsoleSettings(Locations locations = Locations::system(QStringLiteral("kgx")))
        : DconfSource(std::move(locations))
    {
    }
    QString id() const override { return QStringLiteral("gnome-console"); }
    QString displayName() const override { return QStringLiteral("GNOME Console"); }
    QStringList desktopEntries() const override { return {QStringLiteral("org.gnome.Console"), QStringLiteral("kgx")}; }
    QStringList nativeDesktops() const override { return {QStringLiteral("GNOME")}; }
    bool detect() const override;
    QList<ProfileRef> profiles() const override;
    QString defaultProfile() const override { return QStringLiteral("default"); }
    std::optional<TerminalProfile> load(const QString &profile) const override;
};

// GNOME Terminal profiles under /org/gnome/terminal/legacy/profiles:/.
class GnomeTerminalSettings : public DconfSource
{
public:
    explicit GnomeTerminalSettings(Locations locations = Locations::system(QStringLiteral("gnome-terminal")))
        : DconfSource(std::move(locations))
    {
    }
    QString id() const override { return QStringLiteral("gnome-terminal"); }
    QString displayName() const override { return QStringLiteral("GNOME Terminal"); }
    QStringList desktopEntries() const override { return {QStringLiteral("org.gnome.Terminal"), QStringLiteral("gnome-terminal")}; }
    bool detect() const override;
    QList<ProfileRef> profiles() const override;
    QString defaultProfile() const override;
    std::optional<TerminalProfile> load(const QString &profile) const override;
};

// MATE Terminal profiles under /org/mate/terminal/profiles/.
class MateTerminalSettings : public DconfSource
{
public:
    explicit MateTerminalSettings(Locations locations = Locations::system(QStringLiteral("mate-terminal")))
        : DconfSource(std::move(locations))
    {
    }
    QString id() const override { return QStringLiteral("mate-terminal"); }
    QString displayName() const override { return QStringLiteral("MATE Terminal"); }
    QStringList desktopEntries() const override { return {QStringLiteral("mate-terminal")}; }
    QStringList nativeDesktops() const override { return {QStringLiteral("MATE")}; }
    bool detect() const override;
    QList<ProfileRef> profiles() const override;
    QString defaultProfile() const override;
    std::optional<TerminalProfile> load(const QString &profile) const override;
};

// Ptyxis profiles under /org/gnome/Ptyxis/Profiles/. Its palettes are
// compiled into the binary, so only the default "gnome" palette is known;
// other palette ids fall back to it.
class PtyxisSettings : public DconfSource
{
public:
    explicit PtyxisSettings(Locations locations = Locations::system(QStringLiteral("ptyxis")))
        : DconfSource(std::move(locations))
    {
    }
    QString id() const override { return QStringLiteral("ptyxis"); }
    QString displayName() const override { return QStringLiteral("Ptyxis"); }
    QStringList desktopEntries() const override { return {QStringLiteral("org.gnome.Ptyxis"), QStringLiteral("ptyxis")}; }
    bool detect() const override;
    QList<ProfileRef> profiles() const override;
    QString defaultProfile() const override;
    std::optional<TerminalProfile> load(const QString &profile) const override;
};
