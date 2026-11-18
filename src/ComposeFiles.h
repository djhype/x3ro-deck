// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

// Compose files in a checkout and the global options that pick them.
namespace ComposeFiles
{
// Compose files in `dir`: the base files compose itself looks for, in its
// order (compose.yaml, compose.yml, docker-compose.yaml, docker-compose.yml),
// then override files such as docker-compose.dev.yml, by name.
QStringList detect(const QString &dir);
// " -f a.yml -f b.yml --profile p --env-file .env -p name", each value quoted
// for the shell when it needs to be; empty when every part is empty.
QString options(const QStringList &files, const QStringList &profiles, const QString &envFile, const QString &project);
QString shellQuote(const QString &s);
} // namespace ComposeFiles
