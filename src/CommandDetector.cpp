// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CommandDetector.h"

#include "DetectorUtil.h"
#include "ToolchainDetectors.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace DetectorUtil;

const QList<std::shared_ptr<const CommandDetector>> &CommandDetector::all()
{
    static const QList<std::shared_ptr<const CommandDetector>> detectors{
        std::make_shared<MakefileDetector>(), std::make_shared<PackageJsonDetector>(), std::make_shared<JustfileDetector>(),
        std::make_shared<TaskfileDetector>(), std::make_shared<MiseDetector>(),        std::make_shared<CargoDetector>(),
        std::make_shared<GoDetector>(),       std::make_shared<DenoDetector>(),        std::make_shared<PyprojectDetector>(),
        std::make_shared<ToxDetector>(),      std::make_shared<NoxDetector>(),         std::make_shared<ComposerDetector>(),
        std::make_shared<ArtisanDetector>(),  std::make_shared<RakeDetector>(),        std::make_shared<GradleDetector>(),
        std::make_shared<MavenDetector>(),
    };
    return detectors;
}

QList<DetectedCommand> CommandDetector::detectAll(const QString &dir)
{
    QList<DetectedCommand> out;
    for (const auto &d : all()) {
        out += d->detect(dir);
    }
    return out;
}

QStringList CommandDetector::watchedFiles()
{
    QStringList out;
    for (const auto &d : all()) {
        out += d->files();
    }
    out.removeDuplicates();
    return out;
}

// ───────────────────────── Makefile ─────────────────────────

QStringList MakefileDetector::files() const
{
    // The order GNU make looks for them in.
    return {QStringLiteral("GNUmakefile"), QStringLiteral("makefile"), QStringLiteral("Makefile")};
}

QStringList MakefileDetector::targets(const QByteArray &makefile)
{
    // A rule line: targets, then ':' or '::' that does not start an
    // assignment (':=', '::='). Recipes start with a tab and never match.
    static const QRegularExpression rule(QStringLiteral(R"(^([^\s#=:][^#=:]*?)\s*::?(?!=))"));
    QStringList out;
    QSet<QString> seen;
    bool continued = false;
    for (const QString &line : QString::fromUtf8(makefile).split(QLatin1Char('\n'))) {
        const bool wasContinued = continued;
        continued = line.endsWith(QLatin1Char('\\'));
        if (wasContinued || line.startsWith(QLatin1Char('\t'))) {
            continue;
        }
        const QRegularExpressionMatch m = rule.match(line);
        if (!m.hasMatch()) {
            continue;
        }
        const QString head = m.captured(1);
        if (head.startsWith(QLatin1String("export ")) || head.startsWith(QLatin1String("override "))) {
            continue;
        }
        for (const QString &t : head.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts)) {
            if (t.startsWith(QLatin1Char('.')) || t.contains(QLatin1Char('%')) || t.contains(QLatin1Char('$')) || seen.contains(t)) {
                continue;
            }
            seen.insert(t);
            out.append(t);
        }
    }
    return out;
}

QList<DetectedCommand> MakefileDetector::detect(const QString &dir) const
{
    for (const QString &name : files()) {
        if (!exists(dir, name)) {
            continue;
        }
        QList<DetectedCommand> out;
        for (const QString &t : targets(readFile(QDir(dir).filePath(name)))) {
            out.append({t, QStringLiteral("make ") + t, QStringLiteral("make"), name, QString()});
        }
        return out;
    }
    return {};
}

// ───────────────────────── justfile ─────────────────────────

QStringList JustfileDetector::files() const
{
    return {QStringLiteral("justfile"), QStringLiteral("Justfile"), QStringLiteral(".justfile")};
}

