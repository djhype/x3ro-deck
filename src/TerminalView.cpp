#include "TerminalView.h"

#include "TerminalChannel.h"
#include "TerminalScreen.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPainter>
#include <QTimer>

namespace
{
bool before(qint64 aLine, int aCol, qint64 bLine, int bCol)
{
    return aLine < bLine || (aLine == bLine && aCol < bCol);
}
} // namespace

TerminalView::TerminalView(QWidget *parent)
    : QWidget(parent)
    , m_screen(new TerminalScreen(24, 80, this))
    , m_autoScroll(new QTimer(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setCursor(Qt::IBeamCursor);
    setAppearance(TerminalAppearance::fallback());

    m_autoScroll->setInterval(50);
    connect(m_autoScroll, &QTimer::timeout, this, &TerminalView::autoScroll);

    connect(m_screen, &TerminalScreen::changed, this, [this](const QRect &cells) {
        if (m_offset > 0 && m_screen->altScreen()) {
            m_offset = 0; // the history belongs to the primary screen
            update();
            return;
        }
        update(cellsToPixels(cells.translated(0, m_offset)));
    });
    connect(m_screen, &TerminalScreen::cursorMoved, this, [this](const QPoint &from, const QPoint &to) {
        update(cellsToPixels(QRect(from + QPoint(0, m_offset), QSize(2, 1))));
        update(cellsToPixels(QRect(to + QPoint(0, m_offset), QSize(2, 1))));
    });
    // Output never moves a scrolled-back view: the same text stays in place
    // while new lines arrive below it.
    connect(m_screen, &TerminalScreen::scrollbackChanged, this, [this](int pushed) {
        if (m_offset > 0) {
            m_offset += pushed;
        }
        clampOffset();
        if (m_selActive && m_selTo.line < m_screen->droppedLines()) {
            m_selActive = false;
        }
        update();
    });
    connect(m_screen, &TerminalScreen::output, this, [this](const QByteArray &bytes) {
        if (m_channel) {
            m_channel->write(bytes);
        }
    });
}

void TerminalView::setChannel(TerminalChannel *channel)
{
    if (m_channel) {
        disconnect(m_channel, nullptr, this, nullptr);
    }
    m_channel = channel;
    if (channel) {
        connect(channel, &TerminalChannel::received, m_screen, &TerminalScreen::feed);
        channel->resize(rows(), cols());
    }
}

void TerminalView::setAppearance(const TerminalAppearance &appearance)
{
    m_appearance = appearance;
    QFont base = appearance.font;
    base.setStyleHint(QFont::Monospace);
    base.setKerning(false);
    for (int i = 0; i < 4; ++i) {
        QFont f = base;
        f.setBold(i & 1);
        f.setItalic(i & 2);
        m_fonts[i] = f;
    }
    m_screen->setAppearance(appearance);
    updateCellSize();
    syncGrid();
    update();
}

void TerminalView::updateCellSize()
{
    const QFontMetrics fm(m_fonts[0]);
    m_cell = QSize(std::max(1, fm.horizontalAdvance(QLatin1Char('W'))), std::max(1, fm.height()));
    m_ascent = fm.ascent();
}

int TerminalView::rows() const
{
    return m_screen->rows();
}

int TerminalView::cols() const
{
    return m_screen->cols();
}

QSize TerminalView::gridFor(const QSize &size) const
{
    const int pad = 2 * m_appearance.padding;
    return QSize(std::max(2, (size.width() - pad) / m_cell.width()), std::max(1, (size.height() - pad) / m_cell.height()));
}

QSize TerminalView::sizeHint() const
{
    const int pad = 2 * m_appearance.padding;
    return QSize(80 * m_cell.width() + pad, 24 * m_cell.height() + pad);
}

void TerminalView::syncGrid()
{
    const QSize grid = gridFor(size());
    if (grid.width() == cols() && grid.height() == rows()) {
        return;
    }
    m_screen->resize(grid.height(), grid.width());
    if (m_channel) {
        m_channel->resize(grid.height(), grid.width());
    }
    Q_EMIT gridResized(grid.height(), grid.width());
}

void TerminalView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    syncGrid();
    clampOffset();
}

