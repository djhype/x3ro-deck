// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QtGlobal>

// Points the app's config (config.json, themes) and XDG_CONFIG_HOME at `dir`.
// macOS and Windows ignore XDG_CONFIG_HOME, so Config::configDir follows
// X3RO_DECK_CONFIG_DIR instead.
inline void setConfigHome(const QString &dir)
{
    qputenv("XDG_CONFIG_HOME", dir.toLocal8Bit());
    qputenv("X3RO_DECK_CONFIG_DIR", (dir + QStringLiteral("/x3ro-deck")).toLocal8Bit());
}
