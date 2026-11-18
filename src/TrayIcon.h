#pragma once

#include <QIcon>
#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;

// The system tray entry. QSystemTrayIcon has no attention status, so NEEDS
// YOU shows as the app icon with a badge.
class TrayIcon : public QObject
{
    Q_OBJECT
public:
    explicit TrayIcon(const QIcon &icon, QObject *parent = nullptr);
    ~TrayIcon() override;

    void show();
    void setNeedsYou(int count);
    bool needsAttention() const { return m_needsYou > 0; }
    // Names the menu's toggle after what it would do.
    void setWindowVisible(bool visible);

    QIcon currentIcon() const;
    QString toolTip() const;
    QAction *toggleAction() const { return m_toggle; }
    QAction *quitAction() const { return m_quit; }

    // `base` with a filled circle in the bottom-right corner.
    static QIcon withBadge(const QIcon &base, const QColor &color);
    // A plain balloon, where the OS has no notification backend yet.
    void showMessage(const QString &title, const QString &body);

Q_SIGNALS:
    void toggleRequested();
    void quitRequested();
    void aboutRequested();
    void messageClicked();

private:
    QIcon m_icon;
    QIcon m_attentionIcon;
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    QAction *m_toggle;
    QAction *m_quit;
    int m_needsYou = 0;
};
