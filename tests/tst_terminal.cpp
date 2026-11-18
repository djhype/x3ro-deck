// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PtyChannel.h"
#include "TerminalAppearance.h"
#include "TerminalChannel.h"
#include "TerminalScreen.h"
#include "TerminalView.h"
#include "TmuxManager.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QInputMethodEvent>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

namespace
{
// Records what the view writes and lets the test play the program's side.
class FakeChannel : public TerminalChannel
{
public:
    using TerminalChannel::TerminalChannel;
    void write(const QByteArray &bytes) override { written += bytes; }
    void resize(int rows, int cols) override { size = QSize(cols, rows); }
    bool isOpen() const override { return true; }
    void print(const QByteArray &bytes) { Q_EMIT received(bytes); }

    QByteArray written;
    QSize size;
};

QByteArray numberedLines(int from, int to)
{
    QByteArray out;
    for (int i = from; i <= to; ++i) {
        out += "\r\nline " + QByteArray::number(i);
    }
    return out;
}

// The view's text, row by row.
QStringList visibleRows(const TerminalView &view)
{
    QStringList rows;
    for (int r = 0; r < view.rows(); ++r) {
        QString text;
        for (int c = 0; c < view.cols(); ++c) {
            const TerminalScreen::Cell cell = view.screen()->lineCell(view.topLine() + r, c);
            if (cell.width != 0) {
                text += cell.text.isEmpty() ? QStringLiteral(" ") : cell.text;
            }
        }
        rows << text.trimmed();
    }
    return rows;
}

QPoint cellCenter(const TerminalView &view, int col, int row)
{
    const QSize cell = view.cellSize();
    const int pad = view.appearance().padding;
    return QPoint(pad + col * cell.width() + cell.width() / 2, pad + row * cell.height() + cell.height() / 2);
}

void sizeView(TerminalView &view, int cols, int rows)
{
    const QSize cell = view.cellSize();
    const int pad = view.appearance().padding;
    view.resize(cell.width() * cols + 2 * pad, cell.height() * rows + 2 * pad);
}

void wheel(TerminalView &view, int delta, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    const QPointF pos = cellCenter(view, 1, 1);
    QWheelEvent e(pos, view.mapToGlobal(pos), QPoint(), QPoint(0, delta), Qt::NoButton, mods, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&view, &e);
}

QByteArray collect(TerminalScreen &screen, const std::function<void()> &fn)
{
    QByteArray out;
    auto c = QObject::connect(&screen, &TerminalScreen::output, [&out](const QByteArray &b) { out += b; });
    fn();
    QObject::disconnect(c);
    return out;
}
} // namespace

