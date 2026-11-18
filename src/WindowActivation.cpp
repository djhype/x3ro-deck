// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WindowActivation.h"

#include <QGuiApplication>
#include <QRegularExpression>
#include <QWidget>
#include <QWindow>

#if X3RO_DECK_HAVE_XCB
#include <QtGui/qguiapplication_platform.h>

#include <xcb/xcb.h>

#include <cstdlib>
#include <cstring>
#endif

namespace WindowActivation
{

QList<QByteArray> startupInfoChunks(const QByteArray &message)
{
    QByteArray data = message;
    data.append('\0');
    QList<QByteArray> chunks;
    for (qsizetype i = 0; i < data.size(); i += 20) {
        QByteArray chunk = data.mid(i, 20);
        chunk.append(QByteArray(20 - chunk.size(), '\0'));
        chunks.append(chunk);
    }
    return chunks;
}

quint32 startupIdTime(const QString &startupId)
{
    static const QRegularExpression re(QStringLiteral("_TIME(\\d+)$"));
    const QRegularExpressionMatch m = re.match(startupId);
    return m.hasMatch() ? m.captured(1).toUInt() : 0;
}

#if X3RO_DECK_HAVE_XCB
namespace
{
xcb_atom_t atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, std::strlen(name), name), nullptr);
    const xcb_atom_t a = reply ? reply->atom : xcb_atom_t(XCB_ATOM_NONE);
    std::free(reply);
    return a;
}

xcb_window_t rootOf(xcb_connection_t *c)
{
    return xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
}

// Ends the launcher's busy cursor for a launch that only raised us.
void removeStartup(xcb_connection_t *c, const QString &startupId)
{
    QString id = startupId;
    id.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    const QList<QByteArray> chunks = startupInfoChunks(QStringLiteral("remove: ID=\"%1\"").arg(id).toUtf8());
    const xcb_atom_t begin = atom(c, "_NET_STARTUP_INFO_BEGIN");
    const xcb_atom_t more = atom(c, "_NET_STARTUP_INFO");
    const xcb_window_t root = rootOf(c);
    // The sending window has to exist; a throwaway input-only one does.
    const xcb_window_t sender = xcb_generate_id(c);
    xcb_create_window(c, XCB_COPY_FROM_PARENT, sender, root, -100, -100, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, 0,
                      nullptr);
    for (qsizetype i = 0; i < chunks.size(); ++i) {
        xcb_client_message_event_t ev{};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.format = 8;
        ev.window = sender;
        ev.type = i == 0 ? begin : more;
        std::memcpy(ev.data.data8, chunks.at(i).constData(), 20);
        xcb_send_event(c, 0, root, XCB_EVENT_MASK_PROPERTY_CHANGE, reinterpret_cast<const char *>(&ev));
    }
    xcb_destroy_window(c, sender);
}

void activateX11(QWindow *window, const QString &token)
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !x11->connection()) {
        window->requestActivate();
        return;
    }
    xcb_connection_t *c = x11->connection();
    if (!token.isEmpty()) {
        removeStartup(c, token);
    }
    // Source indication 2 ("pager"): the user asked for this window, so the
    // window manager skips its focus stealing prevention.
    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = static_cast<xcb_window_t>(window->winId());
    ev.type = atom(c, "_NET_ACTIVE_WINDOW");
    ev.data.data32[0] = 2;
    ev.data.data32[1] = startupIdTime(token); // 0 = CurrentTime
    ev.data.data32[2] = XCB_WINDOW_NONE;
    xcb_send_event(c, 0, rootOf(c), XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                   reinterpret_cast<const char *>(&ev));
    xcb_flush(c);
}
} // namespace
#endif

void activate(QWidget *window, const QString &token)
{
    activate(window, token, QGuiApplication::platformName());
}

void activate(QWidget *window, const QString &token, const QString &platform)
{
    window->setWindowState((window->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    window->show();
    window->raise();
    QWindow *handle = window->windowHandle();
    if (!handle) {
        window->activateWindow();
        return;
    }
    if (platform.startsWith(QLatin1String("wayland"))) {
        // Qt's xdg-shell integration passes this to xdg_activation_v1 in
        // requestActivate() and unsets it. Without one it asks for attention.
        if (!token.isEmpty()) {
            qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
        }
        handle->requestActivate();
        return;
    }
#if X3RO_DECK_HAVE_XCB
    if (platform == QLatin1String("xcb")) {
        activateX11(handle, token);
        return;
    }
#endif
    window->activateWindow();
}

} // namespace WindowActivation
