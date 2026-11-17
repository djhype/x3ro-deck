#pragma once

#include <QByteArray>
#include <QColor>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>

#include "TerminalAppearance.h"

struct VTerm;
struct VTermScreen;
struct VTermState;

// A terminal emulator without a display: libvterm parses what the program
// prints into a grid of cells, and turns keystrokes into the bytes the
// program expects (honoring its modes, such as application cursor keys and
// bracketed paste). TerminalView paints it; tests read it directly.
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
    QPoint cursor() const { return m_cursor; } // x = column, y = row
    bool cursorVisible() const { return m_cursorVisible; }
    bool altScreen() const { return m_altScreen; }
    QString title() const { return m_title; }

    // Keystrokes; the bytes come out through output().
    void sendKey(Key key, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void sendChar(char32_t ch, Qt::KeyboardModifiers mods = Qt::NoModifier);
    // One paste: bracketed when the program asked for it, line breaks as Enter.
    void paste(const QString &text);
    void setFocused(bool focused);

Q_SIGNALS:
    // Bytes for the program: keystrokes, pastes and replies to its queries.
    void output(const QByteArray &bytes);
    // Cells that need repainting, in cell coordinates (x = column, y = row).
    void changed(const QRect &cells);
    void cursorMoved(const QPoint &from, const QPoint &to);
    void titleChanged(const QString &title);
    void bell();

private:
    friend struct TerminalScreenCallbacks;
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
    QString m_title;
    QByteArray m_titleBuf;
    QRect m_damage;
};
