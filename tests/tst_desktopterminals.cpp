// xfce4-terminal and COSMIC Terminal against tests/fixtures/desktopterm.
#include "DesktopTerminalSettings.h"
#include "TerminalSettings.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
const QString kRoot = QStringLiteral(X3RO_DECK_FIXTURES "/desktopterm");

XfceTerminalSettings::Locations xfce(const QString &configDir, const QByteArray &query = QByteArray())
{
    return {[query] { return query; }, configDir, QString()};
}
} // namespace

class DesktopTerminalsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void xfconfXml()
    {
        const XfceTerminalSettings x(xfce(kRoot + QStringLiteral("/xml")));
        QVERIFY(x.detect());
        const std::optional<TerminalProfile> p = x.load(QString());
        QVERIFY(p);
        const TerminalAppearance &a = p->appearance;
        QCOMPARE(a.font.family(), QStringLiteral("Hack"));
        QCOMPARE(a.font.pointSizeF(), 11.0);
        QCOMPARE(a.background, QColor(QStringLiteral("#2c2c2c")));
        QCOMPARE(a.palette[0], QColor(63, 63, 63));
        QCOMPARE(a.palette[15], QColor(Qt::white));
        QCOMPARE(a.cursorShape, TerminalAppearance::Underline);
        QCOMPARE(p->scrollback, -1);
    }

    void terminalrc()
    {
        const XfceTerminalSettings x(xfce(kRoot + QStringLiteral("/rc")));
        const std::optional<TerminalProfile> p = x.load(QString());
        QVERIFY(p);
        QCOMPARE(p->appearance.font.family(), QStringLiteral("Terminus"));
        QCOMPARE(p->appearance.background, QColor(0, 0, 0x33));
        QCOMPARE(p->appearance.cursorShape, TerminalAppearance::Bar);
        QVERIFY(p->appearance.cursorBlink);
        QCOMPARE(p->scrollback, 3000);
        QCOMPARE(p->appearance.palette[1], QColor(QStringLiteral("#aa0000"))); // xfce's default
    }

    void xfconfQueryWins()
    {
        const XfceTerminalSettings x(xfce(kRoot + QStringLiteral("/xml"), "/color-background   #123456\n/font-name    Iosevka 9\n"));
        const std::optional<TerminalProfile> p = x.load(QString());
        QCOMPARE(p->appearance.background, QColor(QStringLiteral("#123456")));
        QCOMPARE(p->appearance.font.family(), QStringLiteral("Iosevka"));
    }

    void ron()
    {
        const QVariant v = CosmicTerminalSettings::parseRon(
            "{ ProfileId(0): (name: \"A\", n: 3, on: true, opt: Some(\"x\"), none: None, list: [1, 2]), }");
        const QVariantMap p = v.toMap().value(QStringLiteral("ProfileId(0)")).toMap();
        QCOMPARE(p.value(QStringLiteral("name")).toString(), QStringLiteral("A"));
        QCOMPARE(p.value(QStringLiteral("n")).toInt(), 3);
        QCOMPARE(p.value(QStringLiteral("on")).toBool(), true);
        QCOMPARE(p.value(QStringLiteral("opt")).toString(), QStringLiteral("x"));
        QVERIFY(!p.value(QStringLiteral("none")).isValid());
        QCOMPARE(p.value(QStringLiteral("list")).toList().size(), 2);
        QCOMPARE(CosmicTerminalSettings::parseRon("Some(ProfileId(1))").toString(), QStringLiteral("ProfileId(1)"));
    }

    void cosmic()
    {
        const CosmicTerminalSettings c({kRoot + QStringLiteral("/cosmic"), QString()});
        QVERIFY(c.detect());
        QCOMPARE(c.profiles().size(), 2);
        QCOMPARE(c.defaultProfile(), QStringLiteral("ProfileId(1)"));
        const std::optional<TerminalProfile> p = c.load(QString());
        QVERIFY(p);
        QCOMPARE(p->name, QStringLiteral("Hacker"));
        QCOMPARE(p->appearance.font.family(), QStringLiteral("JetBrains Mono"));
        QCOMPARE(p->appearance.font.pointSizeF(), 16.0);
        QCOMPARE(p->appearance.background, QColor(QStringLiteral("#0d0208")));
        QCOMPARE(p->appearance.cursor, QColor(QStringLiteral("#008f11")));
        QCOMPARE(p->appearance.palette[1], QColor(Qt::red));
        QCOMPARE(p->appearance.palette[15], QColor(Qt::white));
        QCOMPARE(p->shell, (QStringList{QStringLiteral("/usr/bin/fish"), QStringLiteral("-l")}));
        // The built-in COSMIC Dark scheme for the other profile.
        const std::optional<TerminalProfile> plain = c.load(QStringLiteral("ProfileId(0)"));
        QCOMPARE(plain->appearance.background, QColor(QStringLiteral("#1b1b1b")));
        QVERIFY(!c.load(QStringLiteral("ProfileId(9)")));
    }

    void reimportsOnChange()
    {
        QTemporaryDir dir;
        const QString rc = dir.filePath(QStringLiteral("xfce4/terminal/terminalrc"));
        QVERIFY(QDir().mkpath(QFileInfo(rc).absolutePath()));
        QVERIFY(QFile::copy(kRoot + QStringLiteral("/rc/xfce4/terminal/terminalrc"), rc));
        QFile::setPermissions(rc, QFile::ReadOwner | QFile::WriteOwner);
        TerminalSettings::Sources sources;
        sources.push_back(std::make_unique<XfceTerminalSettings>(xfce(dir.path())));
        TerminalSettings settings(std::move(sources), dir.path());
        settings.select(QStringLiteral("xfce4-terminal"), QString());
        QCOMPARE(settings.current().appearance.font.family(), QStringLiteral("Terminus"));
        QFile f(rc);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[Configuration]\nFontName=Hack 10\n");
        f.close();
        QTRY_COMPARE_WITH_TIMEOUT(settings.current().appearance.font.family(), QStringLiteral("Hack"), 2000);
    }
};

QTEST_MAIN(DesktopTerminalsTest)
#include "tst_desktopterminals.moc"
