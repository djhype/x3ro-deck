#include "Config.h"

#include "ConfigMigrations.h"
#include "Paths.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace
{
QJsonArray defaultAgents()
{
    auto agent = [](const char *id, const char *label, const char *kind) {
        return QJsonObject{{QStringLiteral("id"), QLatin1String(id)},
                           {QStringLiteral("label"), QLatin1String(label)},
                           {QStringLiteral("kind"), QLatin1String(kind)}};
    };
    // A model on a local server, through Claude Code's Anthropic-compatible
    // client. ANTHROPIC_AUTH_TOKEN must stay set: without it Claude Code would
    // send the claude.ai login to the server. `local connect` prints the
    // values for a running server.
    const QString localModel = QStringLiteral("local-model");
    QJsonObject local = agent("local", "Local", "claude");
    local.insert(QStringLiteral("env"),
                QJsonObject{{QStringLiteral("ANTHROPIC_BASE_URL"), QStringLiteral("http://localhost:8080")},
                            {QStringLiteral("ANTHROPIC_AUTH_TOKEN"), QStringLiteral("local")},
                            {QStringLiteral("ANTHROPIC_MODEL"), localModel},
                            {QStringLiteral("ANTHROPIC_DEFAULT_OPUS_MODEL"), localModel},
                            {QStringLiteral("ANTHROPIC_DEFAULT_SONNET_MODEL"), localModel},
                            {QStringLiteral("ANTHROPIC_DEFAULT_HAIKU_MODEL"), localModel}});
    return QJsonArray{agent("claude", "Claude", "claude"), agent("codex", "Codex", "codex"), local};
}

QJsonObject defaults()
{
    return QJsonObject{
        {QStringLiteral("configVersion"), ConfigMigrations::currentVersion()},
        {QStringLiteral("checkouts"), QJsonArray{}},
        {QStringLiteral("quickCommands"), QJsonArray{}},
        {QStringLiteral("links"), QJsonArray{}},
        {QStringLiteral("agents"), defaultAgents()},
        {QStringLiteral("defaultAgent"), QStringLiteral("claude")},
        {QStringLiteral("dockerHost"), QString()},
        {QStringLiteral("composeBinary"), QStringLiteral("docker compose")},
        {QStringLiteral("composeUp"), QStringLiteral("{compose} up -d")},
        {QStringLiteral("composeDown"), QStringLiteral("{compose} down")},
        {QStringLiteral("composeRestart"), QStringLiteral("{compose} restart")},
        {QStringLiteral("notifyOnDone"), true},
        {QStringLiteral("autostartAgents"), true},
        {QStringLiteral("sessionBackend"), QStringLiteral("tmux")},
        {QStringLiteral("terminalScrollback"), 10000},
        {QStringLiteral("terminalSettings"), QString()},
        {QStringLiteral("terminalProfile"), QString()},
        {QStringLiteral("theme"), QStringLiteral("cyberdecker")},
    };
}

// "FOO=1 aider --model x" → "aider"
QString programName(const QString &command)
{
    static const QRegularExpression assignment(QStringLiteral(R"(^[A-Za-z_][A-Za-z0-9_]*=)"));
    for (const QString &word : command.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!assignment.match(word).hasMatch()) {
            return QFileInfo(word).fileName();
        }
    }
    return {};
}

