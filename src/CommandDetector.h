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
