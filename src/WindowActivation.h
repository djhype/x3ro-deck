#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

class QWidget;

// Brings a top-level window to the front with focus, handing the desktop
// the token that proves a user asked for it. Focus stealing prevention
// refuses activation from a background app without one on Wayland.
namespace WindowActivation
{
// `token`: an xdg-activation token (Wayland) or a startup id (X11), from a
// second launch, a notification click or a global shortcut. May be empty.
void activate(QWidget *window, const QString &token);

// Same, with the platform named explicitly (tests).
void activate(QWidget *window, const QString &token, const QString &platform);

// X11 startup notification: `message` split into the 20-byte chunks of
// _NET_STARTUP_INFO_BEGIN/_NET_STARTUP_INFO client messages, NUL-terminated.
QList<QByteArray> startupInfoChunks(const QByteArray &message);
// The X server time in a startup id ("…_TIME12345"), or 0.
quint32 startupIdTime(const QString &startupId);
} // namespace WindowActivation
