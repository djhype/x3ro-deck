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
    QColor selection; // background of selected cells; their text keeps its color
    // ANSI colors 0–7, then their bright variants 8–15. Colors 16–255 are the
    // standard xterm cube and grey ramp.
    std::array<QColor, 16> palette;
    int padding = 4; // pixels around the cell grid
    int lineSpacing = 0; // extra pixels between rows
    bool boldIsBright = true;
    // The cursor before a program picks its own (DECSCUSR) and after a reset.
    // The values are libvterm's, as in TerminalScreen::CursorShape.
    enum CursorShape { Block = 1, Underline, Bar };
    CursorShape cursorShape = Block;
    bool cursorBlink = false;

    bool operator==(const TerminalAppearance &) const = default;

    // The system monospace font with the app's synthwave colors.
    static TerminalAppearance fallback();
};
