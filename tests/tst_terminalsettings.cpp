#include "KonsoleSettings.h"
#include "TerminalSettings.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
const QString kFixtures = QStringLiteral(X3RO_DECK_FIXTURES "/konsole");

KonsoleSettings::Locations fixtureLocations(const QString &root = kFixtures)
{
    return {root + QStringLiteral("/config"), {root + QStringLiteral("/data")}, QString()};
}

const QColor kBreezeBackground(35, 38, 39);
const QColor kBreezeForeground(252, 252, 252);

// A second terminal, for picking among sources.
class FakeSource : public TerminalSettingsSource
{
public:
    QString id() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    QStringList desktopEntries() const override { return {QStringLiteral("org.example.fake")}; }
    bool detect() const override { return true; }
    QList<ProfileRef> profiles() const override { return {{QStringLiteral("only"), QStringLiteral("Only")}}; }
    QString defaultProfile() const override { return QStringLiteral("only"); }
    std::optional<TerminalProfile> load(const QString &) const override
    {
        TerminalProfile p = TerminalProfile::fallback();
        p.source = id();
        p.name = QStringLiteral("Only");
        return p;
    }
    QStringList watchedPaths(const QString &) const override { return {}; }
};

bool writeFile(const QString &path, const QByteArray &text)
{
    // Like KConfig: a new file renamed over the old one.
    QFile tmp(path + QStringLiteral(".new"));
    if (!tmp.open(QIODevice::WriteOnly) || tmp.write(text) != text.size()) {
        return false;
    }
    tmp.close();
    QFile::remove(path);
    return QFile::rename(tmp.fileName(), path);
}

bool copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    const QFileInfoList entries = QDir(from).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &e : entries) {
        const QString target = QDir(to).filePath(e.fileName());
        if (e.isDir() ? !copyTree(e.filePath(), target) : !QFile::copy(e.filePath(), target)) {
            return false;
        }
    }
    return true;
}
} // namespace

class TerminalSettingsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void iniKeepsCommasAndDropsTranslations();
    void konsoleListsProfiles();
    void konsoleDefaultProfileInheritsParent();
    void konsoleColorScheme();
    void konsoleQt5FontString();
    void konsoleDefaults();
    void konsoleUnknownSchemeIsBreeze();
    void konsoleBuiltinProfile();
    void konsoleMissingProfile();
    void konsoleWithoutDefaultProfile();
    void noTerminalFallsBack();
    void desktopDefault();
    void reimportsOnChange();
};

void TerminalSettingsTest::iniKeepsCommasAndDropsTranslations()
{
    const IniGroups g = parseIni("top=1\n# comment\n[A B]\nColor=1,2,3\nName=x\nName[de]=y\nPath[$e]=$HOME\nText=a\\sb\\\\c\n");
    QCOMPARE(g.value(QString()).value(QStringLiteral("top")), QStringLiteral("1"));
    QCOMPARE(g.value(QStringLiteral("A B")).value(QStringLiteral("Color")), QStringLiteral("1,2,3"));
    QCOMPARE(g.value(QStringLiteral("A B")).value(QStringLiteral("Name")), QStringLiteral("x"));
    QCOMPARE(g.value(QStringLiteral("A B")).value(QStringLiteral("Path")), QStringLiteral("$HOME"));
    QCOMPARE(g.value(QStringLiteral("A B")).value(QStringLiteral("Text")), QStringLiteral("a b\\c"));
}

void TerminalSettingsTest::konsoleListsProfiles()
{
    const KonsoleSettings konsole(fixtureLocations());
    QVERIFY(konsole.detect());
    QCOMPARE(konsole.defaultProfile(), QStringLiteral("Work.profile"));
    QStringList names;
    QStringList ids;
    for (const auto &p : konsole.profiles()) {
        names << p.name;
        ids << p.id;
    }
    QCOMPARE(names, (QStringList{"Built-in", "Base", "Missing Scheme", "Plain", "Work Profile"}));
    QCOMPARE(ids, (QStringList{"FALLBACK/", "Base.profile", "Missing.profile", "Plain.profile", "Work.profile"}));
}

void TerminalSettingsTest::konsoleDefaultProfileInheritsParent()
{
    const KonsoleSettings konsole(fixtureLocations());
    const std::optional<TerminalProfile> p = konsole.load(QString());
    QVERIFY(p);
    QCOMPARE(p->source, QStringLiteral("konsole"));
    QCOMPARE(p->name, QStringLiteral("Work Profile"));
    const TerminalAppearance &a = p->appearance;
    QCOMPARE(a.font.family(), QStringLiteral("Fixture Mono"));
    QCOMPARE(a.font.pointSize(), 13);
    QCOMPARE(a.cursor, QColor(1, 2, 3));
    QVERIFY(a.cursorBlink);
    // From Base.profile, found by file name although Parent= names another home.
    QCOMPARE(a.cursorShape, TerminalAppearance::Underline);
    QCOMPARE(a.padding, 6);
    QCOMPARE(a.lineSpacing, 2);
    QCOMPARE(a.background, QColor(16, 16, 32));
    QCOMPARE(p->scrollback, std::optional<int>(5000));
    QCOMPARE(p->shell, (QStringList{"/bin/fish", "-l"}));
    QCOMPARE(konsole.load(QStringLiteral("Work Profile"))->name, p->name); // by display name too

    const QStringList watched = konsole.watchedPaths(QString());
    QVERIFY(watched.contains(kFixtures + QStringLiteral("/config/konsolerc")));
    QVERIFY(watched.contains(kFixtures + QStringLiteral("/data/konsole/Work.profile")));
    QVERIFY(watched.contains(kFixtures + QStringLiteral("/data/konsole/Base.profile")));
    QVERIFY(watched.contains(kFixtures + QStringLiteral("/data/konsole/Neon.colorscheme")));
}

