#include "MainWindow.h"

#include "AboutDialog.h"
#include "AppController.h"
#include "AppTheme.h"
#include "GlobalHotkey.h"
#include "Notifier.h"
#include "SettingsDialog.h"
#include "TerminalHost.h"
#include "TerminalProfile.h"
#include "TerminalSettings.h"
#include "TrayIcon.h"
#include "WindowActivation.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
const QStringList kShells{QStringLiteral("zsh"), QStringLiteral("bash"), QStringLiteral("fish"), QStringLiteral("sh")};

QString stateText(const QString &state)
{
    if (state == QLatin1String("working")) return QStringLiteral("working");
    if (state == QLatin1String("permission")) return QStringLiteral("needs permission");
    if (state == QLatin1String("question")) return QStringLiteral("has a question");
    if (state == QLatin1String("done")) return QStringLiteral("done, waiting for you");
    return QStringLiteral("ready");
}

// A new shell tab's title: the imported shell's name.
QString shellTitle()
{
    const QString name = QFileInfo(TerminalHost::profile().shell.value(0)).fileName();
    return name.isEmpty() ? QStringLiteral("shell") : name;
}
} // namespace

MainWindow::MainWindow(AppController *app, QWidget *parent)
    : QMainWindow(parent)
    , m_app(app)
    , m_engine(new QQmlEngine(this))
{
    setWindowTitle(QStringLiteral("X3RO Deck"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("x3ro-deck"), QIcon(QStringLiteral(":/data/icons/x3ro-deck.svg"))));
    resize(1600, 980);
    TerminalHost::setScrollbackLines(m_app->config().terminalScrollback);
    connect(m_app->terminalSettings(), &TerminalSettings::changed, this, [](const TerminalProfile &p) { TerminalHost::setProfile(p); });
    TerminalHost::setProfile(m_app->terminalSettings()->current());

    m_theme = new AppTheme(this);
    if (QString error; !m_theme->load(m_app->config().theme, &error)) {
        qWarning("x3ro-deck: theme %s: %s; using the default", qPrintable(m_app->config().theme), qPrintable(error));
    }
    setStyleSheet(m_theme->styleSheet());

    m_engine->rootContext()->setContextProperty(QStringLiteral("app"), m_app);
    m_engine->rootContext()->setContextProperty(QStringLiteral("appTheme"), m_theme);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *topBar = quick(QStringLiteral("TopBar.qml"), central);
    topBar->setFixedHeight(64);
    root->addWidget(topBar);

    auto *body = new QSplitter(Qt::Horizontal, central);
    body->setObjectName(QStringLiteral("bodySplit"));
    body->setChildrenCollapsible(false);
    root->addWidget(body, 1);

    auto *checkoutRail = quick(QStringLiteral("CheckoutRail.qml"), body);
    checkoutRail->setMinimumWidth(220);
    checkoutRail->setMaximumWidth(340);
    body->addWidget(checkoutRail);

    auto *center = new QWidget(body);
    center->setMinimumWidth(760);
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    auto *header = quick(QStringLiteral("ContextHeader.qml"), center);
    header->setFixedHeight(84);
    centerLayout->addWidget(header);

    m_pages = new QStackedWidget(center);
    centerLayout->addWidget(m_pages, 1);

    // Docker page: dashboard above, shell tabs below.
    auto *dockerPage = new QSplitter(Qt::Vertical, m_pages);
    dockerPage->setObjectName(QStringLiteral("dockerSplit"));
    dockerPage->setChildrenCollapsible(false);
    auto *dockerPanel = quick(QStringLiteral("DockerPanel.qml"), dockerPage);
    dockerPanel->setMinimumHeight(220);
    dockerPage->addWidget(dockerPanel);
    m_shellStack = new QStackedWidget(dockerPage);
    m_shellStack->setObjectName(QStringLiteral("shellStack"));
    dockerPage->addWidget(m_shellStack);
    dockerPage->setStretchFactor(0, 3);
    dockerPage->setStretchFactor(1, 2);
    m_pages->addWidget(dockerPage);

    // Agents page: the agent's session beside its session details.
    auto *agentsPage = new QSplitter(Qt::Horizontal, m_pages);
    agentsPage->setObjectName(QStringLiteral("agentsSplit"));
    agentsPage->setChildrenCollapsible(false);
    m_agentStack = new QStackedWidget(agentsPage);
    m_agentStack->setObjectName(QStringLiteral("agentStack"));
    agentsPage->addWidget(m_agentStack);
    auto *session = quick(QStringLiteral("SessionPanel.qml"), agentsPage);
    session->setMinimumWidth(280);
    session->setMaximumWidth(420);
    agentsPage->addWidget(session);
    agentsPage->setStretchFactor(0, 1);
    agentsPage->setStretchFactor(1, 0);
    m_pages->addWidget(agentsPage);

    body->addWidget(center);

    m_rail = quick(QStringLiteral("NeedsYouRail.qml"), body);
    m_rail->setMinimumWidth(300);
    m_rail->setMaximumWidth(480);
    body->addWidget(m_rail);

    body->setStretchFactor(0, 0);
    body->setStretchFactor(1, 1);
    body->setStretchFactor(2, 0);
    body->setSizes({250, 1000, 350});
    agentsPage->setSizes({1000, 320});

    // First run: the welcome screen stands in for the layout until checkouts exist.
    auto *frame = new QStackedWidget(this);
    if (m_app->firstRun()) {
        frame->addWidget(quick(QStringLiteral("Welcome.qml"), frame));
    }
    frame->addWidget(central);
    setCentralWidget(frame);
    connect(m_app, &AppController::welcomeFolderRequested, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a checkout"), QDir::homePath());
        m_app->setWelcomeFolder(dir);
    });
    connect(m_app, &AppController::welcomeFinished, this, &MainWindow::restart);
    connect(m_app, &AppController::welcomeFailed, this, [this](const QString &error) {
        QMessageBox::warning(this, QStringLiteral("X3RO Deck"), QStringLiteral("Could not save the checkouts.\n\n%1").arg(error));
    });

    // Window geometry and every splitter position survive restarts.
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    m_splitters = {body, dockerPage, agentsPage};
    for (QSplitter *sp : std::as_const(m_splitters)) {
        sp->restoreState(settings.value(QStringLiteral("splitter/") + sp->objectName()).toByteArray());
    }

    connect(m_app, &AppController::currentChanged, this, &MainWindow::syncCurrent);
    connect(m_app, &AppController::tabChanged, this, &MainWindow::syncTab);
    connect(m_app, &AppController::agentStateChanged, this, &MainWindow::syncAccents);
    connect(m_app, &AppController::runInTerminal, this, &MainWindow::runInTerminal);
    connect(m_app, &AppController::focusAgentTerminal, this, [this](int i) {
        if (TerminalHost *t = agentTerminal(i)) {
            QTimer::singleShot(0, t, &TerminalHost::focusTerminal);
        }
    });
    m_notifier = Notifier::create(this);
    connect(m_notifier, &Notifier::activationToken, this, [this](const QString &, const QString &token) { m_notificationToken = token; });
    connect(m_notifier, &Notifier::actionInvoked, this, &MainWindow::onNotificationAction);
    connect(m_app, &AppController::attentionRaised, this, &MainWindow::notify);
    connect(m_app, &AppController::railVisibleChanged, this, [this] { m_rail->setVisible(m_app->railVisible()); });
    connect(m_app, &AppController::settingsRequested, this, &MainWindow::openSettings);
    connect(m_app, &AppController::totalsChanged, this, [this] {
        if (m_tray) {
            m_tray->setNeedsYou(m_app->needsYou());
        }
    });
    connect(qApp, &QApplication::aboutToQuit, this, [this] {
        m_quitting = true;
        saveLayout();
    });

    // Agent sessions start with the app; the terminals that attach to them
    // load on first view.
    for (int i = 0; i < m_app->checkoutCount(); ++i) {
        m_app->ensureAgentSession(i);
    }

    buildShortcuts();
    buildTray();
    syncCurrent();
    syncTab();
}

