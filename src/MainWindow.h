#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>

class AppController;
class GlobalHotkey;
class Notifier;
class QQmlEngine;
class QQuickWidget;
class QSplitter;
class QStackedWidget;
class AppTheme;
class QTabWidget;
class AboutDialog;
class SettingsDialog;
class TerminalHost;
class TrayIcon;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(AppController *app, QWidget *parent = nullptr);

    // `token` proves a user asked (see WindowActivation); may be empty.
    void toggleVisible(const QString &token = QString());
    // A tray click deactivates the window before it arrives, so a window
    // that was active just before counts as active.
    void toggleFromTray();
    void reveal(const QString &token = QString());
    // checkouts | agents | docker | general; empty opens the first page.
    void openSettings(const QString &page);
    void openAbout();
    // Set when the app quit to start again; main() relaunches it.
    bool restartRequested() const { return m_restart; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

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
    void onNotificationAction(const QString &id, const QString &key);
    void updateStatsVisibility();
    void saveLayout();
    void applySettings(const QJsonObject &config);
    void selectTheme();
    // Quits after asking whether the agent sessions should stop too.
    void confirmQuit();
    void restart();

    AppController *m_app;
    QQmlEngine *m_engine;
    AppTheme *m_theme = nullptr;
    QStackedWidget *m_pages;
    QStackedWidget *m_shellStack;
    QStackedWidget *m_agentStack;
    QQuickWidget *m_rail;
    QList<QSplitter *> m_splitters;
    QHash<int, QTabWidget *> m_shellTabs;
    QHash<int, TerminalHost *> m_agentTerms;
    TrayIcon *m_tray = nullptr;
    Notifier *m_notifier = nullptr;
    GlobalHotkey *m_hotkeys = nullptr;
    // From the notification whose action comes next.
    QString m_notificationToken;
    QPointer<SettingsDialog> m_settings;
    QPointer<AboutDialog> m_about;
    bool m_quitting = false;
    bool m_confirmingQuit = false;
    bool m_restart = false;
    QElapsedTimer m_deactivated;
};
