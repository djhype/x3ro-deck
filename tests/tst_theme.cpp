#include "AppTheme.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

class TestTheme : public QObject
{
    Q_OBJECT

private:
    static QString read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

private Q_SLOTS:
    void builtinDefinesEveryToken()
    {
        AppTheme theme;
        QCOMPARE(theme.name(), QStringLiteral("Cyberdecker"));
        // Every c("token", …) in Theme.qml, and every ${token} in the templates.
        QStringList tokens;
        static const QRegularExpression qmlToken(QStringLiteral("\\bc\\(\"(\\w+)\""));
        for (auto it = qmlToken.globalMatch(read(QStringLiteral(X3RO_DECK_SOURCE_DIR "/qml/Theme.qml"))); it.hasNext();) {
            tokens.append(it.next().captured(1));
        }
        QVERIFY(tokens.size() > 20);
        static const QRegularExpression tmplToken(QStringLiteral("\\$\\{(\\w+)[}/]"));
        QStringList templates{QStringLiteral(":/data/style.qss")};
        for (QDirIterator it(QStringLiteral(":/data/ui")); it.hasNext();) {
            templates.append(it.next());
        }
        for (const QString &t : std::as_const(templates)) {
            for (auto it = tmplToken.globalMatch(read(t)); it.hasNext();) {
                tokens.append(it.next().captured(1));
            }
        }
        for (const QString &t : std::as_const(tokens)) {
            QVERIFY2(theme.color(t).isValid(), qPrintable(t));
        }
        QCOMPARE(theme.checkoutColors().size(), 6);
        QCOMPARE(theme.fonts().value(QStringLiteral("sans")).toString(), QStringLiteral("Fira Sans"));
    }

    void qmlHasNoHardcodedColors()
    {
        static const QRegularExpression hex(QStringLiteral("#[0-9a-fA-F]{6}"));
        for (QDirIterator it(QStringLiteral(X3RO_DECK_SOURCE_DIR "/qml"), {QStringLiteral("*.qml")}); it.hasNext();) {
            const QString path = it.next();
            if (QFileInfo(path).fileName() != QLatin1String("Theme.qml")) {
                QVERIFY2(!hex.match(read(path)).hasMatch(), qPrintable(path));
            }
        }
    }

    void userThemeOverlaysTheDefault()
    {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        QDir().mkpath(AppTheme::userDir());
        QFile f(AppTheme::userDir() + QStringLiteral("/mine.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"name\": \"Mine\", \"colors\": {\"magenta\": \"#00ff00\"}, \"fonts\": {\"mono\": \"Iosevka\"}}");
        f.close();

        AppTheme theme;
        QVERIFY(theme.load(QStringLiteral("mine")));
        QCOMPARE(theme.name(), QStringLiteral("Mine"));
        QCOMPARE(theme.color(QStringLiteral("magenta")), QColor(0, 255, 0));
        QCOMPARE(theme.color(QStringLiteral("cyan")), QColor(QStringLiteral("#00fff5")));
        QCOMPARE(theme.fonts().value(QStringLiteral("mono")).toString(), QStringLiteral("Iosevka"));
        QCOMPARE(theme.fonts().value(QStringLiteral("sans")).toString(), QStringLiteral("Fira Sans"));
    }

    void badThemesKeepTheDefault()
    {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        QDir().mkpath(AppTheme::userDir());
        QFile f(AppTheme::userDir() + QStringLiteral("/broken.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"colors\": {\"magenta\": \"#00ff00\", \"cyan\": \"not a color\"}}");
        f.close();

        AppTheme theme;
        QString error;
        QVERIFY(!theme.load(QStringLiteral("broken"), &error));
        QVERIFY(error.contains(QLatin1String("cyan")));
        QCOMPARE(theme.color(QStringLiteral("magenta")), QColor(QStringLiteral("#ff6ff2")));
        QVERIFY(!theme.load(QStringLiteral("nope"), &error));
        QCOMPARE(theme.name(), QStringLiteral("Cyberdecker"));
    }

    void fillTemplates()
    {
        QTemporaryDir icons;
        AppTheme theme;
        const QString out
            = theme.fill(QStringLiteral("a: ${magenta}; b: ${magenta/0.5}; f: \"${font.mono}\"; i: url(${icon:check});"), icons.path());
        QCOMPARE(out,
                 QStringLiteral("a: #ff6ff2; b: rgba(255, 111, 242, 0.5); f: \"MesloLGS Nerd Font Mono\"; i: url(%1/check.svg);")
                     .arg(icons.path()));
        const QString svg = read(icons.path() + QStringLiteral("/check.svg"));
        QVERIFY(svg.contains(QLatin1String("#1a1a2e")));
        QVERIFY(!svg.contains(QLatin1String("${")));
    }

    void styleSheetIsComplete()
    {
        QTemporaryDir cache;
        qputenv("XDG_CACHE_HOME", cache.path().toUtf8());
        AppTheme theme;
        const QString qss = theme.styleSheet();
        QVERIFY(qss.contains(QLatin1String("SettingsDialog")));
        QVERIFY(!qss.contains(QLatin1String("${")));
        static const QRegularExpression url(QStringLiteral("url\\(([^)]+)\\)"));
        int icons = 0;
        for (auto it = url.globalMatch(qss); it.hasNext(); ++icons) {
            const QString path = it.next().captured(1);
            QVERIFY2(QFile::exists(path), qPrintable(path));
        }
        QCOMPARE(icons, 4);
    }
};

QTEST_GUILESS_MAIN(TestTheme)
#include "tst_theme.moc"