QQuickWidget *MainWindow::quick(const QString &file, QWidget *parent)
{
    auto *w = new QQuickWidget(m_engine, parent);
    w->setResizeMode(QQuickWidget::SizeRootObjectToView);
    w->setClearColor(m_theme->color(QStringLiteral("bg1")));
    w->setSource(QUrl(QStringLiteral("qrc:/qml/") + file));
    for (const QQmlError &e : w->errors()) {
        qWarning("%s", qPrintable(e.toString()));
    }
    return w;
}

void MainWindow::buildShortcuts()
{
    QList<QKeySequence> keys;
    auto add = [this, &keys](const QKeySequence &seq, auto fn) {
        auto *a = new QAction(this);
        a->setShortcut(seq);
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
        keys.append(seq);
    };
    for (int i = 0; i < std::min(9, m_app->checkoutCount()); ++i) {
        add(QKeySequence(Qt::ALT | Qt::Key(Qt::Key_1 + i)), [this, i] { m_app->select(i); });
    }
    add(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D), [this] { m_app->setTab(QStringLiteral("docker")); });
    add(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A), [this] { m_app->setTab(QStringLiteral("agents")); });
    add(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R), [this] { m_app->toggleRail(); });
    add(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T), [this] {
        m_app->setTab(QStringLiteral("docker"));
        addShellTab(m_app->currentIndex(), shellTitle())->focusTerminal();
    });
    add(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N), [this] {
        if (m_app->attentionModel()->rowCount() > 0) {
            m_app->attentionAction(m_app->attentionModel()->row(0).value(QStringLiteral("id")).toString(), QStringLiteral("open"));
        }
    });
    add(QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { confirmQuit(); });
    // Also in the window, for desktops without the global shortcuts portal.
    const QKeySequence toggle(Qt::META | Qt::ALT | Qt::Key_H);
    add(toggle, [this] { toggleVisible(); });
    TerminalHost::setAppShortcuts(keys);

    m_hotkeys = GlobalHotkey::create(this);
    m_hotkeys->add(QStringLiteral("toggle-window"), QStringLiteral("Show/hide X3RO Deck"), toggle);
    connect(m_hotkeys, &GlobalHotkey::activated, this, [this](const QString &id, const QString &token) {
        if (id == QLatin1String("toggle-window")) {
            toggleVisible(token);
        }
    });
    m_hotkeys->start();
}

