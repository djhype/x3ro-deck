#pragma once

#include "TerminalSettingsSource.h"

// Terminals with one plain-text config file and no profiles: kitty, Ghostty
// and Alacritty. Each offers a single profile, "default".
class PlainConfigSource : public TerminalSettingsSource
{
public:
    struct Locations
    {
        QString configDir; // $XDG_CONFIG_HOME
        QString home;
        QStringList dataDirs; // where installed themes live (Ghostty's share/ghostty/themes)
        QString executable; // empty when not installed
        static Locations system(const QString &program);
    };

    explicit PlainConfigSource(Locations locations)
        : m_loc(std::move(locations))
    {
    }

    bool detect() const override;
    QList<ProfileRef> profiles() const override;
    QString defaultProfile() const override { return QStringLiteral("default"); }
    QStringList watchedPaths(const QString &profile) const override;

    // The main config file, empty when there is none.
    virtual QString configFile() const = 0;
    // Every file load() reads: the config, its includes and the theme.
    virtual QStringList filesRead() const = 0;

protected:
    Locations m_loc;
};

// kitty.conf, following include/globinclude lines (current-theme.conf among them).
class KittySettings : public PlainConfigSource
{
public:
    explicit KittySettings(Locations locations = Locations::system(QStringLiteral("kitty")))
        : PlainConfigSource(std::move(locations))
    {
    }
    QString id() const override { return QStringLiteral("kitty"); }
    QString displayName() const override { return QStringLiteral("kitty"); }
    QStringList desktopEntries() const override { return {QStringLiteral("kitty")}; }
    std::optional<TerminalProfile> load(const QString &profile) const override;
    QString configFile() const override;
    QStringList filesRead() const override;
};

// Ghostty's config (config or config.ghostty), its config-file includes and
// the `theme`, looked up in the user's themes and Ghostty's installed ones.
class GhosttySettings : public PlainConfigSource
{
public:
    explicit GhosttySettings(Locations locations = Locations::system(QStringLiteral("ghostty")))
        : PlainConfigSource(std::move(locations))
    {
    }
    QString id() const override { return QStringLiteral("ghostty"); }
    QString displayName() const override { return QStringLiteral("Ghostty"); }
    QStringList desktopEntries() const override { return {QStringLiteral("com.mitchellh.ghostty"), QStringLiteral("ghostty")}; }
    std::optional<TerminalProfile> load(const QString &profile) const override;
    QString configFile() const override;
    QStringList filesRead() const override;
    QString themeFile(const QString &name) const;
};

// alacritty.toml with its import (or general.import) files.
class AlacrittySettings : public PlainConfigSource
{
public:
    explicit AlacrittySettings(Locations locations = Locations::system(QStringLiteral("alacritty")))
        : PlainConfigSource(std::move(locations))
    {
    }
    QString id() const override { return QStringLiteral("alacritty"); }
    QString displayName() const override { return QStringLiteral("Alacritty"); }
    QStringList desktopEntries() const override { return {QStringLiteral("Alacritty"), QStringLiteral("alacritty")}; }
    std::optional<TerminalProfile> load(const QString &profile) const override;
    QString configFile() const override;
    QStringList filesRead() const override;
};
