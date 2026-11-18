// Compiles and instantiates every panel in qml/, so a QML error that would
// blank a panel fails here too, also on Qt versions the full app can't build
// against yet. `app` is a bare stub: binding errors from its missing
// properties are expected; component errors are not. Theme.qml is also
// checked against a live AppTheme.

#include "AppTheme.h"

#include <QColor>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtTest>

class QmlTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void panelLoads_data();
    void panelLoads();
    void themeFollowsAppTheme();
};

void QmlTest::panelLoads_data()
{
    QTest::addColumn<QString>("file");
    const QDir dir(QStringLiteral(X3RO_DECK_QML_DIR));
    const QStringList files = dir.entryList({QStringLiteral("*.qml")}, QDir::Files);
    QVERIFY(!files.isEmpty());
    for (const QString &f : files) {
        QTest::newRow(qPrintable(f)) << dir.filePath(f);
    }
}

void QmlTest::panelLoads()
{
    QFETCH(QString, file);
    QQmlEngine engine;
    QObject stub;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &stub);
    QQmlComponent component(&engine, QUrl::fromLocalFile(file));
    std::unique_ptr<QObject> root(component.create());
    QString errors;
    for (const QQmlError &e : component.errors()) {
        errors += e.toString() + QLatin1Char('\n');
    }
    QVERIFY2(errors.isEmpty() && root, qPrintable(errors));
}

// Theme.qml's tokens follow the theme the app switches to, without a restart.
void QmlTest::themeFollowsAppTheme()
{
    QQmlEngine engine;
    AppTheme theme;
    engine.rootContext()->setContextProperty(QStringLiteral("appTheme"), &theme);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(X3RO_DECK_QML_DIR "/Theme.qml")));
    std::unique_ptr<QObject> t(component.create());
    QVERIFY2(t, qPrintable(component.errorString()));
    QCOMPARE(t->property("bg0").value<QColor>(), QColor(QStringLiteral("#0a0a16")));
    theme.select(QStringLiteral("cyberdecker-light"));
    QCOMPARE(t->property("bg0").value<QColor>(), theme.color(QStringLiteral("bg0")));
    QCOMPARE(t->property("checkoutColors").toStringList(), theme.checkoutColors());
}

QTEST_MAIN(QmlTest)
#include "tst_qml.moc"
