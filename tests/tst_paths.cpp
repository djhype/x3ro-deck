// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// Paths: the normalization and prefix rules that map hook cwds and tmux
// session paths onto checkouts.

#include "Paths.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using Paths::Style;

class PathsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void windowsDriveAndSeparators();
    void caseSensitivity();
    void pathBoundary();
    void rootsAndTrailingSeparators();
    void tildeRoundTrip();
    void symlinksResolve();
    void subdirectoryOfCheckout();
};

void PathsTest::windowsDriveAndSeparators()
{
    QCOMPARE(Paths::clean(QStringLiteral("c:\\Repo\\x\\"), Style::Windows), QStringLiteral("C:/Repo/x"));
    QVERIFY(Paths::isWithin(QStringLiteral("c:/repo/x/sub"), QStringLiteral("C:\\Repo\\x"), Qt::CaseInsensitive, Style::Windows));
    QVERIFY(Paths::same(QStringLiteral("C:\\Repo\\x"), QStringLiteral("c:/repo/x/"), Qt::CaseInsensitive, Style::Windows));
    QVERIFY(!Paths::isWithin(QStringLiteral("D:/repo/x/sub"), QStringLiteral("C:\\Repo\\x"), Qt::CaseInsensitive, Style::Windows));
}

void PathsTest::caseSensitivity()
{
    QVERIFY(!Paths::same(QStringLiteral("/a/Repo"), QStringLiteral("/a/repo"), Qt::CaseSensitive, Style::Posix));
    QVERIFY(Paths::same(QStringLiteral("/a/Repo"), QStringLiteral("/a/repo"), Qt::CaseInsensitive, Style::Posix));
}

void PathsTest::pathBoundary()
{
    QVERIFY(!Paths::isWithin(QStringLiteral("/a/bc"), QStringLiteral("/a/b")));
    QVERIFY(!Paths::isWithin(QStringLiteral("/a/bc/d"), QStringLiteral("/a/b/")));
    QVERIFY(Paths::isWithin(QStringLiteral("/a/b"), QStringLiteral("/a/b")));
    QVERIFY(Paths::isWithin(QStringLiteral("/a/b/c"), QStringLiteral("/a/b")));
    QVERIFY(!Paths::isWithin(QStringLiteral("C:/a/bc"), QStringLiteral("c:\\a\\b"), Qt::CaseInsensitive, Style::Windows));
    QVERIFY(!Paths::isWithin(QString(), QStringLiteral("/a")));
}

void PathsTest::rootsAndTrailingSeparators()
{
    QVERIFY(Paths::isWithin(QStringLiteral("/a"), QStringLiteral("/")));
    QVERIFY(Paths::isWithin(QStringLiteral("C:/a"), QStringLiteral("c:\\"), Qt::CaseInsensitive, Style::Windows));
    QCOMPARE(Paths::clean(QStringLiteral("C:"), Style::Windows), QStringLiteral("C:/"));
    QCOMPARE(Paths::clean(QStringLiteral("/a/./b/../c/")), QStringLiteral("/a/c"));
}

void PathsTest::tildeRoundTrip()
{
    const QString home = QDir::homePath();
    QCOMPARE(Paths::expandTilde(QStringLiteral("~/repo")), home + QStringLiteral("/repo"));
    QCOMPARE(Paths::expandTilde(QStringLiteral("~")), home);
    QCOMPARE(Paths::expandTilde(QStringLiteral("~other/repo")), QStringLiteral("~other/repo"));
    QCOMPARE(Paths::tilde(home + QStringLiteral("/repo")), QStringLiteral("~/repo"));
    QCOMPARE(Paths::tilde(home), QStringLiteral("~"));
    QCOMPARE(Paths::tilde(home + QStringLiteral("x/repo")), home + QStringLiteral("x/repo"));
    QCOMPARE(Paths::normalize(QStringLiteral("~/x3ro-deck-no-such-dir")), Paths::clean(home) + QStringLiteral("/x3ro-deck-no-such-dir"));
}

void PathsTest::symlinksResolve()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString real = tmp.path() + QStringLiteral("/real");
    const QString link = tmp.path() + QStringLiteral("/link");
    QVERIFY(QDir().mkpath(real + QStringLiteral("/sub")));
    QVERIFY(QFile::link(real, link));
    QCOMPARE(Paths::normalize(link), Paths::normalize(real));
    QVERIFY(Paths::isWithin(Paths::normalize(link + QStringLiteral("/sub")), Paths::normalize(real)));
}

void PathsTest::subdirectoryOfCheckout()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString checkout = Paths::normalize(tmp.path());
    QVERIFY(QDir().mkpath(tmp.path() + QStringLiteral("/src/deep")));
    QVERIFY(Paths::isWithin(Paths::normalize(tmp.path() + QStringLiteral("/src/deep/")), checkout));
    QVERIFY(!Paths::isWithin(Paths::normalize(tmp.path() + QStringLiteral("-sibling")), checkout));
}

QTEST_GUILESS_MAIN(PathsTest)
#include "tst_paths.moc"