void TerminalSettingsTest::konsoleColorScheme()
{
    const KonsoleSettings konsole(fixtureLocations());
    const TerminalAppearance a = konsole.load(QStringLiteral("Base.profile"))->appearance;
    QCOMPARE(a.foreground, QColor(0xe0, 0xe0, 0xf0));
    QCOMPARE(a.background, QColor(16, 16, 32));
    for (int i = 0; i < 8; ++i) {
        QCOMPARE(a.palette[size_t(i)], QColor(10 + i, 20 + i, 30 + i));
    }
    for (int i = 0; i < 7; ++i) {
        QCOMPARE(a.palette[size_t(i + 8)], QColor(110 + i, 120 + i, 130 + i));
    }
    QCOMPARE(a.palette[15], a.palette[7]); // no Color7Intense
    QCOMPARE(a.cursor, a.foreground); // no custom cursor color
    QCOMPARE(a.cursorShape, TerminalAppearance::Underline);
    QVERIFY(!a.cursorBlink);
    QVERIFY(a.boldIsBright);
}

void TerminalSettingsTest::konsoleQt5FontString()
{
    const KonsoleSettings konsole(fixtureLocations());
    const QFont f = konsole.load(QStringLiteral("Base.profile"))->appearance.font;
    QCOMPARE(f.family(), QStringLiteral("DejaVu Sans Mono"));
    QCOMPARE(f.pointSize(), 11);
    QCOMPARE(f.weight(), QFont::Normal); // Qt 5's 50
    QVERIFY(!f.italic());
}

void TerminalSettingsTest::konsoleDefaults()
{
    const KonsoleSettings konsole(fixtureLocations());
    const std::optional<TerminalProfile> p = konsole.load(QStringLiteral("Plain.profile"));
    QVERIFY(p);
    QCOMPARE(p->name, QStringLiteral("Plain"));
    QCOMPARE(p->appearance.background, kBreezeBackground);
    QCOMPARE(p->appearance.foreground, kBreezeForeground);
    QCOMPARE(p->appearance.padding, 1);
    QCOMPARE(p->appearance.lineSpacing, 0);
    QCOMPARE(p->appearance.cursorShape, TerminalAppearance::Block);
    QVERIFY(!p->appearance.boldIsBright);
    QVERIFY(!p->scrollback); // the app's own setting stays
    QCOMPARE(p->shell, QStringList{loginShell()});
}

void TerminalSettingsTest::konsoleUnknownSchemeIsBreeze()
{
    const KonsoleSettings konsole(fixtureLocations());
    const std::optional<TerminalProfile> p = konsole.load(QStringLiteral("Missing.profile"));
    QVERIFY(p);
    QCOMPARE(p->appearance.background, kBreezeBackground);
    QCOMPARE(p->appearance.palette[1], QColor(237, 21, 21));
    QCOMPARE(p->appearance.palette[9], QColor(192, 57, 43));
    QCOMPARE(p->scrollback, std::optional<int>(-1)); // unlimited
}

void TerminalSettingsTest::konsoleBuiltinProfile()
{
    const KonsoleSettings konsole(fixtureLocations());
    const std::optional<TerminalProfile> p = konsole.load(KonsoleSettings::builtinProfile());
    QVERIFY(p);
    QCOMPARE(p->name, QStringLiteral("Built-in"));
    QCOMPARE(p->appearance.background, kBreezeBackground);
}

void TerminalSettingsTest::konsoleMissingProfile()
{
    const KonsoleSettings konsole(fixtureLocations());
    QVERIFY(!konsole.load(QStringLiteral("Gone.profile")));
}

void TerminalSettingsTest::konsoleWithoutDefaultProfile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("config"))));
    QVERIFY(writeFile(dir.filePath(QStringLiteral("config/konsolerc")), "[TabBar]\nTabBarPosition=Bottom\n"));
    const KonsoleSettings konsole(fixtureLocations(dir.path()));
    QVERIFY(konsole.detect()); // konsolerc alone shows Konsole was used
    QCOMPARE(konsole.defaultProfile(), KonsoleSettings::builtinProfile());
    QCOMPARE(konsole.load(QString())->name, QStringLiteral("Built-in"));
}

