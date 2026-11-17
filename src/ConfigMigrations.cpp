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
