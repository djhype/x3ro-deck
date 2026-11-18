// ConfigMigrations::run and the migrate-on-load path in Config::readJson,
// against fixture files in tests/fixtures.

#include "Config.h"
#include "ConfigMigrations.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class ConfigMigrationsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void runsInOrderFromTheFilesVersion();
    void newerVersionIsUntouched();
    void currentVersionIsANoOp();
    void v0FileIsUpgradedWithBackup();
    void newerFileIsLeftAloneWithWarning();
    void v1ComposeCommandsUseThePlaceholder();
    void v3KeepsACheckoutsOwnCompose();
    void v2DropsTheKonsoleKeys();
    void freshInstallWritesCurrentVersion();
    void savingStampsTheVersion();

private:
    QString configFile() const { return m_tmp->path() + QStringLiteral("/x3ro-deck/config.json"); }
    void installFixture(const QString &name);
    static QByteArray read(const QString &path);

    std::unique_ptr<QTemporaryDir> m_tmp;
};

void ConfigMigrationsTest::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    qputenv("XDG_CONFIG_HOME", m_tmp->path().toLocal8Bit());
    QCOMPARE(Config::configPath(), configFile());
}

void ConfigMigrationsTest::installFixture(const QString &name)
{
    QVERIFY(QDir().mkpath(m_tmp->path() + QStringLiteral("/x3ro-deck")));
    QVERIFY(QFile::copy(QStringLiteral(X3RO_DECK_FIXTURES "/") + name, configFile()));
    QFile::setPermissions(configFile(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QByteArray ConfigMigrationsTest::read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void ConfigMigrationsTest::runsInOrderFromTheFilesVersion()
{
    auto step = [](const char *tag) {
        return [tag](QJsonObject c) {
            QJsonArray seen = c.value(QStringLiteral("seen")).toArray();
            seen.append(QLatin1String(tag));
            c.insert(QStringLiteral("seen"), seen);
            return c;
        };
    };
    const QList<ConfigMigrations::Migration> steps{step("0to1"), step("1to2"), step("2to3")};

    ConfigMigrations::Result r = ConfigMigrations::run(QJsonObject{}, steps);
    QVERIFY(r.migrated);
    QCOMPARE(r.from, 0);
    QCOMPARE(r.config.value(QStringLiteral("configVersion")).toInt(), 3);
    QCOMPARE(r.config.value(QStringLiteral("seen")).toArray(),
             (QJsonArray{QStringLiteral("0to1"), QStringLiteral("1to2"), QStringLiteral("2to3")}));

    r = ConfigMigrations::run(QJsonObject{{QStringLiteral("configVersion"), 1}}, steps);
    QCOMPARE(r.from, 1);
    QCOMPARE(r.config.value(QStringLiteral("seen")).toArray(), (QJsonArray{QStringLiteral("1to2"), QStringLiteral("2to3")}));
}

void ConfigMigrationsTest::newerVersionIsUntouched()
{
    const QJsonObject future{{QStringLiteral("configVersion"), 7}, {QStringLiteral("x"), 1}};
    const ConfigMigrations::Result r = ConfigMigrations::run(future);
    QVERIFY(r.tooNew);
    QVERIFY(!r.migrated);
    QCOMPARE(r.config, future);
}

void ConfigMigrationsTest::currentVersionIsANoOp()
{
    const QJsonObject current{{QStringLiteral("configVersion"), ConfigMigrations::currentVersion()}};
    const ConfigMigrations::Result r = ConfigMigrations::run(current);
    QVERIFY(!r.migrated);
    QVERIFY(!r.tooNew);
    QCOMPARE(r.config, current);
}

void ConfigMigrationsTest::v0FileIsUpgradedWithBackup()
{
    installFixture(QStringLiteral("config-v0.json"));
    const QByteArray original = read(configFile());

    QString error;
    const Config c = Config::load(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // The backup is the original file, byte for byte.
    QCOMPARE(read(configFile() + QStringLiteral(".v0.bak")), original);

    // The file now carries the version and still holds only the user's keys.
    const QJsonObject written = QJsonDocument::fromJson(read(configFile())).object();
    QCOMPARE(written.value(QStringLiteral("configVersion")).toInt(), ConfigMigrations::currentVersion());
    QVERIFY(written.contains(QStringLiteral("customKeyFromTheUser")));
    QVERIFY(!written.contains(QStringLiteral("composeUp")));

    // And it loads the same as before.
    QCOMPARE(c.checkouts.size(), 2);
    QCOMPARE(c.checkouts.at(0).agent, QStringLiteral("codex"));
    // v4 → v5 dropped portOffset.
    for (const QJsonValue &v : written.value(QStringLiteral("checkouts")).toArray()) {
        QVERIFY(!v.toObject().contains(QStringLiteral("portOffset")));
    }
    QCOMPARE(c.defaultAgent, QStringLiteral("codex"));
    QVERIFY(!c.autostartAgents);

    // A second load has nothing to do.
    const QByteArray upgraded = read(configFile());
    QFile::remove(configFile() + QStringLiteral(".v0.bak"));
    Config::load(&error);
    QVERIFY(error.isEmpty());
    QCOMPARE(read(configFile()), upgraded);
    QVERIFY(!QFile::exists(configFile() + QStringLiteral(".v0.bak")));
}

void ConfigMigrationsTest::newerFileIsLeftAloneWithWarning()
{
    installFixture(QStringLiteral("config-future.json"));
    const QByteArray original = read(configFile());

    QString error;
    const Config c = Config::load(&error);
    QVERIFY2(error.contains(QStringLiteral("newer")), qPrintable(error));
    QCOMPARE(read(configFile()), original);
    QCOMPARE(QDir(m_tmp->path() + QStringLiteral("/x3ro-deck")).entryList({QStringLiteral("*.bak")}, QDir::Files), QStringList());
    // What it can read still applies.
    QCOMPARE(c.checkouts.size(), 1);
}

void ConfigMigrationsTest::v1ComposeCommandsUseThePlaceholder()
{
    installFixture(QStringLiteral("config-v1.json"));
    QString error;
    const Config c = Config::load(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFile::exists(configFile() + QStringLiteral(".v1.bak")));
    // v3 → v4 then moves them onto the checkouts.
    QCOMPARE(c.checkouts.size(), 2);
    for (const CheckoutConfig &cc : c.checkouts) {
        QCOMPARE(cc.composeCommand(QStringLiteral("up")), QStringLiteral("{compose} up -d --build"));
        QCOMPARE(cc.composeCommand(QStringLiteral("down")), QStringLiteral("make down"));
        QCOMPARE(cc.composeCommand(QStringLiteral("restart")), QStringLiteral("{compose} restart"));
    }
    QCOMPARE(c.composeBinary, QStringLiteral("docker compose"));
    const QJsonObject written = QJsonDocument::fromJson(read(configFile())).object();
    for (const char *key : {"composeUp", "composeDown", "composeRestart"}) {
        QVERIFY(!written.contains(QLatin1String(key)));
    }
    const QJsonObject compose
        = written.value(QStringLiteral("checkouts")).toArray().at(0).toObject().value(QStringLiteral("compose")).toObject();
    QCOMPARE(compose,
             (QJsonObject{{QStringLiteral("up"), QStringLiteral("{compose} up -d --build")},
                          {QStringLiteral("down"), QStringLiteral("make down")}}));
}

void ConfigMigrationsTest::v3KeepsACheckoutsOwnCompose()
{
    const QJsonObject own{{QStringLiteral("up"), QStringLiteral("make up")}};
    const QJsonObject v3{{QStringLiteral("configVersion"), 3},
                         {QStringLiteral("composeUp"), QStringLiteral("{compose} up -d")},
                         {QStringLiteral("composeDown"), QStringLiteral("just down")},
                         {QStringLiteral("checkouts"),
                          QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("a")}},
                                     QJsonObject{{QStringLiteral("name"), QStringLiteral("b")}, {QStringLiteral("compose"), own}}}}};
    const ConfigMigrations::Result r = ConfigMigrations::run(v3);
    const QJsonArray checkouts = r.config.value(QStringLiteral("checkouts")).toArray();
    QCOMPARE(checkouts.at(0).toObject().value(QStringLiteral("compose")).toObject(),
             (QJsonObject{{QStringLiteral("down"), QStringLiteral("just down")}}));
    QCOMPARE(checkouts.at(1).toObject().value(QStringLiteral("compose")).toObject(), own);
    QVERIFY(!r.config.contains(QStringLiteral("composeDown")));
}

void ConfigMigrationsTest::v2DropsTheKonsoleKeys()
{
    installFixture(QStringLiteral("config-v2.json"));
    QString error;
    Config::load(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFile::exists(configFile() + QStringLiteral(".v2.bak")));
    const QJsonObject written = QJsonDocument::fromJson(read(configFile())).object();
    QVERIFY(!written.contains(QStringLiteral("terminal")));
    QVERIFY(!written.contains(QStringLiteral("konsoleProfile")));
    QCOMPARE(written.value(QStringLiteral("terminalScrollback")).toInt(), 5000);
}

void ConfigMigrationsTest::freshInstallWritesCurrentVersion()
{
    QString error;
    Config::load(&error);
    QVERIFY(error.isEmpty());
    const QJsonObject written = QJsonDocument::fromJson(read(configFile())).object();
    QCOMPARE(written.value(QStringLiteral("configVersion")).toInt(), ConfigMigrations::currentVersion());
}

void ConfigMigrationsTest::savingStampsTheVersion()
{
    QVERIFY(QDir().mkpath(m_tmp->path() + QStringLiteral("/x3ro-deck")));
    QVERIFY(Config::writeJson(QJsonObject{{QStringLiteral("checkouts"), QJsonArray{}}}));
    const QJsonObject written = QJsonDocument::fromJson(read(configFile())).object();
    QCOMPARE(written.value(QStringLiteral("configVersion")).toInt(), ConfigMigrations::currentVersion());
}

QTEST_GUILESS_MAIN(ConfigMigrationsTest)
#include "tst_configmigrations.moc"