class TestTerminal : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // A private tmux server and config, never the user's.
        QVERIFY(m_tmp.isValid());
        QFile::setPermissions(m_tmp.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("TMUX_TMPDIR", m_tmp.path().toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", (m_tmp.path() + QStringLiteral("/config")).toLocal8Bit());
        qputenv("SHELL", "/bin/sh");
    }

    void cleanupTestCase()
    {
        TmuxManager tmux;
        tmux.killAll();
    }

    void screenText()
    {
        TerminalScreen s(5, 20);
        s.feed("hello\r\nworld");
        QCOMPARE(s.rowText(0), QStringLiteral("hello"));
        QCOMPARE(s.rowText(1), QStringLiteral("world"));
        QCOMPARE(s.cursor(), QPoint(5, 1));
        // A UTF-8 sequence split across two reads.
        s.feed("\r\n\xc3");
        s.feed("\xa4\xe2\x94\x80");
        QCOMPARE(s.rowText(2), QStringLiteral("ä─"));
    }

    void screenAttributes()
    {
        const TerminalAppearance a = TerminalAppearance::fallback();
        TerminalScreen s(3, 20);
        s.feed("\x1b[31mr\x1b[0m \x1b[1;3;4mb\x1b[0m \x1b[7mi\x1b[0m \x1b[38;5;196mx\x1b[48;5;21my\x1b[0m \x1b[38;2;1;2;3mt");
        QCOMPARE(s.cell(0, 0).fg, a.palette[1]);
        QVERIFY(s.cell(0, 0).defaultBg);
        const TerminalScreen::Cell b = s.cell(0, 2);
        QVERIFY(b.bold && b.italic);
        QCOMPARE(b.underline, 1);
        const TerminalScreen::Cell inv = s.cell(0, 4);
        QCOMPARE(inv.fg, a.background);
        QCOMPARE(inv.bg, a.foreground);
        QVERIFY(!inv.defaultBg);
        QCOMPARE(s.cell(0, 6).fg, QColor(255, 0, 0)); // 256-color cube
        QCOMPARE(s.cell(0, 7).bg, QColor(0, 0, 255));
        QCOMPARE(s.cell(0, 9).fg, QColor(1, 2, 3));
        QCOMPARE(s.cell(0, 1).fg, a.foreground);
    }

    void screenWideAndAltScreen()
    {
        TerminalScreen s(3, 10);
        s.feed("\xe6\x97\xa5x"); // 日x
        QCOMPARE(s.cell(0, 0).width, 2);
        QCOMPARE(s.cell(0, 1).width, 0);
        QCOMPARE(s.cell(0, 2).text, QStringLiteral("x"));
        s.feed("\x1b[?1049h");
        QVERIFY(s.altScreen());
        QCOMPARE(s.rowText(0), QString());
        s.feed("\x1b[?1049l");
        QCOMPARE(s.rowText(0), QStringLiteral("日x"));
        s.feed("\x1b]0;my title\x07");
        QCOMPARE(s.title(), QStringLiteral("my title"));
    }

    void screenTrueColorAndWideText()
    {
        TerminalScreen s(3, 40);
        // A 24-bit gradient keeps every step, foreground and background.
        QByteArray gradient;
        for (int i = 0; i < 32; ++i) {
            gradient += "\x1b[48;2;" + QByteArray::number(i * 8) + ";" + QByteArray::number(255 - i * 8) + ";128m ";
        }
        s.feed(gradient + "\x1b[0m\r\n\x1b[38;2;250;128;7;4:3mc\x1b[0m");
        for (int i = 0; i < 32; ++i) {
            QCOMPARE(s.cell(0, i).bg, QColor(i * 8, 255 - i * 8, 128));
            QVERIFY(!s.cell(0, i).defaultBg);
        }
        QCOMPARE(s.cell(1, 0).fg, QColor(250, 128, 7));
        QCOMPARE(s.cell(1, 0).underline, 3);

        // CJK and emoji take two cells; combining marks stay on their base.
        s.feed("\x1b[3;1H\xe4\xbd\xa0\xe5\xa5\xbd\xf0\x9f\x98\x80"
               "e\xcc\x81|"); // 你好😀é|
        QCOMPARE(s.cell(2, 0).text, QStringLiteral("你"));
        QCOMPARE(s.cell(2, 0).width, 2);
        QCOMPARE(s.cell(2, 1).width, 0);
        QCOMPARE(s.cell(2, 2).text, QStringLiteral("好"));
        QCOMPARE(s.cell(2, 4).text, QString::fromUtf8("\xf0\x9f\x98\x80"));
        QCOMPARE(s.cell(2, 4).width, 2);
        QCOMPARE(s.cell(2, 6).text, QString::fromUtf8("e\xcc\x81"));
        QCOMPARE(s.cell(2, 6).width, 1);
        QCOMPARE(s.cell(2, 7).text, QStringLiteral("|"));
    }

    void screenCursorStyle()
    {
        TerminalScreen s(3, 10);
        QCOMPARE(s.cursorShape(), TerminalScreen::Block);
        QVERIFY(!s.cursorBlink());
        s.feed("\x1b[6 q");
        QCOMPARE(s.cursorShape(), TerminalScreen::Bar);
        QVERIFY(!s.cursorBlink());
        s.feed("\x1b[3 q");
        QCOMPARE(s.cursorShape(), TerminalScreen::Underline);
        QVERIFY(s.cursorBlink());
        s.feed("\x1b[2 q");
        QCOMPARE(s.cursorShape(), TerminalScreen::Block);
        QVERIFY(!s.cursorBlink());
        s.feed("\x1b[?12h");
        QVERIFY(s.cursorBlink());
        s.reset();
        QCOMPARE(s.cursorShape(), TerminalScreen::Block);
        QVERIFY(!s.cursorBlink());

        // The profile's cursor is the default until a program picks its own.
        TerminalAppearance a = TerminalAppearance::fallback();
        a.cursorShape = TerminalAppearance::Underline;
        a.cursorBlink = true;
        s.setAppearance(a);
        QCOMPARE(s.cursorShape(), TerminalScreen::Underline);
        QVERIFY(s.cursorBlink());
        s.feed("\x1b[6 q");
        a.cursorShape = TerminalAppearance::Block;
        s.setAppearance(a);
        QCOMPARE(s.cursorShape(), TerminalScreen::Bar);
        s.reset();
        QCOMPARE(s.cursorShape(), TerminalScreen::Block);
        QVERIFY(s.cursorBlink());
    }

    void viewLineSpacing()
    {
        TerminalView v;
        const int before = v.cellSize().height();
        TerminalAppearance a = v.appearance();
        a.lineSpacing = 3;
        v.setAppearance(a);
        QCOMPARE(v.cellSize().height(), before + 3);
    }

    void screenMouseReports()
    {
        TerminalScreen s(10, 40);
        QByteArray out;
        connect(&s, &TerminalScreen::output, this, [&out](const QByteArray &b) { out += b; });
        auto click = [&s](int row, int col, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            s.sendMouseMove(row, col, mods);
            s.sendMouseButton(1, true, mods);
            s.sendMouseButton(1, false, mods);
        };
        // Nothing until the program asks.
        click(1, 2);
        QVERIFY(out.isEmpty());

        // 1000 (clicks) in the X10 encoding: CSI M, then button, column and
        // row, each plus 32 and 1-based. A release is button 3.
        s.feed("\x1b[?1000h");
        click(1, 2);
        QCOMPARE(out, QByteArray("\x1b[M #\"\x1b[M##\""));
        out.clear();
        // Clicks only: a drag reports nothing but the buttons.
        s.sendMouseMove(4, 4);
        s.sendMouseButton(3, true);
        s.sendMouseMove(4, 6);
        s.sendMouseButton(3, false);
        QCOMPARE(out, QByteArray("\x1b[M\"%%\x1b[M#'%")); // the release lands where the pointer went
        out.clear();

        // SGR (1006): decimal, M for press and m for release, which keeps the button.
        s.feed("\x1b[?1006h");
        click(1, 2);
        QCOMPARE(out, QByteArray("\x1b[<0;3;2M\x1b[<0;3;2m"));
        out.clear();
        s.sendMouseMove(1, 3, Qt::ControlModifier);
        s.sendMouseButton(2, true, Qt::ControlModifier);
        s.sendMouseButton(2, false, Qt::ControlModifier);
        QCOMPARE(out, QByteArray("\x1b[<17;4;2M\x1b[<17;4;2m")); // Ctrl adds 16
        out.clear();

        // 1002 (button events): motion with a button held.
        s.feed("\x1b[?1000l\x1b[?1002h");
        s.sendMouseMove(0, 0);
        s.sendMouseButton(1, true);
        s.sendMouseMove(0, 5);
        s.sendMouseButton(1, false);
        s.sendMouseMove(0, 7); // no button: not reported
        QCOMPARE(out, QByteArray("\x1b[<0;1;1M\x1b[<32;6;1M\x1b[<0;6;1m"));
        out.clear();

        // 1003 (any event): motion without a button, too.
        s.feed("\x1b[?1002l\x1b[?1003h");
        s.sendMouseMove(2, 9);
        QCOMPARE(out, QByteArray("\x1b[<35;10;3M"));
        out.clear();

        s.feed("\x1b[?1003l");
        QVERIFY(!s.mouseReporting());
        click(1, 2);
        QVERIFY(out.isEmpty());
    }

    void viewMouseReports()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        sizeView(view, 20, 6);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        channel.print("hello world\r\n\x1b[?1002h\x1b[?1006h");

        // htop-style click and a drag, reported to the program.
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, cellCenter(view, 2, 1));
        QTest::mouseMove(&view, cellCenter(view, 4, 1));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, cellCenter(view, 4, 1));
        QCOMPARE(channel.written, QByteArray("\x1b[<0;3;2M\x1b[<32;5;2M\x1b[<0;5;2m"));
        QVERIFY(!view.hasSelection());
        channel.written.clear();
        QTest::mouseClick(&view, Qt::RightButton, Qt::AltModifier, cellCenter(view, 0, 0));
        QCOMPARE(channel.written, QByteArray("\x1b[<10;1;1M\x1b[<10;1;1m")); // Alt adds 8
        channel.written.clear();

        // Shift selects locally instead.
        QTest::mousePress(&view, Qt::LeftButton, Qt::ShiftModifier, cellCenter(view, 0, 0));
        QTest::mouseMove(&view, cellCenter(view, 4, 0));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::ShiftModifier, cellCenter(view, 4, 0));
        QVERIFY(channel.written.isEmpty());
        QCOMPARE(view.selectedText(), QStringLiteral("hello"));

        // A release follows its press even if Shift comes down in between.
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, cellCenter(view, 1, 2));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::ShiftModifier, cellCenter(view, 1, 2));
        QCOMPARE(channel.written, QByteArray("\x1b[<0;2;3M\x1b[<4;2;3m"));
        channel.written.clear();

        // Any-event tracking sees the pointer move without a button.
        channel.print("\x1b[?1003h");
        const QPointF hover = cellCenter(view, 7, 3);
        QMouseEvent move(QEvent::MouseMove, hover, view.mapToGlobal(hover), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &move);
        QCOMPARE(channel.written, QByteArray("\x1b[<35;8;4M"));
    }

    void viewInputMethod()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        sizeView(view, 20, 4);
        view.show();
        view.activateWindow();
        view.setFocus();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(view.testAttribute(Qt::WA_InputMethodEnabled));
        QVERIFY(view.inputMethodQuery(Qt::ImEnabled).toBool());
        channel.print("$ ");
        const QSize cell = view.cellSize();
        const int pad = view.appearance().padding;
        QCOMPARE(view.inputMethodQuery(Qt::ImCursorRectangle).toRect(), QRect(pad + 2 * cell.width(), pad, cell.width(), cell.height()));

        // Composing: the preedit is shown at the cursor but not sent.
        {
            QInputMethodEvent e(QStringLiteral("にほ"), {{QInputMethodEvent::Cursor, 2, 1, QVariant()}});
            QCoreApplication::sendEvent(&view, &e);
        }
        QCOMPARE(view.preedit(), QStringLiteral("にほ"));
        QVERIFY(channel.written.isEmpty());
        const QImage img = view.grab().toImage();
        // The preedit underline under the first character, and the caret
        // after the second (two cells each).
        bool underlined = false;
        for (int y = pad; y < pad + cell.height() && !underlined; ++y) {
            bool full = true;
            for (int x = pad + 2 * cell.width(); x < pad + 6 * cell.width() && full; ++x) {
                full = img.pixelColor(x, y) == view.appearance().foreground;
            }
            underlined = full;
        }
        QVERIFY(underlined);
        QCOMPARE(img.pixelColor(pad + 6 * cell.width(), pad + cell.height() / 2), view.appearance().cursor);

        // Committing types the text and clears the preedit.
        {
            QInputMethodEvent e;
            e.setCommitString(QStringLiteral("日本"));
            QCoreApplication::sendEvent(&view, &e);
        }
        QVERIFY(view.preedit().isEmpty());
        QCOMPARE(channel.written, QStringLiteral("日本").toUtf8());
    }

    void viewPaintsCursorShapes()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        sizeView(view, 10, 3);
        view.show();
        view.activateWindow();
        view.setFocus();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.hasFocus());
        const QSize cell = view.cellSize();
        const int pad = view.appearance().padding;
        const QColor cursor = view.appearance().cursor;
        const QColor bg = view.appearance().background;
        auto px = [&](int x, int y) {
            return view.grab().toImage().pixelColor(pad + x, pad + y);
        };
        // Block: the whole cell.
        QCOMPARE(px(cell.width() / 2, cell.height() / 2), cursor);
        // Bar: the left edge only.
        channel.print("\x1b[6 q");
        QCOMPARE(px(0, cell.height() / 2), cursor);
        QCOMPARE(px(cell.width() - 1, cell.height() / 2), bg);
        // Underline: the bottom only.
        channel.print("\x1b[4 q");
        QCOMPARE(px(cell.width() / 2, cell.height() - 1), cursor);
        QCOMPARE(px(cell.width() / 2, 1), bg);
    }

    void viewPaintsWideCharsOnTheGrid()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        sizeView(view, 12, 2);
        // A wide glyph never spills into the next cell's column.
        channel.print("\xe6\x97\xa5\xf0\x9f\x98\x80|\x1b[?25l");
        const QImage img = view.grab().toImage();
        const QSize cell = view.cellSize();
        const int pad = view.appearance().padding;
        const QColor bg = view.appearance().background;
        bool inked = false;
        for (int x = 0; x < 4 * cell.width(); ++x) {
            for (int y = 0; y < cell.height(); ++y) {
                inked |= img.pixelColor(pad + x, pad + y) != bg;
            }
        }
        QVERIFY(inked);
        // Column 5 holds only "|", drawn by the font's ASCII glyph.
        int outside = 0;
        for (int y = 0; y < cell.height(); ++y) {
            outside += img.pixelColor(pad + 5 * cell.width() + 1, pad + y) != bg ? 1 : 0;
        }
        QCOMPARE(outside, 0);
    }

    void screenResize()
    {
        TerminalScreen s(4, 10);
        QSignalSpy changed(&s, &TerminalScreen::changed);
        s.feed("abcdefghij");
        s.resize(6, 30);
        QCOMPARE(s.rows(), 6);
        QCOMPARE(s.cols(), 30);
        QVERIFY(!changed.isEmpty());
        QCOMPARE(s.rowText(0), QStringLiteral("abcdefghij"));
        s.feed("\x1b[6;30HZ");
        QCOMPARE(s.cell(5, 29).text, QStringLiteral("Z"));
    }

    void keyboard()
    {
        TerminalScreen s(5, 20);
        QCOMPARE(collect(s, [&] { s.sendKey(TerminalScreen::Up); }), QByteArray("\x1b[A"));
        s.feed("\x1b[?1h"); // application cursor keys, as Claude Code's menus set
        QCOMPARE(collect(s, [&] { s.sendKey(TerminalScreen::Up); }), QByteArray("\x1bOA"));
        QCOMPARE(collect(s, [&] { s.sendKey(TerminalScreen::Enter); }), QByteArray("\r"));
        QCOMPARE(collect(s, [&] { s.sendKey(TerminalScreen::Backspace); }), QByteArray("\x7f"));
        QCOMPARE(collect(s, [&] { s.sendKey(TerminalScreen::Tab, Qt::ShiftModifier); }), QByteArray("\x1b[Z"));
        QCOMPARE(collect(s, [&] { s.sendKey(TerminalScreen::Key(TerminalScreen::F0 + 1)); }), QByteArray("\x1bOP"));
        QCOMPARE(collect(s, [&] { s.sendChar(U'c', Qt::ControlModifier); }), QByteArray("\x03"));
        QCOMPARE(collect(s, [&] { s.sendChar(U'[', Qt::ControlModifier); }), QByteArray("\x1b"));
        QCOMPARE(collect(s, [&] { s.sendChar(U' ', Qt::ControlModifier); }), QByteArray(1, '\0'));
        QCOMPARE(collect(s, [&] { s.sendChar(U'b', Qt::AltModifier); }),
                 QByteArray("\x1b"
                            "b"));
        QCOMPARE(collect(s, [&] { s.sendChar(U'ü'); }), QByteArray("\xc3\xbc"));
    }

    void paste()
    {
        TerminalScreen s(5, 20);
        QCOMPARE(collect(s, [&] { s.paste(QStringLiteral("a\nb\r\nc")); }), QByteArray("a\rb\rc"));
        s.feed("\x1b[?2004h");
        QCOMPARE(collect(s, [&] { s.paste(QStringLiteral("one\ntwo\x1b[201~")); }), QByteArray("\x1b[200~one\rtwo\x1b[201~"));
    }

    void scrollbackPushPopAndLimit()
    {
        TerminalScreen s(3, 10);
        QSignalSpy changed(&s, &TerminalScreen::scrollbackChanged);
        s.feed("line 1" + numberedLines(2, 5));
        QCOMPARE(s.scrollbackSize(), 2);
        QCOMPARE(s.lineCount(), 5);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toInt(), 2);
        QCOMPARE(s.text(QPoint(0, 0), QPoint(9, 4)), QStringLiteral("line 1\nline 2\nline 3\nline 4\nline 5"));
        // Colors survive the trip into the history.
        s.feed("\r\n\x1b[31mred\x1b[0m" + numberedLines(7, 9));
        const int red = s.scrollbackSize() - 1;
        QCOMPARE(s.lineCell(red, 0).text, QStringLiteral("r"));
        QCOMPARE(s.lineCell(red, 0).fg, TerminalAppearance::fallback().palette[1]);
        QVERIFY(s.lineCell(red, 8).text.isEmpty());

        // Growing the screen takes lines back from the history.
        const int before = s.scrollbackSize();
        s.resize(5, 10);
        QCOMPARE(s.scrollbackSize(), before - 2);
        QCOMPARE(s.rowText(4), QStringLiteral("line 9"));
        QCOMPARE(s.rowText(1), QStringLiteral("red"));

        s.setScrollbackLimit(3);
        QCOMPARE(s.scrollbackSize(), 3);
        const qint64 dropped = s.droppedLines();
        QVERIFY(dropped > 0);
        s.feed(numberedLines(10, 19));
        QCOMPARE(s.scrollbackSize(), 3);
        QCOMPARE(s.droppedLines(), dropped + 10);
        QCOMPARE(s.text(QPoint(0, 0), QPoint(9, 0)), QStringLiteral("line 12"));

        s.feed("\x1b[3J"); // clear scrollback
        QCOMPARE(s.scrollbackSize(), 0);
        QCOMPARE(s.droppedLines(), dropped + 13);

        s.setScrollbackLimit(0);
        s.feed(numberedLines(20, 30));
        QCOMPARE(s.scrollbackSize(), 0);
    }

    void scrollbackSkipsAltScreen()
    {
        TerminalScreen s(3, 10);
        s.feed("\x1b[?1049h" + numberedLines(1, 10));
        QCOMPARE(s.scrollbackSize(), 0);
    }

    void selectionTextWrapsAndWideChars()
    {
        TerminalScreen s(3, 5);
        // "abcdefgh" wraps after five columns; "xy" has trailing blanks; 日本z
        // fills the row with two wide characters.
        s.feed("abcdefgh\r\nxy   \r\n\xe6\x97\xa5\xe6\x9c\xac"
               "z\r\nlast");
        QCOMPARE(s.scrollbackSize(), 2);
        QVERIFY(s.lineWrapped(0));
        QVERIFY(!s.lineWrapped(1));
        QCOMPARE(s.text(QPoint(0, 0), QPoint(4, 4)), QStringLiteral("abcdefgh\nxy\n日本z\nlast"));
        // Starting on the right half of a wide character takes all of it.
        QCOMPARE(s.text(QPoint(1, 3), QPoint(3, 3)), QStringLiteral("日本"));
        // Reversed ends give the same text.
        QCOMPARE(s.text(QPoint(2, 1), QPoint(1, 0)), QStringLiteral("bcdefgh"));
        QCOMPARE(s.text(QPoint(1, 0), QPoint(4, 2)), QStringLiteral("bcdefgh\nxy"));
        QCOMPARE(s.logicalLine(1), std::make_pair(0, 1));
        QCOMPARE(s.logicalLine(2), std::make_pair(2, 2));

        // A soft wrap on the screen joins too.
        TerminalScreen t(4, 6);
        t.feed("hello world\r\nnext");
        QVERIFY(t.lineWrapped(0));
        QCOMPARE(t.text(QPoint(0, 0), QPoint(5, 2)), QStringLiteral("hello world\nnext"));
    }

    void selectionWords()
    {
        TerminalScreen s(3, 40);
        s.feed("ls /usr/local/bin foo, bar");
        auto word = [&s](int col) {
            const auto [from, to] = s.wordAt(QPoint(col, 0));
            return s.text(from, to);
        };
        QCOMPARE(word(8), QStringLiteral("/usr/local/bin"));
        QCOMPARE(word(0), QStringLiteral("ls"));
        QCOMPARE(word(21), QStringLiteral(","));
        // A URL that wraps selects whole.
        TerminalScreen u(3, 10);
        u.feed("see https://ex.com/a/b c");
        const auto [from, to] = u.wordAt(QPoint(6, 0));
        QCOMPARE(u.text(from, to), QStringLiteral("https://ex.com/a/b"));
    }

    void viewStaysPutWhileOutputArrives()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        sizeView(view, 20, 5);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        channel.print("line 1" + numberedLines(2, 3000));
        QCOMPARE(view.scrollOffset(), 0);
        QCOMPARE(visibleRows(view).last(), QStringLiteral("line 3000"));

        view.scrollBy(2000);
        QCOMPARE(view.scrollOffset(), 2000);
        const QStringList shown = visibleRows(view);
        QCOMPARE(shown.first(), QStringLiteral("line 996"));
        channel.print(numberedLines(3001, 3500));
        QCOMPARE(visibleRows(view), shown);
        QCOMPARE(view.scrollOffset(), 2500);

        // Shift+PgUp/PgDn page through it; the wheel moves three lines a notch.
        QTest::keyClick(&view, Qt::Key_PageUp, Qt::ShiftModifier);
        QCOMPARE(view.scrollOffset(), 2504);
        QTest::keyClick(&view, Qt::Key_PageDown, Qt::ShiftModifier);
        wheel(view, -120);
        QCOMPARE(view.scrollOffset(), 2497);
        QVERIFY(channel.written.isEmpty());

        // Typing goes back to the bottom.
        QTest::keyClick(&view, Qt::Key_A);
        QCOMPARE(view.scrollOffset(), 0);
        QCOMPARE(channel.written, QByteArray("a"));
        channel.print(numberedLines(3501, 3502));
        QCOMPARE(view.scrollOffset(), 0);
        QCOMPARE(visibleRows(view).last(), QStringLiteral("line 3502"));

        // Scrolling stops at the top of the history.
        view.scrollBy(1000000);
        QCOMPARE(view.scrollOffset(), view.screen()->scrollbackSize());
        QCOMPARE(view.topLine(), 0);
    }

    void viewWheelInFullScreenPrograms()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        sizeView(view, 20, 5);
        channel.print(numberedLines(1, 20));
        // Alternate screen without mouse reporting: arrow keys.
        channel.print("\x1b[?1049h");
        wheel(view, 120);
        QCOMPARE(view.scrollOffset(), 0);
        QCOMPARE(channel.written, QByteArray("\x1b[A\x1b[A\x1b[A"));
        // With mouse reporting (tmux's "mouse on"): wheel reports, one per notch.
        channel.written.clear();
        channel.print("\x1b[?1000h\x1b[?1006h");
        wheel(view, 120);
        wheel(view, -120);
        QCOMPARE(channel.written, QByteArray("\x1b[<64;2;2M\x1b[<65;2;2M"));
        // Shift keeps the wheel local.
        channel.written.clear();
        wheel(view, 120, Qt::ShiftModifier);
        QVERIFY(!channel.written.contains("\x1b[<"));
    }

    void viewMouseSelection()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        sizeView(view, 20, 6);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        // A long answer that wraps over three rows, then a short line.
        channel.print("Here is a long answer that wraps\r\nsecond line\r\n");

        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, cellCenter(view, 8, 0));
        QTest::mouseMove(&view, cellCenter(view, 5, 2));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, cellCenter(view, 5, 2));
        QCOMPARE(view.selectedText(), QStringLiteral("a long answer that wraps\nsecond"));

        QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("a long answer that wraps\nsecond"));
        QVERIFY(channel.written.isEmpty());

        // A plain click clears it.
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, cellCenter(view, 3, 4));
        QVERIFY(!view.hasSelection());

        // Double-click: a word. A third click: the wrapped line.
        QTest::mouseDClick(&view, Qt::LeftButton, Qt::NoModifier, cellCenter(view, 11, 0));
        QCOMPARE(view.selectedText(), QStringLiteral("long"));
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, cellCenter(view, 11, 0));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, cellCenter(view, 11, 0));
        QCOMPARE(view.selectedText(), QStringLiteral("Here is a long answer that wraps"));

        // The selection follows its text into the history.
        channel.print(numberedLines(1, 10));
        QCOMPARE(view.selectedText(), QStringLiteral("Here is a long answer that wraps"));
    }

    void viewPastes()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        sizeView(view, 20, 5);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        channel.print("\x1b[?2004h" + numberedLines(1, 20));
        view.scrollBy(5);
        QApplication::clipboard()->setText(QStringLiteral("one\ntwo\nthree"));
        QTest::keyClick(&view, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(channel.written, QByteArray("\x1b[200~one\rtwo\rthree\x1b[201~"));
        QCOMPARE(view.scrollOffset(), 0);
        // Middle-click pastes the primary selection, or the clipboard where
        // the platform has none.
        channel.written.clear();
        QClipboard *clip = QApplication::clipboard();
        clip->setText(QStringLiteral("x\ny"), clip->supportsSelection() ? QClipboard::Selection : QClipboard::Clipboard);
        QTest::mouseClick(&view, Qt::MiddleButton, Qt::NoModifier, cellCenter(view, 1, 1));
        QCOMPARE(channel.written, QByteArray("\x1b[200~x\ry\x1b[201~"));
    }

    void viewPaintsSelectionAndHistory()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        sizeView(view, 40, 8);
        channel.print("\x1b[1;35mx3ro-deck\x1b[0m scrollback demo" + numberedLines(1, 40)
                      + "\r\n\xe6\x97\xa5\xe6\x9c\xac wide and a long line that wraps around the edge");
        view.scrollBy(30);
        view.setSelection(QPoint(5, view.topLine() + 2), QPoint(3, view.topLine() + 4));
        const QImage img = view.grab().toImage();
        const QString shot = qEnvironmentVariable("X3RO_DECK_TERMINAL_SHOT");
        if (!shot.isEmpty()) {
            img.save(shot);
        }
        const QSize cell = view.cellSize();
        const int pad = view.appearance().padding;
        auto at = [&](int col, int row) {
            return img.pixelColor(pad + col * cell.width() + 1, pad + row * cell.height() + 1);
        };
        QCOMPARE(at(10, 3), view.appearance().selection);
        QCOMPARE(at(4, 2), view.appearance().background);
        QCOMPARE(at(2, 4), view.appearance().selection);
        QCOMPARE(at(4, 4), view.appearance().background);
        // The position bar sits in the right padding.
        bool bar = false;
        for (int y = 0; y < img.height(); ++y) {
            bar |= img.pixelColor(img.width() - 1, y) != view.appearance().background;
        }
        QVERIFY(bar);
    }

    void ptyRunsAndResizes()
    {
        PtyChannel pty;
        QByteArray out;
        connect(&pty, &PtyChannel::received, this, [&out](const QByteArray &b) { out += b; });
        QSignalSpy closed(&pty, &PtyChannel::closed);
        QString error;
        QVERIFY2(pty.start({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("echo $TERM; stty size; read x; stty size; exit 3")},
                           QDir::tempPath(), 10, 40, {{QStringLiteral("X3RO_TEST"), QStringLiteral("1")}}, &error),
                 qPrintable(error));
        QTRY_VERIFY(out.contains("10 40"));
        QVERIFY(out.contains("xterm-256color"));
        pty.resize(30, 100);
        pty.write("\n");
        QTRY_VERIFY(out.contains("30 100"));
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(!pty.isOpen());
        QCOMPARE(pty.exitCode(), 3);
    }

    void ptyForegroundProcess()
    {
        PtyChannel pty;
        QVERIFY(pty.start({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("exec sleep 30")}, QString(), 5, 20));
        QTRY_COMPARE(pty.foregroundProcessName(), QStringLiteral("sleep"));
    }

    void ptyMissingProgram()
    {
        PtyChannel pty;
        QString error;
        QVERIFY(!pty.start({QStringLiteral("x3ro-deck-no-such-program")}, QString(), 5, 20, {}, &error));
        QVERIFY(error.contains(QStringLiteral("not found")));
    }

    void viewGridFollowsSize()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        const QSize cell = view.cellSize();
        const int pad = view.appearance().padding;
        QSignalSpy resized(&view, &TerminalView::gridResized);
        view.resize(cell.width() * 50 + 2 * pad, cell.height() * 12 + 2 * pad);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(view.cols(), 50);
        QCOMPARE(view.rows(), 12);
        QCOMPARE(channel.size, QSize(50, 12));
        QVERIFY(!resized.isEmpty());
        view.resize(cell.width() * 30 + 2 * pad, cell.height() * 20 + 2 * pad);
        QCOMPARE(channel.size, QSize(30, 20));
    }

    void viewKeys()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        view.show();
        view.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&view));
        QTest::keyClicks(&view, QStringLiteral("hi"));
        QTest::keyClick(&view, Qt::Key_Return);
        QTest::keyClick(&view, Qt::Key_Down);
        QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(&view, Qt::Key_Tab);
        QTest::keyClick(&view, Qt::Key_PageUp);
        QTest::keyClick(&view, Qt::Key_F5);
        QCOMPARE(channel.written, QByteArray("hi\r\x1b[B\x03\t\x1b[5~\x1b[15~"));
        QVERIFY(view.hasFocus()); // Tab did not move focus away
    }

    void viewReservedShortcutsPassThrough()
    {
        TerminalView view;
        const QKeySequence reserved(Qt::CTRL | Qt::Key_Q);
        view.setReservedShortcuts({reserved});
        QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_Q, Qt::ControlModifier);
        QCoreApplication::sendEvent(&view, &override);
        QVERIFY(!override.isAccepted());
        QKeyEvent other(QEvent::ShortcutOverride, Qt::Key_R, Qt::ControlModifier);
        QCoreApplication::sendEvent(&view, &other);
        QVERIFY(other.isAccepted());
    }

    // The agent terminal end to end: the view runs `tmux attach` in a PTY,
    // typing reaches the session and resizing the view resizes its window.
    void viewDrivesTmuxSession()
    {
        TmuxManager tmux;
        QVERIFY(tmux.createSession(QStringLiteral("t"), m_tmp.path(), QStringLiteral("cat")));
        TerminalView view;
        const QSize cell = view.cellSize();
        const int pad = view.appearance().padding;
        view.resize(cell.width() * 60 + 2 * pad, cell.height() * 15 + 2 * pad);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        PtyChannel pty;
        QString error;
        QVERIFY2(pty.start(tmux.attachCommand(QStringLiteral("t")), QString(), view.rows(), view.cols(), {}, &error), qPrintable(error));
        view.setChannel(&pty);

        const QStringList attach = tmux.attachCommand(QStringLiteral("t"));
        const QStringList base = attach.mid(1, attach.indexOf(QStringLiteral("attach-session")) - 1);
        auto windowWidth = [&base] {
            QProcess p;
            p.start(QStringLiteral("tmux"),
                    base
                        + QStringList{QStringLiteral("display"), QStringLiteral("-p"), QStringLiteral("-t"), QStringLiteral("=t:"),
                                      QStringLiteral("#{window_width}")});
            p.waitForFinished();
            return QString::fromUtf8(p.readAllStandardOutput()).trimmed().toInt();
        };
        QTRY_COMPARE(windowWidth(), 60);

        auto rowsEqual = [&view](const QString &text) {
            int n = 0;
            for (int r = 0; r < view.rows(); ++r) {
                n += view.screen()->rowText(r) == text;
            }
            return n;
        };
        QTest::keyClicks(&view, QStringLiteral("hello"));
        QTest::keyClick(&view, Qt::Key_Return);
        // The terminal's echo, then cat's copy.
        QTRY_COMPARE(rowsEqual(QStringLiteral("hello")), 2);

        view.resize(cell.width() * 42 + 2 * pad, cell.height() * 15 + 2 * pad);
        QTRY_COMPARE(windowWidth(), 42);
        tmux.killSession(QStringLiteral("t"));
    }

    void viewPaints()
    {
        TerminalView view;
        FakeChannel channel;
        view.setChannel(&channel);
        const QSize cell = view.cellSize();
        const int pad = view.appearance().padding;
        view.resize(cell.width() * 20 + 2 * pad, cell.height() * 4 + 2 * pad);
        channel.print("\x1b[2;1H\x1b[44m     \x1b[0m\x1b[?25l");
        const QImage img = view.grab().toImage();
        const QColor bg = view.appearance().background;
        const QColor blue = view.appearance().palette[4];
        auto at = [&](int col, int row) {
            return img.pixelColor(pad + col * cell.width() + cell.width() / 2, pad + row * cell.height() + 1);
        };
        QCOMPARE(at(10, 0), bg);
        QCOMPARE(at(2, 1), blue);
        QCOMPARE(at(10, 1), bg);
    }

private:
    QTemporaryDir m_tmp{QStringLiteral("/tmp/xdt-XXXXXX")};
};

QTEST_MAIN(TestTerminal)
#include "tst_terminal.moc"
