// The shortcut registry: defaults, overrides, conflicts and hints.
#include "Shortcuts.h"

#include <QtTest>

class ShortcutsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaultsHaveNoConflicts()
    {
        QVERIFY(Shortcuts::conflicts(Shortcuts::bindings({})).isEmpty());
#if defined(Q_OS_MACOS)
        QCOMPARE(Shortcuts::binding({}, QStringLiteral("select-1")), QKeySequence(Qt::CTRL | Qt::Key_1)); // ⌘1
#else
        QCOMPARE(Shortcuts::binding({}, QStringLiteral("select-1")), QKeySequence(Qt::ALT | Qt::Key_1));
#endif
        QVERIFY(Shortcuts::action(QStringLiteral("toggle-window"))->global);
        QVERIFY(!Shortcuts::action(QStringLiteral("nope")));
        QCOMPARE(Shortcuts::binding({}, QStringLiteral("settings")), QKeySequence(Qt::CTRL | Qt::Key_Comma));
    }

    void overridesAndUnbinding()
    {
        const QVariantMap overrides{{QStringLiteral("next-checkout"), QStringLiteral("Ctrl+Alt+J")}, {QStringLiteral("quit"), QString()}};
        QCOMPARE(Shortcuts::binding(overrides, QStringLiteral("next-checkout")), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_J));
        QVERIFY(Shortcuts::binding(overrides, QStringLiteral("quit")).isEmpty());
        const QVariantMap hints = Shortcuts::hints(overrides);
        QCOMPARE(hints.value(QStringLiteral("next-checkout")).toString(),
                 QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_J).toString(QKeySequence::NativeText));
        QCOMPARE(hints.value(QStringLiteral("quit")).toString(), QString());
    }

    void conflictsIncludeTheGlobalHotkey()
    {
        const QVariantMap overrides{{QStringLiteral("docker-tab"), QStringLiteral("Meta+Alt+H")},
                                    {QStringLiteral("quit"), QString()},
                                    {QStringLiteral("new-shell"), QString()}};
        const QList<QStringList> clashes = Shortcuts::conflicts(Shortcuts::bindings(overrides));
        QCOMPARE(clashes.size(), 1);
        QCOMPARE(QSet<QString>(clashes.first().cbegin(), clashes.first().cend()),
                 (QSet<QString>{QStringLiteral("docker-tab"), QStringLiteral("toggle-window")}));
    }
};

QTEST_GUILESS_MAIN(ShortcutsTest)
#include "tst_shortcuts.moc"
