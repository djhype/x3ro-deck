// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// HookInstaller against fixtures in tests/fixtures/hooks.
#include "HookInstaller.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using Cli = HookInstaller::Cli;
using State = HookInstaller::State;

namespace
{
QByteArray fixture(const char *name)
{
    QFile f(QStringLiteral(X3RO_DECK_FIXTURES "/hooks/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QJsonObject parsed(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

// Every command registered for `event`, in file order.
QStringList commands(const QByteArray &json, const QString &event)
{
    QStringList out;
    const QJsonArray groups = parsed(json).value(QStringLiteral("hooks")).toObject().value(event).toArray();
    for (const QJsonValue &g : groups) {
        for (const QJsonValue &h : g.toObject().value(QStringLiteral("hooks")).toArray()) {
            out << h.toObject().value(QStringLiteral("command")).toString();
        }
    }
    return out;
}

QByteArray stripped(QByteArray json)
{
    json.replace(' ', "").replace('\n', "").replace('\r', ""); // \r: Windows checks the fixtures out with CRLF
    return json;
}
} // namespace

class TestHookInstaller : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void recognizesOurCommands()
    {
        QVERIFY(HookInstaller::isOurs(QStringLiteral("x3ro-deck-hook")));
        QVERIFY(HookInstaller::isOurs(QStringLiteral("/home/u/.local/bin/x3ro-deck-hook")));
        QVERIFY(HookInstaller::isOurs(QStringLiteral("'/opt/my apps/x3ro-deck-hook' --x")));
        QVERIFY(!HookInstaller::isOurs(QStringLiteral("x3ro-deck-hook-old")));
        QVERIFY(!HookInstaller::isOurs(QStringLiteral("echo x3ro-deck-hook")));
        QCOMPARE(HookInstaller::command(QStringLiteral("/opt/my apps/x3ro-deck-hook")), QStringLiteral("'/opt/my apps/x3ro-deck-hook'"));
        QCOMPARE(HookInstaller::command(QStringLiteral("/usr/bin/x3ro-deck-hook")), QStringLiteral("/usr/bin/x3ro-deck-hook"));
    }

    void roundTripKeepsEverythingButWhitespace()
    {
        const QByteArray in = fixture("claude-other.json");
        QVERIFY(!in.isEmpty());
        QCOMPARE(stripped(HookInstaller::normalized(in)), stripped(in));
    }

    void installKeepsOtherHooks()
    {
        const QString cmd = QStringLiteral("/usr/bin/x3ro-deck-hook");
        const QByteArray in = fixture("claude-other.json");
        QCOMPARE(HookInstaller::statusOf(Cli::Claude, in, cmd).state, State::Outdated);
        const QByteArray out = HookInstaller::installed(Cli::Claude, in, cmd);
        QVERIFY(!out.isNull());
        QCOMPARE(commands(out, QStringLiteral("Notification")), (QStringList{QStringLiteral("notify-me \"needs é you\""), cmd}));
        QCOMPARE(commands(out, QStringLiteral("PreToolUse")), (QStringList{QStringLiteral("rtk hook claude"), cmd}));
        for (const QString &event : HookInstaller::events(Cli::Claude)) {
            QCOMPARE(commands(out, event).count(cmd), 1);
        }
        QCOMPARE(HookInstaller::statusOf(Cli::Claude, out, cmd).state, State::Installed);
        // Unrelated keys keep their order and spelling.
        QVERIFY(out.indexOf("\"model\"") < out.indexOf("\"permissions\""));
        QVERIFY(out.indexOf("\"hooks\"") < out.indexOf("\"alwaysThinkingEnabled\""));
        QVERIFY(out.contains("1.5e2"));
        QVERIFY(out.contains("\\u00e9"));
        QVERIFY(out.endsWith("}\n"));
    }

    void uninstallRestoresTheOtherHooks()
    {
        const QString cmd = QStringLiteral("/usr/bin/x3ro-deck-hook");
        const QByteArray clean = fixture("claude-clean.json");
        const QByteArray out = HookInstaller::installed(Cli::Claude, clean, cmd);
        QCOMPARE(stripped(HookInstaller::removed(out)), stripped(clean));
        QCOMPARE(stripped(HookInstaller::removed(fixture("claude-other.json"))), stripped(clean));
        QCOMPARE(HookInstaller::statusOf(Cli::Claude, clean, cmd).state, State::NotInstalled);
    }

    void reinstallReplacesThePath()
    {
        const QByteArray first = HookInstaller::installed(Cli::Codex, QByteArray(), QStringLiteral("/old/x3ro-deck-hook"));
        QCOMPARE(HookInstaller::statusOf(Cli::Codex, first, QStringLiteral("/new/x3ro-deck-hook")).state, State::Outdated);
        const QByteArray second = HookInstaller::installed(Cli::Codex, first, QStringLiteral("/new/x3ro-deck-hook"));
        for (const QString &event : HookInstaller::events(Cli::Codex)) {
            QCOMPARE(commands(second, event), QStringList{QStringLiteral("/new/x3ro-deck-hook")});
        }
        QVERIFY(!second.contains("Notification"));
        QCOMPARE(stripped(HookInstaller::removed(second)), QByteArray("{}"));
    }

    void bareNameOnPathCountsAsInstalled()
    {
        QTemporaryDir dir;
#if defined(Q_OS_WIN)
        const QString hook = dir.filePath(QStringLiteral("x3ro-deck-hook.exe")); // found by its suffix
#else
        const QString hook = dir.filePath(QStringLiteral("x3ro-deck-hook"));
#endif
        QFile f(hook);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QVERIFY(f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", dir.path().toLocal8Bit());
        const QByteArray bare = HookInstaller::installed(Cli::Codex, QByteArray(), QStringLiteral("x3ro-deck-hook"));
        QCOMPARE(HookInstaller::statusOf(Cli::Codex, bare, hook).state, State::Installed);
        QCOMPARE(HookInstaller::statusOf(Cli::Codex, bare, QStringLiteral("/elsewhere/x3ro-deck-hook")).state, State::Outdated);
        qputenv("PATH", path);
    }

    void refusesInvalidJson()
    {
        QString error;
        QVERIFY(HookInstaller::installed(Cli::Claude, "{\"a\": [1,}", QStringLiteral("x3ro-deck-hook"), &error).isNull());
        QVERIFY(!error.isEmpty());
        QVERIFY(HookInstaller::installed(Cli::Claude, "[1]", QStringLiteral("x3ro-deck-hook")).isNull());
        QCOMPARE(HookInstaller::statusOf(Cli::Claude, "{\"a\": tru}", QString()).state, State::Error);
    }

    void filesGetABackup()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("sub/settings.json"));
        const QString cmd = QStringLiteral("/usr/bin/x3ro-deck-hook");
        QString error;
        QVERIFY2(HookInstaller::install(Cli::Claude, file, cmd, &error), qPrintable(error));
        QVERIFY(!QFile::exists(file + QStringLiteral(".bak")));
        QCOMPARE(HookInstaller::status(Cli::Claude, file, cmd).state, State::Installed);

        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray installed = f.readAll();
        f.close();
        QVERIFY2(HookInstaller::uninstall(file, &error), qPrintable(error));
        QFile bak(file + QStringLiteral(".bak"));
        QVERIFY(bak.open(QIODevice::ReadOnly));
        QCOMPARE(bak.readAll(), installed);
        QCOMPARE(HookInstaller::status(Cli::Claude, file, cmd).state, State::NotInstalled);
    }
};

QTEST_GUILESS_MAIN(TestHookInstaller)
#include "tst_hookinstaller.moc"
