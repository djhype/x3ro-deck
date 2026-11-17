#pragma once

#include "TerminalAppearance.h"

#include <QKeySequence>
#include <QPointer>
#include <QWidget>

class TerminalChannel;
class TerminalScreen;

// The built-in terminal: paints a TerminalScreen cell by cell and turns key
// presses into input for the channel behind it. Its grid follows the widget's
// size, and the channel is resized to match.
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

    QSize sizeHint() const override;

Q_SIGNALS:
    void gridResized(int rows, int cols);

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    bool focusNextPrevChild(bool next) override;

private:
    void updateCellSize();
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
};
