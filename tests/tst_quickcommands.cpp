// CommandDetector and QuickCommands against fixture projects in
// tests/fixtures/quick.
#include "CommandDetector.h"
#include "Config.h"
#include "QuickCommands.h"
#include "ToolchainDetectors.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
const QString kFixture = QStringLiteral(X3RO_DECK_FIXTURES "/quick/make-pnpm");

void write(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(data);
}

QStringList labels(const QList<DetectedCommand> &list, const QString &source)
{
    QStringList out;
    for (const DetectedCommand &d : list) {
        if (d.source == source) {
            out << d.label;
        }
    }
    return out;
}
} // namespace

class TestQuickCommands : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("x3ro-deck-test"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_quickcommands"));
        QVERIFY(m_config.isValid());
        qputenv("XDG_CONFIG_HOME", m_config.path().toLocal8Bit());
    }

    void makefileTargets()
    {
        const QList<DetectedCommand> found = MakefileDetector().detect(kFixture);
        QCOMPARE(labels(found, QStringLiteral("Makefile")),
                 (QStringList{QStringLiteral("build"), QStringLiteral("test"), QStringLiteral("lint"), QStringLiteral("fmt"),
                              QStringLiteral("clean"), QStringLiteral("long")}));
        QCOMPARE(found.first().command, QStringLiteral("make build"));
        QCOMPARE(found.first().group, QStringLiteral("make"));
    }

    void makefileNamesInMakesOrder()
    {
        QTemporaryDir dir;
        write(dir.filePath(QStringLiteral("Makefile")), "a:\n");
        write(dir.filePath(QStringLiteral("GNUmakefile")), "b:\n");
        const QList<DetectedCommand> found = MakefileDetector().detect(dir.path());
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().label, QStringLiteral("b"));
        QCOMPARE(found.first().source, QStringLiteral("GNUmakefile"));
    }

    void packageScriptsInFileOrder()
    {
        const QList<DetectedCommand> found = PackageJsonDetector().detect(kFixture);
        QCOMPARE(labels(found, QStringLiteral("package.json")),
                 (QStringList{QStringLiteral("dev"), QStringLiteral("test"), QStringLiteral("build"), QStringLiteral("a:b")}));
        // The lockfile beats the packageManager field.
        QCOMPARE(found.at(1).command, QStringLiteral("pnpm run test"));
        QCOMPARE(found.at(1).group, QStringLiteral("pnpm"));
    }

    void packageManagerFromLockfiles_data()
    {
        QTest::addColumn<QString>("lockfile");
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("pm");
        QTest::newRow("yarn") << QStringLiteral("yarn.lock") << QByteArray("{}") << QStringLiteral("yarn");
        QTest::newRow("bun binary") << QStringLiteral("bun.lockb") << QByteArray("{}") << QStringLiteral("bun");
        QTest::newRow("bun text") << QStringLiteral("bun.lock") << QByteArray("{}") << QStringLiteral("bun");
        QTest::newRow("npm") << QStringLiteral("package-lock.json") << QByteArray("{}") << QStringLiteral("npm");
        QTest::newRow("declared") << QString() << QByteArray(R"({"packageManager": "pnpm@9.1.0"})") << QStringLiteral("pnpm");
        QTest::newRow("unknown") << QString() << QByteArray(R"({"packageManager": "deno@2"})") << QStringLiteral("npm");
        QTest::newRow("none") << QString() << QByteArray("{}") << QStringLiteral("npm");
    }

    void packageManagerFromLockfiles()
    {
        QFETCH(QString, lockfile);
        QFETCH(QByteArray, json);
        QFETCH(QString, pm);
        QTemporaryDir dir;
        if (!lockfile.isEmpty()) {
            write(dir.filePath(lockfile), "");
        }
        QCOMPARE(PackageJsonDetector::packageManager(dir.path(), json), pm);
    }

    void taskRunners()
    {
        const QString dir = QStringLiteral(X3RO_DECK_FIXTURES "/quick/runners");
        auto describe = [](const QList<DetectedCommand> &list) {
            QStringList out;
            for (const DetectedCommand &d : list) {
                out << d.label + QLatin1Char('|') + d.command + QLatin1Char('|') + d.description;
            }
            return out;
        };
        QCOMPARE(describe(JustfileDetector().detect(dir)),
                 (QStringList{QStringLiteral("build|just build|Build everything"), QStringLiteral("test|just test|Run the tests"),
                              QStringLiteral("serve|just serve|"), QStringLiteral("quiet|just quiet|")}));
        QCOMPARE(describe(TaskfileDetector().detect(dir)),
                 (QStringList{QStringLiteral("build|task build|Build the app"), QStringLiteral("lint:go|task lint:go|Lint Go code"),
                              QStringLiteral("test|task test|")}));
        QCOMPARE(
            describe(MiseDetector().detect(dir)),
            (QStringList{QStringLiteral("fmt|mise run fmt|"), QStringLiteral("check|mise run check|Type-check"),
                         QStringLiteral("build|mise run build|Build with esbuild"), QStringLiteral("db:migrate|mise run db:migrate|")}));

        // All three show up, each under its own file.
        QSet<QString> sources;
        for (const DetectedCommand &d : CommandDetector::detectAll(dir)) {
            sources.insert(d.source);
        }
        QCOMPARE(sources, (QSet<QString>{QStringLiteral("justfile"), QStringLiteral("Taskfile.yml"), QStringLiteral("mise.toml")}));
    }

    void toolchains()
    {
        auto describe = [](const QList<DetectedCommand> &list) {
            QStringList out;
            for (const DetectedCommand &d : list) {
                out << d.label + QLatin1Char('|') + d.command + QLatin1Char('|') + d.source + QLatin1Char('|') + d.description;
            }
            return out;
        };
        const QString fixtures = QStringLiteral(X3RO_DECK_FIXTURES "/quick/");
        QCOMPARE(describe(CargoDetector().detect(fixtures + QStringLiteral("cargo"))),
                 (QStringList{QStringLiteral("build|cargo build|Cargo.toml|"), QStringLiteral("test|cargo test|Cargo.toml|"),
                              QStringLiteral("run|cargo run|Cargo.toml|"), QStringLiteral("clippy|cargo clippy|Cargo.toml|"),
                              QStringLiteral("b|cargo b|.cargo/config.toml|cargo build"),
                              QStringLiteral("rr|cargo rr|.cargo/config.toml|cargo run --release"),
                              QStringLiteral("xtask|cargo xtask|.cargo/config.toml|cargo run --package xtask --")}));
        QCOMPARE(describe(GoDetector().detect(fixtures + QStringLiteral("go"))),
                 (QStringList{QStringLiteral("build|go build ./...|go.mod|"), QStringLiteral("test|go test ./...|go.mod|"),
                              QStringLiteral("vet|go vet ./...|go.mod|")}));
        QCOMPARE(describe(DenoDetector().detect(fixtures + QStringLiteral("deno"))),
                 (QStringList{QStringLiteral("dev|deno task dev|deno.jsonc|"),
                              QStringLiteral("check|deno task check|deno.jsonc|Type-check // not a comment"),
                              QStringLiteral("test|deno task test|deno.jsonc|")}));
        // Nothing where the project files are missing.
        QVERIFY(CargoDetector().detect(fixtures + QStringLiteral("go")).isEmpty());
        QVERIFY(GoDetector().detect(fixtures + QStringLiteral("cargo")).isEmpty());
        QVERIFY(DenoDetector().detect(fixtures + QStringLiteral("go")).isEmpty());
    }

    void renamedRecipeUpdatesLive()
    {
        QTemporaryDir dir;
        write(dir.filePath(QStringLiteral("justfile")), "build:\n    echo\n");
        QuickCommands quick;
        quick.setCheckouts({QStringLiteral("just")}, {dir.path()});
        QCOMPARE(labels(quick.detected(0), QStringLiteral("justfile")), QStringList{QStringLiteral("build")});
        write(dir.filePath(QStringLiteral("justfile")), "compile:\n    echo\n");
        QTRY_COMPARE_WITH_TIMEOUT(labels(quick.detected(0), QStringLiteral("justfile")), QStringList{QStringLiteral("compile")}, 1000);
    }

    void entriesMergePinAndHide()
    {
        QuickCommands quick;
        quick.setCheckouts({QStringLiteral("fixture")}, {kFixture});
        const QList<QuickCommand> manual{{QStringLiteral("Seed"), QStringLiteral("make seed"), QStringLiteral("data")}};
        QList<QuickEntry> e = quick.entries(0, manual);
        QCOMPARE(e.first().key, QStringLiteral("Manual:Seed"));
        QCOMPARE(e.at(1).source, QStringLiteral("Makefile"));
        QCOMPARE(e.last().source, QStringLiteral("package.json"));

        quick.setPinned(0, QStringLiteral("package.json:build"), true);
        quick.setHidden(0, QStringLiteral("Makefile:clean"), true);
        e = quick.entries(0, manual);
        QCOMPARE(e.first().key, QStringLiteral("package.json:build"));
        QVERIFY(e.first().pinned);
        QCOMPARE(e.at(1).key, QStringLiteral("Manual:Seed"));

        // Another instance (a restart) reads the same flags.
        QuickCommands again;
        again.setCheckouts({QStringLiteral("fixture")}, {kFixture});
        e = again.entries(0, manual);
        QCOMPARE(e.first().key, QStringLiteral("package.json:build"));
        const auto clean = std::find_if(e.begin(), e.end(), [](const QuickEntry &q) { return q.key == QLatin1String("Makefile:clean"); });
        QVERIFY(clean != e.end() && clean->hidden);

        again.setPinned(0, QStringLiteral("package.json:build"), false);
        again.setHidden(0, QStringLiteral("Makefile:clean"), false);
        QCOMPARE(again.entries(0, manual).first().key, QStringLiteral("Manual:Seed"));
    }

    void rescansWhenFilesChange()
    {
        QTemporaryDir dir;
        write(dir.filePath(QStringLiteral("package.json")), R"({"scripts": {"dev": "vite"}})");
        QuickCommands quick;
        quick.setCheckouts({QStringLiteral("live")}, {dir.path()});
        QCOMPARE(quick.detected(0).size(), 1);
        QSignalSpy changed(&quick, &QuickCommands::changed);

        QElapsedTimer timer;
        timer.start();
        write(dir.filePath(QStringLiteral("package.json")), R"({"scripts": {"dev": "vite", "test": "vitest"}})");
        QTRY_COMPARE_WITH_TIMEOUT(quick.detected(0).size(), 2, 1000);
        QVERIFY(timer.elapsed() < 1000);
        QCOMPARE(changed.size(), 1);

        // A file that appears, and one replaced by rename (how editors save).
        write(dir.filePath(QStringLiteral("Makefile")), "up:\n\techo\n");
        QTRY_COMPARE_WITH_TIMEOUT(quick.detected(0).size(), 3, 1000);
        write(dir.filePath(QStringLiteral("package.json.tmp")), R"({"scripts": {"x": "y"}})");
        QFile::remove(dir.filePath(QStringLiteral("package.json")));
        QVERIFY(QFile::rename(dir.filePath(QStringLiteral("package.json.tmp")), dir.filePath(QStringLiteral("package.json"))));
        QTRY_COMPARE_WITH_TIMEOUT(labels(quick.detected(0), QStringLiteral("package.json")), QStringList{QStringLiteral("x")}, 1000);
        write(dir.filePath(QStringLiteral("package.json")), R"({"scripts": {"x": "y", "z": "w"}})");
        QTRY_COMPARE_WITH_TIMEOUT(labels(quick.detected(0), QStringLiteral("package.json")).size(), 2, 1000);
    }

private:
    QTemporaryDir m_config;
};

QTEST_GUILESS_MAIN(TestQuickCommands)
#include "tst_quickcommands.moc"
