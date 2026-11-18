#include "CommandDetector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace
{
QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool exists(const QString &dir, const QString &name)
{
    return QFileInfo::exists(QDir(dir).filePath(name));
}
} // namespace

const QList<std::shared_ptr<const CommandDetector>> &CommandDetector::all()
{
    static const QList<std::shared_ptr<const CommandDetector>> detectors{
        std::make_shared<MakefileDetector>(),
        std::make_shared<PackageJsonDetector>(),
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
            out.append({t, QStringLiteral("make ") + t, QStringLiteral("make"), name});
        }
        return out;
    }
    return {};
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
    QStringList names = scripts.keys();
    // QJsonObject sorts its keys; put them back in file order.
    const qsizetype start = std::max<qsizetype>(0, packageJson.indexOf("\"scripts\""));
    auto position = [&](const QString &name) {
        const QByteArray quoted = QJsonDocument(QJsonObject{{name, 0}}).toJson(QJsonDocument::Compact).mid(1).chopped(3);
        const qsizetype at = packageJson.indexOf(quoted, start);
        return at < 0 ? packageJson.size() : at;
    };
    std::stable_sort(names.begin(), names.end(), [&](const QString &a, const QString &b) { return position(a) < position(b); });
    return names;
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
        out.append({s, QStringLiteral("%1 run %2").arg(pm, s), pm, QStringLiteral("package.json")});
    }
    return out;
}
