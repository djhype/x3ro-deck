#include "SessionScreen.h"

#include <QList>

#include <vterm.h>

#include <algorithm>
#include <vector>

namespace
{
// DEC private modes the redraw restores besides the cursor, the alternate
// screen and origin mode: cursor keys, reverse video, autowrap, numeric
// keypad, mouse reporting and its encodings, focus events, bracketed paste.
constexpr int kModes[] = {1, 5, 7, 66, 1000, 1002, 1003, 1004, 1005, 1006, 1015, 1016, 2004};
constexpr int kModeCount = int(std::size(kModes));
constexpr int kAutowrap = 2; // index of mode 7
constexpr qsizetype kMaxSequence = 64;
constexpr size_t kMaxKittyStack = 64;

void appendNumber(QByteArray &out, int n)
{
    out.append(QByteArray::number(n));
}

void appendUtf8(QByteArray &out, uint32_t c)
{
    if (c < 0x80) {
        out.append(char(c));
    } else if (c < 0x800) {
        out.append(char(0xc0 | (c >> 6)));
        out.append(char(0x80 | (c & 0x3f)));
    } else if (c < 0x10000) {
        out.append(char(0xe0 | (c >> 12)));
        out.append(char(0x80 | ((c >> 6) & 0x3f)));
        out.append(char(0x80 | (c & 0x3f)));
    } else if (c < 0x110000) {
        out.append(char(0xf0 | (c >> 18)));
        out.append(char(0x80 | ((c >> 12) & 0x3f)));
        out.append(char(0x80 | ((c >> 6) & 0x3f)));
        out.append(char(0x80 | (c & 0x3f)));
    }
}

void appendColor(QByteArray &out, const VTermColor &c, bool foreground)
{
    if (foreground ? VTERM_COLOR_IS_DEFAULT_FG(&c) : VTERM_COLOR_IS_DEFAULT_BG(&c)) {
        return;
    }
    if (VTERM_COLOR_IS_INDEXED(&c)) {
        const int i = c.indexed.idx;
        out.append(';');
        if (i < 8) {
            appendNumber(out, (foreground ? 30 : 40) + i);
        } else if (i < 16) {
            appendNumber(out, (foreground ? 90 : 100) + i - 8);
        } else {
            out.append(foreground ? "38;5;" : "48;5;");
            appendNumber(out, i);
        }
    } else if (VTERM_COLOR_IS_RGB(&c)) {
        out.append(foreground ? ";38;2;" : ";48;2;");
        appendNumber(out, c.rgb.red);
        out.append(';');
        appendNumber(out, c.rgb.green);
        out.append(';');
        appendNumber(out, c.rgb.blue);
    }
}

struct Pen
{
    bool bold = false;
    int underline = 0;
    bool italic = false;
    bool blink = false;
    bool reverse = false;
    bool conceal = false;
    bool strike = false;
    VTermColor fg{.type = VTERM_COLOR_DEFAULT_FG};
    VTermColor bg{.type = VTERM_COLOR_DEFAULT_BG};

    // A full SGR that sets exactly this pen, starting from a reset.
    QByteArray sgr() const
    {
        QByteArray out("\x1b[0");
        if (bold) {
            out.append(";1");
        }
        if (italic) {
            out.append(";3");
        }
        if (underline == VTERM_UNDERLINE_DOUBLE) {
            out.append(";21");
        } else if (underline == VTERM_UNDERLINE_CURLY) {
            out.append(";4:3");
        } else if (underline) {
            out.append(";4");
        }
        if (blink) {
            out.append(";5");
        }
        if (reverse) {
            out.append(";7");
        }
        if (conceal) {
            out.append(";8");
        }
        if (strike) {
            out.append(";9");
        }
        appendColor(out, fg, true);
        appendColor(out, bg, false);
        out.append('m');
        return out;
    }
};

Pen cellPen(const VTermScreenCell &c)
{
    Pen p;
    p.bold = c.attrs.bold;
    p.underline = int(c.attrs.underline);
    p.italic = c.attrs.italic;
    p.blink = c.attrs.blink;
    p.reverse = c.attrs.reverse;
    p.conceal = c.attrs.conceal;
    p.strike = c.attrs.strike;
    p.fg = c.fg;
    p.bg = c.bg;
    return p;
}

// A cell a cleared screen already shows.
bool isBlank(const VTermScreenCell &c)
{
    return c.chars[0] == 0 && VTERM_COLOR_IS_DEFAULT_BG(&c.bg) && !c.attrs.reverse && !c.attrs.underline && !c.attrs.strike;
}

// Follows the modes libvterm keeps to itself, by watching the same bytes for
// the sequences that set them.
struct ModeTracker
{
    enum State {
        Ground,
        Escape,
        EscapeIntermediate,
        Csi,
        String,
        StringEscape,
    };

