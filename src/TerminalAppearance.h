#pragma once

#include <QColor>
#include <QFont>

#include <array>

// Everything that decides how the built-in terminal looks. TerminalView takes
// one through setAppearance(), so imported terminal settings only have to
// produce this struct.
struct TerminalAppearance
{
    QFont font;
    QColor foreground;
    QColor background;
    QColor cursor;
    // ANSI colors 0–7, then their bright variants 8–15. Colors 16–255 are the
    // standard xterm cube and grey ramp.
    std::array<QColor, 16> palette;
    int padding = 4; // pixels around the cell grid
    bool boldIsBright = true;

    // The system monospace font with the app's synthwave colors.
    static TerminalAppearance fallback();
};