void TerminalView::reset()
{
    m_screen->reset();
    m_offset = 0;
    m_selActive = false;
    m_selecting = false;
    update();
}

int TerminalView::topLine() const
{
    return m_screen->scrollbackSize() - m_offset;
}

void TerminalView::clampOffset()
{
    const int max = m_screen->altScreen() ? 0 : m_screen->scrollbackSize();
    m_offset = std::clamp(m_offset, 0, max);
}

void TerminalView::scrollTo(int offset)
{
    const int before = m_offset;
    m_offset = offset;
    clampOffset();
    if (m_offset != before) {
        update();
    }
}

void TerminalView::scrollBy(int lines)
{
    scrollTo(m_offset + lines);
}

void TerminalView::beforeInput()
{
    scrollTo(0);
}

QRect TerminalView::cellsToPixels(const QRect &cells) const
{
    const int pad = m_appearance.padding;
    return QRect(pad + cells.x() * m_cell.width(), pad + cells.y() * m_cell.height(), cells.width() * m_cell.width(),
                 cells.height() * m_cell.height());
}

void TerminalView::paintEvent(QPaintEvent *e)
{
    QPainter p(this);
    const QRect dirty = e->rect();
    p.fillRect(dirty, m_appearance.background);

    const int pad = m_appearance.padding;
    const int cw = m_cell.width();
    const int ch = m_cell.height();
    const int firstRow = std::max(0, (dirty.top() - pad) / ch);
    const int lastRow = std::min(rows() - 1, (dirty.bottom() - pad) / ch);
    // One column early, so a wide character that starts left of the dirty area is drawn.
    const int firstCol = std::max(0, (dirty.left() - pad) / cw - 1);
    const int lastCol = std::min(cols() - 1, (dirty.right() - pad) / cw);
    const QPoint cursor = m_screen->cursor() + QPoint(0, m_offset);
    const bool showCursor = m_screen->cursorVisible();
    const bool focused = hasFocus();
    const int top = topLine();
    QPoint selFrom;
    QPoint selTo;
    const bool selection = selectionLines(&selFrom, &selTo);
    int currentFont = -1;

    for (int row = firstRow; row <= lastRow; ++row) {
        const int y = pad + row * ch;
        const int line = top + row;
        for (int col = firstCol; col <= lastCol; ++col) {
            TerminalScreen::Cell c = m_screen->lineCell(line, col);
            if (c.width == 0) {
                continue;
            }
            const QRect r(pad + col * cw, y, cw * c.width, ch);
            const bool isCursor = showCursor && cursor == QPoint(col, row);
            const bool selected
                = selection && !before(line, col + c.width - 1, selFrom.y(), selFrom.x()) && !before(selTo.y(), selTo.x(), line, col);
            if (isCursor && focused) {
                p.fillRect(r, m_appearance.cursor);
                c.fg = m_appearance.background;
            } else if (selected) {
                p.fillRect(r, m_appearance.selection);
            } else if (!c.defaultBg) {
                p.fillRect(r, c.bg);
            }
            if (!c.text.isEmpty() && c.text != QLatin1String(" ")) {
                const int f = (c.bold ? 1 : 0) | (c.italic ? 2 : 0);
                if (f != currentFont) {
                    p.setFont(m_fonts[f]);
                    currentFont = f;
                }
                p.setPen(c.fg);
                p.drawText(r.x(), y + m_ascent, c.text);
            }
            if (c.underline) {
                const int uy = std::min(y + m_ascent + 2, y + ch - 1);
                p.fillRect(QRect(r.x(), uy, r.width(), 1), c.fg);
                if (c.underline == 2 && uy + 2 < y + ch) {
                    p.fillRect(QRect(r.x(), uy + 2, r.width(), 1), c.fg);
                }
            }
            if (c.strike) {
                p.fillRect(QRect(r.x(), y + ch / 2, r.width(), 1), c.fg);
            }
            if (isCursor && !focused) {
                p.setPen(m_appearance.cursor);
                p.drawRect(r.adjusted(0, 0, -1, -1));
            }
        }
    }

    // Where the view is in the history, in the right padding while scrolled back.
    if (m_offset > 0) {
        const int total = m_screen->lineCount();
        const int h = height();
        const int thumb = std::max(8, h * rows() / total);
        const int y = (h - thumb) * top / std::max(1, total - rows());
        QColor c = m_appearance.cursor;
        c.setAlpha(160);
        p.fillRect(QRect(width() - std::max(2, pad - 1), y, std::max(2, pad - 1), thumb), c);
    }
}

