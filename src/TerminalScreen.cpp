#include "TerminalScreen.h"

#include <vterm.h>

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
        case VTERM_PROP_ALTSCREEN:
            s->m_altScreen = val->boolean;
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
};

namespace
{
// Scrollback (sb_pushline/sb_popline) is left to the view's later scrollback
// support; without it libvterm drops lines that scroll off the top.
const VTermScreenCallbacks kScreenCallbacks = {
    TerminalScreenCallbacks::damage,
    nullptr, // moverect: repaint the moved region as damage
    TerminalScreenCallbacks::movecursor,
    TerminalScreenCallbacks::settermprop,
    TerminalScreenCallbacks::bell,
    TerminalScreenCallbacks::resize,
    nullptr,
    nullptr,
    nullptr,
};
} // namespace

TerminalScreen::TerminalScreen(int rows, int cols, QObject *parent)
    : QObject(parent)
    , m_rows(std::max(1, rows))
    , m_cols(std::max(1, cols))
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
    setAppearance(TerminalAppearance::fallback());
}

TerminalScreen::~TerminalScreen()
{
    vterm_free(m_vt);
}

void TerminalScreen::setAppearance(const TerminalAppearance &appearance)
{
    m_appearance = appearance;
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
    if (!m_damage.isEmpty()) {
        const QRect r = m_damage;
        m_damage = QRect();
        Q_EMIT changed(r);
    }
}

void TerminalScreen::resize(int rows, int cols)
{
    rows = std::max(1, rows);
    cols = std::max(1, cols);
    if (rows == m_rows && cols == m_cols) {
        return;
    }
    vterm_set_size(m_vt, rows, cols);
    m_rows = rows;
    m_cols = cols;
    m_damage = QRect(0, 0, cols, rows);
    flush();
}

void TerminalScreen::reset()
{
    vterm_screen_reset(m_screen, 1);
    m_altScreen = false;
    m_reverseScreen = false;
    m_cursorVisible = true;
    setAppearance(m_appearance); // a hard reset restores libvterm's own palette
}

TerminalScreen::Cell TerminalScreen::cell(int row, int col) const
{
    Cell out;
    VTermScreenCell c;
    if (row < 0 || col < 0 || row >= m_rows || col >= m_cols || !vterm_screen_get_cell(m_screen, VTermPos{row, col}, &c)) {
        out.fg = m_fg;
        out.bg = m_bg;
        return out;
    }
    if (c.chars[0] == uint32_t(-1)) {
        out.width = 0; // right half of a wide character
    } else {
        out.width = c.width > 1 ? 2 : 1;
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && c.chars[i]; ++i) {
            const char32_t ch = c.chars[i];
            out.text += QString::fromUcs4(&ch, 1);
        }
    }
    out.fg = VTERM_COLOR_IS_DEFAULT_FG(&c.fg) ? m_fg : toQColor(m_screen, c.fg);
    out.defaultBg = VTERM_COLOR_IS_DEFAULT_BG(&c.bg);
    out.bg = out.defaultBg ? m_bg : toQColor(m_screen, c.bg);
    if (bool(c.attrs.reverse) != m_reverseScreen) {
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