AgentProfile parseAgent(const QJsonObject &user)
{
    const QString kind = user.value(QStringLiteral("kind")).toString(QStringLiteral("claude"));
    QJsonObject o = Config::kindDefaults(kind);
    for (auto it = user.begin(); it != user.end(); ++it) {
        o.insert(it.key(), it.value());
    }
    AgentProfile p;
    p.kind = kind == QLatin1String("codex") || kind == QLatin1String("custom") ? kind : QStringLiteral("claude");
    p.id = o.value(QStringLiteral("id")).toString(p.kind);
    p.label = o.value(QStringLiteral("label")).toString(p.id);
    p.command = o.value(QStringLiteral("command")).toString();
    p.continueArgs = o.value(QStringLiteral("continueArgs")).toString();
    p.process = o.value(QStringLiteral("process")).toString();
    if (p.process.isEmpty() && p.kind == QLatin1String("custom")) {
        p.process = programName(p.command);
    }
    const QJsonObject env = o.value(QStringLiteral("env")).toObject();
    for (auto it = env.begin(); it != env.end(); ++it) {
        p.env.append({it.key(), it.value().toString()});
    }
    const QJsonObject keys = o.value(QStringLiteral("permissionKeys")).toObject();
    p.allowKey = keys.value(QStringLiteral("allow")).toString();
    p.alwaysKey = keys.value(QStringLiteral("always")).toString();
    p.denyKey = keys.value(QStringLiteral("deny")).toString();
    p.prPrompt = o.value(QStringLiteral("prPrompt")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("prompts")).toArray()) {
        p.prompts.append(v.toString());
    }
    p.submitDelayMs = o.value(QStringLiteral("submitDelayMs")).toInt();
    return p;
}

// Splits on whitespace outside quotes, keeping each token's raw text so quoting survives.
QStringList shellTokens(const QString &line)
{
    QStringList tokens;
    QString cur;
    QChar quote;
    for (const QChar ch : line) {
        if (!quote.isNull()) {
            quote = ch == quote ? QChar() : quote;
        } else if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
        } else if (ch.isSpace()) {
            if (!cur.isEmpty()) {
                tokens.append(std::exchange(cur, QString()));
            }
            continue;
        }
        cur.append(ch);
    }
    if (!cur.isEmpty()) {
        tokens.append(cur);
    }
    return tokens;
}

bool isFlag(const QString &token)
{
    return token.size() > 1 && token.startsWith(QLatin1Char('-'));
}

QString flagName(const QString &token)
{
    return token.left(token.indexOf(QLatin1Char('=')) < 0 ? -1 : token.indexOf(QLatin1Char('=')));
}

// Drops every flag of `base` that `extra` also sets, together with its value token.
QStringList withoutOverridden(const QStringList &base, const QStringList &extra)
{
    QSet<QString> names;
    for (const QString &t : extra) {
        if (isFlag(t)) {
            names.insert(flagName(t));
        }
    }
    QStringList kept;
    for (qsizetype i = 0; i < base.size(); ++i) {
        if (i > 0 && isFlag(base.at(i)) && names.contains(flagName(base.at(i)))) {
            const bool hasValue = !base.at(i).contains(QLatin1Char('=')) && i + 1 < base.size() && !isFlag(base.at(i + 1));
            i += hasValue ? 1 : 0;
            continue;
        }
        kept.append(base.at(i));
    }
    return kept;
}
} // namespace

QString AgentProfile::launchLine(bool resume, const QString &extraArgs) const
{
    QStringList cmd = shellTokens(command);
    if (resume && !continueArgs.isEmpty()) {
        cmd += shellTokens(continueArgs);
    }
    const QStringList extra = shellTokens(extraArgs);
    return (withoutOverridden(cmd, extra) + extra).join(QLatin1Char(' '));
}

QMap<QString, QString> AgentProfile::sessionEnv(const QProcessEnvironment &from) const
{
    QMap<QString, QString> out;
    for (const auto &[name, value] : env) {
        out.insert(name, expandVariables(value, from));
    }
    return out;
}

QString expandVariables(const QString &value, const QProcessEnvironment &env)
{
    static const QRegularExpression var(QStringLiteral(R"(\$(?:\{([A-Za-z_][A-Za-z0-9_]*)\}|([A-Za-z_][A-Za-z0-9_]*)))"));
    QString out;
    qsizetype last = 0;
    for (auto it = var.globalMatch(value); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += value.mid(last, m.capturedStart() - last);
        out += env.value(m.hasCaptured(1) ? m.captured(1) : m.captured(2));
        last = m.capturedEnd();
    }
    return out + value.mid(last);
}

