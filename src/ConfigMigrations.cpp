#include "ConfigMigrations.h"

#include <QJsonArray>

#include <tuple>

namespace ConfigMigrations
{

namespace
{
const QString kVersionKey = QStringLiteral("configVersion");
}

const QList<Migration> &all()
{
    static const QList<Migration> migrations{
        // v0 → v1: introduces configVersion itself; run() stamps it.
        [](QJsonObject c) { return c; },
        // v1 → v2: compose commands take {compose}, so composeBinary applies.
        [](QJsonObject c) {
            static const QLatin1String prefix("docker compose ");
            for (const char *key : {"composeUp", "composeDown", "composeRestart"}) {
                const QString cmd = c.value(QLatin1String(key)).toString();
                if (cmd.startsWith(prefix)) {
                    c.insert(QLatin1String(key), QStringLiteral("{compose} ") + cmd.mid(prefix.size()));
                }
            }
            return c;
        },
        // v2 → v3: the built-in terminal is the only one; konsolepart's switch
        // and profile are gone.
        [](QJsonObject c) {
            c.remove(QStringLiteral("terminal"));
            c.remove(QStringLiteral("konsoleProfile"));
            return c;
        },
        // v3 → v4: compose commands belong to each checkout. The globals the
        // file changed move into every checkout's `compose`; defaults stay
        // implicit there too.
        [](QJsonObject c) {
            QJsonObject compose;
            for (const auto &[from, to, def] :
                 {std::tuple{"composeUp", "up", "{compose} up -d"}, std::tuple{"composeDown", "down", "{compose} down"},
                  std::tuple{"composeRestart", "restart", "{compose} restart"}}) {
                const QString cmd = c.value(QLatin1String(from)).toString().trimmed();
                if (!cmd.isEmpty() && cmd != QLatin1String(def)) {
                    compose.insert(QLatin1String(to), cmd);
                }
                c.remove(QLatin1String(from));
            }
            if (!compose.isEmpty()) {
                QJsonArray checkouts = c.value(QStringLiteral("checkouts")).toArray();
                for (QJsonValueRef v : checkouts) {
                    QJsonObject o = v.toObject();
                    if (!o.contains(QStringLiteral("compose"))) {
                        o.insert(QStringLiteral("compose"), compose);
                    }
                    v = o;
                }
                c.insert(QStringLiteral("checkouts"), checkouts);
            }
            return c;
        },
    };
    return migrations;
}

int currentVersion()
{
    return static_cast<int>(all().size());
}

int versionOf(const QJsonObject &config)
{
    return config.value(kVersionKey).toInt(0);
}

Result run(const QJsonObject &config, const QList<Migration> &migrations)
{
    Result r;
    r.config = config;
    r.from = versionOf(config);
    const int target = static_cast<int>(migrations.size());
    if (r.from > target) {
        r.tooNew = true;
        return r;
    }
    for (int v = r.from; v < target; ++v) {
        r.config = migrations.at(v)(r.config);
        r.config.insert(kVersionKey, v + 1);
        r.migrated = true;
    }
    return r;
}

} // namespace ConfigMigrations
