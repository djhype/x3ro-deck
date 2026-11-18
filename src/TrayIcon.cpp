#include "TrayIcon.h"

#include <QAction>
#include <QMenu>
#include <QPainter>
#include <QSystemTrayIcon>

namespace
{
// Theme.qml's amber, the color of permission and question states.
const QColor kAttention(0xf5, 0xa6, 0x23);
} // namespace

TrayIcon::TrayIcon(const QIcon &icon, QObject *parent)
    : QObject(parent)
    , m_icon(icon)
    , m_attentionIcon(withBadge(icon, kAttention))
    , m_tray(new QSystemTrayIcon(icon, this))
    , m_menu(new QMenu)
{
    m_toggle = m_menu->addAction(QStringLiteral("Hide X3RO Deck"));
    m_menu->addSeparator();
    connect(m_menu->addAction(QIcon::fromTheme(QStringLiteral("help-about")), QStringLiteral("About x3ro deck")), &QAction::triggered, this,
            &TrayIcon::aboutRequested);
    m_quit = m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), QStringLiteral("Quit"));
    connect(m_toggle, &QAction::triggered, this, &TrayIcon::toggleRequested);
    connect(m_quit, &QAction::triggered, this, &TrayIcon::quitRequested);
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            Q_EMIT toggleRequested();
        }
    });
    setNeedsYou(0);
}

TrayIcon::~TrayIcon()
{
    // A context menu is not owned by the tray icon.
    delete m_menu;
}

void TrayIcon::show()
{
    m_tray->show();
}

void TrayIcon::setNeedsYou(int count)
{
    const bool changed = (count > 0) != (m_needsYou > 0);
    m_needsYou = count;
    m_tray->setToolTip(count > 0 ? QStringLiteral("X3RO Deck: %1 item(s) need you").arg(count) : QStringLiteral("X3RO Deck: all quiet"));
    if (changed || m_tray->icon().isNull()) {
        m_tray->setIcon(currentIcon());
    }
}

void TrayIcon::setWindowVisible(bool visible)
{
    m_toggle->setText(visible ? QStringLiteral("Hide X3RO Deck") : QStringLiteral("Show X3RO Deck"));
}

QIcon TrayIcon::currentIcon() const
{
    return m_needsYou > 0 ? m_attentionIcon : m_icon;
}

QString TrayIcon::toolTip() const
{
    return m_tray->toolTip();
}

QIcon TrayIcon::withBadge(const QIcon &base, const QColor &color)
{
    QIcon out;
    for (const int size : {16, 22, 24, 32, 48, 64}) {
        // A pixmap-backed icon never scales up, so draw into a canvas of the size.
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        base.paint(&p, QRect(0, 0, size, size));
        const qreal d = size * 0.5;
        const QRectF dot(size - d, size - d, d, d);
        // The outline keeps the badge readable on light and dark panels.
        p.setPen(QPen(QColor(0x0d, 0x0d, 0x1f), std::max(1.0, size / 16.0)));
        p.setBrush(color);
        p.drawEllipse(dot.adjusted(0.5, 0.5, -0.5, -0.5));
        p.end();
        out.addPixmap(pm);
    }
    return out;
}
