// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TerminalAppearance.h"

#include <QFontDatabase>

TerminalAppearance TerminalAppearance::fallback()
{
    TerminalAppearance a;
    a.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    a.font.setStyleHint(QFont::Monospace);
    a.font.setFixedPitch(true);
    a.font.setKerning(false);
    // Colors from qml/Theme.qml.
    a.foreground = QColor(0xe6, 0xe6, 0xf5);
    a.background = QColor(0x0a, 0x0a, 0x16);
    a.cursor = QColor(0x00, 0xff, 0xf5);
    a.selection = QColor(0x4a, 0x34, 0x7a);
    a.palette = {
        QColor(0x1a, 0x1a, 0x2e), // black
        QColor(0xff, 0x5c, 0x7a), // red
        QColor(0x5a, 0xf7, 0x8e), // green
        QColor(0xf5, 0xa6, 0x23), // yellow
        QColor(0x57, 0xc7, 0xff), // blue
        QColor(0xff, 0x6f, 0xf2), // magenta
        QColor(0x00, 0xe5, 0xdc), // cyan
        QColor(0xc8, 0xc8, 0xe8), // white
        QColor(0x55, 0x55, 0x80), // bright black
        QColor(0xff, 0x80, 0x99), // bright red
        QColor(0x86, 0xff, 0xae), // bright green
        QColor(0xff, 0xc8, 0x5c), // bright yellow
        QColor(0x8c, 0xd9, 0xff), // bright blue
        QColor(0xff, 0x9c, 0xf6), // bright magenta
        QColor(0x00, 0xff, 0xf5), // bright cyan
        QColor(0xff, 0xff, 0xff), // bright white
    };
    return a;
}