bool TerminalView::event(QEvent *e)
{
    // The terminal claims every key except the window's own shortcuts, so
    // Ctrl+R, Ctrl+T and the like reach the program.
    if (e->type() == QEvent::ShortcutOverride) {
        auto *k = static_cast<QKeyEvent *>(e);
        const QKeySequence seq(k->keyCombination());
        for (const QKeySequence &s : std::as_const(m_reserved)) {
            if (s.matches(seq) == QKeySequence::ExactMatch) {
                return QWidget::event(e);
            }
        }
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

bool TerminalView::focusNextPrevChild(bool)
{
    return false; // Tab and Shift+Tab belong to the program
}

void TerminalView::keyPressEvent(QKeyEvent *e)
{
    const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    const int key = e->key();

#ifdef Q_OS_MACOS
    // Qt reports Cmd as Control on macOS, so Cmd+C and Cmd+V leave Ctrl+C alone.
    const Qt::KeyboardModifiers clipMods = Qt::ControlModifier;
#else
    const Qt::KeyboardModifiers clipMods = Qt::ControlModifier | Qt::ShiftModifier;
#endif
    if ((key == Qt::Key_V && mods == clipMods) || (key == Qt::Key_Insert && mods == Qt::ShiftModifier)) {
        pasteClipboard();
        return;
    }
    if (key == Qt::Key_C && mods == clipMods) {
        copySelection();
        return;
    }
    if ((key == Qt::Key_PageUp || key == Qt::Key_PageDown) && mods == Qt::ShiftModifier && !m_screen->altScreen()) {
        const int page = std::max(1, rows() - 1);
        scrollBy(key == Qt::Key_PageUp ? page : -page);
        return;
    }

    auto special = [key]() -> int {
        switch (key) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return TerminalScreen::Enter;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            return TerminalScreen::Tab;
        case Qt::Key_Backspace:
            return TerminalScreen::Backspace;
        case Qt::Key_Escape:
            return TerminalScreen::Escape;
        case Qt::Key_Up:
            return TerminalScreen::Up;
        case Qt::Key_Down:
            return TerminalScreen::Down;
        case Qt::Key_Left:
            return TerminalScreen::Left;
        case Qt::Key_Right:
            return TerminalScreen::Right;
        case Qt::Key_Insert:
            return TerminalScreen::Insert;
        case Qt::Key_Delete:
            return TerminalScreen::Delete;
        case Qt::Key_Home:
            return TerminalScreen::Home;
        case Qt::Key_End:
            return TerminalScreen::End;
        case Qt::Key_PageUp:
            return TerminalScreen::PageUp;
        case Qt::Key_PageDown:
            return TerminalScreen::PageDown;
        default:
            if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
                return TerminalScreen::F0 + 1 + (key - Qt::Key_F1);
            }
            return 0;
        }
    }();
    if (special) {
        Qt::KeyboardModifiers m = mods;
        if (key == Qt::Key_Backtab) {
            m |= Qt::ShiftModifier;
        }
        beforeInput();
        m_screen->sendKey(TerminalScreen::Key(special), m);
        return;
    }

    // Ctrl combinations go by the key, not the text: Qt's text for them is
    // already a control character, or empty.
    if ((mods & Qt::ControlModifier) && key >= 0x20 && key < 0x7f) {
        char32_t ch = char32_t(key);
        if (ch >= U'A' && ch <= U'Z') {
            ch += U'a' - U'A';
        }
        beforeInput();
        m_screen->sendChar(ch, mods);
        return;
    }

    const QString text = e->text();
    if (text.isEmpty()) {
        QWidget::keyPressEvent(e);
        return;
    }
    const QList<uint> chars = text.toUcs4();
    for (const uint ch : chars) {
        if (ch < 0x20 || ch == 0x7f) {
            continue; // a control character with no key we know: drop it
        }
        beforeInput();
        m_screen->sendChar(char32_t(ch), mods & ~Qt::ShiftModifier);
    }
}

