#include "AppController.h"
#include "MainWindow.h"

#include <KDBusService>

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    // Agent terminals run `tmux attach`, which refuses to nest inside another
    // tmux client; x3ro-deck may itself be launched from a tmux pane.
    qunsetenv("TMUX");
    qunsetenv("TMUX_PANE");

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("x3ro-deck"));
    QApplication::setApplicationDisplayName(QStringLiteral("Example Control"));
    QApplication::setOrganizationDomain(QStringLiteral("x3ro.dev"));
    QApplication::setOrganizationName(QStringLiteral("x3ro-deck")); // QSettings → ~/.config/x3ro-deck/x3ro-deck.conf
    QApplication::setDesktopFileName(QStringLiteral("x3ro-deck"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setQuitOnLastWindowClosed(false);

    // A second launch raises the running window instead of starting another.
    KDBusService service(KDBusService::Unique);

    AppController controller;
    MainWindow window(&controller);
    QObject::connect(&service, &KDBusService::activateRequested, &window, [&window] { window.reveal(); });

    controller.start();
    window.show();

    // --screenshot <file> [delay-ms]: render the window, save it and quit (for development).
    const QStringList args = QApplication::arguments();
    if (const qsizetype tab = args.indexOf(QStringLiteral("--tab")); tab >= 0 && tab + 1 < args.size()) {
        controller.setTab(args.at(tab + 1));
    }
    if (const qsizetype sel = args.indexOf(QStringLiteral("--select")); sel >= 0 && sel + 1 < args.size()) {
        controller.select(args.at(sel + 1).toInt());
    }
    const qsizetype shot = args.indexOf(QStringLiteral("--screenshot"));
    if (shot >= 0 && shot + 1 < args.size()) {
        const QString path = args.at(shot + 1);
        const int delay = shot + 2 < args.size() ? args.at(shot + 2).toInt() : 6000;
        QTimer::singleShot(delay, &window, [&window, path] {
            window.grab().save(path);
            QApplication::quit();
        });
    }
    return app.exec();
}