QJsonObject Config::kindDefaults(const QString &kind)
{
    if (kind == QLatin1String("codex")) {
        return QJsonObject{
            {QStringLiteral("command"), QStringLiteral("codex")},
            {QStringLiteral("continueArgs"), QStringLiteral("resume --last")},
            {QStringLiteral("process"), QStringLiteral("codex")},
            // y approves once, a approves for the rest of the session, Esc declines.
            {QStringLiteral("permissionKeys"),
             QJsonObject{{QStringLiteral("allow"), QStringLiteral("y")},
                         {QStringLiteral("always"), QStringLiteral("a")},
                         {QStringLiteral("deny"), QStringLiteral("Escape")}}},
            {QStringLiteral("prPrompt"), QString()},
            {QStringLiteral("prompts"), QJsonArray{QStringLiteral("/review")}},
            // Codex reads fast typed input as a paste and turns an Enter that
            // arrives within 120 ms of it into a newline instead of a submit.
            {QStringLiteral("submitDelayMs"), 300},
        };
    }
    if (kind == QLatin1String("custom")) {
        return QJsonObject{
            {QStringLiteral("command"), QString()},  {QStringLiteral("continueArgs"), QString()}, {QStringLiteral("process"), QString()},
            {QStringLiteral("prPrompt"), QString()}, {QStringLiteral("prompts"), QJsonArray{}},   {QStringLiteral("submitDelayMs"), 0},
        };
    }
    return QJsonObject{
        {QStringLiteral("command"), QStringLiteral("claude")},
        {QStringLiteral("continueArgs"), QStringLiteral("--continue")},
        {QStringLiteral("process"), QStringLiteral("claude")},
        {QStringLiteral("permissionKeys"),
         QJsonObject{{QStringLiteral("allow"), QStringLiteral("1")},
                     {QStringLiteral("always"), QStringLiteral("2")},
                     {QStringLiteral("deny"), QStringLiteral("Escape")}}},
        {QStringLiteral("prPrompt"), QStringLiteral("/create-pr")},
        {QStringLiteral("prompts"), QJsonArray{QStringLiteral("/full-review")}},
        {QStringLiteral("submitDelayMs"), 0},
    };
}

const AgentProfile &Config::agent(const QString &id) const
{
    for (const QString &want : {id, defaultAgent}) {
        for (const AgentProfile &p : agents) {
            if (p.id == want) {
                return p;
            }
        }
    }
    return agents.first();
}

QString Config::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/x3ro-deck");
}

QString Config::configPath()
{
    return configDir() + QStringLiteral("/config.json");
}

namespace
{
// Keeps the original bytes as config.json.v<from>.bak, then writes the
// migrated file. Without a backup it writes nothing.
bool writeMigrated(const QByteArray &original, int from, const QJsonObject &migrated, QString *error)
{
    const QString backup = Config::configPath() + QStringLiteral(".v%1.bak").arg(from);
    QSaveFile bak(backup);
    if (!bak.open(QIODevice::WriteOnly) || bak.write(original) < 0 || !bak.commit()) {
        *error = QStringLiteral("Could not back up %1 before upgrading it: %2").arg(Config::configPath(), bak.errorString());
        return false;
    }
    QSaveFile file(Config::configPath());
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(migrated).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        *error = QStringLiteral("Could not save the upgraded %1: %2").arg(Config::configPath(), file.errorString());
        return false;
    }
    return true;
}
} // namespace

QJsonObject Config::readJson(QString *error)
{
    QDir().mkpath(configDir());
    QFile file(configPath());
    if (!file.exists() && file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(defaults()).toJson(QJsonDocument::Indented));
        file.close();
    }

    QJsonObject root = defaults();
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray raw = file.readAll();
        file.close();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (doc.isObject()) {
            QJsonObject user = doc.object();
            const ConfigMigrations::Result migration = ConfigMigrations::run(user);
            if (migration.tooNew && error) {
                // Settings refuse to open while `error` is set, so nothing overwrites it.
                *error = QStringLiteral("%1 is version %2, newer than this X3RO Deck understands (%3). It is left as is; update X3RO "
                                        "Deck before changing settings.")
                             .arg(Paths::tilde(configPath()))
                             .arg(migration.from)
                             .arg(ConfigMigrations::currentVersion());
            } else if (migration.migrated) {
                user = migration.config;
                QString writeError;
                if (!writeMigrated(raw, migration.from, user, &writeError) && error) {
                    *error = writeError;
                }
            }
            for (auto it = user.begin(); it != user.end(); ++it) {
                root.insert(it.key(), it.value());
            }
        } else if (error) {
            *error = QStringLiteral("%1: %2 — using defaults").arg(configPath(), parseError.errorString());
        }
    }
    return root;
}

