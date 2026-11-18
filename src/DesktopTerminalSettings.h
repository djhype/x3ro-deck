// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "TerminalSettingsSource.h"

#include <QVariant>

#include <functional>

// xfce4-terminal: the xfconf channel xfce4-terminal (live values through
// xfconf-query, else the XML file xfconfd persists), else the older
// ~/.config/xfce4/terminal/terminalrc. One profile.
class XfceTerminalSettings : public TerminalSettingsSource
{
public:
    // `xfconf-query -c xfce4-terminal -lv` output; empty when it is missing.
    using Query = std::function<QByteArray()>;
    struct Locations
    {
        Query query;
        QString configDir; // $XDG_CONFIG_HOME
        QString executable;
        static Locations system();
    };
    explicit XfceTerminalSettings(Locations locations = Locations::system())
        : m_loc(std::move(locations))
    {
    }
    QString id() const override { return QStringLiteral("xfce4-terminal"); }
    QString displayName() const override { return QStringLiteral("Xfce Terminal"); }
    QStringList desktopEntries() const override { return {QStringLiteral("xfce4-terminal")}; }
    QStringList nativeDesktops() const override { return {QStringLiteral("XFCE")}; }
    bool detect() const override;
    QList<ProfileRef> profiles() const override { return {{defaultProfile(), QStringLiteral("Default")}}; }
    QString defaultProfile() const override { return QStringLiteral("default"); }
    std::optional<TerminalProfile> load(const QString &profile) const override;
    QStringList watchedPaths(const QString &profile) const override;

    // Property name without the leading slash ("color-background") → value.
    QHash<QString, QString> properties() const;

private:
    QString xmlFile() const;
    QString rcFile() const;
    Locations m_loc;
};

// COSMIC Terminal's settings: one RON file per key under
// ~/.config/cosmic/com.system76.CosmicTerm/v1/. Profiles pick a color scheme
// by name; custom schemes come from color_schemes_dark/light.
class CosmicTerminalSettings : public TerminalSettingsSource
{
public:
    struct Locations
    {
        QString configDir;
        QString executable;
        static Locations system();
    };
    explicit CosmicTerminalSettings(Locations locations = Locations::system())
        : m_loc(std::move(locations))
    {
    }
    QString id() const override { return QStringLiteral("cosmic-term"); }
    QString displayName() const override { return QStringLiteral("COSMIC Terminal"); }
    QStringList desktopEntries() const override { return {QStringLiteral("com.system76.CosmicTerm"), QStringLiteral("cosmic-term")}; }
    QStringList nativeDesktops() const override { return {QStringLiteral("COSMIC")}; }
    bool detect() const override;
    QList<ProfileRef> profiles() const override;
    QString defaultProfile() const override;
    std::optional<TerminalProfile> load(const QString &profile) const override;
    QStringList watchedPaths(const QString &profile) const override;

    // A minimal RON reader: structs and maps become QVariantMap (map keys as
    // their text, e.g. "ProfileId(0)"), tuples and lists QVariantList,
    // Some(x) is x, None and unit are invalid QVariants.
    static QVariant parseRon(const QByteArray &ron);

private:
    QString dir() const;
    QVariant key(const QString &name) const;
    Locations m_loc;
};