void MainWindow::buildTray()
{
    m_tray = new TrayIcon(windowIcon(), this);
    m_tray->setNeedsYou(m_app->needsYou());
    m_tray->setWindowVisible(isVisible());
    connect(m_tray, &TrayIcon::toggleRequested, this, &MainWindow::toggleFromTray);
    // Queued: the prompt must not run inside the tray menu's activation.
    connect(m_tray, &TrayIcon::quitRequested, this, &MainWindow::confirmQuit, Qt::QueuedConnection);
    connect(m_tray, &TrayIcon::aboutRequested, this, &MainWindow::openAbout, Qt::QueuedConnection);
    m_tray->show();
}

void MainWindow::confirmQuit()
{
    if (m_confirmingQuit) {
        return;
    }
    m_confirmingQuit = true;
    QStringList running;
    for (int i = 0; i < m_app->checkoutCount(); ++i) {
        const Checkout &c = m_app->checkout(i);
        if (c.agent.running) {
            running.append(QStringLiteral("• %1 · %2, %3").arg(c.cfg.name, m_app->profile(i).label, stateText(c.agent.state)));
        }
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Quit X3RO Deck"));
    box.setText(QStringLiteral("Stop the agents too?"));
    box.setInformativeText(
        (running.isEmpty() ? QStringLiteral("No agent is running.") : QStringLiteral("Running now:\n") + running.join(QLatin1Char('\n')))
        + QStringLiteral("\n\nStopping ends every agent session, so the next launch starts each checkout's agent fresh. "
                         "Kept agents carry on, and the next launch reattaches to them."));
    QPushButton *stop = box.addButton(QStringLiteral("Stop agents and quit"), QMessageBox::DestructiveRole);
    QPushButton *keep = box.addButton(QStringLiteral("Quit, keep agents"), QMessageBox::AcceptRole);
    box.setEscapeButton(box.addButton(QMessageBox::Cancel));
    box.setDefaultButton(keep);
    box.exec();
    m_confirmingQuit = false;
    if (box.clickedButton() != stop && box.clickedButton() != keep) {
        return;
    }
    // Set before the attached terminals exit, so none recreates its session.
    m_quitting = true;
    if (box.clickedButton() == stop) {
        m_app->sessions()->killAll();
    }
    qApp->quit();
}

void MainWindow::restart()
{
    m_restart = true;
    m_quitting = true;
    qApp->quit();
}

void MainWindow::openAbout()
{
    if (m_about) {
        m_about->raise();
        m_about->activateWindow();
        return;
    }
    // Over Settings when it is open, so it stays in front of it.
    QWidget *parent = m_settings ? static_cast<QWidget *>(m_settings) : this;
    if (parent == this) {
        reveal();
    }
    auto *dialog = new AboutDialog(m_theme->color(QStringLiteral("cyan")), parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_about = dialog;
    dialog->open();
}

void MainWindow::openSettings(const QString &page)
{
    if (m_settings) {
        m_settings->showPage(page);
        m_settings->raise();
        m_settings->activateWindow();
        return;
    }
    QString error;
    const QJsonObject config = Config::readJson(&error);
    if (!error.isEmpty()) {
        // Saving the form over a file it could not parse would lose the file's contents.
        if (QMessageBox::warning(this, QStringLiteral("Settings"), error + QStringLiteral("\n\nFix the file in a text editor first."),
                                 QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Open)
            == QMessageBox::Open) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(Config::configPath()));
        }
        return;
    }
    reveal();
    auto *dialog = new SettingsDialog(config, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->showPage(page);
    const auto fillDependencies = [this, dialog] {
        dialog->setDependencies(m_app->dependencyChecker()->list(), m_app->dependencyChecker()->ready());
    };
    fillDependencies();
    connect(m_app, &AppController::dependenciesChanged, dialog, fillDependencies);
    connect(dialog, &QDialog::accepted, this, [this, dialog] { applySettings(dialog->config()); });
    connect(dialog, &SettingsDialog::aboutRequested, this, &MainWindow::openAbout);
    m_settings = dialog;
    dialog->open();
}

void MainWindow::applySettings(const QJsonObject &config)
{
    QString error;
    if (!Config::writeJson(config, &error)) {
        QMessageBox::warning(this, QStringLiteral("Settings"), QStringLiteral("Could not save the settings.\n\n%1").arg(error));
        return;
    }
    const bool applied = m_app->reloadConfig();
    TerminalHost::setScrollbackLines(m_app->config().terminalScrollback);
    if (applied) {
        return;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Restart X3RO Deck"));
    box.setText(QStringLiteral("The checkout list or the session backend changed. It takes effect when X3RO Deck restarts."));
    box.setInformativeText(QStringLiteral("Agents keep running through the restart. Shell tabs close."));
    QPushButton *now = box.addButton(QStringLiteral("Restart now"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(now);
    box.exec();
    if (box.clickedButton() == now) {
        restart();
    }
}

void MainWindow::toggleVisible(const QString &token)
{
    if (isVisible() && isActiveWindow()) {
        hide();
    } else {
        reveal(token);
    }
}

void MainWindow::toggleFromTray()
{
    const bool wasActive = isActiveWindow() || (m_deactivated.isValid() && m_deactivated.elapsed() < 500);
    if (isVisible() && !isMinimized() && wasActive) {
        hide();
    } else {
        reveal();
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        m_deactivated.start();
    }
}

void MainWindow::reveal(const QString &token)
{
    WindowActivation::activate(this, token);
}

void MainWindow::saveLayout()
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    for (QSplitter *sp : std::as_const(m_splitters)) {
        settings.setValue(QStringLiteral("splitter/") + sp->objectName(), sp->saveState());
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveLayout();
    // Closing hides to the tray so hook events and notifications keep flowing.
    // Ctrl+Q or the tray menu quits; agents keep running either way. Without
    // a tray (stock GNOME) a hidden window could not come back, so it minimizes.
    // Only the window manager's close counts: QApplication::quit() closes the
    // windows first and gives up if one stays visible.
    if (!m_quitting && event->spontaneous()) {
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            hide();
        } else {
            showMinimized();
        }
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    updateStatsVisibility();
    if (m_tray) {
        m_tray->setWindowVisible(true);
    }
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    updateStatsVisibility();
    if (m_tray) {
        m_tray->setWindowVisible(false);
    }
}

void MainWindow::updateStatsVisibility()
{
    m_app->setStatsVisible(isVisible() && m_app->tab() == QLatin1String("docker"));
}

// ───────────────────────── terminals ─────────────────────────

QTabWidget *MainWindow::shellTabs(int checkout)
{
    if (QTabWidget *t = m_shellTabs.value(checkout)) {
        return t;
    }
    auto *tabs = new QTabWidget(m_shellStack);
    tabs->setObjectName(QStringLiteral("shellTabs"));
    tabs->setDocumentMode(true);
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    auto *plus = new QToolButton(tabs);
    plus->setObjectName(QStringLiteral("newTab"));
    plus->setText(QStringLiteral("+"));
    plus->setToolTip(QStringLiteral("New shell (Ctrl+Alt+T)"));
    connect(plus, &QToolButton::clicked, this, [this, checkout] { addShellTab(checkout, shellTitle())->focusTerminal(); });
    tabs->setCornerWidget(plus, Qt::TopRightCorner);
    connect(tabs, &QTabWidget::tabCloseRequested, this, [tabs](int i) {
        QWidget *w = tabs->widget(i);
        tabs->removeTab(i);
        w->deleteLater();
    });
    m_shellStack->addWidget(tabs);
    m_shellTabs.insert(checkout, tabs);
    addShellTab(checkout, shellTitle());
    return tabs;
}

TerminalHost *MainWindow::addShellTab(int checkout, const QString &title)
{
    QTabWidget *tabs = shellTabs(checkout);
    const Checkout &c = m_app->checkout(checkout);
    auto *term = TerminalHost::shell(c.cfg.path, tabs);
    const int idx = tabs->addTab(term, title);
    tabs->setCurrentIndex(idx);
    connect(term, &TerminalHost::exited, this, [this, tabs, term, checkout] {
        if (m_quitting) {
            return;
        }
        const int i = tabs->indexOf(term);
        if (i >= 0) {
            tabs->removeTab(i);
        }
        term->deleteLater();
        if (tabs->count() == 0) {
            addShellTab(checkout, shellTitle());
        }
    });
    return term;
}

void MainWindow::runInTerminal(int checkout, const QString &title, const QString &command)
{
    m_app->setTab(QStringLiteral("docker"));
    QTabWidget *tabs = shellTabs(checkout);
    TerminalHost *target = nullptr;
    // Reuse a tab with the same title if its shell is idle, so repeated runs
    // of one quick command do not pile up tabs.
    for (int i = 0; i < tabs->count(); ++i) {
        auto *t = qobject_cast<TerminalHost *>(tabs->widget(i));
        if (t && tabs->tabText(i) == title && kShells.contains(t->foregroundProcessName())) {
            target = t;
            tabs->setCurrentIndex(i);
            break;
        }
    }
    if (!target) {
        target = addShellTab(checkout, title);
    }
    target->sendInput(command + QLatin1Char('\n'));
    target->focusTerminal();
}

TerminalHost *MainWindow::agentTerminal(int checkout)
{
    if (TerminalHost *t = m_agentTerms.value(checkout)) {
        return t;
    }
    if (!m_app->ensureAgentSession(checkout)) {
        return nullptr;
    }
    const Checkout &c = m_app->checkout(checkout);
    // Konsole's startProgram takes the full argv, program name included.
    const QStringList argv = m_app->sessions()->attachCommand(c.session);
    // The built-in terminal reads the session's stream directly when the
    // backend has one, by the session's name at the time it (re)attaches.
    auto direct = [this, checkout](int rows, int cols, QObject *parent) {
        return m_app->sessions()->attachChannel(m_app->checkout(checkout).session, rows, cols, parent);
    };
    auto *term = TerminalHost::program(argv.first(), argv, m_agentStack, direct);
    m_agentStack->addWidget(term);
    m_agentTerms.insert(checkout, term);
    // The attached client exits when the session is killed or detached: recreate
    // the session if needed and reattach.
    connect(term, &TerminalHost::exited, this, [this, term, checkout] {
        if (m_quitting) {
            return;
        }
        QTimer::singleShot(700, term, [this, term, checkout] {
            m_app->ensureAgentSession(checkout);
            term->respawn();
        });
    });
    syncAccents();
    return term;
}

void MainWindow::syncCurrent()
{
    const int i = m_app->currentIndex();
    if (i >= m_app->checkoutCount()) {
        return;
    }
    m_shellStack->setCurrentWidget(shellTabs(i));
    if (m_app->tab() == QLatin1String("agents")) {
        if (TerminalHost *t = agentTerminal(i)) {
            m_agentStack->setCurrentWidget(t);
        }
    }
}

void MainWindow::syncTab()
{
    const bool agents = m_app->tab() == QLatin1String("agents");
    m_pages->setCurrentIndex(agents ? 1 : 0);
    syncCurrent();
    if (agents) {
        if (TerminalHost *t = m_agentTerms.value(m_app->currentIndex())) {
            t->focusTerminal();
        }
    }
    updateStatsVisibility();
}

void MainWindow::syncAccents()
{
    for (auto it = m_agentTerms.cbegin(); it != m_agentTerms.cend(); ++it) {
        const QString state = m_app->checkout(it.key()).agent.state;
        const bool attention = state == QLatin1String("permission") || state == QLatin1String("question");
        it.value()->setAccent(attention ? QStringLiteral("attention") : QString());
    }
}

void MainWindow::notify(const QVariantMap &item)
{
    const QString kind = item.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("done") && !m_app->config().notifyOnDone) {
        return;
    }
    const int checkout = item.value(QStringLiteral("checkout")).toInt();
    // Skip what you are already looking at.
    if (isActiveWindow() && checkout == m_app->currentIndex()) {
        return;
    }
    const QString id = item.value(QStringLiteral("id")).toString();
    Notifier::Notification n;
    const QString who = kind == QLatin1String("docker")
        ? item.value(QStringLiteral("checkoutName")).toString()
        : QStringLiteral("%1 · %2").arg(item.value(QStringLiteral("checkoutName")).toString(),
                                        item.value(QStringLiteral("agentLabel")).toString());
    n.title = QStringLiteral("%1 · %2").arg(who, item.value(QStringLiteral("title")).toString());
    n.body = item.value(QStringLiteral("detail")).toString();
    if (kind == QLatin1String("permission")) {
        n.urgency = Notifier::Urgency::Critical;
        n.actions = {{QStringLiteral("allow"), QStringLiteral("Allow")}, {QStringLiteral("deny"), QStringLiteral("Deny")}};
    }
    m_notifier->show(n, id);
}

void MainWindow::onNotificationAction(const QString &id, const QString &key)
{
    const QString token = std::exchange(m_notificationToken, QString());
    if (key == Notifier::defaultAction()) {
        reveal(token);
        m_app->attentionAction(id, QStringLiteral("open"));
    } else if (key == QLatin1String("allow") || key == QLatin1String("deny")) {
        m_app->attentionAction(id, key);
    }
}
