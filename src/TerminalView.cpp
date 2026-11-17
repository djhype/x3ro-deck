#include "TerminalView.h"

#include "TerminalChannel.h"
#include "TerminalScreen.h"

#include <QApplication>
#include <QClipboard>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QTimer>

namespace
{
bool before(qint64 aLine, int aCol, qint64 bLine, int bCol)
{
    return aLine < bLine || (aLine == bLine && aCol < bCol);
}

int mouseButton(Qt::MouseButton b)
{
    switch (b) {
    case Qt::LeftButton:
        return 1;
    case Qt::MiddleButton:
        return 2;
    case Qt::RightButton:
        return 3;
    default:
        return 0;
    }
}

Qt::KeyboardModifiers terminalMods(Qt::KeyboardModifiers mods)
{
    return mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
}

// Cells a character takes in the preedit, which is not in libvterm's grid:
// East Asian wide and emoji ranges take two, combining marks none.
int charCells(char32_t ch)
{
    const QChar::Category cat = QChar::category(ch);
    if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_Enclosing || ch == 0x200d) {
        return 0;
    }
    const bool wide = (ch >= 0x1100 && ch <= 0x115f) || (ch >= 0x2e80 && ch <= 0xa4cf && ch != 0x303f) || (ch >= 0xac00 && ch <= 0xd7a3)
        || (ch >= 0xf900 && ch <= 0xfaff) || (ch >= 0xfe30 && ch <= 0xfe4f) || (ch >= 0xff00 && ch <= 0xff60)
        || (ch >= 0xffe0 && ch <= 0xffe6) || (ch >= 0x1f300 && ch <= 0x1f64f) || (ch >= 0x1f900 && ch <= 0x1f9ff)
        || (ch >= 0x20000 && ch <= 0x3fffd);
    return wide ? 2 : 1;
}
} // namespace

TerminalView::TerminalView(QWidget *parent)
    : QWidget(parent)
    , m_screen(new TerminalScreen(24, 80, this))
    , m_autoScroll(new QTimer(this))
    , m_blink(new QTimer(this))
    , m_imUpdate(new QTimer(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    setMouseTracking(true); // any-event tracking reports motion without a button
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setCursor(Qt::IBeamCursor);
    setAppearance(TerminalAppearance::fallback());

    m_autoScroll->setInterval(50);
    connect(m_autoScroll, &QTimer::timeout, this, &TerminalView::autoScroll);
    connect(m_blink, &QTimer::timeout, this, [this] {
        m_blinkOn = !m_blinkOn;
        update(cursorRect().adjusted(0, 0, m_cell.width(), 0));
    });
    m_imUpdate->setSingleShot(true);
    m_imUpdate->setInterval(30);
    connect(m_imUpdate, &QTimer::timeout, this, [this] {
        if (hasFocus()) {
            QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
        }
    });

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
        updateBlink();
        if (hasFocus() && !m_imUpdate->isActive()) {
            m_imUpdate->start();
        }
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
        m_advance[i].clear();
    }
    m_screen->setAppearance(appearance);
    updateCellSize();
    syncGrid();
    update();
}

void TerminalView::updateCellSize()
{
    const QFontMetrics fm(m_fonts[0]);
    const int spacing = std::max(0, m_appearance.lineSpacing);
    m_cell = QSize(std::max(1, fm.horizontalAdvance(QLatin1Char('W'))), std::max(1, fm.height() + spacing));
    m_ascent = fm.ascent() + spacing / 2;
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
    if (m_blink->isActive()) {
        m_blinkOn = true; // the cursor stays lit while typing
        m_blink->start();
        update(cursorRect().adjusted(0, 0, m_cell.width(), 0));
    }
}

void TerminalView::updateBlink()
{
    const int flash = QApplication::cursorFlashTime();
    if (!hasFocus() || !m_screen->cursorBlink() || !m_screen->cursorVisible() || flash <= 0) {
        m_blink->stop();
        m_blinkOn = true;
        return;
    }
    if (!m_blink->isActive()) {
        m_blinkOn = true;
        m_blink->start(flash / 2);
    }
}

