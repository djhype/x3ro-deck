// kitty, Ghostty and Alacritty settings against fixtures in tests/fixtures/gpu.
#include "GpuTerminalSettings.h"
#include "TerminalSettings.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
const QString kRoot = QStringLiteral(X3RO_DECK_FIXTURES "/gpu");

PlainConfigSource::Locations locations(const QString &root = kRoot)
{
    return {root + QStringLiteral("/config"), root + QStringLiteral("/home"), {root + QStringLiteral("/share")}, QString()};
}

bool copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    for (const QFileInfo &fi : QDir(from).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString target = to + QLatin1Char('/') + fi.fileName();
        if (fi.isDir() ? !copyTree(fi.filePath(), target) : !QFile::copy(fi.filePath(), target)) {
            return false;
        }
    }
    return true;
}
} // namespace

class GpuTerminalsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void kitty()
    {
        const KittySettings kitty(locations());
        QVERIFY(kitty.detect());
        QCOMPARE(kitty.profiles().size(), 1);
        const std::optional<TerminalProfile> p = kitty.load(QString());
        QVERIFY(p);
        const TerminalAppearance &a = p->appearance;
        QCOMPARE(a.font.family(), QStringLiteral("JetBrains Mono"));
        QCOMPARE(a.font.pointSizeF(), 13.5);
        // The theme include sets the colors; a later line in kitty.conf wins.
        QCOMPARE(a.background, QColor(QStringLiteral("#282a36")));
        QCOMPARE(a.foreground, QColor(QStringLiteral("#f0f0f0")));
        QCOMPARE(a.selection, QColor(QStringLiteral("#44475a")));
        QCOMPARE(a.palette[1], QColor(QStringLiteral("#ff5555")));
        QCOMPARE(a.palette[9], QColor(QStringLiteral("#ff6e6e")));
        QCOMPARE(a.palette[2], QColor(QStringLiteral("#19cb00"))); // kitty's own default
        QCOMPARE(a.cursorShape, TerminalAppearance::Bar);
        QVERIFY(!a.cursorBlink);
        QCOMPARE(a.padding, 6);
        QCOMPARE(p->scrollback, 5000);
        QCOMPARE(p->shell, (QStringList{QStringLiteral("/bin/zsh"), QStringLiteral("-l")}));
        QVERIFY(kitty.watchedPaths(QString()).contains(kRoot + QStringLiteral("/config/kitty/current-theme.conf")));
        QVERIFY(!kitty.load(QStringLiteral("other")));
    }

    void ghostty()
    {
        const GhosttySettings ghostty(locations());
        const std::optional<TerminalProfile> p = ghostty.load(QString());
        QVERIFY(p);
        const TerminalAppearance &a = p->appearance;
        QCOMPARE(a.font.family(), QStringLiteral("Iosevka Term"));
        QCOMPARE(a.font.pointSizeF(), 14.0);
        // The dark half of "light:…,dark:Nordish", from the installed themes;
        // extra.conf (config-file) overrides the theme's background.
        QCOMPARE(a.background, QColor(QStringLiteral("#101010")));
        QCOMPARE(a.foreground, QColor(QStringLiteral("#d8dee9")));
        QCOMPARE(a.cursor, QColor(QStringLiteral("#eceff4")));
        QCOMPARE(a.selection, QColor(QStringLiteral("#4c566a")));
        QCOMPARE(a.palette[1], QColor(QStringLiteral("#bf616a")));
        QCOMPARE(a.palette[15], QColor(QStringLiteral("#eceff4")));
        QCOMPARE(a.cursorShape, TerminalAppearance::Underline);
        QVERIFY(!a.cursorBlink);
        QCOMPARE(a.padding, 10);
        QCOMPARE(p->shell, QStringList{QStringLiteral("/usr/bin/fish")});
        const QStringList watched = ghostty.watchedPaths(QString());
        QVERIFY(watched.contains(kRoot + QStringLiteral("/share/ghostty/themes/Nordish")));
        QVERIFY(watched.contains(kRoot + QStringLiteral("/config/ghostty/extra.conf")));
    }

    void alacritty()
    {
        const AlacrittySettings alacritty(locations());
        const std::optional<TerminalProfile> p = alacritty.load(QString());
        QVERIFY(p);
        const TerminalAppearance &a = p->appearance;
        QCOMPARE(a.font.family(), QStringLiteral("Fira Code"));
        QCOMPARE(a.font.pointSizeF(), 12.0);
        // The imported theme, then the file's own background.
        QCOMPARE(a.background, QColor(QStringLiteral("#1d2021")));
        QCOMPARE(a.foreground, QColor(QStringLiteral("#ebdbb2")));
        QCOMPARE(a.cursor, QColor(QStringLiteral("#fe8019")));
        QCOMPARE(a.palette[1], QColor(QStringLiteral("#cc241d")));
        QCOMPARE(a.palette[15], QColor(QStringLiteral("#fbf1c7")));
        QCOMPARE(a.palette[2], QColor(QStringLiteral("#90a959"))); // Alacritty's own default
        QCOMPARE(a.cursorShape, TerminalAppearance::Bar);
        QVERIFY(a.cursorBlink);
        QCOMPARE(a.padding, 5);
        QCOMPARE(p->scrollback, 20000);
        QCOMPARE(p->shell, (QStringList{QStringLiteral("/bin/bash"), QStringLiteral("--login")}));
    }

    void missingConfigs()
    {
        QTemporaryDir empty;
        for (const PlainConfigSource *s : std::initializer_list<const PlainConfigSource *>{
                 new KittySettings(locations(empty.path())), new GhosttySettings(locations(empty.path())),
                 new AlacrittySettings(locations(empty.path()))}) {
            QVERIFY(!s->detect());
            const std::optional<TerminalProfile> p = s->load(QString());
            QVERIFY(p); // the terminal's defaults
            QVERIFY(!p->shell.isEmpty());
            delete s;
        }
    }

    void reimportsOnChange()
    {
        QTemporaryDir dir;
        QVERIFY(copyTree(kRoot, dir.path()));
        TerminalSettings::Sources sources;
        sources.push_back(std::make_unique<KittySettings>(locations(dir.path())));
        TerminalSettings settings(std::move(sources), dir.path() + QStringLiteral("/config"));
        settings.select(QStringLiteral("kitty"), QString());
        QCOMPARE(settings.current().appearance.background, QColor(QStringLiteral("#282a36")));
        QSignalSpy changed(&settings, &TerminalSettings::changed);
        QFile theme(dir.filePath(QStringLiteral("config/kitty/current-theme.conf")));
        QVERIFY(theme.open(QIODevice::WriteOnly | QIODevice::Truncate));
        theme.write("background #123456\n");
        theme.close();
        QTRY_COMPARE_WITH_TIMEOUT(settings.current().appearance.background, QColor(QStringLiteral("#123456")), 2000);
    }
};

QTEST_MAIN(GpuTerminalsTest)
#include "tst_gputerminals.moc"
