// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TerminalScreen.h"

#include <vterm.h>

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

static_assert(int(TerminalScreen::Enter) == VTERM_KEY_ENTER && int(TerminalScreen::PageDown) == VTERM_KEY_PAGEDOWN
              && int(TerminalScreen::F0) == VTERM_KEY_FUNCTION_0);

namespace
{
QColor toQColor(const VTermScreen *screen, VTermColor c)
{
    vterm_screen_convert_color_to_rgb(screen, &c);
    return QColor(c.rgb.red, c.rgb.green, c.rgb.blue);
}

VTermColor toVTerm(const QColor &c)
{
    VTermColor v;
    vterm_color_rgb(&v, static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue()));
    return v;
}

VTermModifier toVTerm(Qt::KeyboardModifiers mods)
{
    int m = VTERM_MOD_NONE;
    if (mods & Qt::ShiftModifier) {
        m |= VTERM_MOD_SHIFT;
    }
    if (mods & Qt::AltModifier) {
        m |= VTERM_MOD_ALT;
    }
    if (mods & Qt::ControlModifier) {
        m |= VTERM_MOD_CTRL;
    }
    return VTermModifier(m);
}

// The byte Ctrl+`ch` sends on a classic terminal (xterm's defaults), or -1.
// libvterm would encode some of these as CSI u, which tmux and most programs
// do not read.
int controlByte(char32_t ch)
{
    if (ch >= U'a' && ch <= U'z') {
        return int(ch - U'a' + 1);
    }
    if (ch >= U'@' && ch <= U'_') { // @ A–Z [ \ ] ^ _
        return int(ch & 0x1f);
    }
    switch (ch) {
    case U' ':
    case U'2':
        return 0;
    case U'3':
        return 0x1b;
    case U'4':
        return 0x1c;
    case U'5':
        return 0x1d;
    case U'6':
        return 0x1e;
    case U'7':
    case U'/':
    case U'-':
        return 0x1f;
    case U'8':
    case U'?':
        return 0x7f;
    default:
        return -1;
    }
}
} // namespace

// A line that scrolled off: one entry per cell, with trailing blanks dropped
// unless the line wraps (a wrapped line's blanks are real spaces).
struct ScrollbackCell
{
    uint32_t ch; // 0 blank, uint32_t(-1) right half of a wide character
    VTermColor fg;
    VTermColor bg;
    VTermScreenCellAttrs attrs;
    uint8_t width;
};

struct ScrollbackLine
{
    std::vector<ScrollbackCell> cells;
    // Combining characters after the first, by column; rare enough to keep apart.
    std::vector<std::pair<int, std::u32string>> extra;
    bool wrapped = false;
};

struct TerminalScreen::Scrollback
{
    std::deque<ScrollbackLine> lines;
};

struct TerminalScreenCallbacks
{
    static TerminalScreen *self(void *user) { return static_cast<TerminalScreen *>(user); }

    static int damage(VTermRect r, void *user)
    {
        TerminalScreen *s = self(user);
        s->m_damage |= QRect(r.start_col, r.start_row, r.end_col - r.start_col, r.end_row - r.start_row);
        return 1;
    }

    static int movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
    {
        TerminalScreen *s = self(user);
        s->m_cursor = QPoint(pos.col, pos.row);
        s->m_cursorVisible = visible;
        Q_EMIT s->cursorMoved(QPoint(oldpos.col, oldpos.row), s->m_cursor);
        return 1;
    }

