#pragma once

#include "TerminalAppearance.h"

#include <QElapsedTimer>
#include <QKeySequence>
#include <QPointer>
#include <QWidget>

class QTimer;

class TerminalChannel;
class TerminalScreen;

// The built-in terminal: paints a TerminalScreen cell by cell and turns key
// presses into input for the channel behind it. Its grid follows the widget's
// size, and the channel is resized to match.
//
// It shows the screen's scrollback when scrolled back, and keeps a mouse
// selection. Mouse events go to the program only when it asked for them and
// Shift is not held (reportsMouse()); so far that covers the wheel, and
// button and motion reports join it there.
class TerminalView : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalView(QWidget *parent = nullptr);

    // Not owned. Output from the channel is fed to the screen; keystrokes go back.
    void setChannel(TerminalChannel *channel);
    TerminalChannel *channel() const { return m_channel; }
    TerminalScreen *screen() const { return m_screen; }

    void setAppearance(const TerminalAppearance &appearance);
    const TerminalAppearance &appearance() const { return m_appearance; }
    // Shortcuts the window keeps even while the terminal has focus.
    void setReservedShortcuts(const QList<QKeySequence> &shortcuts) { m_reserved = shortcuts; }

    int rows() const;
    int cols() const;
    QSize cellSize() const { return m_cell; }
    // The grid that fits `size`.
    QSize gridFor(const QSize &size) const; // width = columns, height = rows

    void paste(const QString &text);
    // Clears the screen and modes for a fresh program; the scrollback stays.
    void reset();

    // Lines scrolled back from the bottom; 0 follows the output.
    int scrollOffset() const { return m_offset; }
    void scrollTo(int offset);
    void scrollBy(int lines); // positive goes back in history
    // The screen line (TerminalScreen's line numbers) shown in the top row.
    int topLine() const;

    // Selection in line coordinates (x = column, y = line), both ends included.
    void setSelection(QPoint from, QPoint to);
    void clearSelection();
    bool hasSelection() const;
    QString selectedText() const;
    void copySelection();

    QSize sizeHint() const override;

Q_SIGNALS:
    void gridResized(int rows, int cols);

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    bool focusNextPrevChild(bool next) override;

private:
    // A cell by absolute line (TerminalScreen::droppedLines() + line), which
    // stays on the same text while the history grows.
    struct Pos
    {
        qint64 line = 0;
        int col = 0;
    };
    enum class SelectMode { Chars, Words, Lines };

    void updateCellSize();
    bool reportsMouse(Qt::KeyboardModifiers mods) const;
    QPoint viewCellAt(const QPoint &px) const; // clamped to the grid
    Pos posAt(const QPoint &px) const;
    void extendSelection(const QPoint &px);
    void updateSelection();
    // The selection in line coordinates, or an empty pair.
    bool selectionLines(QPoint *from, QPoint *to) const;
    void clampOffset();
    void autoScroll();
    void beforeInput();
    void syncGrid();
    QRect cellsToPixels(const QRect &cells) const;
    void pasteClipboard();

    TerminalScreen *m_screen;
    QPointer<TerminalChannel> m_channel;
    TerminalAppearance m_appearance;
    QFont m_fonts[4]; // regular, bold, italic, bold italic
    QSize m_cell;
    int m_ascent = 0;
    QList<QKeySequence> m_reserved;

    int m_offset = 0;
    int m_wheelDelta = 0;
    SelectMode m_selectMode = SelectMode::Chars;
    bool m_selecting = false; // a button is held
    bool m_selActive = false;
    Pos m_anchor;
    Pos m_head;
    Pos m_selFrom; // normalized and expanded to words or lines
    Pos m_selTo;
    QElapsedTimer m_doubleClick; // for a triple click
    QPoint m_lastMouse;
    QTimer *m_autoScroll;
};
