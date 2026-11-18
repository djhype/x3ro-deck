// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// GlobalHotkey on macOS: Carbon's RegisterEventHotKey, which needs no
// Accessibility permission.

#include "GlobalHotkey.h"

#include <QHash>
#include <QMetaObject>
#include <QPointer>

#include <Carbon/Carbon.h>

namespace
{
// Qt::Key → macOS virtual key code, for the keys a shortcut uses.
int virtualKey(Qt::Key key)
{
    static const QHash<int, int> keys{
        {Qt::Key_A, kVK_ANSI_A},   {Qt::Key_B, kVK_ANSI_B},   {Qt::Key_C, kVK_ANSI_C},   {Qt::Key_D, kVK_ANSI_D},
        {Qt::Key_E, kVK_ANSI_E},   {Qt::Key_F, kVK_ANSI_F},   {Qt::Key_G, kVK_ANSI_G},   {Qt::Key_H, kVK_ANSI_H},
        {Qt::Key_I, kVK_ANSI_I},   {Qt::Key_J, kVK_ANSI_J},   {Qt::Key_K, kVK_ANSI_K},   {Qt::Key_L, kVK_ANSI_L},
        {Qt::Key_M, kVK_ANSI_M},   {Qt::Key_N, kVK_ANSI_N},   {Qt::Key_O, kVK_ANSI_O},   {Qt::Key_P, kVK_ANSI_P},
        {Qt::Key_Q, kVK_ANSI_Q},   {Qt::Key_R, kVK_ANSI_R},   {Qt::Key_S, kVK_ANSI_S},   {Qt::Key_T, kVK_ANSI_T},
        {Qt::Key_U, kVK_ANSI_U},   {Qt::Key_V, kVK_ANSI_V},   {Qt::Key_W, kVK_ANSI_W},   {Qt::Key_X, kVK_ANSI_X},
        {Qt::Key_Y, kVK_ANSI_Y},   {Qt::Key_Z, kVK_ANSI_Z},   {Qt::Key_0, kVK_ANSI_0},   {Qt::Key_1, kVK_ANSI_1},
        {Qt::Key_2, kVK_ANSI_2},   {Qt::Key_3, kVK_ANSI_3},   {Qt::Key_4, kVK_ANSI_4},   {Qt::Key_5, kVK_ANSI_5},
        {Qt::Key_6, kVK_ANSI_6},   {Qt::Key_7, kVK_ANSI_7},   {Qt::Key_8, kVK_ANSI_8},   {Qt::Key_9, kVK_ANSI_9},
        {Qt::Key_Space, kVK_Space}, {Qt::Key_Return, kVK_Return}, {Qt::Key_Escape, kVK_Escape}, {Qt::Key_Tab, kVK_Tab},
        {Qt::Key_Left, kVK_LeftArrow}, {Qt::Key_Right, kVK_RightArrow}, {Qt::Key_Up, kVK_UpArrow}, {Qt::Key_Down, kVK_DownArrow},
        {Qt::Key_PageUp, kVK_PageUp}, {Qt::Key_PageDown, kVK_PageDown}, {Qt::Key_Home, kVK_Home}, {Qt::Key_End, kVK_End},
        {Qt::Key_F1, kVK_F1},      {Qt::Key_F2, kVK_F2},      {Qt::Key_F3, kVK_F3},      {Qt::Key_F4, kVK_F4},
        {Qt::Key_F5, kVK_F5},      {Qt::Key_F6, kVK_F6},      {Qt::Key_F7, kVK_F7},      {Qt::Key_F8, kVK_F8},
        {Qt::Key_F9, kVK_F9},      {Qt::Key_F10, kVK_F10},    {Qt::Key_F11, kVK_F11},    {Qt::Key_F12, kVK_F12},
    };
    return keys.value(key, -1);
}

// On macOS Qt calls ⌘ Control and ⌃ Meta.
UInt32 carbonModifiers(Qt::KeyboardModifiers m)
{
    UInt32 out = 0;
    if (m & Qt::ControlModifier) {
        out |= cmdKey;
    }
    if (m & Qt::MetaModifier) {
        out |= controlKey;
    }
    if (m & Qt::AltModifier) {
        out |= optionKey;
    }
    if (m & Qt::ShiftModifier) {
        out |= shiftKey;
    }
    return out;
}

class MacGlobalHotkey : public GlobalHotkey
{
public:
    explicit MacGlobalHotkey(QObject *parent)
        : GlobalHotkey(parent)
    {
    }
    ~MacGlobalHotkey() override
    {
        for (EventHotKeyRef ref : std::as_const(m_refs)) {
            UnregisterEventHotKey(ref);
        }
        if (m_handler) {
            RemoveEventHandler(m_handler);
        }
    }

    void start() override
    {
        const EventTypeSpec pressed{kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(&MacGlobalHotkey::onEvent, 1, &pressed, this, &m_handler);
        UInt32 next = 1;
        for (const Hotkey &h : std::as_const(m_hotkeys)) {
            if (h.sequence.isEmpty()) {
                continue;
            }
            const QKeyCombination combo = h.sequence[0];
            const int key = virtualKey(combo.key());
            if (key < 0) {
                qWarning("x3ro-deck: no macOS key code for the %s shortcut", qPrintable(h.sequence.toString()));
                continue;
            }
            const EventHotKeyID hotKeyId{'x3rd', next};
            EventHotKeyRef ref = nullptr;
            if (RegisterEventHotKey(UInt32(key), carbonModifiers(combo.keyboardModifiers()), hotKeyId, GetApplicationEventTarget(), 0, &ref)
                == noErr) {
                m_refs.insert(next, ref);
                m_ids.insert(next, h.id);
            } else {
                qWarning("x3ro-deck: another app holds %s; the system-wide shortcut is off", qPrintable(h.sequence.toString()));
            }
            ++next;
        }
    }

private:
    static OSStatus onEvent(EventHandlerCallRef, EventRef event, void *data)
    {
        EventHotKeyID id{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id) != noErr) {
            return eventNotHandledErr;
        }
        auto *self = static_cast<MacGlobalHotkey *>(data);
        const QString hotkey = self->m_ids.value(id.id);
        if (!hotkey.isEmpty()) {
            QPointer<MacGlobalHotkey> guard(self);
            QMetaObject::invokeMethod(self, [guard, hotkey] {
                if (guard) {
                    Q_EMIT guard->activated(hotkey, QString());
                }
            }, Qt::QueuedConnection);
        }
        return noErr;
    }

    EventHandlerRef m_handler = nullptr;
    QHash<UInt32, EventHotKeyRef> m_refs;
    QHash<UInt32, QString> m_ids;
};
} // namespace

GlobalHotkey *GlobalHotkey::create(QObject *parent)
{
    return new MacGlobalHotkey(parent);
}
