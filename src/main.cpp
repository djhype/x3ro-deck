#include "AppController.h"
#include "AttachClient.h"
#include "MainWindow.h"

#include <KDBusService>

#include <QApplication>
#include <QProcess>
#include <QTimer>

int main(int argc, char *argv[])
{
    // `x3ro-deck attach <checkout>` is a terminal client: no window, no
    // single-instance guard.
    if (argc > 1 && qstrcmp(argv[1], "attach") == 0) {
        return runAttach(argc - 1, argv + 1);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("x3ro-deck"));
    QApplication::setApplicationDisplayName(QStringLiteral("X3RO Deck"));
    QApplication::setOrganizationDomain(QStringLiteral("x3ro.dev"));
    QApplication::setOrganizationName(QStringLiteral("x3ro-deck")); // QSettings → ~/.config/x3ro-deck/x3ro-deck.conf
    QApplication::setDesktopFileName(QStringLiteral("x3ro-deck"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setQuitOnLastWindowClosed(false);

    // Resolved now, while it still names the file: `cmake --install` replaces
    // the binary under a running instance, and a restart should run the new one.
    const QString executable = QApplication::applicationFilePath();

    // A second launch raises the running window instead of starting another.
    KDBusService service(KDBusService::Unique);

    int exitCode = 0;
    bool restart = false;
    {
        AppController controller;
        MainWindow window(&controller);
        QObject::connect(&service, &KDBusService::activateRequested, &window, [&window] { window.reveal(); });

        controller.start();
        window.show();

        // Development flags. --screenshot <file> [delay-ms] renders the window,
        // or the dialog open over it, saves it and quits.
        const QStringList args = QApplication::arguments();
        if (const qsizetype tab = args.indexOf(QStringLiteral("--tab")); tab >= 0 && tab + 1 < args.size()) {
            controller.setTab(args.at(tab + 1));
        }
        if (const qsizetype sel = args.indexOf(QStringLiteral("--select")); sel >= 0 && sel + 1 < args.size()) {
            controller.select(args.at(sel + 1).toInt());
        }
        if (const qsizetype settings = args.indexOf(QStringLiteral("--settings")); settings >= 0) {
            window.openSettings(settings + 1 < args.size() ? args.at(settings + 1) : QString());
        }
        const qsizetype shot = args.indexOf(QStringLiteral("--screenshot"));
        if (shot >= 0 && shot + 1 < args.size()) {
            const QString path = args.at(shot + 1);
            const int delay = shot + 2 < args.size() ? args.at(shot + 2).toInt() : 6000;
            QTimer::singleShot(delay, &window, [&window, path] {
                QWidget *modal = QApplication::activeModalWidget();
                (modal ? modal : &window)->grab().save(path);
                QApplication::quit();
            });
        }
        exitCode = app.exec();
        restart = window.restartRequested();
    }
    // The hook socket closed with the controller; with the D-Bus name released
    // too, the new instance gets both.
    if (restart) {
        service.unregister();
        QProcess::startDetached(executable, {});
    }
    return exitCode;
}