    State state = Ground;
    QByteArray sequence;
    bool overflow = false;
    bool modes[kModeCount] = {};
    bool origin = false;
    bool keypad = false;
    int top = 0; // scroll region, 1-based; 0 is the screen edge
    int bottom = 0;
    int cursorShape = 0;
    int kitty = 0;
    std::vector<int> kittyStack;
    int modifyOtherKeys = 0;

    ModeTracker() { reset(); }

    void reset()
    {
        std::fill(std::begin(modes), std::end(modes), false);
        modes[kAutowrap] = true;
        origin = false;
        keypad = false;
        top = bottom = 0;
        cursorShape = 0;
        kitty = 0;
        kittyStack.clear();
        modifyOtherKeys = 0;
    }

    void setMode(int mode, bool on)
    {
        if (mode == 6) {
            origin = on;
            return;
        }
        for (int i = 0; i < kModeCount; ++i) {
            if (kModes[i] == mode) {
                modes[i] = on;
            }
        }
    }

    void scan(const char *bytes, size_t len)
    {
        for (size_t i = 0; i < len; ++i) {
            const auto c = static_cast<unsigned char>(bytes[i]);
            switch (state) {
            case Ground:
                if (c == 0x1b) {
                    state = Escape;
                }
                break;
            case Escape:
                state = Ground;
                if (c == '[') {
                    sequence.clear();
                    overflow = false;
                    state = Csi;
                } else if (c == ']' || c == 'P' || c == '_' || c == '^' || c == 'X') {
                    state = String;
                } else if (c == '=') {
                    keypad = true;
                } else if (c == '>') {
                    keypad = false;
                } else if (c == 'c') {
                    reset();
                } else if (c >= 0x20 && c <= 0x2f) {
                    state = EscapeIntermediate;
                } else if (c == 0x1b) {
                    state = Escape;
                }
                break;
            case EscapeIntermediate:
                if (c == 0x1b) {
                    state = Escape;
                } else if (c >= 0x30 || c == 0x18 || c == 0x1a) {
                    state = Ground;
                }
                break;
            case Csi:
                if (c >= 0x40 && c <= 0x7e) {
                    if (!overflow) {
                        dispatch(char(c));
                    }
                    state = Ground;
                } else if (c >= 0x20 && c <= 0x3f) {
                    if (sequence.size() < kMaxSequence) {
                        sequence.append(char(c));
                    } else {
                        overflow = true;
                    }
                } else if (c == 0x1b) {
                    state = Escape;
                } else if (c == 0x18 || c == 0x1a) {
                    state = Ground;
                }
                break;
            case String:
                if (c == 0x07 || c == 0x18 || c == 0x1a) {
                    state = Ground;
                } else if (c == 0x1b) {
                    state = StringEscape;
                }
                break;
            case StringEscape:
                // ESC \ ends the string; any other escape ends it and starts anew.
                state = Escape;
                if (c == '\\') {
                    state = Ground;
                } else {
                    --i;
                }
                break;
            }
        }
    }

