#pragma once

#include <QHash>
#include <QMainWindow>

class AppController;
class KStatusNotifierItem;
class QQmlEngine;
class QQuickWidget;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class TerminalHost;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(AppController *app, QWidget *parent = nullptr);

    void toggleVisible();
    void reveal();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QQuickWidget *quick(const QString &file, QWidget *parent);
    void buildShortcuts();
    void buildTray();
    QTabWidget *shellTabs(int checkout);
    TerminalHost *agentTerminal(int checkout);
    TerminalHost *addShellTab(int checkout, const QString &title);
    void runInTerminal(int checkout, const QString &title, const QString &command);
    void syncCurrent();
    void syncTab();
    void syncAccents();
    void notify(const QVariantMap &item);
    void updateStatsVisibility();
    void saveLayout();

    AppController *m_app;
    QQmlEngine *m_engine;
    QStackedWidget *m_pages;
    QStackedWidget *m_shellStack;
    QStackedWidget *m_agentStack;
    QQuickWidget *m_rail;
    QList<QSplitter *> m_splitters;
    QHash<int, QTabWidget *> m_shellTabs;
    QHash<int, TerminalHost *> m_agentTerms;
    KStatusNotifierItem *m_tray = nullptr;
    bool m_quitting = false;
};
