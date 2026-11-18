#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

// A quick command found in a checkout's project files.
struct DetectedCommand
{
    QString label;
    QString command;
    QString group; // free-form; picks the button's accent colour
    QString source; // the file it came from, shown as the section header
    QString description; // the task's own doc line, when it has one
};

// Finds quick commands in one kind of project file. Register new detectors in
// CommandDetector::all().
class CommandDetector
{
public:
    virtual ~CommandDetector() = default;
    virtual QString id() const = 0;
    // File names (relative to the checkout) that detect() reads; changes to
    // them trigger a re-scan.
    virtual QStringList files() const = 0;
    virtual QList<DetectedCommand> detect(const QString &dir) const = 0;

    static const QList<std::shared_ptr<const CommandDetector>> &all();
    // Every detector's commands for `dir`, in registry order.
    static QList<DetectedCommand> detectAll(const QString &dir);
    static QStringList watchedFiles();
};

class MakefileDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("make"); }
    QStringList files() const override;
    QList<DetectedCommand> detect(const QString &dir) const override;
    // Rule targets in file order, without pattern, dot or variable targets.
    static QStringList targets(const QByteArray &makefile);
};

// Recipes in a justfile, parsed from the file (running `just` would block the
// scan). Private recipes (`_name`, [private]) and ones that need arguments
// are left out; a comment line above a recipe is its description.
class JustfileDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("just"); }
    QStringList files() const override;
    QList<DetectedCommand> detect(const QString &dir) const override;
    static QList<DetectedCommand> recipes(const QByteArray &justfile, const QString &source);
};

// go-task's Taskfile: the keys under `tasks:` with their `desc`, without
// `internal: true` tasks.
class TaskfileDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("task"); }
    QStringList files() const override;
    QList<DetectedCommand> detect(const QString &dir) const override;
    static QList<DetectedCommand> tasks(const QByteArray &taskfile, const QString &source);
};

// mise tasks: [tasks.name] tables and `name = "…"` entries under [tasks],
// with their `description`.
class MiseDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("mise"); }
    QStringList files() const override;
    QList<DetectedCommand> detect(const QString &dir) const override;
    static QList<DetectedCommand> tasks(const QByteArray &toml, const QString &source);
};

class PackageJsonDetector : public CommandDetector
{
public:
    QString id() const override { return QStringLiteral("package.json"); }
    QStringList files() const override;
    QList<DetectedCommand> detect(const QString &dir) const override;
    // npm, pnpm, yarn or bun, from the lockfile or the packageManager field.
    static QString packageManager(const QString &dir, const QByteArray &packageJson);
    // Script names in file order.
    static QStringList scripts(const QByteArray &packageJson);
};
