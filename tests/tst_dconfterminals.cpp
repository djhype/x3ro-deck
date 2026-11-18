// GNOME Console, GNOME Terminal, MATE Terminal and Ptyxis against `dconf
// dump` output in tests/fixtures/dconf.
#include "DconfTerminalSettings.h"
#include "TerminalSettings.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
QByteArray fixture(const QString &name)
{
    QFile f(QStringLiteral(X3RO_DECK_FIXTURES "/dconf/") + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

DconfSource::Locations locations(const QString &db = QString())
{
    const QHash<QString, QString> files{{QStringLiteral("/org/gnome/desktop/interface/"), QStringLiteral("interface.txt")},
                                        {QStringLiteral("/org/gnome/terminal/legacy/profiles:/"), QStringLiteral("gnome-terminal.txt")},
                                        {QStringLiteral("/org/mate/terminal/"), QStringLiteral("mate.txt")},
                                        {QStringLiteral("/org/mate/interface/"), QStringLiteral("mate-interface.txt")},
                                        {QStringLiteral("/org/gnome/Ptyxis/"), QStringLiteral("ptyxis.txt")},
                                        {QStringLiteral("/org/gnome/Console/"), QStringLiteral("console.txt")}};
    return {[files](const QString &dir) { return files.contains(dir) ? fixture(files.value(dir)) : QByteArray(); }, db, QString()};
}
} // namespace

class DconfTerminalsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void gvariants()
    {
        QCOMPARE(DconfSource::gString(QStringLiteral("'it\\'s'")), QStringLiteral("it's"));
        QCOMPARE(DconfSource::gList(QStringLiteral("['a', 'b c']")), (QStringList{QStringLiteral("a"), QStringLiteral("b c")}));
        QCOMPARE(DconfSource::gList(QStringLiteral("@as []")), QStringList());
        QCOMPARE(*DconfSource::gColor(QStringLiteral("'rgb(1,2,3)'")), QColor(1, 2, 3));
        QCOMPARE(*DconfSource::gColor(QStringLiteral("'#FFFF00000000'")), QColor(255, 0, 0));
        const QFont f = DconfSource::pangoFont(QStringLiteral("Source Code Pro Semi-Bold 11"));
        QCOMPARE(f.family(), QStringLiteral("Source Code Pro"));
        QCOMPARE(f.pointSizeF(), 11.0);
    }

    void gnomeTerminal()
    {
        const GnomeTerminalSettings gt(locations());
        QVERIFY(gt.detect());
        const QList<TerminalSettingsSource::ProfileRef> profiles = gt.profiles();
        QCOMPARE(profiles.size(), 2);
        QCOMPARE(profiles.first().name, QStringLiteral("Dracula"));
        QCOMPARE(gt.defaultProfile(), QStringLiteral("b1dcc9dd-5262-4d8d-a863-c897e6d979b9"));
        const std::optional<TerminalProfile> p = gt.load(QString());
        QVERIFY(p);
        const TerminalAppearance &a = p->appearance;
        QCOMPARE(p->name, QStringLiteral("Dracula"));
        QCOMPARE(a.font.family(), QStringLiteral("JetBrains Mono"));
        QCOMPARE(a.font.pointSizeF(), 13.0);
        QCOMPARE(a.background, QColor(40, 42, 54));
        QCOMPARE(a.foreground, QColor(QStringLiteral("#f8f8f2")));
        QCOMPARE(a.palette[1], QColor(255, 85, 85));
        QCOMPARE(a.palette[15], QColor(255, 255, 255));
        QCOMPARE(a.cursorShape, TerminalAppearance::Bar);
        QVERIFY(!a.cursorBlink); // "system", and the desktop has blinking off
        QVERIFY(a.boldIsBright);
        QCOMPARE(p->scrollback, -1);
        QCOMPARE(p->shell, (QStringList{QStringLiteral("tmux"), QStringLiteral("new"), QStringLiteral("-A")}));

        // A profile with nothing set: the system font and GNOME's dark colors.
        const std::optional<TerminalProfile> plain = gt.load(QStringLiteral("0a1b2c3d-0000-4000-8000-000000000001"));
        QVERIFY(plain);
        QCOMPARE(plain->appearance.font.family(), QStringLiteral("Source Code Pro"));
        QCOMPARE(plain->appearance.background, QColor(0x1e, 0x1e, 0x1e));
        QCOMPARE(plain->appearance.palette[1], QColor(QStringLiteral("#c01c28")));
        QVERIFY(!plain->scrollback);
        QVERIFY(!gt.load(QStringLiteral("nope")));
    }

    void mateTerminal()
    {
        const MateTerminalSettings mate(locations());
        QCOMPARE(mate.defaultProfile(), QStringLiteral("profile0"));
        QCOMPARE(mate.profiles().size(), 2);
        const std::optional<TerminalProfile> p = mate.load(QString());
        QVERIFY(p);
        QCOMPARE(p->name, QStringLiteral("Solarized"));
        QCOMPARE(p->appearance.background, QColor(0x00, 0x2b, 0x36));
        QCOMPARE(p->appearance.palette[1], QColor(0xdc, 0x32, 0x2f));
        QCOMPARE(p->appearance.palette[15], QColor(0xfd, 0xf6, 0xe3));
        QCOMPARE(p->appearance.font.family(), QStringLiteral("DejaVu Sans Mono"));
        QCOMPARE(p->scrollback, 4000);
    }

    void ptyxis()
    {
        const PtyxisSettings ptyxis(locations());
        const std::optional<TerminalProfile> p = ptyxis.load(QString());
        QVERIFY(p);
        QCOMPARE(p->name, QStringLiteral("Work"));
        QCOMPARE(p->appearance.font.family(), QStringLiteral("Cascadia Code"));
        QCOMPARE(p->appearance.font.pointSizeF(), 15.0);
        // interface-style light; the "dracula" palette is compiled into
        // Ptyxis, so the GNOME palette stands in.
        QCOMPARE(p->appearance.background, QColor(Qt::white));
        QCOMPARE(p->appearance.palette[1], QColor(QStringLiteral("#c01c28")));
        QCOMPARE(p->scrollback, 2500);
    }

    void gnomeConsole()
    {
        const GnomeConsoleSettings console(locations());
        const std::optional<TerminalProfile> p = console.load(QString());
        QVERIFY(p);
        QCOMPARE(p->appearance.font.family(), QStringLiteral("Fira Mono"));
        QCOMPARE(p->appearance.background, QColor(Qt::white)); // theme 'day'
    }

    void nothingInstalled()
    {
        DconfSource::Locations none{[](const QString &) { return QByteArray(); }, QString(), QString()};
        QVERIFY(!GnomeTerminalSettings(none).detect());
        QVERIFY(!PtyxisSettings(none).detect());
        QVERIFY(!MateTerminalSettings(none).detect());
        QVERIFY(GnomeTerminalSettings(none).load(QString())); // built-in defaults
    }

    // dconf rewrites its database file on every change; that re-imports.
    void reimportsWhenTheDatabaseChanges()
    {
        QTemporaryDir dir;
        const QString db = dir.filePath(QStringLiteral("user"));
        QFile f(db);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        auto background = std::make_shared<QString>(QStringLiteral("rgb(1,1,1)"));
        DconfSource::Locations loc = locations(db);
        const DconfSource::Dumper base = loc.dump;
        loc.dump = [base, background](const QString &d) {
            QByteArray out = base(d);
            out.replace("rgb(40,42,54)", background->toUtf8());
            return out;
        };
        TerminalSettings::Sources sources;
        sources.push_back(std::make_unique<GnomeTerminalSettings>(loc));
        TerminalSettings settings(std::move(sources), dir.path());
        settings.select(QStringLiteral("gnome-terminal"), QString());
        QCOMPARE(settings.current().appearance.background, QColor(1, 1, 1));
        *background = QStringLiteral("rgb(9,9,9)");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("changed");
        f.close();
        QTRY_COMPARE_WITH_TIMEOUT(settings.current().appearance.background, QColor(9, 9, 9), 2000);
    }
};

QTEST_MAIN(DconfTerminalsTest)
#include "tst_dconfterminals.moc"
