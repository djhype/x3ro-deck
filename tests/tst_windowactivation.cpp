#include "WindowActivation.h"

#include <QTest>
#include <QWidget>

class WindowActivationTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void startupInfoChunks();
    void startupIdTime();
    void waylandHandsTheTokenToQt();
    void unminimizesAndShows();
};

void WindowActivationTest::startupInfoChunks()
{
    const QList<QByteArray> chunks = WindowActivation::startupInfoChunks("remove: ID=\"abc_TIME42\"");
    // 24 bytes plus the NUL: two chunks, padded with NULs.
    QCOMPARE(chunks.size(), 2);
    QCOMPARE(chunks.at(0), QByteArray("remove: ID=\"abc_TIME"));
    QCOMPARE(chunks.at(1).size(), 20);
    QCOMPARE(chunks.at(1).left(4), QByteArray("42\"\0", 4));
    QCOMPARE(chunks.at(1).mid(3).count('\0'), 17);
    // Exactly 20 bytes still needs a chunk for the terminating NUL.
    QCOMPARE(WindowActivation::startupInfoChunks(QByteArray(20, 'x')).size(), 2);
}

void WindowActivationTest::startupIdTime()
{
    QCOMPARE(WindowActivation::startupIdTime(QStringLiteral("krunner-1234-host-x3ro-deck-0_TIME98765")), 98765u);
    QCOMPARE(WindowActivation::startupIdTime(QStringLiteral("no-time-here")), 0u);
    QCOMPARE(WindowActivation::startupIdTime(QString()), 0u);
}

void WindowActivationTest::waylandHandsTheTokenToQt()
{
    qunsetenv("XDG_ACTIVATION_TOKEN");
    QWidget w;
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    // Offscreen ignores the token, so it stays where Qt's Wayland plugin
    // would pick it up.
    WindowActivation::activate(&w, QStringLiteral("tok-1"), QStringLiteral("wayland"));
    QCOMPARE(qEnvironmentVariable("XDG_ACTIVATION_TOKEN"), QStringLiteral("tok-1"));
    qunsetenv("XDG_ACTIVATION_TOKEN");
    // No token: nothing is set.
    WindowActivation::activate(&w, QString(), QStringLiteral("wayland"));
    QVERIFY(!qEnvironmentVariableIsSet("XDG_ACTIVATION_TOKEN"));
}

void WindowActivationTest::unminimizesAndShows()
{
    QWidget w;
    w.showMinimized();
    WindowActivation::activate(&w, QString());
    QVERIFY(w.isVisible());
    QVERIFY(!(w.windowState() & Qt::WindowMinimized));

    w.hide();
    WindowActivation::activate(&w, QStringLiteral("ignored-off-x11"));
    QVERIFY(w.isVisible());
}

QTEST_MAIN(WindowActivationTest)
#include "tst_windowactivation.moc"
