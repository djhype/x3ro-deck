#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

// x3ro-deck-sessiond's model of one session's terminal, QtCore and libvterm
// only. Everything the session prints goes through feed(); redraw() turns the
// current state back into bytes that rebuild it on a fresh terminal of the
// same size, so a client attaching late sees the screen at once instead of a
// replay of raw output written at another size.
//
// The redraw covers the cells (text, colors, attributes, wide characters,
// soft wraps), the alternate screen, the cursor (position, visibility, shape),
// the pen, the title and the modes that change what the attached terminal
// sends: cursor and keypad keys, bracketed paste, mouse and focus reporting,
// kitty keyboard flags and modifyOtherKeys, plus autowrap, origin mode and
// the scroll region. It leaves out scrollback and, while the alternate screen
// is up, the hidden primary screen.
class SessionScreen
{
public:
    SessionScreen(int rows, int cols);
    ~SessionScreen();
    SessionScreen(const SessionScreen &) = delete;
    SessionScreen &operator=(const SessionScreen &) = delete;

    void feed(const QByteArray &bytes);
    // Rewraps the primary screen's lines to the new width.
    void resize(int rows, int cols);
    QByteArray redraw() const;

    int rows() const;
    int cols() const;
    // The row's characters, trailing blanks removed.
    QString rowText(int row) const;
    int cursorRow() const;
    int cursorCol() const;
    bool cursorVisible() const;
    bool altScreen() const;
    QString title() const;

    struct Private;

private:
    std::unique_ptr<Private> d;
};
