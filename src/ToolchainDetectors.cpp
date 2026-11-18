// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

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

// ───────────────────────── pyproject ─────────────────────────

QStringList PyprojectDetector::files() const
{
    return {QStringLiteral("pyproject.toml"), QStringLiteral("uv.lock"), QStringLiteral("poetry.lock")};
}

QString PyprojectDetector::runner(const QString &dir)
{
    if (exists(dir, QStringLiteral("uv.lock"))) {
        return QStringLiteral("uv run ");
    }
    if (exists(dir, QStringLiteral("poetry.lock"))) {
        return QStringLiteral("poetry run ");
    }
    return {};
}

QList<DetectedCommand> PyprojectDetector::detect(const QString &dir) const
{
    const QString source = QStringLiteral("pyproject.toml");
    if (!exists(dir, source)) {
        return {};
    }
    const QString run = runner(dir);
    const QString group = run.isEmpty() ? QStringLiteral("python") : run.section(QLatin1Char(' '), 0, 0);
    QList<DetectedCommand> out;
    QStringList seen;
    auto add = [&](const QString &label, const QString &cmd, const QString &description = QString()) {
        if (!seen.contains(label)) {
            seen.append(label);
            out.append(command(label, run + cmd, group, source, description));
        }
    };
    static const QRegularExpression inlineHelp(QStringLiteral(R"re(\bhelp\s*=\s*("[^"]*"|'[^']*'))re"));
    QStringList tables;
    const QList<TomlEntry> entries = tomlEntries(readFile(QDir(dir).filePath(source)), &tables);
    for (const TomlEntry &e : entries) {
        if (e.table == QLatin1String("project.scripts") || e.table == QLatin1String("tool.poetry.scripts")) {
            add(e.key, e.key);
        } else if (e.table == QLatin1String("tool.poe.tasks")) {
            // name = "cmd" or name = { cmd = "…", help = "…" }
            const QRegularExpressionMatch h = inlineHelp.match(e.value);
            add(e.key, QStringLiteral("poe ") + e.key, h.hasMatch() ? unquote(h.captured(1)) : QString());
        }
    }
    // [tool.poe.tasks.name] tables, with their help line.
    for (const QString &t : std::as_const(tables)) {
        if (t.startsWith(QLatin1String("tool.poe.tasks.")) && t.count(QLatin1Char('.')) == 3) {
            const QString name = t.section(QLatin1Char('.'), 3);
            QString help;
            for (const TomlEntry &e : entries) {
                if (e.table == t && e.key == QLatin1String("help")) {
                    help = unquote(e.value);
                }
            }
            add(name, QStringLiteral("poe ") + name, help);
        }
    }
    return out;
}

// ───────────────────────── tox ─────────────────────────

QStringList ToxDetector::expandEnv(const QString &env)
{
    static const QRegularExpression brace(QStringLiteral(R"re(\{([^{}]*)\})re"));
    const QRegularExpressionMatch m = brace.match(env);
    if (!m.hasMatch()) {
        return {env.trimmed()};
    }
    QStringList out;
    for (const QString &alt : m.captured(1).split(QLatin1Char(','))) {
        QString next = env;
        next.replace(m.capturedStart(), m.capturedLength(), alt.trimmed());
        out += expandEnv(next);
    }
    return out;
}

QList<DetectedCommand> ToxDetector::detect(const QString &dir) const
{
    QStringList envs;
    QString source;
    auto addEnv = [&](const QString &list) {
        // Commas or newlines; braces may hold commas of their own.
        QString item;
        int depth = 0;
        for (const QChar c : list + QLatin1Char(',')) {
            if (c == QLatin1Char('{')) {
                ++depth;
            } else if (c == QLatin1Char('}')) {
                --depth;
            }
            if ((c == QLatin1Char(',') || c == QLatin1Char('\n')) && depth == 0) {
                for (const QString &e : expandEnv(item)) {
                    if (!e.isEmpty() && !envs.contains(e)) {
                        envs.append(e);
                    }
                }
                item.clear();
            } else {
                item += c;
            }
        }
    };
    if (exists(dir, QStringLiteral("tox.ini"))) {
        source = QStringLiteral("tox.ini");
        static const QRegularExpression section(QStringLiteral(R"re(^\[([^\]]+)\])re"));
        QString current;
        QString list;
        bool inList = false;
        for (const QString &line : QString::fromUtf8(readFile(QDir(dir).filePath(source))).split(QLatin1Char('\n'))) {
            if (const QRegularExpressionMatch s = section.match(line); s.hasMatch()) {
                current = s.captured(1).trimmed();
                inList = false;
                if (current.startsWith(QLatin1String("testenv:"))) {
                    addEnv(current.mid(8));
                }
                continue;
            }
            if (current != QLatin1String("tox")) {
                continue;
            }
            const QString t = line.trimmed();
            if (inList && !line.isEmpty() && line.front().isSpace()) {
                list += QLatin1Char('\n') + t;
                continue;
            }
            inList = false;
            if (t.startsWith(QLatin1String("envlist")) || t.startsWith(QLatin1String("env_list"))) {
                list = t.section(QLatin1Char('='), 1).trimmed();
                inList = true;
            }
        }
        // The envlist first, in its order, then [testenv:*] ones it left out.
        const QStringList sections = envs;
        envs.clear();
        addEnv(list);
        for (const QString &e : sections) {
            if (!envs.contains(e)) {
                envs.append(e);
            }
        }
    } else if (exists(dir, QStringLiteral("pyproject.toml"))) {
        QStringList tables;
        const QList<TomlEntry> entries = tomlEntries(readFile(QDir(dir).filePath(QStringLiteral("pyproject.toml"))), &tables);
        for (const TomlEntry &e : entries) {
            if (e.table == QLatin1String("tool.tox") && (e.key == QLatin1String("env_list") || e.key == QLatin1String("envlist"))) {
                for (const QJsonValue &v : QJsonDocument::fromJson(e.value.toUtf8()).array()) {
                    addEnv(v.toString());
                }
            }
        }
        for (const QString &t : std::as_const(tables)) {
            if (t.startsWith(QLatin1String("tool.tox.env.")) && t.count(QLatin1Char('.')) == 3) {
                addEnv(t.section(QLatin1Char('.'), 3));
            }
        }
        if (!envs.isEmpty()) {
            source = QStringLiteral("pyproject.toml");
        }
    }
    QList<DetectedCommand> out;
    for (const QString &e : std::as_const(envs)) {
        out.append(command(e, QStringLiteral("tox -e ") + e, QStringLiteral("tox"),
                           source == QLatin1String("tox.ini") ? source : QStringLiteral("pyproject.toml · tox")));
    }
    return out;
}

// ───────────────────────── nox ─────────────────────────

QList<DetectedCommand> NoxDetector::detect(const QString &dir) const
{
    const QString source = QStringLiteral("noxfile.py");
    if (!exists(dir, source)) {
        return {};
    }
    // @nox.session, @nox.session(python=…, name="x"), then def name(…).
    static const QRegularExpression session(
        QStringLiteral(R"re(@nox\.session(?:\s*\(([^)]*)\))?\s*\n(?:\s*@[^\n]*\n)*\s*def\s+([A-Za-z_][A-Za-z0-9_]*))re"));
    static const QRegularExpression named(QStringLiteral(R"re(\bname\s*=\s*["']([^"']+)["'])re"));
    QList<DetectedCommand> out;
    const QString text = QString::fromUtf8(readFile(QDir(dir).filePath(source)));
    for (auto it = session.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QRegularExpressionMatch n = named.match(m.captured(1));
        const QString name = n.hasMatch() ? n.captured(1) : m.captured(2);
        out.append(command(name, QStringLiteral("nox -s ") + name, QStringLiteral("nox"), source));
    }
    return out;
}

// ───────────────────────── Composer / artisan ─────────────────────────

QList<DetectedCommand> ComposerDetector::detect(const QString &dir) const
{
    const QString source = QStringLiteral("composer.json");
    if (!exists(dir, source)) {
        return {};
    }
    const QByteArray json = readFile(QDir(dir).filePath(source));
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonObject scripts = root.value(QStringLiteral("scripts")).toObject();
    const QJsonObject descriptions = root.value(QStringLiteral("scripts-descriptions")).toObject();
    QList<DetectedCommand> out;
    for (const QString &name : keysInFileOrder(json, scripts, "\"scripts\"")) {
        // Composer's own event hooks run by themselves, not from a button.
        if (name.startsWith(QLatin1String("pre-")) || name.startsWith(QLatin1String("post-"))) {
            continue;
        }
        out.append(
            command(name, QStringLiteral("composer run ") + name, QStringLiteral("composer"), source, descriptions.value(name).toString()));
    }
    return out;
}

QList<DetectedCommand> ArtisanDetector::detect(const QString &dir) const
{
    if (!exists(dir, QStringLiteral("artisan"))) {
        return {};
    }
    QList<DetectedCommand> out;
    for (const char *verb : {"serve", "migrate", "test", "queue:work"}) {
        out.append(command(QLatin1String(verb), QStringLiteral("php artisan ") + QLatin1String(verb), QStringLiteral("artisan"),
                           QStringLiteral("artisan")));
    }
    return out;
}

// ───────────────────────── Rake ─────────────────────────

QStringList RakeDetector::files() const
{
    return {QStringLiteral("Rakefile"), QStringLiteral("rakefile"), QStringLiteral("Rakefile.rb"), QStringLiteral("rakefile.rb")};
}

QList<DetectedCommand> RakeDetector::detect(const QString &dir) const
{
    const QString source = firstExisting(dir, files());
    if (source.isEmpty()) {
        return {};
    }
    // task :name, task name: [...], task "name" => ...
    static const QRegularExpression task(QStringLiteral(R"re(^(\s*)task\s*\(?\s*(?::(\w+)|["'](\w[\w:-]*)["']|(\w+):))re"));
    static const QRegularExpression ns(QStringLiteral(R"re(^(\s*)namespace\s*\(?\s*(?::(\w+)|["'](\w+)["']))re"));
    static const QRegularExpression desc(QStringLiteral(R"re(^\s*desc\s*\(?\s*["'](.*)["'])re"));
    static const QRegularExpression end(QStringLiteral(R"re(^(\s*)end\b)re"));
    struct Scope
    {
        int indent;
        QString name;
    };
    QList<Scope> scopes;
    QString pendingDesc;
    QList<DetectedCommand> out;
    for (const QString &line : QString::fromUtf8(readFile(QDir(dir).filePath(source))).split(QLatin1Char('\n'))) {
        if (const QRegularExpressionMatch e = end.match(line); e.hasMatch()) {
            if (!scopes.isEmpty() && e.capturedLength(1) == scopes.last().indent) {
                scopes.removeLast();
            }
            continue;
        }
        if (const QRegularExpressionMatch n = ns.match(line); n.hasMatch()) {
            scopes.append({static_cast<int>(n.capturedLength(1)), n.captured(2) + n.captured(3)});
            continue;
        }
        if (const QRegularExpressionMatch d = desc.match(line); d.hasMatch()) {
            pendingDesc = d.captured(1);
            continue;
        }
        if (const QRegularExpressionMatch t = task.match(line); t.hasMatch()) {
            QStringList parts;
            for (const Scope &s : std::as_const(scopes)) {
                parts.append(s.name);
            }
            parts.append(t.captured(2) + t.captured(3) + t.captured(4));
            const QString name = parts.join(QLatin1Char(':'));
            out.append(command(name, QStringLiteral("rake ") + name, QStringLiteral("rake"), source, pendingDesc));
            pendingDesc.clear();
        }
    }
    return out;
}