    static int settermprop(VTermProp prop, VTermValue *val, void *user)
    {
        TerminalScreen *s = self(user);
        switch (prop) {
        case VTERM_PROP_CURSORVISIBLE:
            s->m_cursorVisible = val->boolean;
            Q_EMIT s->cursorMoved(s->m_cursor, s->m_cursor);
            break;
        case VTERM_PROP_CURSORSHAPE:
            s->m_cursorShape = val->number >= TerminalScreen::Block && val->number <= TerminalScreen::Bar
                ? TerminalScreen::CursorShape(val->number)
                : TerminalScreen::Block;
            Q_EMIT s->cursorMoved(s->m_cursor, s->m_cursor);
            break;
        case VTERM_PROP_CURSORBLINK:
            s->m_cursorBlink = val->boolean;
            Q_EMIT s->cursorMoved(s->m_cursor, s->m_cursor);
            break;
        case VTERM_PROP_ALTSCREEN:
            s->m_altScreen = val->boolean;
            break;
        case VTERM_PROP_MOUSE:
            s->m_mouseMode = val->number;
            break;
        case VTERM_PROP_REVERSE:
            s->m_reverseScreen = val->boolean;
            s->m_damage = QRect(0, 0, s->m_cols, s->m_rows);
            break;
        case VTERM_PROP_TITLE:
            if (val->string.initial) {
                s->m_titleBuf.clear();
            }
            s->m_titleBuf.append(val->string.str, qsizetype(val->string.len));
            if (val->string.final) {
                s->m_title = QString::fromUtf8(s->m_titleBuf);
                Q_EMIT s->titleChanged(s->m_title);
            }
            break;
        default:
            break;
        }
        return 1;
    }

    static int bell(void *user)
    {
        Q_EMIT self(user)->bell();
        return 1;
    }

    static int resize(int rows, int cols, void *user)
    {
        TerminalScreen *s = self(user);
        s->m_rows = rows;
        s->m_cols = cols;
        s->m_damage = QRect(0, 0, cols, rows);
        return 1;
    }

    static void output(const char *bytes, size_t len, void *user) { Q_EMIT self(user)->output(QByteArray(bytes, qsizetype(len))); }

    static bool isBlank(const VTermScreenCell &c)
    {
        return (c.chars[0] == 0 || c.chars[0] == ' ') && VTERM_COLOR_IS_DEFAULT_BG(&c.bg) && !c.attrs.reverse && !c.attrs.underline
            && !c.attrs.strike;
    }

    static int pushline(int cols, const VTermScreenCell *cells, void *user)
    {
        TerminalScreen *s = self(user);
        if (s->m_scrollbackLimit <= 0) {
            return 0;
        }
        ScrollbackLine line;
        // libvterm has already shifted its line info, so row 0 is the line
        // after this one. A resize pushes lines without that info.
        line.wrapped = !s->m_resizing && vterm_state_get_lineinfo(s->m_state, 0)->continuation;
        int n = cols;
        if (!line.wrapped) {
            while (n > 0 && isBlank(cells[n - 1])) {
                --n;
            }
        }
        line.cells.reserve(size_t(n));
        for (int i = 0; i < n; ++i) {
            const VTermScreenCell &c = cells[i];
            line.cells.push_back({c.chars[0], c.fg, c.bg, c.attrs, uint8_t(c.width)});
            if (c.chars[0] != uint32_t(-1) && c.chars[0] && c.chars[1]) {
                std::u32string more;
                for (int k = 1; k < VTERM_MAX_CHARS_PER_CELL && c.chars[k]; ++k) {
                    more += char32_t(c.chars[k]);
                }
                line.extra.emplace_back(i, std::move(more));
            }
        }
        auto &lines = s->m_sb->lines;
        lines.push_back(std::move(line));
        while (int(lines.size()) > s->m_scrollbackLimit) {
            lines.pop_front();
            ++s->m_dropped;
        }
        ++s->m_pushed;
        s->m_sbChanged = true;
        return 1;
    }