void TerminalView::paste(const QString &text)
{
    if (!text.isEmpty()) {
        beforeInput();
        m_screen->paste(text);
    }
}

void TerminalView::pasteClipboard()
{
    paste(QApplication::clipboard()->text());
}

bool TerminalView::reportsMouse(Qt::KeyboardModifiers mods) const
{
    return m_screen->mouseReporting() && !(mods & Qt::ShiftModifier);
}

QPoint TerminalView::viewCellAt(const QPoint &px) const
{
    const int pad = m_appearance.padding;
    const int col = std::clamp((px.x() - pad) / m_cell.width(), 0, cols() - 1);
    const int row = std::clamp((px.y() - pad) / m_cell.height(), 0, rows() - 1);
    return QPoint(col, row);
}

TerminalView::Pos TerminalView::posAt(const QPoint &px) const
{
    const QPoint cell = viewCellAt(px);
    return Pos{m_screen->droppedLines() + topLine() + cell.y(), cell.x()};
}

void TerminalView::setSelection(QPoint from, QPoint to)
{
    const qint64 dropped = m_screen->droppedLines();
    m_selectMode = SelectMode::Chars;
    m_anchor = Pos{dropped + from.y(), from.x()};
    m_head = Pos{dropped + to.y(), to.x()};
    m_selActive = true;
    updateSelection();
}

void TerminalView::clearSelection()
{
    if (m_selActive) {
        m_selActive = false;
        update();
    }
}

bool TerminalView::hasSelection() const
{
    QPoint from;
    QPoint to;
    return selectionLines(&from, &to);
}

void TerminalView::updateSelection()
{
    const qint64 dropped = m_screen->droppedLines();
    auto toLine = [dropped](const Pos &p) {
        return QPoint(p.col, int(std::max<qint64>(0, p.line - dropped)));
    };
    QPoint a = toLine(m_anchor);
    QPoint h = toLine(m_head);
    if (before(h.y(), h.x(), a.y(), a.x())) {
        std::swap(a, h);
    }
    switch (m_selectMode) {
    case SelectMode::Chars:
        break;
    case SelectMode::Words:
        a = m_screen->wordAt(a).first;
        h = m_screen->wordAt(h).second;
        break;
    case SelectMode::Lines: {
        const int last = m_screen->logicalLine(h.y()).second;
        a = QPoint(0, m_screen->logicalLine(a.y()).first);
        h = QPoint(std::max(0, m_screen->lineCols(last) - 1), last);
        break;
    }
    }
    m_selFrom = Pos{dropped + a.y(), a.x()};
    m_selTo = Pos{dropped + h.y(), h.x()};
    update();
}

bool TerminalView::selectionLines(QPoint *from, QPoint *to) const
{
    const qint64 dropped = m_screen->droppedLines();
    if (!m_selActive || m_selTo.line < dropped) {
        return false;
    }
    *from = m_selFrom.line < dropped ? QPoint(0, 0) : QPoint(m_selFrom.col, int(m_selFrom.line - dropped));
    *to = QPoint(m_selTo.col, int(m_selTo.line - dropped));
    return true;
}

QString TerminalView::selectedText() const
{
    QPoint from;
    QPoint to;
    return selectionLines(&from, &to) ? m_screen->text(from, to) : QString();
}

void TerminalView::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text, QClipboard::Clipboard);
    }
}