// ───────────────────────── Gradle / Maven ─────────────────────────

QStringList GradleDetector::files() const
{
    return {QStringLiteral("build.gradle.kts"), QStringLiteral("build.gradle"), QStringLiteral("gradlew")};
}

QList<DetectedCommand> GradleDetector::detect(const QString &dir) const
{
    const QString source = firstExisting(dir, {QStringLiteral("build.gradle.kts"), QStringLiteral("build.gradle")});
    if (source.isEmpty()) {
        return {};
    }
    const QString gradle = exists(dir, QStringLiteral("gradlew")) ? QStringLiteral("./gradlew") : QStringLiteral("gradle");
    const QString build = QString::fromUtf8(readFile(QDir(dir).filePath(source)));
    static const QRegularExpression application(
        QStringLiteral(R"re((\bid\s*\(?\s*["']application["']|apply\s+plugin\s*:\s*["']application["']|^\s*application\s*$))re"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression boot(QStringLiteral(R"re(org\.springframework\.boot)re"));
    QStringList tasks{QStringLiteral("build"), QStringLiteral("test")};
    if (application.match(build).hasMatch()) {
        tasks.append(QStringLiteral("run"));
    }
    if (boot.match(build).hasMatch()) {
        tasks.append(QStringLiteral("bootRun"));
    }
    QList<DetectedCommand> out;
    for (const QString &t : std::as_const(tasks)) {
        out.append(command(t, gradle + QLatin1Char(' ') + t, QStringLiteral("gradle"), source));
    }
    return out;
}

QList<DetectedCommand> MavenDetector::detect(const QString &dir) const
{
    if (!exists(dir, QStringLiteral("pom.xml"))) {
        return {};
    }
    const QString mvn = exists(dir, QStringLiteral("mvnw")) ? QStringLiteral("./mvnw") : QStringLiteral("mvn");
    QList<DetectedCommand> out;
    for (const char *phase : {"package", "test", "verify"}) {
        out.append(command(QLatin1String(phase), mvn + QLatin1Char(' ') + QLatin1String(phase), QStringLiteral("maven"),
                           QStringLiteral("pom.xml")));
    }
    return out;
}
