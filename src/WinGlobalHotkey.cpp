// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// GlobalHotkey on Windows: RegisterHotKey for the GUI thread, whose WM_HOTKEY
// messages a native event filter picks up.

#include "GlobalHotkey.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QHash>

#include <windows.h>

namespace
{
UINT virtualKey(Qt::Key key)
{
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) {
        return UINT(key); // VK codes match ASCII for letters and digits
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return UINT(VK_F1 + (key - Qt::Key_F1));
    }
    static const QHash<int, UINT> keys{{Qt::Key_Space, VK_SPACE},   {Qt::Key_Return, VK_RETURN}, {Qt::Key_Escape, VK_ESCAPE},
                                       {Qt::Key_Tab, VK_TAB},       {Qt::Key_Left, VK_LEFT},     {Qt::Key_Right, VK_RIGHT},
                                       {Qt::Key_Up, VK_UP},         {Qt::Key_Down, VK_DOWN},     {Qt::Key_PageUp, VK_PRIOR},
                                       {Qt::Key_PageDown, VK_NEXT}, {Qt::Key_Home, VK_HOME},     {Qt::Key_End, VK_END}};
    return keys.value(key, 0);
}

UINT modifiers(Qt::KeyboardModifiers m)
{
    UINT out = MOD_NOREPEAT;
    if (m & Qt::ControlModifier) {
        out |= MOD_CONTROL;
    }
    if (m & Qt::AltModifier) {
        out |= MOD_ALT;
    }
    if (m & Qt::ShiftModifier) {
        out |= MOD_SHIFT;
    }
    if (m & Qt::MetaModifier) {
        out |= MOD_WIN;
    }
    return out;
}

class WinGlobalHotkey : public GlobalHotkey, public QAbstractNativeEventFilter
{
public:
    explicit WinGlobalHotkey(QObject *parent)
        : GlobalHotkey(parent)
    {
    }
    ~WinGlobalHotkey() override
    {
        for (auto it = m_ids.cbegin(); it != m_ids.cend(); ++it) {
            UnregisterHotKey(nullptr, it.key());
        }
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }

    void start() override
    {
        QCoreApplication::instance()->installNativeEventFilter(this);
        int next = 1;
        for (const Hotkey &h : std::as_const(m_hotkeys)) {
            if (h.sequence.isEmpty()) {
                continue;
            }
            const QKeyCombination combo = h.sequence[0];
            const UINT vk = virtualKey(combo.key());
            if (!vk) {
                qWarning("x3ro-deck: no Windows key code for the %s shortcut", qPrintable(h.sequence.toString()));
                continue;
            }
            if (RegisterHotKey(nullptr, next, modifiers(combo.keyboardModifiers()), vk)) {
                m_ids.insert(next, h.id);
            } else {
                qWarning("x3ro-deck: another app holds %s (error %lu); the system-wide shortcut is off", qPrintable(h.sequence.toString()),
                         GetLastError());
            }
            ++next;
        }
    }

    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override
    {
        if (type != "windows_generic_MSG") {
            return false;
        }
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_HOTKEY) {
            return false;
        }
        const QString id = m_ids.value(int(msg->wParam));
        if (id.isEmpty()) {
            return false;
        }
        Q_EMIT activated(id, QString());
        return true;
    }

private:
    QHash<int, QString> m_ids;
};
} // namespace

GlobalHotkey *GlobalHotkey::create(QObject *parent)
{
    return new WinGlobalHotkey(parent);
}
