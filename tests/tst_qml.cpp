// Compiles and instantiates every panel in qml/, so a QML error that would
// blank a panel fails here too, also on Qt versions the full app can't build
// against yet. `app` is a bare stub: binding errors from its missing
// properties are expected; component errors are not.

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

QTEST_MAIN(QmlTest)
#include "tst_qml.moc"
