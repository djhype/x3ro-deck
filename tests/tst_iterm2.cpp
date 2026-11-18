// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// iTerm2 settings from an XML preferences plist and a DynamicProfile, in
// tests/fixtures/iterm2. The reader is portable, so this runs everywhere.
#include "MacTerminalSettings.h"

#include <QTest>

namespace
{
ITerm2Settings::Locations fixture()
{
    const QString root = QStringLiteral(X3RO_DECK_FIXTURES "/iterm2");
    return {root + QStringLiteral("/com.googlecode.iterm2.plist"), root + QStringLiteral("/DynamicProfiles"), QString()};
}
} // namespace

class ITerm2Test : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void profiles()
    {
        const ITerm2Settings iterm(fixture());
        QVERIFY(iterm.detect());
        QCOMPARE(iterm.defaultProfile(), QStringLiteral("B9A1-SOLAR"));
        QStringList names;
        for (const auto &p : iterm.profiles()) {
            names << p.name;
        }
        QCOMPARE(names, (QStringList{QStringLiteral("Default"), QStringLiteral("Solarized"), QStringLiteral("Work (dynamic)")}));
    }

    void defaultProfile()
    {
        const std::optional<TerminalProfile> p = ITerm2Settings(fixture()).load(QString());
        QVERIFY(p);
        QCOMPARE(p->name, QStringLiteral("Solarized"));
        QCOMPARE(p->appearance.font.family(), QStringLiteral("MesloLGS NF"));
        QCOMPARE(p->appearance.font.pointSizeF(), 14.0);
        QCOMPARE(p->appearance.background, QColor(0x00, 0x2b, 0x36));
        QCOMPARE(p->appearance.palette[1], QColor(0xdc, 0x32, 0x2f));
        QCOMPARE(p->appearance.palette[2], QColor(QStringLiteral("#00c200"))); // iTerm2's own default
        QCOMPARE(p->appearance.cursorShape, TerminalAppearance::Bar);
        QVERIFY(p->appearance.cursorBlink);
        QCOMPARE(p->scrollback, -1);
        QCOMPARE(p->shell, (QStringList{QStringLiteral("/opt/homebrew/bin/fish"), QStringLiteral("-l")}));
    }

    void dynamicProfileInheritsItsParent()
    {
        const std::optional<TerminalProfile> p = ITerm2Settings(fixture()).load(QStringLiteral("DYN-WORK"));
        QVERIFY(p);
        QCOMPARE(p->name, QStringLiteral("Work (dynamic)"));
        QCOMPARE(p->appearance.font.family(), QStringLiteral("JetBrainsMono"));
        QCOMPARE(p->appearance.background, QColor(0x00, 0x2b, 0x36)); // from Solarized
        QVERIFY(!ITerm2Settings(fixture()).load(QStringLiteral("nope")));
    }

    void plistTypes()
    {
        const QVariant v = ITerm2Settings::parsePlist("<plist><dict><key>a</key><integer>3</integer><key>b</key><array><false/><real>1.5</"
                                                      "real><string>x</string></array></dict></plist>");
        QCOMPARE(v.toMap().value(QStringLiteral("a")).toInt(), 3);
        const QVariantList b = v.toMap().value(QStringLiteral("b")).toList();
        QCOMPARE(b.size(), 3);
        QCOMPARE(b.at(0).toBool(), false);
        QCOMPARE(b.at(1).toDouble(), 1.5);
    }
};

QTEST_MAIN(ITerm2Test)
#include "tst_iterm2.moc"
