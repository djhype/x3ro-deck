// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// Windows Terminal (JSONC fixture) and Console Host (registry values passed
// in). Both readers are portable, so this runs on every OS.
#include "WindowsTerminalSettings.h"

#include <QTest>

namespace
{
WindowsTerminalSettings fixture()
{
    return WindowsTerminalSettings(false, {QStringLiteral(X3RO_DECK_FIXTURES "/winterm/settings.json")});
}
} // namespace

class WinTerminalsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void windowsTerminal()
    {
        const WindowsTerminalSettings wt = fixture();
        QVERIFY(wt.detect());
        QStringList names;
        for (const auto &p : wt.profiles()) {
            names << p.name;
        }
        QCOMPARE(names, (QStringList{QStringLiteral("Windows PowerShell"), QStringLiteral("Command Prompt")})); // hidden left out
        const std::optional<TerminalProfile> p = wt.load(QString());
        QVERIFY(p);
        QCOMPARE(p->name, QStringLiteral("Windows PowerShell"));
        QCOMPARE(p->appearance.font.family(), QStringLiteral("CaskaydiaCove Nerd Font"));
        QCOMPARE(p->appearance.font.pointSizeF(), 11.0);
        QCOMPARE(p->appearance.background, QColor(QStringLiteral("#282C34")));
        QCOMPARE(p->appearance.palette[1], QColor(QStringLiteral("#E06C75")));
        QCOMPARE(p->appearance.palette[2], QColor(QStringLiteral("#13A10E"))); // Campbell fills the rest
        QCOMPARE(p->appearance.cursorShape, TerminalAppearance::Block);
        QCOMPARE(p->scrollback, 12000);
        QCOMPARE(p->shell, (QStringList{QStringLiteral("powershell.exe"), QStringLiteral("-NoLogo")}));

        // Campbell with a profile's own background.
        const std::optional<TerminalProfile> cmd = wt.load(QStringLiteral("{0caa0dad-35be-5f56-a8ff-afceeeaa6101}"));
        QVERIFY(cmd);
        QCOMPARE(cmd->appearance.background, QColor(QStringLiteral("#202020")));
        QCOMPARE(cmd->appearance.foreground, QColor(QStringLiteral("#CCCCCC")));
        QVERIFY(!wt.load(QStringLiteral("{nope}")));
    }

    void consoleHost()
    {
        const ConsoleHostSettings conhost([] {
            return QVariantMap{{QStringLiteral("FaceName"), QStringLiteral("Lucida Console")},
                               {QStringLiteral("FontSize"), uint(18) << 16},
                               {QStringLiteral("ColorTable00"), 0x00563412u}, // RGB 12 34 56
                               {QStringLiteral("ColorTable07"), 0x00f0f0f0u},
                               {QStringLiteral("ScreenColors"), 0x07u},
                               {QStringLiteral("CursorSize"), 100}};
        });
        QVERIFY(conhost.detect());
        const std::optional<TerminalProfile> p = conhost.load(QString());
        QVERIFY(p);
        QCOMPARE(p->appearance.font.family(), QStringLiteral("Lucida Console"));
        QCOMPARE(p->appearance.font.pixelSize(), 18);
        QCOMPARE(p->appearance.background, QColor(0x12, 0x34, 0x56));
        QCOMPARE(p->appearance.foreground, QColor(0xf0, 0xf0, 0xf0));
        QCOMPARE(p->appearance.cursorShape, TerminalAppearance::Block);
        QVERIFY(!ConsoleHostSettings([] { return QVariantMap(); }).detect());
    }
};

QTEST_MAIN(WinTerminalsTest)
#include "tst_winterminals.moc"