    static int popline(int cols, VTermScreenCell *cells, void *user)
    {
        TerminalScreen *s = self(user);
        auto &lines = s->m_sb->lines;
        if (lines.empty()) {
            return 0;
        }
        const ScrollbackLine line = std::move(lines.back());
        lines.pop_back();
        s->m_sbChanged = true;
        VTermColor fg;
        VTermColor bg;
        vterm_state_get_default_colors(s->m_state, &fg, &bg);
        for (int i = 0; i < cols; ++i) {
            VTermScreenCell &c = cells[i];
            c = VTermScreenCell{};
            if (size_t(i) < line.cells.size()) {
                const ScrollbackCell &sc = line.cells[size_t(i)];
                c.chars[0] = sc.ch;
                c.fg = sc.fg;
                c.bg = sc.bg;
                c.attrs = sc.attrs;
                c.width = char(sc.width);
            } else {
                c.fg = fg;
                c.bg = bg;
                c.width = 1;
            }
        }
        for (const auto &[col, more] : line.extra) {
            if (col < cols) {
                for (size_t k = 0; k < more.size() && k + 1 < VTERM_MAX_CHARS_PER_CELL; ++k) {
                    cells[col].chars[k + 1] = uint32_t(more[k]);
                }
            }
        }
        return 1;
    }

    static int sbClear(void *user)
    {
        TerminalScreen *s = self(user);
        s->m_dropped += qint64(s->m_sb->lines.size());
        s->m_sb->lines.clear();
        s->m_sbChanged = true;
        return 1;
    }

    static TerminalScreen::Cell toCell(const TerminalScreen *s, const VTermScreenCell &c, const std::u32string *more = nullptr)
    {
        TerminalScreen::Cell out;
        if (c.chars[0] == uint32_t(-1)) {
            out.width = 0; // right half of a wide character
        } else {
            out.width = c.width > 1 ? 2 : 1;
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && c.chars[i]; ++i) {
                const char32_t ch = c.chars[i];
                out.text += QString::fromUcs4(&ch, 1);
            }
            if (more && c.chars[0]) {
                out.text += QString::fromUcs4(more->data(), qsizetype(more->size()));
            }
        }
        out.fg = VTERM_COLOR_IS_DEFAULT_FG(&c.fg) ? s->m_fg : toQColor(s->m_screen, c.fg);
        out.defaultBg = VTERM_COLOR_IS_DEFAULT_BG(&c.bg);
        out.bg = out.defaultBg ? s->m_bg : toQColor(s->m_screen, c.bg);
        if (bool(c.attrs.reverse) != s->m_reverseScreen) {
            std::swap(out.fg, out.bg);
            out.defaultBg = false;
        }
        if (c.attrs.conceal) {
            out.fg = out.bg;
        }
        out.bold = c.attrs.bold;
        out.italic = c.attrs.italic;
        out.underline = int(c.attrs.underline);
        out.strike = c.attrs.strike;
        return out;
    }
};

namespace
{
const VTermScreenCallbacks kScreenCallbacks = {
    TerminalScreenCallbacks::damage,
    nullptr, // moverect: repaint the moved region as damage
    TerminalScreenCallbacks::movecursor,
    TerminalScreenCallbacks::settermprop,
    TerminalScreenCallbacks::bell,
    TerminalScreenCallbacks::resize,
    TerminalScreenCallbacks::pushline,
    TerminalScreenCallbacks::popline,
    TerminalScreenCallbacks::sbClear,
};
} // namespace

TerminalScreen::TerminalScreen(int rows, int cols, QObject *parent)
    : QObject(parent)
    , m_rows(std::max(1, rows))
    , m_cols(std::max(1, cols))
    , m_sb(std::make_unique<Scrollback>())
{
    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, TerminalScreenCallbacks::output, this);
    m_state = vterm_obtain_state(m_vt);
    m_screen = vterm_obtain_screen(m_vt);
    vterm_screen_set_callbacks(m_screen, &kScreenCallbacks, this);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_enable_reflow(m_screen, true);
    vterm_screen_reset(m_screen, 1);
    m_appearance = TerminalAppearance::fallback();
    resetCursorStyle();
    setAppearance(m_appearance);
}