bool Config::writeJson(const QJsonObject &root, QString *error)
{
    QJsonObject stamped = root;
    stamped.insert(QStringLiteral("configVersion"), ConfigMigrations::currentVersion());
    QSaveFile file(configPath());
    if (file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(stamped).toJson(QJsonDocument::Indented)) >= 0 && file.commit()) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("%1: %2").arg(configPath(), file.errorString());
    }
    return false;
}

Config Config::load(QString *error)
{
    const QJsonObject root = readJson(error);
    Config c;
    for (const QJsonValue &v : root.value(QStringLiteral("checkouts")).toArray()) {
        const QJsonObject o = v.toObject();
        CheckoutConfig cc;
        cc.name = o.value(QStringLiteral("name")).toString();
        cc.path = Paths::normalize(o.value(QStringLiteral("path")).toString());
        cc.portOffset = o.value(QStringLiteral("portOffset")).toInt();
        cc.agent = o.value(QStringLiteral("agent")).toString();
        cc.args = o.value(QStringLiteral("args")).toString().trimmed();
        if (!cc.name.isEmpty()) {
            c.checkouts.append(cc);
        }
    }
    for (const QJsonValue &v : root.value(QStringLiteral("quickCommands")).toArray()) {
        const QJsonObject o = v.toObject();
        c.quickCommands.append({o.value(QStringLiteral("label")).toString(), o.value(QStringLiteral("command")).toString(),
                                o.value(QStringLiteral("group")).toString(QStringLiteral("check"))});
    }
    for (const QJsonValue &v : root.value(QStringLiteral("links")).toArray()) {
        const QJsonObject o = v.toObject();
        c.links.append({o.value(QStringLiteral("label")).toString(), o.value(QStringLiteral("service")).toString(),
                        o.value(QStringLiteral("containerPort")).toInt()});
    }
    for (const QJsonValue &v : root.value(QStringLiteral("agents")).toArray()) {
        if (v.isObject()) {
            c.agents.append(parseAgent(v.toObject()));
        }
    }
    if (c.agents.isEmpty()) {
        for (const QJsonValue &v : defaultAgents()) {
            c.agents.append(parseAgent(v.toObject()));
        }
    }
    c.defaultAgent = root.value(QStringLiteral("defaultAgent")).toString(c.agents.first().id);
    c.dockerHost = root.value(QStringLiteral("dockerHost")).toString().trimmed();
    c.composeBinary = root.value(QStringLiteral("composeBinary")).toString(c.composeBinary).trimmed();
    if (c.composeBinary.isEmpty()) {
        c.composeBinary = QStringLiteral("docker compose");
    }
    c.composeUp = root.value(QStringLiteral("composeUp")).toString(c.composeUp);
    c.composeDown = root.value(QStringLiteral("composeDown")).toString(c.composeDown);
    c.composeRestart = root.value(QStringLiteral("composeRestart")).toString(c.composeRestart);
    c.notifyOnDone = root.value(QStringLiteral("notifyOnDone")).toBool(true);
    c.autostartAgents = root.value(QStringLiteral("autostartAgents")).toBool(true);
    c.sessionBackend = root.value(QStringLiteral("sessionBackend")).toString(c.sessionBackend).trimmed();
    c.terminalScrollback = std::clamp(root.value(QStringLiteral("terminalScrollback")).toInt(c.terminalScrollback), 0, 1000000);
    c.terminalSettings = root.value(QStringLiteral("terminalSettings")).toString().trimmed();
    c.terminalProfile = root.value(QStringLiteral("terminalProfile")).toString().trimmed();
    c.theme = root.value(QStringLiteral("theme")).toString().trimmed();
    return c;
}
