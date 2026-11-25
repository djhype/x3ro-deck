// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// ThemeEditor: live preview, copies of built-ins, save, import and export.
#include "AppTheme.h"
#include "TestEnv.h"
#include "ThemeEditor.h"

#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ThemeEditorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_config = std::make_unique<QTemporaryDir>();
        setConfigHome(m_config->path());
    }

    void editsACopyOfABuiltinWithALivePreview()
    {
        AppTheme live;
        QSignalSpy changed(&live, &AppTheme::changed);
        ThemeEditor editor(&live, QStringLiteral("cyberdecker"));
        QCOMPARE(editor.theme().value(QStringLiteral("name")).toString(), QStringLiteral("Cyberdecker (copy)"));
        QVERIFY(editor.id().isEmpty());

        // The accent recolors the running app at once.
        editor.setColor(QStringLiteral("magenta"), QColor(QStringLiteral("#00ff00")));
        QCOMPARE(live.color(QStringLiteral("magenta")), QColor(QStringLiteral("#00ff00")));
        QVERIFY(!changed.isEmpty());

        // Saving writes a new file; the built-in stays as it was.
        QSignalSpy saved(&editor, &ThemeEditor::saved);
        QString error;
        QVERIFY2(editor.save(&error), qPrintable(error));
        QCOMPARE(editor.id(), QStringLiteral("cyberdecker-copy"));
        QCOMPARE(saved.size(), 1);
        AppTheme builtin;
        QVERIFY(builtin.load(QStringLiteral("cyberdecker")));
        QCOMPARE(builtin.color(QStringLiteral("magenta")), QColor(QStringLiteral("#ff6ff2")));

        // The file alone, in a fresh themes folder, gives the same look.
        QFile file(AppTheme::userDir() + QStringLiteral("/cyberdecker-copy.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray json = file.readAll();
        file.close();
        QTemporaryDir other;
        setConfigHome(other.path());
        QVERIFY(QDir().mkpath(AppTheme::userDir()));
        QFile copy(AppTheme::userDir() + QStringLiteral("/shared.json"));
        QVERIFY(copy.open(QIODevice::WriteOnly));
        copy.write(json);
        copy.close();
        AppTheme elsewhere;
        QVERIFY(elsewhere.load(QStringLiteral("shared")));
        QCOMPARE(elsewhere.toJson(), live.toJson());
        setConfigHome(m_config->path());

        // A second copy does not overwrite the first.
        ThemeEditor again(&live, QStringLiteral("cyberdecker"));
        QVERIFY(again.save());
        QCOMPARE(again.id(), QStringLiteral("cyberdecker-copy-2"));
    }

    void savesAUserThemeInPlace()
    {
        QVERIFY(AppTheme::save(QStringLiteral("mine"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Mine")}}));
        AppTheme live;
        ThemeEditor editor(&live, QStringLiteral("mine"));
        QCOMPARE(editor.id(), QStringLiteral("mine"));
        editor.setName(QStringLiteral("Mine, renamed"));
        editor.setCheckoutColor(0, Qt::red);
        editor.setFont(QStringLiteral("mono"), QStringLiteral("Iosevka"));
        QVERIFY(editor.save());
        AppTheme reread;
        QVERIFY(reread.load(QStringLiteral("mine")));
        QCOMPARE(reread.name(), QStringLiteral("Mine, renamed"));
        QCOMPARE(reread.checkoutColors().first(), QStringLiteral("#ff0000"));
        QCOMPARE(reread.fonts().value(QStringLiteral("mono")).toString(), QStringLiteral("Iosevka"));
    }

    void importsAndExports()
    {
        AppTheme live;
        ThemeEditor editor(&live, QStringLiteral("cyberdecker"));
        QTemporaryDir files;
        const QString in = files.filePath(QStringLiteral("Sunset.json"));
        QFile f(in);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"colors\": {\"bg0\": \"#330000\"}}");
        f.close();
        QSignalSpy saved(&editor, &ThemeEditor::saved);
        QString error;
        QVERIFY2(editor.importFile(in, &error), qPrintable(error));
        QCOMPARE(editor.id(), QStringLiteral("sunset"));
        QCOMPARE(saved.size(), 1);
        QCOMPARE(live.color(QStringLiteral("bg0")), QColor(QStringLiteral("#330000")));
        QVERIFY(QFile::exists(AppTheme::userDir() + QStringLiteral("/sunset.json")));

        const QString out = files.filePath(QStringLiteral("out.json"));
        QVERIFY(editor.exportFile(out));
        QFile exported(out);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(exported.readAll()).object(), editor.theme());

        const QString bad = files.filePath(QStringLiteral("bad.json"));
        QFile b(bad);
        QVERIFY(b.open(QIODevice::WriteOnly));
        b.write("{\"colors\": {\"bg0\": \"nope\"}}");
        b.close();
        QVERIFY(!editor.importFile(bad, &error));
        QVERIFY(error.contains(QLatin1String("bg0")));
    }

private:
    std::unique_ptr<QTemporaryDir> m_config;
};

QTEST_MAIN(ThemeEditorTest)
#include "tst_themeeditor.moc"