    void dispatch(char final)
    {
        QByteArray body = sequence;
        char prefix = 0;
        if (!body.isEmpty() && (body.front() == '?' || body.front() == '<' || body.front() == '=' || body.front() == '>')) {
            prefix = body.front();
            body.remove(0, 1);
        }
        QByteArray intermediates;
        while (!body.isEmpty() && body.back() >= 0x20 && body.back() <= 0x2f) {
            intermediates.prepend(body.back());
            body.chop(1);
        }
        std::vector<int> params;
        for (const QByteArray &part : body.split(';')) {
            const QByteArray main = part.left(part.indexOf(':') < 0 ? part.size() : part.indexOf(':'));
            bool ok = false;
            const int n = main.toInt(&ok);
            params.push_back(ok ? n : -1);
        }
        auto param = [&params](size_t i, int fallback) {
            return i < params.size() && params[i] >= 0 ? params[i] : fallback;
        };

        if (prefix == '?' && intermediates.isEmpty() && (final == 'h' || final == 'l')) {
            for (int p : params) {
                setMode(p, final == 'h');
            }
        } else if (prefix == 0 && intermediates.isEmpty() && final == 'r') {
            top = param(0, 0);
            bottom = param(1, 0);
            if (top <= 1) {
                top = 0;
            }
        } else if (prefix == 0 && intermediates == "!" && final == 'p') {
            // DECSTR, as libvterm applies it.
            setMode(1, false);
            setMode(7, true);
            origin = false;
            keypad = false;
            top = bottom = 0;
        } else if (prefix == 0 && intermediates == " " && final == 'q') {
            cursorShape = param(0, 0);
        } else if (prefix == '>' && final == 'u') {
            if (kittyStack.size() >= kMaxKittyStack) {
                kittyStack.erase(kittyStack.begin());
            }
            kittyStack.push_back(kitty);
            kitty = param(0, 0);
        } else if (prefix == '<' && final == 'u') {
            for (int n = param(0, 1); n > 0; --n) {
                kitty = kittyStack.empty() ? 0 : kittyStack.back();
                if (!kittyStack.empty()) {
                    kittyStack.pop_back();
                }
            }
        } else if (prefix == '=' && final == 'u') {
            const int flags = param(0, 0);
            switch (param(1, 1)) {
            case 2:
                kitty |= flags;
                break;
            case 3:
                kitty &= ~flags;
                break;
            default:
                kitty = flags;
                break;
            }
        } else if (prefix == '>' && final == 'm') {
            if (param(0, -1) == 4) {
                modifyOtherKeys = param(1, 0);
            } else if (param(0, -1) < 0) {
                modifyOtherKeys = 0;
            }
        } else if (prefix == '>' && final == 'n' && param(0, -1) == 4) {
            modifyOtherKeys = 0;
        }
    }
};
} // namespace

struct SessionScreen::Private
{
    VTerm *vt = nullptr;
    VTermState *state = nullptr;
    VTermScreen *screen = nullptr;
    int rows = 0;
    int cols = 0;
    bool cursorVisible = true;
    bool altScreen = false;
    QByteArray title;
    QByteArray titleBuffer;
    ModeTracker tracker;

    static int settermprop(VTermProp prop, VTermValue *val, void *user)
    {
        auto *d = static_cast<Private *>(user);
        switch (prop) {
        case VTERM_PROP_CURSORVISIBLE:
            d->cursorVisible = val->boolean;
            break;
        case VTERM_PROP_ALTSCREEN:
            d->altScreen = val->boolean;
            break;
        case VTERM_PROP_TITLE:
            if (val->string.initial) {
                d->titleBuffer.clear();
            }
            d->titleBuffer.append(val->string.str, qsizetype(val->string.len));
            if (val->string.final) {
                d->title = d->titleBuffer;
            }
            break;
        default:
            break;
        }
        return 1;
    }

    // The daemon has no display to answer queries from; the attached
    // terminal answers them.
    static void discardOutput(const char *, size_t, void *) { }

    bool cell(int row, int col, VTermScreenCell &out) const { return vterm_screen_get_cell(screen, VTermPos{row, col}, &out) != 0; }

    bool continuation(int row) const
    {
        const VTermLineInfo *info = vterm_state_get_lineinfo(state, row);
        return info && info->continuation;
    }

