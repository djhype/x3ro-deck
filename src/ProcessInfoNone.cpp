// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// ProcessInfo's platform layer where none exists yet: reports nothing, so the
// pane poll sees no agent running. macOS (libproc) and Windows (Toolhelp32)
// get their own files.

#include "ProcessInfo.h"

namespace ProcessInfo
{

qint64 foregroundGroup(int)
{
    return 0;
}

std::optional<Process> process(qint64)
{
    return std::nullopt;
}

QList<Process> tree(qint64)
{
    return {};
}

} // namespace ProcessInfo
