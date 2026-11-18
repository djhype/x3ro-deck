// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GlobalHotkey.h"

namespace
{
// Until an OS has its own backend; the in-window shortcuts still work.
class NoGlobalHotkey : public GlobalHotkey
{
public:
    using GlobalHotkey::GlobalHotkey;
    void start() override { }
};
} // namespace

GlobalHotkey *GlobalHotkey::create(QObject *parent)
{
    return new NoGlobalHotkey(parent);
}
