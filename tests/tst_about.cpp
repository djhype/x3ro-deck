// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AboutDialog.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QtTest>

class TestAbout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void licenseHasTheAttributionTerm()
    {
        const QString license = AboutDialog::text(QStringLiteral(":/LICENSE"));
        QVERIFY(license.startsWith(QLatin1String("                    GNU GENERAL PUBLIC LICENSE")));
        QVERIFY(license.contains(QLatin1String("ADDITIONAL TERMS UNDER SECTION 7")));
        QVERIFY(license.contains(QLatin1String("(b) Attribution.")));
        QVERIFY(license.contains(QLatin1String("\"x3ro deck by x3ro\"")));
    }

    void dialogShowsCreditVersionAndNotices()
    {
        AboutDialog dialog;
        QStringList labels;
        for (const QLabel *l : dialog.findChildren<QLabel *>()) {
            labels.append(l->text());
        }
        const QString all = labels.join(QLatin1Char('\n'));
        QVERIFY(all.contains(AboutDialog::credit()));
        QVERIFY(all.contains(QStringLiteral("Version ") + AboutDialog::version()));
        QVERIFY(!AboutDialog::version().isEmpty());
        QVERIFY(all.contains(AboutDialog::sourceUrl()));
        QVERIFY(all.contains(QLatin1String("download.qt.io")));

        QStringList texts;
        for (const QPlainTextEdit *e : dialog.findChildren<QPlainTextEdit *>()) {
            texts.append(e->toPlainText());
        }
        QCOMPARE(texts.size(), 3); // LICENSE, LGPL-3.0, libvterm
        const QString joined = texts.join(QLatin1Char('\n'));
        QVERIFY(joined.contains(QLatin1String("ADDITIONAL TERMS UNDER SECTION 7")));
        QVERIFY(joined.contains(QLatin1String("GNU LESSER GENERAL PUBLIC LICENSE")));
        QVERIFY(joined.contains(QLatin1String("Paul Evans")));
    }
};

QTEST_MAIN(TestAbout)
#include "tst_about.moc"
