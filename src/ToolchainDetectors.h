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
