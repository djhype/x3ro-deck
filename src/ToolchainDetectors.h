#pragma once

#include "CommandDetector.h"

// Quick commands that come with a language's own toolchain.

// Cargo.toml: build, test, run and clippy, plus the [alias] entries of
// .cargo/config.toml.
class CargoDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("cargo"); }
    QStringList files() const override;
    QList<DetectedCommand> detect(const QString &dir) const override;
};

// go.mod: build, test and vet over ./...
class GoDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("go"); }
    QStringList files() const override { return {QStringLiteral("go.mod")}; }
    QList<DetectedCommand> detect(const QString &dir) const override;
};

// deno.json / deno.jsonc tasks.
class DenoDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("deno"); }
    QStringList files() const override { return {QStringLiteral("deno.json"), QStringLiteral("deno.jsonc")}; }
    QList<DetectedCommand> detect(const QString &dir) const override;
};

// pyproject.toml: [project.scripts], [tool.poetry.scripts] and
// [tool.poe.tasks], run through `uv run` or `poetry run` when the project's
// lockfile says which.
class PyprojectDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("pyproject"); }
    QStringList files() const override;
    QList<DetectedCommand> detect(const QString &dir) const override;
    // "uv run ", "poetry run " or empty.
    static QString runner(const QString &dir);
};

// tox environments from tox.ini's envlist and [testenv:*] sections, or
// pyproject's [tool.tox] (env_list and [tool.tox.env.*]).
class ToxDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("tox"); }
    QStringList files() const override { return {QStringLiteral("tox.ini"), QStringLiteral("pyproject.toml")}; }
    QList<DetectedCommand> detect(const QString &dir) const override;
    // "py{311,312}-lint" → py311-lint, py312-lint
    static QStringList expandEnv(const QString &env);
};

// nox sessions: functions decorated with @nox.session in noxfile.py.
class NoxDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("nox"); }
    QStringList files() const override { return {QStringLiteral("noxfile.py")}; }
    QList<DetectedCommand> detect(const QString &dir) const override;
};
