#include "Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace
{
QString expandHome(QString path)
{
    if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/"))) {
        path.replace(0, 1, QDir::homePath());
    }
    QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QJsonObject defaults()
{
    auto checkout = [](const char *name, int offset) {
        return QJsonObject{{QStringLiteral("name"), QLatin1String(name)},
                           {QStringLiteral("path"), QStringLiteral("~/Development/") + QLatin1String(name)},
                           {QStringLiteral("portOffset"), offset}};
    };
    auto cmd = [](const char *label, const char *command, const char *group) {
        return QJsonObject{{QStringLiteral("label"), QLatin1String(label)},
                           {QStringLiteral("command"), QLatin1String(command)},
                           {QStringLiteral("group"), QLatin1String(group)}};
    };
    auto link = [](const char *label, const char *service, int port) {
        return QJsonObject{{QStringLiteral("label"), QLatin1String(label)},
                           {QStringLiteral("service"), QLatin1String(service)},
                           {QStringLiteral("containerPort"), port}};
    };
    return QJsonObject{
        {QStringLiteral("checkouts"), QJsonArray{checkout("example", 0), checkout("example-2", 10000), checkout("example-3", 20000)}},
        {QStringLiteral("quickCommands"),
         QJsonArray{cmd("Check", "make check", "check"),
                    cmd("Unit tests", "make test-unit", "check"),
                    cmd("Integration", "make test-integration-affected", "check"),
                    cmd("Type-check", "make type-check", "check"),
                    cmd("Sync env", "make sync-env", "env"),
                    cmd("Leapp up", "make leapp-up", "env"),
                    cmd("DB migrate", "make db-migrate", "data"),
                    cmd("Service logs", "make services-logs", "data")}},
        {QStringLiteral("links"),
         QJsonArray{link("windmill", "windmill-server", 8000),
                    link("maildev", "maildev", 1080),
                    link("minio console", "minio", 9001),
                    link("verdaccio", "verdaccio", 4873)}},
        {QStringLiteral("agentCommand"), QStringLiteral("claude")},
        {QStringLiteral("konsoleProfile"), QString()},
        {QStringLiteral("composeUp"), QStringLiteral("make services-up")},
        {QStringLiteral("composeDown"), QStringLiteral("make services-down")},
        {QStringLiteral("composeRestart"), QStringLiteral("make services-down && make services-up")},
        {QStringLiteral("permissionKeys"),
         QJsonObject{{QStringLiteral("allow"), QStringLiteral("1")},
                     {QStringLiteral("always"), QStringLiteral("2")},
                     {QStringLiteral("deny"), QStringLiteral("Escape")}}},
        {QStringLiteral("notifyOnDone"), true},
        {QStringLiteral("autostartAgents"), true},
    };
}
} // namespace

QString Config::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/x3ro-deck");
}

QString Config::configPath()
{
    return configDir() + QStringLiteral("/config.json");
}

Config Config::load(QString *error)
{
    QDir().mkpath(configDir());
    QFile file(configPath());
    if (!file.exists() && file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(defaults()).toJson(QJsonDocument::Indented));
        file.close();
    }

    QJsonObject root = defaults();
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (doc.isObject()) {
            const QJsonObject user = doc.object();
            for (auto it = user.begin(); it != user.end(); ++it) {
                root.insert(it.key(), it.value());
            }
        } else if (error) {
            *error = QStringLiteral("%1: %2 — using defaults").arg(configPath(), parseError.errorString());
        }
    }

    Config c;
    for (const QJsonValue &v : root.value(QStringLiteral("checkouts")).toArray()) {
        const QJsonObject o = v.toObject();
        CheckoutConfig cc;
        cc.name = o.value(QStringLiteral("name")).toString();
        cc.path = expandHome(o.value(QStringLiteral("path")).toString());
        cc.portOffset = o.value(QStringLiteral("portOffset")).toInt();
        if (!cc.name.isEmpty()) {
            c.checkouts.append(cc);
        }
    }
    for (const QJsonValue &v : root.value(QStringLiteral("quickCommands")).toArray()) {
        const QJsonObject o = v.toObject();
        c.quickCommands.append({o.value(QStringLiteral("label")).toString(),
                                o.value(QStringLiteral("command")).toString(),
                                o.value(QStringLiteral("group")).toString(QStringLiteral("check"))});
    }
    for (const QJsonValue &v : root.value(QStringLiteral("links")).toArray()) {
        const QJsonObject o = v.toObject();
        c.links.append({o.value(QStringLiteral("label")).toString(),
                        o.value(QStringLiteral("service")).toString(),
                        o.value(QStringLiteral("containerPort")).toInt()});
    }
    c.agentCommand = root.value(QStringLiteral("agentCommand")).toString(c.agentCommand);
    c.konsoleProfile = root.value(QStringLiteral("konsoleProfile")).toString();
    c.composeUp = root.value(QStringLiteral("composeUp")).toString(c.composeUp);
    c.composeDown = root.value(QStringLiteral("composeDown")).toString(c.composeDown);
    c.composeRestart = root.value(QStringLiteral("composeRestart")).toString(c.composeRestart);
    const QJsonObject keys = root.value(QStringLiteral("permissionKeys")).toObject();
    c.allowKey = keys.value(QStringLiteral("allow")).toString(c.allowKey);
    c.alwaysKey = keys.value(QStringLiteral("always")).toString(c.alwaysKey);
    c.denyKey = keys.value(QStringLiteral("deny")).toString(c.denyKey);
    c.notifyOnDone = root.value(QStringLiteral("notifyOnDone")).toBool(true);
    c.autostartAgents = root.value(QStringLiteral("autostartAgents")).toBool(true);
    return c;
}
