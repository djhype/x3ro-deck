#include "ToolchainDetectors.h"

#include "DetectorUtil.h"

#include <QJsonArray>

using namespace DetectorUtil;

namespace
{
DetectedCommand command(const QString &label, const QString &command, const QString &group, const QString &source,
                        const QString &description = QString())
{
    return {label, command, group, source, description};
}
} // namespace

// ───────────────────────── Cargo ─────────────────────────

QStringList CargoDetector::files() const
{
    return {QStringLiteral("Cargo.toml"), QStringLiteral(".cargo/config.toml"), QStringLiteral(".cargo/config")};
}

QList<DetectedCommand> CargoDetector::detect(const QString &dir) const
{
    if (!exists(dir, QStringLiteral("Cargo.toml"))) {
        return {};
    }
    const QString source = QStringLiteral("Cargo.toml");
    QList<DetectedCommand> out;
    for (const char *verb : {"build", "test", "run", "clippy"}) {
        out.append(command(QLatin1String(verb), QStringLiteral("cargo ") + QLatin1String(verb), QStringLiteral("cargo"), source));
    }
    const QString config = firstExisting(dir, {QStringLiteral(".cargo/config.toml"), QStringLiteral(".cargo/config")});
    if (!config.isEmpty()) {
        for (const TomlEntry &e : tomlEntries(readFile(QDir(dir).filePath(config)))) {
            if (e.table != QLatin1String("alias")) {
                continue;
            }
            // b = "build" or rr = ["run", "--release"]
            QString expands = e.value;
            if (expands.startsWith(QLatin1Char('['))) {
                QStringList words;
                for (const QJsonValue &v : QJsonDocument::fromJson(expands.toUtf8()).array()) {
                    words.append(v.toString());
                }
                expands = words.join(QLatin1Char(' '));
            } else {
                expands = unquote(expands);
            }
            out.append(
                command(e.key, QStringLiteral("cargo ") + e.key, QStringLiteral("cargo"), config, QStringLiteral("cargo ") + expands));
        }
    }
    return out;
}

// ───────────────────────── Go ─────────────────────────

QList<DetectedCommand> GoDetector::detect(const QString &dir) const
{
    if (!exists(dir, QStringLiteral("go.mod"))) {
        return {};
    }
    QList<DetectedCommand> out;
    for (const char *verb : {"build", "test", "vet"}) {
        out.append(command(QLatin1String(verb), QStringLiteral("go %1 ./...").arg(QLatin1String(verb)), QStringLiteral("go"),
                           QStringLiteral("go.mod")));
    }
    return out;
}

// ───────────────────────── Deno ─────────────────────────

QList<DetectedCommand> DenoDetector::detect(const QString &dir) const
{
    const QString name = firstExisting(dir, files());
    if (name.isEmpty()) {
        return {};
    }
    const QByteArray json = stripJsonc(readFile(QDir(dir).filePath(name)));
    const QJsonObject tasks = QJsonDocument::fromJson(json).object().value(QStringLiteral("tasks")).toObject();
    QList<DetectedCommand> out;
    for (const QString &task : keysInFileOrder(json, tasks, "\"tasks\"")) {
        // "dev": "deno run main.ts" or "dev": {"command": "…", "description": "…"}
        const QJsonValue v = tasks.value(task);
        const QString description = v.isObject() ? v.toObject().value(QStringLiteral("description")).toString() : QString();
        out.append(command(task, QStringLiteral("deno task ") + task, QStringLiteral("deno"), name, description));
    }
    return out;
}