QRect TerminalView::cursorRect() const
{
    return cellsToPixels(QRect(m_screen->cursor() + QPoint(0, m_offset), QSize(1, 1)));
}

void TerminalView::drawGlyphs(QPainter &p, const QRect &r, const QString &text, int font)
{
    const int baseline = r.y() + m_ascent;
    if (text.at(0).unicode() < 0x80 && text.size() == 1) {
        p.drawText(r.x(), baseline, text);
        return;
    }
    // A glyph from a fallback font (CJK, emoji, symbols) may be wider or
    // narrower than its cells: center it, and shrink it when it would spill
    // into the next cell, so the grid stays aligned.
    QHash<QString, int> &cache = m_advance[font];
    auto it = cache.find(text);
    if (it == cache.end()) {
        it = cache.insert(text, QFontMetrics(m_fonts[font]).horizontalAdvance(text));
    }
    const int advance = *it;
    if (advance <= r.width()) {
        p.drawText(r.x() + (r.width() - advance) / 2, baseline, text);
        return;
    }
    const qreal k = qreal(r.width()) / advance;
    const qreal mid = r.y() + r.height() / 2.0;
    p.save();
    p.translate(r.x(), mid);
    p.scale(k, k);
    p.drawText(QPointF(0, baseline - mid), text);
    p.restore();
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
    const bool focused = hasFocus();
    // The preedit takes the cursor's place while the input method composes.
    const bool showCursor = m_screen->cursorVisible() && m_preedit.isEmpty() && (!focused || m_blinkOn);
    const TerminalScreen::CursorShape shape = m_screen->cursorShape();
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
            if (isCursor && focused && shape == TerminalScreen::Block) {
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
                drawGlyphs(p, r, c.text, f);
            }
            if (c.underline) {
                const int uy = std::min(y + m_ascent + 2, y + ch - 1);
                if (c.underline == 3) {
                    // Curly: a zigzag between uy - 1 and uy + 1.
                    const int low = std::min(uy + 1, y + ch - 1);
                    QPolygon wave;
                    for (int x = r.x(); x <= r.right() + 1; x += 2) {
                        wave << QPoint(x, ((x - pad) / 2) % 2 ? low : uy - 1);
                    }
                    p.setPen(c.fg);
                    p.drawPolyline(wave);
                } else {
                    p.fillRect(QRect(r.x(), uy, r.width(), 1), c.fg);
                    if (c.underline == 2 && uy + 2 < y + ch) {
                        p.fillRect(QRect(r.x(), uy + 2, r.width(), 1), c.fg);
                    }
                }
            }
            if (c.strike) {
                p.fillRect(QRect(r.x(), y + ch / 2, r.width(), 1), c.fg);
            }
            if (isCursor && focused && shape != TerminalScreen::Block) {
                const int t = std::max(2, ch / 10);
                p.fillRect(shape == TerminalScreen::Bar ? QRect(r.x(), y, t, ch) : QRect(r.x(), y + ch - t, r.width(), t),
                           m_appearance.cursor);
            }
            if (isCursor && !focused) {
                p.setPen(m_appearance.cursor);
                p.drawRect(r.adjusted(0, 0, -1, -1));
            }
        }
    }

    if (!m_preedit.isEmpty()) {
        // Over the cells at the cursor, a cell or two per character,
        // underlined, with the input method's own cursor as a bar.
        const QRect start = cursorRect();
        int x = start.x();
        int caretX = -1;
        p.setFont(m_fonts[0]);
        currentFont = 0;
        const QList<uint> chars = m_preedit.toUcs4();
        int at = 0; // UTF-16 index of the next character
        int i = 0;
        while (i < chars.size()) {
            if (caretX < 0 && at >= m_preeditCursor) {
                caretX = x;
            }
            QString cluster;
            int cells = 0;
            do {
                const char32_t u = chars[i];
                cluster += QString::fromUcs4(&u, 1);
                cells = std::max(cells, charCells(u));
                ++i;
            } while (i < chars.size() && charCells(chars[i]) == 0);
            at += int(cluster.size());
            const QRect r(x, start.y(), std::max(1, cells) * cw, ch);
            p.fillRect(r, m_appearance.background);
            p.setPen(m_appearance.foreground);
            drawGlyphs(p, r, cluster, 0);
            p.fillRect(QRect(r.x(), std::min(r.y() + m_ascent + 2, r.bottom()), r.width(), 1), m_appearance.foreground);
            x += r.width();
        }
        if (caretX < 0) {
            caretX = x;
        }
        p.fillRect(QRect(caretX, start.y(), 2, ch), m_appearance.cursor);
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

bool TerminalView::reportButton(QMouseEvent *e, bool pressed)
{
    const int button = mouseButton(e->button());
    const Qt::KeyboardModifiers mods = terminalMods(e->modifiers());
    if (!button) {
        return false;
    }
    if (pressed) {
        if (!reportsMouse(mods)) {
            return false;
        }
        scrollTo(0); // the program's cells are the screen, not the history
        m_reported |= e->button();
    } else {
        // A release goes where its press went, whatever Shift does meanwhile.
        if (!(m_reported & e->button())) {
            return false;
        }
        m_reported &= ~e->button();
    }
    const QPoint cell = viewCellAt(e->position().toPoint()) - QPoint(0, m_offset);
    m_screen->sendMouseMove(cell.y(), cell.x(), mods);
    m_screen->sendMouseButton(button, pressed, mods);
    return true;
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
    if (reportButton(e, true)) {
        return;
    }
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
    if (reportButton(e, true)) {
        return;
    }
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
    // libvterm drops the moves the program's tracking mode does not want.
    if (m_reported || (!m_selecting && reportsMouse(terminalMods(e->modifiers())))) {
        const QPoint cell = viewCellAt(e->position().toPoint()) - QPoint(0, m_offset);
        m_screen->sendMouseMove(cell.y(), cell.x(), terminalMods(e->modifiers()));
        return;
    }
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
    if (reportButton(e, false)) {
        return;
    }
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

void TerminalView::inputMethodEvent(QInputMethodEvent *e)
{
    const QString commit = e->commitString();
    if (!commit.isEmpty()) {
        beforeInput();
        const QList<uint> chars = commit.toUcs4();
        for (const uint ch : chars) {
            if (ch >= 0x20 && ch != 0x7f) {
                m_screen->sendChar(char32_t(ch));
            }
        }
    }
    m_preedit = e->preeditString();
    m_preeditCursor = int(m_preedit.size());
    const QList<QInputMethodEvent::Attribute> attrs = e->attributes();
    for (const QInputMethodEvent::Attribute &a : attrs) {
        if (a.type == QInputMethodEvent::Cursor) {
            m_preeditCursor = std::clamp(a.start, 0, int(m_preedit.size()));
        }
    }
    if (!m_preedit.isEmpty()) {
        scrollTo(0);
    }
    e->accept();
    update();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle:
        return cursorRect();
    case Qt::ImFont:
        return m_fonts[0];
    case Qt::ImHints:
        return int(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase | Qt::ImhMultiLine);
    // The terminal has no text the input method could edit around the cursor.
    case Qt::ImSurroundingText:
    case Qt::ImCurrentSelection:
    case Qt::ImTextBeforeCursor:
    case Qt::ImTextAfterCursor:
        return QString();
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition:
        return 0;
    default:
        return QWidget::inputMethodQuery(query);
    }
}

void TerminalView::focusInEvent(QFocusEvent *e)
{
    QWidget::focusInEvent(e);
    m_screen->setFocused(true);
    updateBlink();
    update(cellsToPixels(QRect(m_screen->cursor(), QSize(2, 1))));
}

void TerminalView::focusOutEvent(QFocusEvent *e)
{
    QWidget::focusOutEvent(e);
    m_screen->setFocused(false);
    updateBlink();
    update(cellsToPixels(QRect(m_screen->cursor(), QSize(2, 1))));
}
