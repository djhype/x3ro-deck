#include "ConfigMigrations.h"

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
