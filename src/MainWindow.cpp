#include "MainWindow.h"

#include "AppController.h"
#include "TerminalHost.h"

#include <KGlobalAccel>
#include <KNotification>
#include <KStatusNotifierItem>
#include <KWindowSystem>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QMenu>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
const QStringList kShells{QStringLiteral("zsh"), QStringLiteral("bash"), QStringLiteral("fish"), QStringLiteral("sh")};
}

MainWindow::MainWindow(AppController *app, QWidget *parent)
    : QMainWindow(parent)
    , m_app(app)
    , m_engine(new QQmlEngine(this))
{
    setWindowTitle(QStringLiteral("X3RO Deck"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("x3ro-deck"), QIcon(QStringLiteral(":/data/icons/x3ro-deck.svg"))));
    resize(1600, 980);

    QFile qss(QStringLiteral(":/data/style.qss"));
    if (qss.open(QIODevice::ReadOnly)) {
        setStyleSheet(QString::fromUtf8(qss.readAll()));
    }

    m_engine->rootContext()->setContextProperty(QStringLiteral("app"), m_app);

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

    // Agents page: the agent's tmux session beside its session details.
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

    setCentralWidget(central);

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
    connect(m_app, &AppController::attentionRaised, this, &MainWindow::notify);
    connect(m_app, &AppController::railVisibleChanged, this, [this] { m_rail->setVisible(m_app->railVisible()); });
    connect(m_app, &AppController::totalsChanged, this, [this] {
        if (m_tray) {
            const int n = m_app->needsYou();
            m_tray->setStatus(n > 0 ? KStatusNotifierItem::NeedsAttention : KStatusNotifierItem::Active);
            m_tray->setToolTip(QStringLiteral("utilities-terminal"), QStringLiteral("X3RO Deck"),
                               n > 0 ? QStringLiteral("%1 item(s) need you").arg(n) : QStringLiteral("All quiet"));
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
    w->setClearColor(QColor(QStringLiteral("#0d0d1f")));
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
        addShellTab(m_app->currentIndex(), QStringLiteral("zsh"))->focusTerminal();
    });
    add(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N), [this] {
        if (m_app->attentionModel()->rowCount() > 0) {
            m_app->attentionAction(m_app->attentionModel()->row(0).value(QStringLiteral("id")).toString(), QStringLiteral("open"));
        }
    });
    add(QKeySequence(Qt::CTRL | Qt::Key_Q), [] { qApp->quit(); });
    TerminalHost::setAppShortcuts(keys);

    auto *toggle = new QAction(QStringLiteral("Show/hide X3RO Deck"), this);
    toggle->setObjectName(QStringLiteral("toggle-window"));
    connect(toggle, &QAction::triggered, this, &MainWindow::toggleVisible);
    KGlobalAccel::setGlobalShortcut(toggle, QKeySequence(Qt::META | Qt::ALT | Qt::Key_H));
}

void MainWindow::buildTray()
{
    m_tray = new KStatusNotifierItem(QStringLiteral("x3ro-deck"), this);
    m_tray->setIconByName(QStringLiteral("x3ro-deck"));
    m_tray->setAttentionIconByName(QStringLiteral("dialog-warning"));
    m_tray->setTitle(QStringLiteral("X3RO Deck"));
    m_tray->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_tray->setStatus(KStatusNotifierItem::Active);
    m_tray->setStandardActionsEnabled(true);
    connect(m_tray, &KStatusNotifierItem::activateRequested, this, [this](bool, const QPoint &) { toggleVisible(); });
}

void MainWindow::toggleVisible()
{
    if (isVisible() && isActiveWindow()) {
        hide();
    } else {
        reveal();
    }
}

void MainWindow::reveal()
{
    show();
    raise();
    activateWindow();
    if (windowHandle()) {
        KWindowSystem::activateWindow(windowHandle());
    }
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
    // Ctrl+Q or the tray menu quits; agents keep running in tmux either way.
    if (!m_quitting) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    updateStatsVisibility();
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    updateStatsVisibility();
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
    connect(plus, &QToolButton::clicked, this, [this, checkout] { addShellTab(checkout, QStringLiteral("zsh"))->focusTerminal(); });
    tabs->setCornerWidget(plus, Qt::TopRightCorner);
    connect(tabs, &QTabWidget::tabCloseRequested, this, [tabs](int i) {
        QWidget *w = tabs->widget(i);
        tabs->removeTab(i);
        w->deleteLater();
    });
    m_shellStack->addWidget(tabs);
    m_shellTabs.insert(checkout, tabs);
    addShellTab(checkout, QStringLiteral("zsh"));
    return tabs;
}

TerminalHost *MainWindow::addShellTab(int checkout, const QString &title)
{
    QTabWidget *tabs = shellTabs(checkout);
    const Checkout &c = m_app->checkout(checkout);
    auto *term = TerminalHost::shell(c.cfg.path, m_app->config().konsoleProfile, tabs);
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
            addShellTab(checkout, QStringLiteral("zsh"));
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
    const QStringList args = m_app->tmux()->attachArgs(c.session);
    auto *term = TerminalHost::program(QStringLiteral("tmux"), args, m_app->config().konsoleProfile, m_agentStack);
    m_agentStack->addWidget(term);
    m_agentTerms.insert(checkout, term);
    // The tmux client exits when the session is killed or detached: recreate
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
    auto *n = new KNotification(kind == QLatin1String("docker") ? QStringLiteral("containerIssue") : QStringLiteral("needsYou"));
    const QString who = kind == QLatin1String("docker")
        ? item.value(QStringLiteral("checkoutName")).toString()
        : QStringLiteral("%1 · %2").arg(item.value(QStringLiteral("checkoutName")).toString(), item.value(QStringLiteral("agentLabel")).toString());
    n->setTitle(QStringLiteral("%1 · %2").arg(who, item.value(QStringLiteral("title")).toString()));
    n->setText(item.value(QStringLiteral("detail")).toString().toHtmlEscaped());
    n->setIconName(QStringLiteral("x3ro-deck"));
    if (kind == QLatin1String("permission")) {
        n->setUrgency(KNotification::HighUrgency);
        connect(n->addAction(QStringLiteral("Allow")), &KNotificationAction::activated, this, [this, id] {
            m_app->attentionAction(id, QStringLiteral("allow"));
        });
        connect(n->addAction(QStringLiteral("Deny")), &KNotificationAction::activated, this, [this, id] {
            m_app->attentionAction(id, QStringLiteral("deny"));
        });
    }
    connect(n->addDefaultAction(QStringLiteral("Open")), &KNotificationAction::activated, this, [this, id] {
        reveal();
        m_app->attentionAction(id, QStringLiteral("open"));
    });
    n->sendEvent();
}