void TerminalScreen::resetCursorStyle()
{
    // libvterm's reset turns blinking on; like Konsole, the cursor keeps the
    // profile's style until the program asks otherwise.
    VTermValue blink;
    blink.boolean = m_appearance.cursorBlink ? 1 : 0;
    vterm_state_set_termprop(m_state, VTERM_PROP_CURSORBLINK, &blink);
    VTermValue shape;
    shape.number = m_appearance.cursorShape;
    vterm_state_set_termprop(m_state, VTERM_PROP_CURSORSHAPE, &shape);
}

TerminalScreen::~TerminalScreen()
{
    vterm_free(m_vt);
}

void TerminalScreen::setAppearance(const TerminalAppearance &appearance)
{
    // A cursor the program has not restyled follows the new profile.
    const bool profileCursor = m_cursorShape == CursorShape(m_appearance.cursorShape) && m_cursorBlink == m_appearance.cursorBlink;
    m_appearance = appearance;
    if (profileCursor) {
        resetCursorStyle();
    }
    m_fg = appearance.foreground;
    m_bg = appearance.background;
    const VTermColor fg = toVTerm(m_fg);
    const VTermColor bg = toVTerm(m_bg);
    vterm_screen_set_default_colors(m_screen, &fg, &bg);
    for (int i = 0; i < 16; ++i) {
        const VTermColor c = toVTerm(appearance.palette.at(size_t(i)));
        vterm_state_set_palette_color(m_state, i, &c);
    }
    vterm_state_set_bold_highbright(m_state, appearance.boldIsBright ? 1 : 0);
    m_damage = QRect(0, 0, m_cols, m_rows);
    flush();
}

void TerminalScreen::feed(const QByteArray &bytes)
{
    vterm_input_write(m_vt, bytes.constData(), size_t(bytes.size()));
    flush();
}

void TerminalScreen::flush()
{
    vterm_screen_flush_damage(m_screen);
    if (m_sbChanged) {
        const int pushed = std::exchange(m_pushed, 0);
        m_sbChanged = false;
        Q_EMIT scrollbackChanged(pushed);
    }
    if (!m_damage.isEmpty()) {
        const QRect r = m_damage;
        m_damage = QRect();
        Q_EMIT changed(r);
    }
}

void TerminalScreen::resize(int rows, int cols)
{
    // A view collapsed to a sliver (a hidden stack page, a splitter at its
    // minimum) keeps its grid: libvterm's reflow aborts on some content at a
    // width of one or two cells, and nothing is readable there anyway.
    if (rows < 2 || cols < 4) {
        return;
    }
    if (rows == m_rows && cols == m_cols) {
        return;
    }
    m_resizing = true;
    vterm_set_size(m_vt, rows, cols);
    m_resizing = false;
    m_rows = rows;
    m_cols = cols;
    m_damage = QRect(0, 0, cols, rows);
    flush();
}

void TerminalScreen::reset()
{
    vterm_screen_reset(m_screen, 1);
    resetCursorStyle();
    m_altScreen = false;
    m_reverseScreen = false;
    m_mouseMode = 0;
    m_cursorVisible = true;
    setAppearance(m_appearance); // a hard reset restores libvterm's own palette
}

TerminalScreen::Cell TerminalScreen::cell(int row, int col) const
{
    VTermScreenCell c;
    if (row < 0 || col < 0 || row >= m_rows || col >= m_cols || !vterm_screen_get_cell(m_screen, VTermPos{row, col}, &c)) {
        Cell out;
        out.fg = m_fg;
        out.bg = m_bg;
        return out;
    }
    return TerminalScreenCallbacks::toCell(this, c);
}

QString TerminalScreen::rowText(int row) const
{
    QString text;
    for (int col = 0; col < m_cols; ++col) {
        const Cell c = cell(row, col);
        if (c.width == 0) {
            continue;
        }
        text += c.text.isEmpty() ? QStringLiteral(" ") : c.text;
    }
    while (text.endsWith(QLatin1Char(' '))) {
        text.chop(1);
    }
    return text;
}