QList<DetectedCommand> JustfileDetector::recipes(const QByteArray &justfile, const QString &source)
{
    // name, then parameters, then ':' that is not ':=' (an assignment).
    static const QRegularExpression header(
        QStringLiteral(R"(^@?([A-Za-z_][A-Za-z0-9_-]*)((?:\s+[^:=\s]+(?:=(?:"[^"]*"|'[^']*'|[^\s:]+))?)*)\s*:(?!=))"));
    static const QRegularExpression keyword(QStringLiteral(R"(^(set|alias|export|import|mod)\b)"));
    QList<DetectedCommand> out;
    QString comment;
    bool isPrivate = false;
    for (const QString &line : QString::fromUtf8(justfile).split(QLatin1Char('\n'))) {
        if (line.isEmpty() || line.front().isSpace()) {
            if (line.trimmed().isEmpty()) {
                comment.clear();
                isPrivate = false;
            }
            continue;
        }
        if (line.startsWith(QLatin1Char('#'))) {
            comment = line.mid(1).trimmed();
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            isPrivate = isPrivate || line.contains(QLatin1String("private"));
            continue;
        }
        const QRegularExpressionMatch m = header.match(line);
        if (m.hasMatch() && !keyword.match(line).hasMatch()) {
            const QString name = m.captured(1);
            // A parameter without a default (other than *variadic) needs an argument.
            bool needsArgs = false;
            for (const QString &p : m.captured(2).split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts)) {
                needsArgs = needsArgs || (!p.contains(QLatin1Char('=')) && !p.startsWith(QLatin1Char('*')));
            }
            if (!name.startsWith(QLatin1Char('_')) && !isPrivate && !needsArgs) {
                out.append({name, QStringLiteral("just ") + name, QStringLiteral("just"), source, comment});
            }
        }
        comment.clear();
        isPrivate = false;
    }
    return out;
}

QList<DetectedCommand> JustfileDetector::detect(const QString &dir) const
{
    const QString name = firstExisting(dir, files());
    return name.isEmpty() ? QList<DetectedCommand>{} : recipes(readFile(QDir(dir).filePath(name)), name);
}

// ───────────────────────── Taskfile ─────────────────────────

QStringList TaskfileDetector::files() const
{
    return {QStringLiteral("Taskfile.yml"),  QStringLiteral("taskfile.yml"),      QStringLiteral("Taskfile.yaml"),
            QStringLiteral("taskfile.yaml"), QStringLiteral("Taskfile.dist.yml"), QStringLiteral("Taskfile.dist.yaml")};
}

QList<DetectedCommand> TaskfileDetector::tasks(const QByteArray &taskfile, const QString &source)
{
    static const QRegularExpression key(QStringLiteral(R"(^(\s*)("[^"]+"|'[^']+'|[^\s#:][^:#]*?)\s*:(\s|$))"));
    QList<DetectedCommand> out;
    bool inTasks = false;
    int taskIndent = -1;
    bool internal = false;
    auto flush = [&] {
        if (internal && !out.isEmpty()) {
            out.removeLast();
        }
        internal = false;
    };
    for (const QString &line : QString::fromUtf8(taskfile).split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty() || line.trimmed().startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QRegularExpressionMatch m = key.match(line);
        const int lead = static_cast<int>(line.indexOf(line.trimmed().front()));
        if (lead == 0) {
            flush();
            inTasks = m.hasMatch() && m.captured(2) == QLatin1String("tasks");
            taskIndent = -1;
            continue;
        }
        if (!inTasks || !m.hasMatch()) {
            continue;
        }
        if (taskIndent < 0) {
            taskIndent = lead;
        }
        const QString name = unquote(m.captured(2));
        if (lead == taskIndent) {
            flush();
            out.append({name, QStringLiteral("task ") + name, QStringLiteral("task"), source, QString()});
        } else if (lead > taskIndent && !out.isEmpty()) {
            const QString value = unquote(line.mid(m.capturedEnd(0)));
            if (name == QLatin1String("desc") && out.last().description.isEmpty()) {
                out.last().description = value;
            } else if (name == QLatin1String("internal") && value == QLatin1String("true")) {
                internal = true;
            }
        }
    }
    flush();
    return out;
}

QList<DetectedCommand> TaskfileDetector::detect(const QString &dir) const
{
    const QString name = firstExisting(dir, files());
    return name.isEmpty() ? QList<DetectedCommand>{} : tasks(readFile(QDir(dir).filePath(name)), name);
}

// ───────────────────────── mise ─────────────────────────

QStringList MiseDetector::files() const
{
    return {QStringLiteral("mise.toml"), QStringLiteral(".mise.toml")};
}

QList<DetectedCommand> MiseDetector::tasks(const QByteArray &toml, const QString &source)
{
    static const QRegularExpression table(QStringLiteral(R"(^\[\s*([^\]]+?)\s*\]\s*(#.*)?$)"));
    static const QRegularExpression entry(QStringLiteral(R"(^("[^"]+"|'[^']+'|[A-Za-z0-9_.:-]+)\s*=\s*(.*)$)"));
    QList<DetectedCommand> out;
    enum { Other, TasksTable, OneTask } where = Other;
    auto add = [&](const QString &name) {
        out.append({name, QStringLiteral("mise run ") + name, QStringLiteral("mise"), source, QString()});
    };
    for (const QString &raw : QString::fromUtf8(toml).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (const QRegularExpressionMatch t = table.match(line); t.hasMatch() && !line.startsWith(QLatin1String("[["))) {
            const QString path = t.captured(1);
            if (path == QLatin1String("tasks")) {
                where = TasksTable;
            } else if (path.startsWith(QLatin1String("tasks."))) {
                const QString name = unquote(path.mid(6));
                // [tasks.name.something] is a sub-table of a task already listed.
                if (!path.mid(6).startsWith(QLatin1Char('"')) && name.contains(QLatin1Char('.'))) {
                    where = Other;
                    continue;
                }
                add(name);
                where = OneTask;
            } else {
                where = Other;
            }
            continue;
        }
        const QRegularExpressionMatch e = entry.match(line);
        if (!e.hasMatch()) {
            continue;
        }
        if (where == TasksTable) {
            const QString name = unquote(e.captured(1));
            add(name);
            // name = { run = "…", description = "…" }
            static const QRegularExpression inlineDesc(QStringLiteral(R"(description\s*=\s*("[^"]*"|'[^']*'))"));
            if (const QRegularExpressionMatch d = inlineDesc.match(e.captured(2)); d.hasMatch()) {
                out.last().description = unquote(d.captured(1));
            }
        } else if (where == OneTask && unquote(e.captured(1)) == QLatin1String("description")) {
            out.last().description = unquote(e.captured(2));
        }
    }
    return out;
}

QList<DetectedCommand> MiseDetector::detect(const QString &dir) const
{
    const QString name = firstExisting(dir, files());
    return name.isEmpty() ? QList<DetectedCommand>{} : tasks(readFile(QDir(dir).filePath(name)), name);
}

// ───────────────────────── package.json ─────────────────────────

QStringList PackageJsonDetector::files() const
{
    return {QStringLiteral("package.json"), QStringLiteral("pnpm-lock.yaml"), QStringLiteral("yarn.lock"),
            QStringLiteral("bun.lockb"),    QStringLiteral("bun.lock"),       QStringLiteral("package-lock.json")};
}

QString PackageJsonDetector::packageManager(const QString &dir, const QByteArray &packageJson)
{
    static const QList<QPair<QString, QString>> lockfiles{{QStringLiteral("pnpm-lock.yaml"), QStringLiteral("pnpm")},
                                                          {QStringLiteral("yarn.lock"), QStringLiteral("yarn")},
                                                          {QStringLiteral("bun.lockb"), QStringLiteral("bun")},
                                                          {QStringLiteral("bun.lock"), QStringLiteral("bun")},
                                                          {QStringLiteral("package-lock.json"), QStringLiteral("npm")}};
    for (const auto &[file, pm] : lockfiles) {
        if (exists(dir, file)) {
            return pm;
        }
    }
    // "packageManager": "pnpm@9.1.0"
    const QString declared = QJsonDocument::fromJson(packageJson).object().value(QStringLiteral("packageManager")).toString();
    const QString name = declared.section(QLatin1Char('@'), 0, 0).trimmed();
    for (const char *pm : {"pnpm", "yarn", "bun", "npm"}) {
        if (name == QLatin1String(pm)) {
            return name;
        }
    }
    return QStringLiteral("npm");
}

QStringList PackageJsonDetector::scripts(const QByteArray &packageJson)
{
    const QJsonObject scripts = QJsonDocument::fromJson(packageJson).object().value(QStringLiteral("scripts")).toObject();
    return keysInFileOrder(packageJson, scripts, "\"scripts\"");
}

QList<DetectedCommand> PackageJsonDetector::detect(const QString &dir) const
{
    const QString file = QDir(dir).filePath(QStringLiteral("package.json"));
    if (!QFileInfo::exists(file)) {
        return {};
    }
    const QByteArray json = readFile(file);
    const QString pm = packageManager(dir, json);
    QList<DetectedCommand> out;
    for (const QString &s : scripts(json)) {
        out.append({s, QStringLiteral("%1 run %2").arg(pm, s), pm, QStringLiteral("package.json"), QString()});
    }
    return out;
}
