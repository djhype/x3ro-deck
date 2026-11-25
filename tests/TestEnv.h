// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSettings>
#include <QString>
#include <QtGlobal>

// Points the app's config (config.json, themes), its QSettings and
// XDG_CONFIG_HOME at `dir`. macOS and Windows ignore XDG_CONFIG_HOME, so
// Config::configDir follows X3RO_DECK_CONFIG_DIR, and QSettings switch from
// the macOS plists and the Windows registry to INI files under `dir`.
inline void setConfigHome(const QString &dir)
{
    qputenv("XDG_CONFIG_HOME", dir.toLocal8Bit());
    qputenv("X3RO_DECK_CONFIG_DIR", (dir + QStringLiteral("/x3ro-deck")).toLocal8Bit());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
}