int TerminalScreen::scrollbackSize() const
{
    return int(m_sb->lines.size());
}

void TerminalScreen::setScrollbackLimit(int lines)
{
    m_scrollbackLimit = std::max(0, lines);
    auto &sb = m_sb->lines;
    if (int(sb.size()) > m_scrollbackLimit) {
        const int drop = int(sb.size()) - m_scrollbackLimit;
        sb.erase(sb.begin(), sb.begin() + drop);
        m_dropped += drop;
        Q_EMIT scrollbackChanged(0);
    }
}

void TerminalScreen::clearScrollback()
{
    TerminalScreenCallbacks::sbClear(this);
    flush();
}

TerminalScreen::Cell TerminalScreen::lineCell(int line, int col) const
{
    const int sb = scrollbackSize();
    if (line >= sb) {
        return cell(line - sb, col);
    }
    if (line < 0 || col < 0 || size_t(col) >= m_sb->lines[size_t(line)].cells.size()) {
        Cell out;
        out.fg = m_fg;
        out.bg = m_bg;
        return out;
    }
    const ScrollbackLine &l = m_sb->lines[size_t(line)];
    const ScrollbackCell &sc = l.cells[size_t(col)];
    VTermScreenCell c{};
    c.chars[0] = sc.ch;
    c.fg = sc.fg;
    c.bg = sc.bg;
    c.attrs = sc.attrs;
    c.width = char(sc.width);
    const std::u32string *more = nullptr;
    for (const auto &[at, chars] : l.extra) {
        if (at == col) {
            more = &chars;
        }
    }
    return TerminalScreenCallbacks::toCell(this, c, more);
}

int TerminalScreen::lineCols(int line) const
{
    const int sb = scrollbackSize();
    if (line >= sb || line < 0) {
        return m_cols;
    }
    return int(m_sb->lines[size_t(line)].cells.size());
}

bool TerminalScreen::lineWrapped(int line) const
{
    const int sb = scrollbackSize();
    if (line < 0) {
        return false;
    }
    if (line < sb) {
        return m_sb->lines[size_t(line)].wrapped;
    }
    const int next = line - sb + 1;
    return next < m_rows && vterm_state_get_lineinfo(m_state, next)->continuation;
}

QString TerminalScreen::text(QPoint from, QPoint to) const
{
    if (to.y() < from.y() || (to.y() == from.y() && to.x() < from.x())) {
        std::swap(from, to);
    }
    const int first = std::max(0, from.y());
    const int last = std::min(lineCount() - 1, to.y());
    QString out;
    for (int line = first; line <= last; ++line) {
        const int cols = lineCols(line);
        int start = line == from.y() ? std::max(0, from.x()) : 0;
        const int end = line == to.y() ? std::min(cols - 1, to.x()) : cols - 1;
        if (start > 0 && lineCell(line, start).width == 0) {
            --start; // the selection began on the right half of a wide character
        }
        QString part;
        for (int col = start; col <= end; ++col) {
            const Cell c = lineCell(line, col);
            if (c.width == 0) {
                continue;
            }
            part += c.text.isEmpty() ? QStringLiteral(" ") : c.text;
        }
        const bool wrapped = lineWrapped(line);
        if (!wrapped || line == last) {
            while (part.endsWith(QLatin1Char(' '))) {
                part.chop(1);
            }
        }
        out += part;
        if (line < last && !wrapped) {
            out += QLatin1Char('\n');
        }
    }
    return out;
}

namespace
{
// Characters that belong to a word besides letters and digits, so paths and
// URLs select whole (Konsole's default set).
bool isWordChar(QChar ch)
{
    return ch.isLetterOrNumber() || QStringView(u":@-./_~?&=%+#").contains(ch);
}

int charClass(const QString &text)
{
    if (text.isEmpty() || text.at(0).isSpace()) {
        return 0;
    }
    return isWordChar(text.at(0)) ? 1 : 2;
}
} // namespace