void TerminalSettingsTest::noTerminalFallsBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TerminalSettings::Sources sources;
    sources.push_back(std::make_unique<KonsoleSettings>(fixtureLocations(dir.path())));
    TerminalSettings settings(std::move(sources), dir.path());
    QVERIFY(!settings.source(QStringLiteral("konsole"))->detect());
    QVERIFY(!settings.desktopDefault());
    for (const QString &source : {QString(), QStringLiteral("konsole"), TerminalSettings::none(), QStringLiteral("unknown")}) {
        settings.select(source, QString());
        QCOMPARE(settings.current(), TerminalProfile::fallback());
    }
    QCOMPARE(settings.current().appearance, TerminalAppearance::fallback());
    QCOMPARE(settings.current().shell, QStringList{loginShell()});
}

void TerminalSettingsTest::desktopDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_CURRENT_DESKTOP", "KDE");
    auto make = [&dir] {
        TerminalSettings::Sources sources;
        sources.push_back(std::make_unique<FakeSource>());
        sources.push_back(std::make_unique<KonsoleSettings>(fixtureLocations()));
        return std::make_unique<TerminalSettings>(std::move(sources), dir.path());
    };
    // Plasma without a kdeglobals choice: its own terminal.
    QCOMPARE(make()->desktopDefault()->id(), QStringLiteral("konsole"));
    QVERIFY(writeFile(dir.filePath(QStringLiteral("kdeglobals")),
                      "[General]\nTerminalApplication=fake\nTerminalService=org.example.fake.desktop\n"));
    QCOMPARE(make()->desktopDefault()->id(), QStringLiteral("fake"));
    QVERIFY(writeFile(dir.filePath(QStringLiteral("kdeglobals")), "[General]\nTerminalApplication=/usr/bin/konsole\n"));
    QCOMPARE(make()->desktopDefault()->id(), QStringLiteral("konsole"));
    QVERIFY(QFile::remove(dir.filePath(QStringLiteral("kdeglobals"))));
    QVERIFY(writeFile(dir.filePath(QStringLiteral("xdg-terminals.list")), "# mine\n-org.kde.konsole.desktop\norg.example.fake.desktop\n"));
    QCOMPARE(make()->desktopDefault()->id(), QStringLiteral("fake"));
    QVERIFY(QFile::remove(dir.filePath(QStringLiteral("xdg-terminals.list"))));
    // Elsewhere, the first terminal found.
    qputenv("XDG_CURRENT_DESKTOP", "GNOME");
    QCOMPARE(make()->desktopDefault()->id(), QStringLiteral("fake"));

    auto settings = make();
    settings->select(QString(), QString());
    QCOMPARE(settings->current().source, QStringLiteral("fake"));
    settings->select(QStringLiteral("konsole"), QStringLiteral("Gone.profile")); // falls back to the default profile
    QCOMPARE(settings->current().name, QStringLiteral("Work Profile"));
    qunsetenv("XDG_CURRENT_DESKTOP");
}

void TerminalSettingsTest::reimportsOnChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(copyTree(kFixtures, dir.path()));
    TerminalSettings::Sources sources;
    sources.push_back(std::make_unique<KonsoleSettings>(fixtureLocations(dir.path())));
    TerminalSettings settings(std::move(sources), dir.path());
    settings.select(QStringLiteral("konsole"), QString());
    QCOMPARE(settings.current().appearance.font.family(), QStringLiteral("Fixture Mono"));

    QSignalSpy changed(&settings, &TerminalSettings::changed);
    const QString work = dir.filePath(QStringLiteral("data/konsole/Work.profile"));
    QFile in(work);
    QVERIFY(in.open(QIODevice::ReadOnly));
    const QByteArray text = in.readAll();
    in.close();
    QByteArray edited = text;
    edited.replace("Fixture Mono,13", "Other Mono,15");
    QVERIFY(writeFile(work, edited));
    QVERIFY(changed.wait(1000));
    QCOMPARE(settings.current().appearance.font.family(), QStringLiteral("Other Mono"));
    QCOMPARE(settings.current().appearance.font.pointSize(), 15);

    // The renamed-over file is watched again, and so is the inherited scheme.
    const QString scheme = dir.filePath(QStringLiteral("data/konsole/Neon.colorscheme"));
    QVERIFY(writeFile(scheme, "[Background]\nColor=1,1,1\n"));
    QTRY_COMPARE_WITH_TIMEOUT(settings.current().appearance.background, QColor(1, 1, 1), 1000);

    // A new default profile in konsolerc.
    QVERIFY(writeFile(dir.filePath(QStringLiteral("config/konsolerc")), "[Desktop Entry]\nDefaultProfile=Plain.profile\n"));
    QTRY_COMPARE_WITH_TIMEOUT(settings.current().name, QStringLiteral("Plain"), 1000);
}

QTEST_MAIN(TerminalSettingsTest)
#include "tst_terminalsettings.moc"
