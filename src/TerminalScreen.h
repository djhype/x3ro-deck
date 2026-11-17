#pragma once

#include <QByteArray>
#include <QColor>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>

#include <memory>
#include <utility>

#include "TerminalAppearance.h"

struct VTerm;
struct VTermScreen;
struct VTermState;

// A terminal emulator without a display: libvterm parses what the program
// prints into a grid of cells, and turns keystrokes into the bytes the
// program expects (honoring its modes, such as application cursor keys and
// bracketed paste). TerminalView paints it; tests read it directly.
//
// Lines that scroll off the top of the primary screen go to a scrollback
// buffer. "Line" coordinates cover both: lines 0 to scrollbackSize() - 1 are
// the history, oldest first, and the screen's rows follow.
class TerminalScreen : public QObject
{
    Q_OBJECT
public:
    // The values match libvterm's VTermKey.
    enum Key {
        Enter = 1,
        Tab,
        Backspace,
        Escape,
        Up,
        Down,
        Left,
        Right,
        Insert,
        Delete,
        Home,
        End,
        PageUp,
        PageDown,
        F0 = 256, // F1 is F0 + 1
    };

    struct Cell
    {
        QString text; // empty for a blank cell or the right half of a wide character
        QColor fg; // reverse video already applied
        QColor bg;
        int width = 1; // 2 for a wide character, 0 for the cell it covers
        bool bold = false;
        bool italic = false;
        int underline = 0; // 0 none, 1 single, 2 double, 3 curly
        bool strike = false;
        bool defaultBg = true; // bg is the terminal background
    };

    TerminalScreen(int rows, int cols, QObject *parent = nullptr);
    ~TerminalScreen() override;

    void setAppearance(const TerminalAppearance &appearance);

    // Bytes the program wrote.
    void feed(const QByteArray &bytes);
    void resize(int rows, int cols);
    // Clears the screen and every mode, as on a fresh terminal.
    void reset();
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

    Cell cell(int row, int col) const;
    // The row's characters, trailing blanks removed.
    QString rowText(int row) const;

    // Lines kept at most; older ones are dropped. 0 keeps none.
    void setScrollbackLimit(int lines);
    int scrollbackLimit() const { return m_scrollbackLimit; }
    int scrollbackSize() const;
    void clearScrollback();
    // Lines dropped from the top of the scrollback since the start. Adding it
    // to a line number gives a position that stays put as the history grows.
    qint64 droppedLines() const { return m_dropped; }
    int lineCount() const { return scrollbackSize() + m_rows; }
    Cell lineCell(int line, int col) const;
    // Columns the line holds: the screen width, or the width it had when it
    // scrolled off.
    int lineCols(int line) const;
    // The line ran out of columns and continues on the next one.
    bool lineWrapped(int line) const;
    // The text from `from` to `to`, both included (x = column, y = line).
    // Soft-wrapped lines join without a break; trailing blanks of the other
    // lines are dropped, and wide characters count once.
    QString text(QPoint from, QPoint to) const;
    // The word around a cell, which may run across wrapped lines.
    std::pair<QPoint, QPoint> wordAt(QPoint pos) const;
    // The first and last line of the wrapped line that holds `line`.
    std::pair<int, int> logicalLine(int line) const;
    QPoint cursor() const { return m_cursor; } // x = column, y = row
    bool cursorVisible() const { return m_cursorVisible; }
    bool altScreen() const { return m_altScreen; }
    // The program asked for mouse events (any tracking mode).
    bool mouseReporting() const { return m_mouseMode != 0; }
    QString title() const { return m_title; }

    // Keystrokes; the bytes come out through output().
    void sendKey(Key key, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void sendChar(char32_t ch, Qt::KeyboardModifiers mods = Qt::NoModifier);
    // One paste: bracketed when the program asked for it, line breaks as Enter.
    void paste(const QString &text);
    void setFocused(bool focused);
    // One wheel step at a screen cell, encoded as the program's mouse mode asks.
    void sendWheel(int row, int col, bool up, Qt::KeyboardModifiers mods = Qt::NoModifier);

Q_SIGNALS:
    // Bytes for the program: keystrokes, pastes and replies to its queries.
    void output(const QByteArray &bytes);
    // Cells that need repainting, in cell coordinates (x = column, y = row).
    void changed(const QRect &cells);
    void cursorMoved(const QPoint &from, const QPoint &to);
    void titleChanged(const QString &title);
    void bell();
    // The scrollback grew by `pushed` lines (possibly dropping old ones), or
    // shrank or was cleared (pushed = 0).
    void scrollbackChanged(int pushed);

private:
    friend struct TerminalScreenCallbacks;
    struct Scrollback;
    void flush();

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    VTermState *m_state = nullptr;
    int m_rows;
    int m_cols;
    TerminalAppearance m_appearance;
    QColor m_fg;
    QColor m_bg;
    QPoint m_cursor;
    bool m_cursorVisible = true;
    bool m_altScreen = false;
    bool m_reverseScreen = false;
    int m_mouseMode = 0;
    std::unique_ptr<Scrollback> m_sb;
    int m_scrollbackLimit = 10000;
    qint64 m_dropped = 0;
    int m_pushed = 0;
    bool m_sbChanged = false;
    bool m_resizing = false;
    QString m_title;
    QByteArray m_titleBuf;
    QRect m_damage;
};
