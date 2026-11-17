#include "TerminalView.h"

#include "TerminalChannel.h"
#include "TerminalScreen.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>

TerminalView::TerminalView(QWidget *parent)
    : QWidget(parent)
    , m_screen(new TerminalScreen(24, 80, this))
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setCursor(Qt::IBeamCursor);
    setAppearance(TerminalAppearance::fallback());

    connect(m_screen, &TerminalScreen::changed, this, [this](const QRect &cells) { update(cellsToPixels(cells)); });
    connect(m_screen, &TerminalScreen::cursorMoved, this, [this](const QPoint &from, const QPoint &to) {
        update(cellsToPixels(QRect(from, QSize(2, 1))));
        update(cellsToPixels(QRect(to, QSize(2, 1))));
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
    const QPoint cursor = m_screen->cursor();
    const bool showCursor = m_screen->cursorVisible();
    const bool focused = hasFocus();
    int currentFont = -1;

    for (int row = firstRow; row <= lastRow; ++row) {
        const int y = pad + row * ch;
        for (int col = firstCol; col <= lastCol; ++col) {
            TerminalScreen::Cell c = m_screen->cell(row, col);
            if (c.width == 0) {
                continue;
            }
            const QRect r(pad + col * cw, y, cw * c.width, ch);
            const bool isCursor = showCursor && cursor == QPoint(col, row);
            if (isCursor && focused) {
                p.fillRect(r, m_appearance.cursor);
                c.fg = m_appearance.background;
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

    if ((key == Qt::Key_V && mods == (Qt::ControlModifier | Qt::ShiftModifier)) || (key == Qt::Key_Insert && mods == Qt::ShiftModifier)) {
        pasteClipboard();
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
        m_screen->sendChar(char32_t(ch), mods & ~Qt::ShiftModifier);
    }
}

void TerminalView::paste(const QString &text)
{
    if (!text.isEmpty()) {
        m_screen->paste(text);
    }
}

void TerminalView::pasteClipboard()
{
    paste(QGuiApplication::clipboard()->text());
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