void TerminalView::extendSelection(const QPoint &px)
{
    const Pos head = posAt(px);
    if (head.line == m_head.line && head.col == m_head.col && m_selActive) {
        return;
    }
    m_head = head;
    m_selActive = m_selectMode != SelectMode::Chars || m_head.line != m_anchor.line || m_head.col != m_anchor.col;
    updateSelection();
}

void TerminalView::mousePressEvent(QMouseEvent *e)
{
    // Button reports to the program (PE-131) go here, under reportsMouse().
    m_lastMouse = e->position().toPoint();
    if (e->button() == Qt::MiddleButton) {
        QClipboard *clip = QApplication::clipboard();
        paste(clip->text(clip->supportsSelection() ? QClipboard::Selection : QClipboard::Clipboard));
        return;
    }
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    const bool triple = m_doubleClick.isValid() && m_doubleClick.elapsed() < QApplication::doubleClickInterval();
    m_doubleClick.invalidate();
    m_selectMode = triple ? SelectMode::Lines : SelectMode::Chars;
    m_selecting = true;
    m_anchor = m_head = posAt(m_lastMouse);
    m_selActive = triple;
    if (triple) {
        updateSelection();
    } else {
        update();
    }
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(e);
        return;
    }
    m_doubleClick.start();
    m_selectMode = SelectMode::Words;
    m_selecting = true;
    m_anchor = m_head = posAt(e->position().toPoint());
    m_selActive = true;
    updateSelection();
}

void TerminalView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_selecting) {
        QWidget::mouseMoveEvent(e);
        return;
    }
    m_lastMouse = e->position().toPoint();
    extendSelection(m_lastMouse);
    const int pad = m_appearance.padding;
    const bool outside = m_lastMouse.y() < pad || m_lastMouse.y() >= height() - pad;
    if (outside && !m_autoScroll->isActive()) {
        m_autoScroll->start();
    } else if (!outside) {
        m_autoScroll->stop();
    }
}

void TerminalView::autoScroll()
{
    if (!m_selecting) {
        m_autoScroll->stop();
        return;
    }
    scrollBy(m_lastMouse.y() < m_appearance.padding ? 1 : -1);
    extendSelection(m_lastMouse);
}

void TerminalView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !m_selecting) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    m_selecting = false;
    m_autoScroll->stop();
    QClipboard *clip = QApplication::clipboard();
    const QString text = selectedText();
    if (!text.isEmpty() && clip->supportsSelection()) {
        clip->setText(text, QClipboard::Selection);
    }
}

void TerminalView::wheelEvent(QWheelEvent *e)
{
    // 120 is one notch of a classic wheel: three lines, as in Konsole.
    m_wheelDelta += e->angleDelta().y();
    const int lines = m_wheelDelta / 40;
    m_wheelDelta -= lines * 40;
    e->accept();
    if (lines == 0) {
        return;
    }
    const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    if (reportsMouse(mods)) {
        // One report per three lines, which tmux turns into its own scrolling.
        const QPoint cell = viewCellAt(e->position().toPoint()) - QPoint(0, m_offset);
        for (int i = 0; i < (std::abs(lines) + 2) / 3; ++i) {
            m_screen->sendWheel(cell.y(), cell.x(), lines > 0, mods);
        }
        return;
    }
    if (m_screen->altScreen()) {
        // Full-screen programs without mouse support scroll by arrow keys
        // (xterm's alternateScroll).
        for (int i = 0; i < std::abs(lines); ++i) {
            m_screen->sendKey(lines > 0 ? TerminalScreen::Up : TerminalScreen::Down);
        }
        return;
    }
    scrollBy(lines);
}

void TerminalView::focusInEvent(QFocusEvent *e)
{
    QWidget::focusInEvent(e);
    m_screen->setFocused(true);
    update(cellsToPixels(QRect(m_screen->cursor(), QSize(2, 1))));
}

void TerminalView::focusOutEvent(QFocusEvent *e)
{
    QWidget::focusOutEvent(e);
    m_screen->setFocused(false);
    update(cellsToPixels(QRect(m_screen->cursor(), QSize(2, 1))));
}
