// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QList>

#include <functional>

// Upgrades config.json between layouts. `configVersion` names the layout; a
// file without it is version 0. Migration i turns version i into i + 1 and
// works on the user's file only, before defaults are merged in.
namespace ConfigMigrations
{

using Migration = std::function<QJsonObject(QJsonObject)>;

// The migrations this build knows, in order. Their count is the current version.
const QList<Migration> &all();
int currentVersion();
int versionOf(const QJsonObject &config);

struct Result
{
    QJsonObject config;
    int from = 0;
    bool migrated = false; // config changed and should be written back
    bool tooNew = false; // written by a newer build; left untouched
};

// Runs every migration from `config`'s version up, stamping the version after
// each step.
Result run(const QJsonObject &config, const QList<Migration> &migrations = all());

} // namespace ConfigMigrations