    Pen pen() const
    {
        Pen p;
        VTermValue v;
        vterm_state_get_penattr(state, VTERM_ATTR_BOLD, &v);
        p.bold = v.boolean;
        vterm_state_get_penattr(state, VTERM_ATTR_UNDERLINE, &v);
        p.underline = v.number;
        vterm_state_get_penattr(state, VTERM_ATTR_ITALIC, &v);
        p.italic = v.boolean;
        vterm_state_get_penattr(state, VTERM_ATTR_BLINK, &v);
        p.blink = v.boolean;
        vterm_state_get_penattr(state, VTERM_ATTR_REVERSE, &v);
        p.reverse = v.boolean;
        vterm_state_get_penattr(state, VTERM_ATTR_CONCEAL, &v);
        p.conceal = v.boolean;
        vterm_state_get_penattr(state, VTERM_ATTR_STRIKE, &v);
        p.strike = v.boolean;
        vterm_state_get_penattr(state, VTERM_ATTR_FOREGROUND, &v);
        p.fg = v.color;
        vterm_state_get_penattr(state, VTERM_ATTR_BACKGROUND, &v);
        p.bg = v.color;
        return p;
    }

    // The columns of `row` worth writing: all of them before a soft wrap, so
    // the attached terminal wraps too, otherwise up to the last non-blank cell.
    bool rowWraps(int row) const { return row + 1 < rows && continuation(row + 1); }

    int rowEnd(int row) const
    {
        if (rowWraps(row)) {
            return cols;
        }
        int end = cols;
        VTermScreenCell c;
        while (end > 0 && cell(row, end - 1, c) && isBlank(c)) {
            --end;
        }
        return end;
    }
};

namespace
{
const VTermScreenCallbacks kCallbacks = {
    nullptr, // damage
    nullptr, // moverect
    nullptr, // movecursor
    SessionScreen::Private::settermprop,
    nullptr, // bell
    nullptr, // resize
    nullptr, // sb_pushline: no scrollback
    nullptr, // sb_popline
    nullptr, // sb_clear
};
} // namespace

SessionScreen::SessionScreen(int rows, int cols)
    : d(std::make_unique<Private>())
{
    d->rows = std::max(1, rows);
    d->cols = std::max(1, cols);
    d->vt = vterm_new(d->rows, d->cols);
    vterm_set_utf8(d->vt, 1);
    vterm_output_set_callback(d->vt, Private::discardOutput, nullptr);
    d->state = vterm_obtain_state(d->vt);
    d->screen = vterm_obtain_screen(d->vt);
    vterm_screen_set_callbacks(d->screen, &kCallbacks, d.get());
    vterm_screen_enable_altscreen(d->screen, 1);
    vterm_screen_enable_reflow(d->screen, true);
    vterm_screen_reset(d->screen, 1);
}

SessionScreen::~SessionScreen()
{
    vterm_free(d->vt);
}

void SessionScreen::feed(const QByteArray &bytes)
{
    vterm_input_write(d->vt, bytes.constData(), size_t(bytes.size()));
    d->tracker.scan(bytes.constData(), size_t(bytes.size()));
}

void SessionScreen::resize(int rows, int cols)
{
    rows = std::max(1, rows);
    cols = std::max(1, cols);
    if (rows == d->rows && cols == d->cols) {
        return;
    }
    vterm_set_size(d->vt, rows, cols);
    d->rows = rows;
    d->cols = cols;
    // libvterm drops a region that no longer fits; the program sets a new
    // one when it handles SIGWINCH.
    d->tracker.top = d->tracker.bottom = 0;
}

