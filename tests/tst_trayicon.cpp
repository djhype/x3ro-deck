// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TrayIcon.h"

#include <QAction>
#include <QSignalSpy>
#include <QTest>

class TrayIconTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void attentionSwapsIconAndToolTip();
    void badgeMarksTheCorner();
    void menuActionsEmit();

private:
    static QIcon plainIcon();
};

QIcon TrayIconTest::plainIcon()
{
    QPixmap pm(32, 32);
    pm.fill(Qt::blue);
    return QIcon(pm);
}

void TrayIconTest::attentionSwapsIconAndToolTip()
{
    TrayIcon tray(plainIcon());
    QVERIFY(!tray.needsAttention());
    const qint64 quiet = tray.currentIcon().cacheKey();
    QVERIFY(tray.toolTip().contains(QStringLiteral("all quiet")));

    tray.setNeedsYou(2);
    QVERIFY(tray.needsAttention());
    QVERIFY(tray.currentIcon().cacheKey() != quiet);
    QVERIFY(tray.toolTip().contains(QStringLiteral("2 item(s)")));

    tray.setNeedsYou(0);
    QCOMPARE(tray.currentIcon().cacheKey(), quiet);
}

void TrayIconTest::badgeMarksTheCorner()
{
    const QImage img = TrayIcon::withBadge(plainIcon(), QColor(0xf5, 0xa6, 0x23)).pixmap(QSize(32, 32)).toImage();
    QCOMPARE(img.size(), QSize(32, 32));
    QCOMPARE(QColor(img.pixel(4, 4)), QColor(Qt::blue));
    QCOMPARE(QColor(img.pixel(24, 24)), QColor(0xf5, 0xa6, 0x23));
}

void TrayIconTest::menuActionsEmit()
{
    TrayIcon tray(plainIcon());
    QSignalSpy toggle(&tray, &TrayIcon::toggleRequested);
    QSignalSpy quit(&tray, &TrayIcon::quitRequested);
    tray.toggleAction()->trigger();
    tray.quitAction()->trigger();
    QCOMPARE(toggle.size(), 1);
    QCOMPARE(quit.size(), 1);

    tray.setWindowVisible(false);
    QCOMPARE(tray.toggleAction()->text(), QStringLiteral("Show X3RO Deck"));
    tray.setWindowVisible(true);
    QCOMPARE(tray.toggleAction()->text(), QStringLiteral("Hide X3RO Deck"));
}

QTEST_MAIN(TrayIconTest)
#include "tst_trayicon.moc"
