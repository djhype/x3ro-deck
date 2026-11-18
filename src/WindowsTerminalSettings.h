// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "TerminalSettingsSource.h"

#include <QVariantMap>

#include <functional>

// Windows Terminal (stable or Preview): settings.json, JSON with comments, in
// the package's LocalState folder or the unpackaged path. Profiles merge
// profiles.defaults; colorScheme names an entry of schemes or a built-in.
// The reader is portable, so fixtures run on every OS.
class WindowsTerminalSettings : public TerminalSettingsSource
{
public:
    explicit WindowsTerminalSettings(bool preview = false, QStringList files = {});
    QString id() const override { return m_preview ? QStringLiteral("windows-terminal-preview") : QStringLiteral("windows-terminal"); }
    QString displayName() const override
    {
        return m_preview ? QStringLiteral("Windows Terminal Preview") : QStringLiteral("Windows Terminal");
    }
    QStringList desktopEntries() const override { return {id()}; }
    bool detect() const override;
    QList<ProfileRef> profiles() const override;
    QString defaultProfile() const override;
    std::optional<TerminalProfile> load(const QString &profile) const override;
    QStringList watchedPaths(const QString &profile) const override;

private:
    QString file() const;
    QVariantMap settings() const;
    bool m_preview;
    QStringList m_files; // candidates, first existing wins
};

// Console Host (conhost): its defaults under HKCU\Console.
class ConsoleHostSettings : public TerminalSettingsSource
{
public:
    // The registry values by name; tests pass their own.
    using Reader = std::function<QVariantMap()>;
    explicit ConsoleHostSettings(Reader read = {});
    QString id() const override { return QStringLiteral("conhost"); }
    QString displayName() const override { return QStringLiteral("Console Host"); }
    QStringList desktopEntries() const override { return {QStringLiteral("conhost")}; }
    bool detect() const override;
    QList<ProfileRef> profiles() const override { return {{defaultProfile(), QStringLiteral("Default")}}; }
    QString defaultProfile() const override { return QStringLiteral("default"); }
    std::optional<TerminalProfile> load(const QString &profile) const override;
    QStringList watchedPaths(const QString &profile) const override;

private:
    Reader m_read;
};