QByteArray SessionScreen::redraw() const
{
    const ModeTracker &t = d->tracker;
    QByteArray out;
    out.reserve(d->rows * (d->cols + 16));
    out.append("\x1b[0m");
    // Leaving with 1047, not 1049: 1049 would restore a cursor this terminal
    // never saved, and libvterm restores an all-zero pen with it.
    out.append(d->altScreen ? "\x1b[?1049h" : "\x1b[?1047l");
    out.append("\x1b[?6l\x1b[r\x1b[?7h\x1b[H\x1b[2J");

    QByteArray pen = Pen().sgr();
    bool wroteFullRow = false;
    VTermScreenCell c;
    for (int row = 0; row < d->rows; ++row) {
        const int end = d->rowEnd(row);
        if (end == 0) {
            wroteFullRow = false;
            continue;
        }
        if (!(row > 0 && wroteFullRow && d->continuation(row))) {
            out.append("\x1b[");
            appendNumber(out, row + 1);
            out.append('H');
        }
        for (int col = 0; col < end;) {
            if (!d->cell(row, col, c) || c.chars[0] == uint32_t(-1)) {
                ++col;
                continue;
            }
            const QByteArray cellSgr = cellPen(c).sgr();
            if (cellSgr != pen) {
                out.append(cellSgr);
                pen = cellSgr;
            }
            if (c.chars[0] == 0) {
                // A run of erased cells stays erased (ECH, which takes the
                // pen's background), unless the row has to wrap at its end.
                int run = 1;
                VTermScreenCell next;
                while (col + run < end && d->cell(row, col + run, next) && next.chars[0] == 0 && cellPen(next).sgr() == cellSgr) {
                    ++run;
                }
                if (col + run == d->cols && d->rowWraps(row)) {
                    out.append(QByteArray(run, ' '));
                } else {
                    out.append("\x1b[");
                    appendNumber(out, run);
                    out.append('X');
                    if (col + run < end) {
                        out.append("\x1b[");
                        appendNumber(out, run);
                        out.append('C');
                    }
                }
                col += run;
                continue;
            }
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && c.chars[i]; ++i) {
                appendUtf8(out, c.chars[i]);
            }
            col += std::max(1, int(c.width));
        }
        wroteFullRow = end == d->cols;
    }

    for (int i = 0; i < kModeCount; ++i) {
        out.append("\x1b[?");
        appendNumber(out, kModes[i]);
        out.append(t.modes[i] ? 'h' : 'l');
    }
    out.append(t.keypad ? "\x1b=" : "\x1b>");
    if (t.kitty) {
        out.append("\x1b[=");
        appendNumber(out, t.kitty);
        out.append(";1u");
    }
    if (t.modifyOtherKeys) {
        out.append("\x1b[>4;");
        appendNumber(out, t.modifyOtherKeys);
        out.append('m');
    }
    if (t.cursorShape) {
        out.append("\x1b[");
        appendNumber(out, t.cursorShape);
        out.append(" q");
    }
    if (!d->title.isEmpty()) {
        out.append("\x1b]2;");
        out.append(d->title);
        out.append("\x1b\\");
    }
    if (t.top || t.bottom) {
        out.append("\x1b[");
        appendNumber(out, std::max(1, t.top));
        out.append(';');
        appendNumber(out, t.bottom ? t.bottom : d->rows);
        out.append('r');
    }
    if (t.origin) {
        out.append("\x1b[?6h");
    }
    out.append(d->cursorVisible ? "\x1b[?25h" : "\x1b[?25l");
    out.append(d->pen().sgr());

    VTermPos pos;
    vterm_state_get_cursorpos(d->state, &pos);
    int row = pos.row + 1;
    if (t.origin && t.top > 1) {
        row -= t.top - 1;
    }
    out.append("\x1b[");
    appendNumber(out, std::max(1, row));
    out.append(';');
    appendNumber(out, pos.col + 1);
    out.append('H');
    return out;
}

int SessionScreen::rows() const
{
    return d->rows;
}

int SessionScreen::cols() const
{
    return d->cols;
}

QString SessionScreen::rowText(int row) const
{
    QByteArray text;
    VTermScreenCell c;
    for (int col = 0; col < d->cols; ++col) {
        if (!d->cell(row, col, c) || c.chars[0] == uint32_t(-1)) {
            continue;
        }
        if (c.chars[0] == 0) {
            text.append(' ');
        }
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && c.chars[i]; ++i) {
            appendUtf8(text, c.chars[i]);
        }
    }
    QString s = QString::fromUtf8(text);
    while (s.endsWith(QLatin1Char(' '))) {
        s.chop(1);
    }
    return s;
}

int SessionScreen::cursorRow() const
{
    VTermPos pos;
    vterm_state_get_cursorpos(d->state, &pos);
    return pos.row;
}

int SessionScreen::cursorCol() const
{
    VTermPos pos;
    vterm_state_get_cursorpos(d->state, &pos);
    return pos.col;
}

bool SessionScreen::cursorVisible() const
{
    return d->cursorVisible;
}

bool SessionScreen::altScreen() const
{
    return d->altScreen;
}

QString SessionScreen::title() const
{
    return QString::fromUtf8(d->title);
}