std::pair<QPoint, QPoint> TerminalScreen::wordAt(QPoint pos) const
{
    auto textAt = [this](QPoint p) {
        Cell c = lineCell(p.y(), p.x());
        if (c.width == 0 && p.x() > 0) {
            c = lineCell(p.y(), p.x() - 1);
        }
        return c.text;
    };
    const int cls = charClass(textAt(pos));
    if (cls == 2) {
        QPoint end = pos;
        if (lineCell(pos.y(), pos.x()).width == 2) {
            end.rx() += 1;
        }
        return {pos, end};
    }
    QPoint start = pos;
    for (;;) {
        QPoint prev = start;
        if (prev.x() > 0) {
            prev.rx() -= 1;
        } else if (prev.y() > 0 && lineWrapped(prev.y() - 1)) {
            prev = QPoint(lineCols(prev.y() - 1) - 1, prev.y() - 1);
        } else {
            break;
        }
        if (charClass(textAt(prev)) != cls) {
            break;
        }
        start = prev;
    }
    QPoint end = pos;
    for (;;) {
        QPoint next = end;
        if (next.x() + 1 < lineCols(next.y())) {
            next.rx() += 1;
        } else if (lineWrapped(next.y()) && next.y() + 1 < lineCount()) {
            next = QPoint(0, next.y() + 1);
        } else {
            break;
        }
        if (charClass(textAt(next)) != cls) {
            break;
        }
        end = next;
    }
    return {start, end};
}

std::pair<int, int> TerminalScreen::logicalLine(int line) const
{
    int first = line;
    while (first > 0 && lineWrapped(first - 1)) {
        --first;
    }
    int last = line;
    while (last + 1 < lineCount() && lineWrapped(last)) {
        ++last;
    }
    return {first, last};
}

void TerminalScreen::sendWheel(int row, int col, bool up, Qt::KeyboardModifiers mods)
{
    vterm_mouse_move(m_vt, std::clamp(row, 0, m_rows - 1), std::clamp(col, 0, m_cols - 1), toVTerm(mods));
    vterm_mouse_button(m_vt, up ? 4 : 5, true, toVTerm(mods));
}

void TerminalScreen::sendMouseMove(int row, int col, Qt::KeyboardModifiers mods)
{
    vterm_mouse_move(m_vt, std::clamp(row, 0, m_rows - 1), std::clamp(col, 0, m_cols - 1), toVTerm(mods));
}

void TerminalScreen::sendMouseButton(int button, bool pressed, Qt::KeyboardModifiers mods)
{
    vterm_mouse_button(m_vt, button, pressed, toVTerm(mods));
}

void TerminalScreen::sendKey(Key key, Qt::KeyboardModifiers mods)
{
    vterm_keyboard_key(m_vt, VTermKey(key), toVTerm(mods));
}

void TerminalScreen::sendChar(char32_t ch, Qt::KeyboardModifiers mods)
{
    if (mods & Qt::ControlModifier) {
        const int b = controlByte(ch);
        if (b >= 0) {
            QByteArray bytes;
            if (mods & Qt::AltModifier) {
                bytes.append('\x1b');
            }
            bytes.append(char(b));
            Q_EMIT output(bytes);
            return;
        }
    }
    vterm_keyboard_unichar(m_vt, uint32_t(ch), toVTerm(mods));
}

void TerminalScreen::paste(const QString &text)
{
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    t.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    // A pasted end marker would end the paste early and run the rest as keys.
    t.remove(QStringLiteral("\x1b[201~"));
    vterm_keyboard_start_paste(m_vt);
    Q_EMIT output(t.toUtf8());
    vterm_keyboard_end_paste(m_vt);
}

void TerminalScreen::setFocused(bool focused)
{
    if (focused) {
        vterm_state_focus_in(m_state);
    } else {
        vterm_state_focus_out(m_state);
    }
}
