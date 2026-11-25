// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// x3ro-deck-sessiond end to end: the wire protocol, DaemonBackend on top of
// it, and the attach client in a real PTY. Every daemon runs on a socket in a
// private temporary directory.

#include "DaemonBackend.h"
#include "ProcessInfo.h"
#include "SessionScreen.h"
#include "SessiondChannel.h"
#include "SessiondProtocol.h"
#include "TerminalScreen.h"

#include <QFile>
#include <QJsonArray>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <csignal>
#include <memory>

#include <poll.h>

#if defined(Q_OS_MACOS)
#include <util.h>
#else
#include <pty.h>
#endif
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
// A protocol client that collects the output of the sessions it attached to.
class Client
{
public:
    bool connectTo(const QString &path)
    {
        sock.connectToServer(path);
        return sock.waitForConnected(2000);
    }

    QJsonObject request(QJsonObject req)
    {
        const int id = nextId++;
        req.insert(QStringLiteral("id"), id);
        sock.write(Sessiond::encodeControl(req));
        QDeadlineTimer deadline(3000);
        while (!deadline.hasExpired()) {
            pump(50);
            for (qsizetype i = 0; i < controls.size(); ++i) {
                if (controls.at(i).value(QStringLiteral("re")).toInt() == id) {
                    return controls.takeAt(i);
                }
            }
        }
        return {};
    }

    void pump(int ms)
    {
        sock.flush();
        if (sock.bytesAvailable() == 0) {
            sock.waitForReadyRead(ms);
        }
        buffer.append(sock.readAll());
        Sessiond::Frame f;
        while (Sessiond::takeFrame(buffer, f)) {
            if (f.type == Sessiond::Control) {
                controls.append(Sessiond::parseControl(f.payload));
            } else if (f.type == Sessiond::Output) {
                quint32 id = 0;
                QByteArray bytes;
                if (Sessiond::splitData(f.payload, id, bytes)) {
                    output[id].append(bytes);
                }
            }
        }
    }

    // Pumps until `session` printed `text`.
    bool waitFor(quint32 session, const QByteArray &text, int ms = 5000)
    {
        QDeadlineTimer deadline(ms);
        while (!output.value(session).contains(text)) {
            if (deadline.hasExpired()) {
                return false;
            }
            pump(50);
        }
        return true;
    }

    QJsonObject event(const QString &name, int ms = 5000)
    {
        QDeadlineTimer deadline(ms);
        while (!deadline.hasExpired()) {
            for (qsizetype i = 0; i < controls.size(); ++i) {
                if (controls.at(i).value(QStringLiteral("event")).toString() == name) {
                    return controls.takeAt(i);
                }
            }
            pump(50);
        }
        return {};
    }

    QLocalSocket sock;
    QByteArray buffer;
    QList<QJsonObject> controls;
    QHash<quint32, QByteArray> output;
    int nextId = 1;
};

QJsonObject op(const char *name, QJsonObject fields = {})
{
    fields.insert(QStringLiteral("op"), QLatin1String(name));
    return fields;
}

QJsonObject sessionNamed(const QJsonObject &list, const QString &name)
{
    for (const QJsonValue &v : list.value(QStringLiteral("sessions")).toArray()) {
        if (v.toObject().value(QStringLiteral("name")).toString() == name) {
            return v.toObject();
        }
    }
    return {};
}

// The first difference between two screens, or an empty string.
QString screenDiff(const TerminalScreen &a, const TerminalScreen &b)
{
    if (a.rows() != b.rows() || a.cols() != b.cols()) {
        return QStringLiteral("size %1x%2 vs %3x%4").arg(a.cols()).arg(a.rows()).arg(b.cols()).arg(b.rows());
    }
    for (int row = 0; row < a.rows(); ++row) {
        for (int col = 0; col < a.cols(); ++col) {
            const TerminalScreen::Cell x = a.cell(row, col);
            const TerminalScreen::Cell y = b.cell(row, col);
            if (x.text != y.text || x.width != y.width || x.fg != y.fg || x.bg != y.bg || x.bold != y.bold || x.italic != y.italic
                || x.underline != y.underline || x.strike != y.strike) {
                return QStringLiteral("cell %1,%2: '%3' %4/%5 b%6 i%7 u%8 s%9 vs '%10' %11/%12 b%13 i%14 u%15 s%16")
                    .arg(row)
                    .arg(col)
                    .arg(x.text, x.fg.name(), x.bg.name())
                    .arg(x.bold)
                    .arg(x.italic)
                    .arg(x.underline)
                    .arg(x.strike)
                    .arg(y.text, y.fg.name(), y.bg.name())
                    .arg(y.bold)
                    .arg(y.italic)
                    .arg(y.underline)
                    .arg(y.strike);
            }
        }
    }
    if (a.cursor() != b.cursor()) {
        return QStringLiteral("cursor %1,%2 vs %3,%4").arg(a.cursor().y()).arg(a.cursor().x()).arg(b.cursor().y()).arg(b.cursor().x());
    }
    if (a.cursorVisible() != b.cursorVisible() || a.altScreen() != b.altScreen() || a.title() != b.title()) {
        return QStringLiteral("cursor visibility, alternate screen or title");
    }
    return {};
}

// Feeds `bytes` to a terminal directly and, through SessionScreen's redraw,
// to a fresh one; the two must look the same.
struct RoundTrip
{
    RoundTrip(int rows, int cols)
        : direct(rows, cols)
        , redrawn(rows, cols)
        , model(rows, cols)
    {
    }
    void feed(const QByteArray &bytes)
    {
        direct.feed(bytes);
        model.feed(bytes);
    }
    QString check()
    {
        redrawn.reset();
        redrawn.feed(model.redraw());
        return screenDiff(direct, redrawn);
    }
    // Live output after the redraw.
    void follow(const QByteArray &bytes)
    {
        direct.feed(bytes);
        redrawn.feed(bytes);
        model.feed(bytes);
    }

    TerminalScreen direct;
    TerminalScreen redrawn;
    SessionScreen model;
};

QString screenText(const TerminalScreen &screen)
{
    QStringList rows;
    for (int row = 0; row < screen.rows(); ++row) {
        rows.append(screen.rowText(row));
    }
    return rows.join(QLatin1Char('\n'));
}

// A SessiondChannel feeding a terminal screen, as TerminalView does.
struct Viewer
{
    Viewer(int rows, int cols)
        : channel(std::make_unique<SessiondChannel>())
        , screen(rows, cols)
        , closed(channel.get(), &TerminalChannel::closed)
    {
        QObject::connect(channel.get(), &TerminalChannel::received, &screen, [this](const QByteArray &bytes) {
            raw.append(bytes);
            screen.feed(bytes);
        });
    }
    bool shows(const QString &text) const { return screenText(screen).contains(text); }

    std::unique_ptr<SessiondChannel> channel;
    TerminalScreen screen;
    QSignalSpy closed;
    QByteArray raw;
};

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}
} // namespace

class SessiondTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void keyBytes();
    void commandName();
    void screenRedraw();
    void screenModes();
    void screenAltScreen();
    void screenResize();
    void protocol();
    void backend();
    void foregroundJob();
    void attachClient();
    void directChannel();
    void directChannelLosesDaemon();

private:
    // Runs the daemon in the foreground on the test socket.
    bool startForeground();
    QString shellCommandSession(Client &c, const QString &name, quint32 *id);

    QTemporaryDir m_tmp{QStringLiteral("/tmp/xds-XXXXXX")};
    QString m_socket;
    QProcess m_daemon;
};

void SessiondTest::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    QFile::setPermissions(m_tmp.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qunsetenv("X3RO_DECK_SESSIOND_SOCKET");
    qunsetenv("ENV");
    qputenv("XDG_RUNTIME_DIR", m_tmp.path().toLocal8Bit());
    qputenv("TMPDIR", m_tmp.path().toLocal8Bit()); // where macOS puts the socket
    // A predictable shell without the user's rc files.
    qputenv("HOME", m_tmp.path().toLocal8Bit());
    qputenv("SHELL", "/bin/sh");
    m_socket = m_tmp.path() + QStringLiteral("/x3ro-deck-sessiond.sock");
    QCOMPARE(Sessiond::socketPath(), m_socket);
    QVERIFY(QFileInfo(DaemonBackend::daemonProgram()).isExecutable());
}

void SessiondTest::cleanup()
{
    Client c;
    if (c.connectTo(m_socket)) {
        c.request(op("shutdown"));
    }
    if (m_daemon.state() != QProcess::NotRunning) {
        QVERIFY(m_daemon.waitForFinished(6000));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(m_socket), 6000);
}

bool SessiondTest::startForeground()
{
    m_daemon.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_daemon.start(DaemonBackend::daemonProgram(), {});
    QDeadlineTimer deadline(5000);
    while (!deadline.hasExpired()) {
        QLocalSocket probe;
        probe.connectToServer(m_socket);
        if (probe.waitForConnected(200)) {
            return true;
        }
        QTest::qWait(50);
    }
    return false;
}

QString SessiondTest::shellCommandSession(Client &c, const QString &name, quint32 *id)
{
    const QJsonObject created = c.request(op("create",
                                             {{QStringLiteral("name"), name},
                                              {QStringLiteral("cwd"), m_tmp.path()},
                                              {QStringLiteral("command"), QJsonArray{QStringLiteral("/bin/sh")}},
                                              {QStringLiteral("env"), QJsonObject{{QStringLiteral("X3RO_T"), QStringLiteral("v1")}}},
                                              {QStringLiteral("cols"), 100},
                                              {QStringLiteral("rows"), 30}}));
    *id = quint32(created.value(QStringLiteral("session")).toInt());
    return created.value(QStringLiteral("ok")).toBool() ? QString() : created.value(QStringLiteral("error")).toString(QStringLiteral("?"));
}

void SessiondTest::keyBytes()
{
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("Enter")), QByteArray("\r"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("Escape")), QByteArray("\x1b"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("C-c")), QByteArray("\x03"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("C-d")), QByteArray("\x04"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("Up")), QByteArray("\x1b[A"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("M-x")), QByteArray("\x1bx"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("1")), QByteArray("1"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("y")), QByteArray("y"));
    QCOMPARE(DaemonBackend::keyBytes(QStringLiteral("hello")), QByteArray("hello"));
}

void SessiondTest::commandName()
{
    using ProcessInfo::commandName;
    QCOMPARE(commandName({QStringLiteral("/usr/bin/sleep"), QStringLiteral("30")}, QStringLiteral("sleep")), QStringLiteral("sleep"));
    // A login shell, and a title a program set over its argv.
    QCOMPARE(commandName({QStringLiteral("-zsh")}, QStringLiteral("zsh")), QStringLiteral("zsh"));
    QCOMPARE(commandName({QStringLiteral("claude --resume")}, QStringLiteral("node")), QStringLiteral("claude"));
    QCOMPARE(commandName({QStringLiteral("exec /opt/x/codex")}, QString()), QStringLiteral("codex"));
    QCOMPARE(commandName({QStringLiteral("\"/opt/x/agent\" --flag")}, QString()), QStringLiteral("agent"));
    QCOMPARE(commandName({QStringLiteral("./run.sh")}, QString()), QStringLiteral("run.sh"));
    // Zombies and kernel threads have no argv.
    QCOMPARE(commandName({}, QStringLiteral("kworker/0:1")), QStringLiteral("kworker/0:1"));
    QCOMPARE(commandName({}, QStringLiteral("defunct")), QStringLiteral("defunct"));
    QCOMPARE(commandName({}, QString()), QString());
}

void SessiondTest::screenRedraw()
{
    RoundTrip t(8, 20);
    t.feed("plain\r\n\x1b[1;31mbold red\x1b[0m \x1b[3;4mul\x1b[0m \x1b[38;5;200mx\x1b[48;2;10;20;30mrgb\x1b[0m\r\n");
    t.feed("\x1b[7mrev\x1b[27m \x1b[9mstrike\x1b[0m \x1b[21mdbl\x1b[24m \x1b[44m\x1b[K\x1b[0m\r\n");
    t.feed("\xe6\x97\xa5\xe6\x9c\xac e\xcc\x81 \x1b[93;100mbright\x1b[0m\r\n");
    t.feed("a line long enough to wrap twice on a narrow screen\r\n");
    t.feed("\x1b]2;my title\x07\x1b[3;7H\x1b[32;1m");
    QCOMPARE(t.check(), QString());
    QCOMPARE(t.model.rowText(0), QStringLiteral("plain"));
    QCOMPARE(t.model.cursorRow(), 2);
    QCOMPARE(t.model.cursorCol(), 6);
    QCOMPARE(t.model.title(), QStringLiteral("my title"));

    // The pen and the cursor carry on where the program left them.
    t.follow("Xy");
    QCOMPARE(screenDiff(t.direct, t.redrawn), QString());

    // Soft wraps survive: widening rejoins the wrapped line on both.
    t.direct.resize(8, 60);
    t.redrawn.resize(8, 60);
    QCOMPARE(screenDiff(t.direct, t.redrawn), QString());
    QVERIFY(t.redrawn.rowText(4).startsWith(QStringLiteral("a line long enough to wrap twice")));

    // Text that fills the last row's last cell doesn't scroll the redraw.
    RoundTrip full(3, 5);
    full.feed("abcde\r\nfghij\r\nklmno");
    QCOMPARE(full.check(), QString());
}

void SessiondTest::screenModes()
{
    RoundTrip t(10, 40);
    const QByteArray modes = "\x1b[?1h\x1b[?2004h\x1b[?1000;1006h\x1b=\x1b[>1u\x1b[5 q\x1b[>4;2m\x1b]0;x[?2004l\x07\x1bP+q[?1l\x1b\\ok";
    // One byte at a time: sequences split across reads still count.
    for (char c : modes) {
        t.feed(QByteArray(1, c));
    }
    QCOMPARE(t.check(), QString());
    const QByteArray redraw = t.model.redraw();
    for (const char *expected :
         {"\x1b[?1h", "\x1b[?2004h", "\x1b[?1000h", "\x1b[?1006h", "\x1b=", "\x1b[=1;1u", "\x1b[5 q", "\x1b[>4;2m"}) {
        QVERIFY2(redraw.contains(expected), expected + 1);
    }

    // A terminal fed the redraw sends what the program asked for.
    QByteArray sent;
    QObject::connect(&t.redrawn, &TerminalScreen::output, &t.redrawn, [&sent](const QByteArray &b) { sent += b; });
    t.redrawn.sendKey(TerminalScreen::Up);
    QCOMPARE(sent, QByteArray("\x1bOA"));
    sent.clear();
    t.redrawn.paste(QStringLiteral("p"));
    QCOMPARE(sent, QByteArray("\x1b[200~p\x1b[201~"));

    // Popping and resetting turn them off again.
    t.feed("\x1b[<u\x1b[?2004l");
    QVERIFY(!t.model.redraw().contains("=1;1u"));
    QVERIFY(t.model.redraw().contains("\x1b[?2004l"));
    t.feed("\x1b"
           "c");
    const QByteArray reset = t.model.redraw();
    QVERIFY(reset.contains("\x1b[?1l"));
    QVERIFY(reset.contains("\x1b>"));
    QVERIFY(!reset.contains("5 q"));
    QVERIFY(!reset.contains(">4;2m"));

    // A scroll region and origin mode: the cursor lands where it was.
    RoundTrip region(10, 20);
    region.feed("top\x1b[3;8r\x1b[?6h\x1b[2;4Hin");
    QCOMPARE(region.check(), QString());
    region.follow("\r\n\r\n\r\n\r\n\r\n\r\n\r\nscrolled");
    QCOMPARE(screenDiff(region.direct, region.redrawn), QString());
}

void SessiondTest::screenAltScreen()
{
    RoundTrip t(6, 30);
    t.feed("shell prompt $ \x1b[?1049h\x1b[H\x1b[2J\x1b[44m\x1b[2Jfull-screen app\x1b[?25l\x1b[4;3H");
    QCOMPARE(t.check(), QString());
    QVERIFY(t.model.altScreen());
    QVERIFY(!t.model.cursorVisible());
    QVERIFY(t.redrawn.altScreen());
    QVERIFY(t.model.redraw().contains("\x1b[?1049h"));
    t.feed("\x1b[?1049l\x1b[?25h");
    QVERIFY(!t.model.altScreen());
    QCOMPARE(t.model.rowText(0), QStringLiteral("shell prompt $"));
    QCOMPARE(t.check(), QString());
}

void SessiondTest::screenResize()
{
    // Output at one size, a client at another: the redraw is already rewrapped.
    SessionScreen model(10, 20);
    TerminalScreen direct(10, 20);
    const QByteArray out = "$ echo one two three four five six\r\none two three four five six\r\n$ ";
    model.feed(out);
    direct.feed(out);
    QCOMPARE(model.rowText(0), QStringLiteral("$ echo one two three"));
    QCOMPARE(model.rowText(1), QStringLiteral(" four five six"));

    model.resize(10, 40);
    direct.resize(10, 40);
    QCOMPARE(model.rowText(0), QStringLiteral("$ echo one two three four five six"));
    TerminalScreen attached(10, 40);
    attached.feed(model.redraw());
    QCOMPARE(screenDiff(direct, attached), QString());

    // Narrower than the text: it wraps again, and the cursor follows it.
    model.resize(10, 12);
    direct.resize(10, 12);
    TerminalScreen narrow(10, 12);
    narrow.feed(model.redraw());
    QCOMPARE(screenDiff(direct, narrow), QString());
    QCOMPARE(narrow.rowText(narrow.cursor().y()), QStringLiteral("$"));
}

void SessiondTest::protocol()
{
    QVERIFY(startForeground());
    Client c;
    QVERIFY(c.connectTo(m_socket));
    const QJsonObject hello = c.request(op("hello", {{QStringLiteral("version"), Sessiond::kProtocolVersion}}));
    QCOMPARE(hello.value(QStringLiteral("version")).toInt(), Sessiond::kProtocolVersion);
    QVERIFY(!c.request(op("frobnicate")).value(QStringLiteral("ok")).toBool());

    quint32 id = 0;
    QCOMPARE(shellCommandSession(c, QStringLiteral("p"), &id), QString());
    QVERIFY(id > 0);
    // Names are unique.
    quint32 dup = 0;
    QVERIFY(!shellCommandSession(c, QStringLiteral("p"), &dup).isEmpty());

    QVERIFY(c.request(op("attach", {{QStringLiteral("name"), QStringLiteral("p")}})).value(QStringLiteral("ok")).toBool());
    QVERIFY(c.request(op("write",
                         {{QStringLiteral("name"), QStringLiteral("p")},
                          {QStringLiteral("data"), QStringLiteral("echo \"<$X3RO_T>\"; pwd; stty size\r")}}))
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY2(c.waitFor(id, "<v1>"), c.output.value(id).constData());
    QVERIFY2(c.waitFor(id, m_tmp.path().toUtf8()), c.output.value(id).constData());
    QVERIFY2(c.waitFor(id, "30 100"), c.output.value(id).constData());

    // Resize, then input as an Input frame.
    QVERIFY(c.request(op("resize", {{QStringLiteral("session"), qint64(id)}, {QStringLiteral("cols"), 120}, {QStringLiteral("rows"), 40}}))
                .value(QStringLiteral("ok"))
                .toBool());
    c.sock.write(Sessiond::encodeData(Sessiond::Input, id, "stty size; cat\r"));
    QVERIFY2(c.waitFor(id, "40 120"), c.output.value(id).constData());

    // The list reports the session, its env and its foreground process.
    QTRY_COMPARE_WITH_TIMEOUT(sessionNamed(c.request(op("list")), QStringLiteral("p")).value(QStringLiteral("foreground")).toString(),
                              QStringLiteral("cat"), 5000);
    QJsonObject listed = sessionNamed(c.request(op("list")), QStringLiteral("p"));
    QCOMPARE(listed.value(QStringLiteral("cwd")).toString(), m_tmp.path());
    QCOMPARE(listed.value(QStringLiteral("env")).toObject().value(QStringLiteral("X3RO_T")).toString(), QStringLiteral("v1"));
    QCOMPARE(listed.value(QStringLiteral("cols")).toInt(), 120);

    // Renamed, the attached client keeps receiving output.
    QVERIFY(c.request(op("rename", {{QStringLiteral("name"), QStringLiteral("p")}, {QStringLiteral("to"), QStringLiteral("q")}}))
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(sessionNamed(c.request(op("list")), QStringLiteral("p")).isEmpty());
    c.request(op("write", {{QStringLiteral("name"), QStringLiteral("q")}, {QStringLiteral("data"), QStringLiteral("renamed\r")}}));
    // The terminal's echo and then cat's copy: only once both arrived is the
    // session quiet, so nothing reaches the late client below as live output.
    QVERIFY2(c.waitFor(id, "renamed\r\nrenamed\r\n"), c.output.value(id).constData());

    // A second client attaching at another size gets the screen redrawn at
    // that size, not the raw output written at the old one.
    Client late;
    QVERIFY(late.connectTo(m_socket));
    const QJsonObject attached = late.request(
        op("attach", {{QStringLiteral("name"), QStringLiteral("q")}, {QStringLiteral("cols"), 50}, {QStringLiteral("rows"), 12}}));
    QVERIFY(attached.value(QStringLiteral("ok")).toBool());
    QCOMPARE(attached.value(QStringLiteral("cols")).toInt(), 50);
    QVERIFY(late.waitFor(id, "renamed"));
    QVERIFY2(late.output.value(id).startsWith("\x1b[0m\x1b[?1047l"), late.output.value(id).left(40).constData());
    QVERIFY(!late.output.value(id).contains("\r\n")); // placed rows, not replayed lines
    TerminalScreen lateView(12, 50);
    lateView.feed(late.output.value(id));
    const QJsonObject screen = c.request(op("screen", {{QStringLiteral("name"), QStringLiteral("q")}}));
    QCOMPARE(screen.value(QStringLiteral("cols")).toInt(), 50);
    QCOMPARE(screen.value(QStringLiteral("rows")).toInt(), 12);
    const QJsonArray lines = screen.value(QStringLiteral("lines")).toArray();
    QCOMPARE(lines.size(), 12);
    for (int row = 0; row < 12; ++row) {
        QCOMPARE(lateView.rowText(row), lines.at(row).toString());
    }
    QVERIFY(lines.contains(QStringLiteral("renamed")));
    QCOMPARE(lateView.cursor(),
             QPoint(screen.value(QStringLiteral("cursor"))[QStringLiteral("col")].toInt(),
                    screen.value(QStringLiteral("cursor"))[QStringLiteral("row")].toInt()));
    QVERIFY(!c.request(op("screen", {{QStringLiteral("name"), QStringLiteral("nope")}})).value(QStringLiteral("ok")).toBool());

    // Kill ends the session and tells the attached clients.
    QVERIFY(c.request(op("kill", {{QStringLiteral("name"), QStringLiteral("q")}})).value(QStringLiteral("ok")).toBool());
    QCOMPARE(c.event(QStringLiteral("exited")).value(QStringLiteral("session")).toInt(), int(id));
    QVERIFY(sessionNamed(c.request(op("list")), QStringLiteral("q")).isEmpty());

    // A session whose shell exits ends by itself, with its exit status.
    quint32 second = 0;
    QCOMPARE(shellCommandSession(c, QStringLiteral("r"), &second), QString());
    c.request(op("attach", {{QStringLiteral("session"), qint64(second)}}));
    c.request(op("write",
                 {{QStringLiteral("name"), QStringLiteral("r")}, {QStringLiteral("data"), QStringLiteral("echo last words; exit 3\r")}}));
    const QJsonObject exited = c.event(QStringLiteral("exited"));
    QCOMPARE(exited.value(QStringLiteral("name")).toString(), QStringLiteral("r"));
    QCOMPARE(exited.value(QStringLiteral("status")).toInt(), 3);
    QVERIFY(c.output.value(second).contains("last words"));

    // Only one daemon per socket.
    QProcess second_daemon;
    second_daemon.start(DaemonBackend::daemonProgram(), {QStringLiteral("--detach")});
    QVERIFY(second_daemon.waitForFinished(5000));
    QCOMPARE(second_daemon.exitCode(), 0);
    QVERIFY(c.request(op("hello")).value(QStringLiteral("pid")).toInteger() == m_daemon.processId());
}

void SessiondTest::backend()
{
    const QString dir = m_tmp.path() + QStringLiteral("/repo");
    QVERIFY(QDir().mkpath(dir));
    const QString log = m_tmp.path() + QStringLiteral("/keys.log");
    DaemonBackend backend;

    // Asking about sessions never starts the daemon.
    QVERIFY(!backend.hasSession(QStringLiteral("b")));
    QVERIFY(backend.sessionsIn(dir).isEmpty());
    QVERIFY(!QFile::exists(m_socket));

    // Creating one does, in the background.
    const SessionBackend::Environment env{{QStringLiteral("X3RO_T"), QStringLiteral("e1")}};
    // -u: BSD cat buffers its output to a file otherwise.
    QVERIFY(backend.createSession(QStringLiteral("b"), dir, QStringLiteral("cat -uv >> ") + log, env));
    QVERIFY(QFile::exists(m_socket));
    QVERIFY(backend.hasSession(QStringLiteral("b")));
    QCOMPARE(backend.sessionEnvironment(QStringLiteral("b")), env);
    QCOMPARE(backend.sessionsIn(dir), QStringList{QStringLiteral("b")});

    QSignalSpy spy(&backend, &SessionBackend::foregroundProcesses);
    backend.startPolling(200);
    using Commands = QHash<QString, QStringList>;
    auto polledCommand = [&spy] {
        return spy.isEmpty() ? QStringList() : spy.last().at(0).value<Commands>().value(QStringLiteral("b"));
    };
    QTRY_COMPARE_WITH_TIMEOUT(polledCommand(), QStringList{QStringLiteral("cat")}, 5000);
    // Permission keys and prompt buttons.
    backend.sendKeys(QStringLiteral("b"), {QStringLiteral("1")});
    backend.sendKeys(QStringLiteral("b"), {QStringLiteral("Escape"), QStringLiteral("Enter")});
    QTRY_COMPARE_WITH_TIMEOUT(readFile(log), QStringLiteral("1^[\n"), 5000);
    backend.submit(QStringLiteral("b"), QStringLiteral("hello"));
    QTRY_COMPARE_WITH_TIMEOUT(readFile(log), QStringLiteral("1^[\nhello\n"), 5000);
    backend.submit(QStringLiteral("b"), QStringLiteral("later"), 300);
    QTest::qWait(100);
    QCOMPARE(readFile(log), QStringLiteral("1^[\nhello\n"));
    QTRY_COMPARE_WITH_TIMEOUT(readFile(log), QStringLiteral("1^[\nhello\nlater\n"), 5000);

    QVERIFY(backend.renameSession(QStringLiteral("b"), QStringLiteral("c")));
    QVERIFY(!backend.hasSession(QStringLiteral("b")));
    QVERIFY(backend.hasSession(QStringLiteral("c")));
    QCOMPARE(backend.attachCommand(QStringLiteral("c")).last(), QStringLiteral("c"));
    QCOMPARE(backend.attachHint(QStringLiteral("c")), QStringLiteral("x3ro-deck attach c"));

    backend.killSession(QStringLiteral("c"));
    QVERIFY(!backend.hasSession(QStringLiteral("c")));

    // Another backend (the next app start) finds the daemon's sessions.
    QVERIFY(backend.createSession(QStringLiteral("d"), dir, QString()));
    {
        DaemonBackend next;
        QVERIFY(next.hasSession(QStringLiteral("d")));
    }

    // "Stop agents and quit" ends every session and the daemon.
    backend.killAll();
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(m_socket), 3000);
    QVERIFY(!backend.hasSession(QStringLiteral("d")));
}

void SessiondTest::foregroundJob()
{
    QVERIFY(startForeground());
    Client c;
    QVERIFY(c.connectTo(m_socket));
    quint32 id = 0;
    QCOMPARE(shellCommandSession(c, QStringLiteral("fg"), &id), QString());
    auto job = [&c] {
        const QJsonObject s = sessionNamed(c.request(op("list", {{QStringLiteral("processes"), true}})), QStringLiteral("fg"));
        // Merged as DaemonBackend does, so the moment between a job ending
        // and the shell taking the terminal back looks like the idle shell.
        QStringList names{s.value(QStringLiteral("foreground")).toString()};
        for (const QJsonValue &v : s.value(QStringLiteral("foregroundGroup")).toArray()) {
            if (!names.contains(v.toString())) {
                names.append(v.toString());
            }
        }
        return names;
    };
    auto type = [&c](const QString &text) {
        c.request(op("write", {{QStringLiteral("name"), QStringLiteral("fg")}, {QStringLiteral("data"), text}}));
    };

    // The idle shell; a plain list leaves the job out.
    QTRY_COMPARE_WITH_TIMEOUT(job(), QStringList{QStringLiteral("sh")}, 5000);
    QVERIFY(!sessionNamed(c.request(op("list")), QStringLiteral("fg")).contains(QStringLiteral("foregroundGroup")));

    // A program in the foreground, then the shell again once it ends.
    type(QStringLiteral("sleep 30\r"));
    QTRY_COMPARE_WITH_TIMEOUT(job(), QStringList{QStringLiteral("sleep")}, 5000);

    // The tree has the shell and the program, which leads the foreground group.
    const QJsonObject tree = c.request(op("processes", {{QStringLiteral("name"), QStringLiteral("fg")}}));
    QVERIFY(tree.value(QStringLiteral("ok")).toBool());
    const QJsonArray procs = tree.value(QStringLiteral("processes")).toArray();
    QCOMPARE(procs.size(), 2);
    const qint64 shell = sessionNamed(c.request(op("list")), QStringLiteral("fg")).value(QStringLiteral("pid")).toInteger();
    QCOMPARE(procs.at(0)[QStringLiteral("pid")].toInteger(), shell);
    QCOMPARE(procs.at(1)[QStringLiteral("ppid")].toInteger(), shell);
    QCOMPARE(procs.at(1)[QStringLiteral("name")].toString(), QStringLiteral("sleep"));
    QCOMPARE(procs.at(1)[QStringLiteral("args")].toArray().last().toString(), QStringLiteral("30"));
    QCOMPARE(procs.at(1)[QStringLiteral("pgid")].toInteger(), tree.value(QStringLiteral("foreground")).toInteger());
    QCOMPARE(procs.at(1)[QStringLiteral("pid")].toInteger(), tree.value(QStringLiteral("foreground")).toInteger());
    QVERIFY(!c.request(op("processes", {{QStringLiteral("name"), QStringLiteral("nope")}})).value(QStringLiteral("ok")).toBool());

    type(QStringLiteral("\x03"));
    QTRY_COMPARE_WITH_TIMEOUT(job(), QStringList{QStringLiteral("sh")}, 5000);

    // An agent behind a wrapper: the leader is the wrapper, and the agent
    // follows it in the group.
    const QString bin = m_tmp.path() + QStringLiteral("/bin");
    QVERIFY(QDir().mkpath(bin));
    QVERIFY(QFile::link(QStandardPaths::findExecutable(QStringLiteral("sleep")), bin + QStringLiteral("/fakeagent")));
    type(QStringLiteral("sh -c '%1/fakeagent 30; true'\r").arg(bin));
    QTRY_COMPARE_WITH_TIMEOUT(job(), (QStringList{QStringLiteral("sh"), QStringLiteral("fakeagent")}), 5000);
    type(QStringLiteral("\x03"));
    QTRY_COMPARE_WITH_TIMEOUT(job(), QStringList{QStringLiteral("sh")}, 5000);

    // DaemonBackend's poll reports the whole job, the leader first.
    DaemonBackend backend;
    QSignalSpy spy(&backend, &SessionBackend::foregroundProcesses);
    backend.startPolling(200);
    type(QStringLiteral("sh -c '%1/fakeagent 30; true'\r").arg(bin));
    using Commands = QHash<QString, QStringList>;
    QTRY_COMPARE_WITH_TIMEOUT(spy.isEmpty() ? QStringList() : spy.last().at(0).value<Commands>().value(QStringLiteral("fg")),
                              (QStringList{QStringLiteral("sh"), QStringLiteral("fakeagent")}), 5000);
}

void SessiondTest::attachClient()
{
    QVERIFY(startForeground());
    Client c;
    QVERIFY(c.connectTo(m_socket));
    quint32 id = 0;
    QCOMPARE(shellCommandSession(c, QStringLiteral("my_checkout"), &id), QString());
    c.request(
        op("write",
           {{QStringLiteral("name"), QStringLiteral("my_checkout")}, {QStringLiteral("data"), QStringLiteral("echo before-attach\r")}}));
    QTest::qWait(300);

    // No session of that name: an error, not a hang.
    QProcess missing;
    missing.start(QStringLiteral(X3RO_DECK_ATTACH_PATH), {QStringLiteral("nope")});
    QVERIFY(missing.waitForFinished(5000));
    QCOMPARE(missing.exitCode(), 1);

    // Runs the attach client in a PTY of its own, like a terminal would.
    auto attach = [](const QString &checkout, int *master) {
        struct winsize ws{};
        ws.ws_col = 90;
        ws.ws_row = 20;
        const QByteArray program(X3RO_DECK_ATTACH_PATH);
        const QByteArray arg = checkout.toUtf8();
        const pid_t pid = forkpty(master, nullptr, nullptr, &ws);
        if (pid == 0) {
            execl(program.constData(), program.constData(), arg.constData(), static_cast<char *>(nullptr));
            _exit(127);
        }
        return pid;
    };
    auto readUntil = [](int fd, QByteArray &seen, const QByteArray &text) {
        QDeadlineTimer deadline(5000);
        while (!seen.contains(text) && !deadline.hasExpired()) {
            pollfd p{fd, POLLIN, 0};
            if (poll(&p, 1, 100) > 0) {
                char buf[4096];
                const ssize_t n = ::read(fd, buf, sizeof buf);
                if (n <= 0) {
                    break;
                }
                seen.append(buf, n);
            }
        }
        return seen.contains(text);
    };
    auto exitCode = [](pid_t pid) {
        int status = -1;
        QDeadlineTimer deadline(5000);
        while (waitpid(pid, &status, WNOHANG) == 0 && !deadline.hasExpired()) {
            QTest::qWait(20);
        }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    };

    // The checkout name maps to its session name, as in AppController.
    int master = -1;
    pid_t pid = attach(QStringLiteral("my.checkout"), &master);
    QVERIFY(pid > 0);
    QByteArray seen;
    QVERIFY2(readUntil(master, seen, "before-attach"), seen.constData());
    QTRY_COMPARE_WITH_TIMEOUT(sessionNamed(c.request(op("list")), QStringLiteral("my_checkout")).value(QStringLiteral("cols")).toInt(), 90,
                              5000);

    // Typing reaches the session.
    QCOMPARE(::write(master, "echo typed-$((40+2))\r", 21), 21);
    QVERIFY2(readUntil(master, seen, "typed-42"), seen.constData());

    // The terminal's size follows the PTY.
    struct winsize ws{};
    ws.ws_col = 70;
    ws.ws_row = 25;
    ioctl(master, TIOCSWINSZ, &ws);
    QTRY_COMPARE_WITH_TIMEOUT(sessionNamed(c.request(op("list")), QStringLiteral("my_checkout")).value(QStringLiteral("cols")).toInt(), 70,
                              5000);

    // Ctrl+\ detaches and leaves the session running.
    QCOMPARE(::write(master, "\x1c", 1), 1);
    QVERIFY2(readUntil(master, seen, "[detached from my_checkout]"), seen.constData());
    QCOMPARE(exitCode(pid), 0);
    ::close(master);
    QVERIFY(!sessionNamed(c.request(op("list")), QStringLiteral("my_checkout")).isEmpty());

    // A full-screen program, resized while nobody is attached: the next
    // client gets its alternate screen and modes at the client's size.
    c.request(op("write",
                 {{QStringLiteral("name"), QStringLiteral("my_checkout")},
                  {QStringLiteral("data"),
                   QStringLiteral("printf '\\033[?1049h\\033[?1h\\033[H\\033[2J\\033[1;33mfull-%s\\033[0m' screen; cat\r")}}));
    QTRY_VERIFY_WITH_TIMEOUT(
        c.request(op("screen", {{QStringLiteral("name"), QStringLiteral("my_checkout")}})).value(QStringLiteral("altScreen")).toBool(),
        5000);
    c.request(op("resize",
                 {{QStringLiteral("name"), QStringLiteral("my_checkout")}, {QStringLiteral("cols"), 60}, {QStringLiteral("rows"), 15}}));
    const QJsonObject resized = c.request(op("screen", {{QStringLiteral("name"), QStringLiteral("my_checkout")}}));
    QCOMPARE(resized.value(QStringLiteral("cols")).toInt(), 60);
    QCOMPARE(resized.value(QStringLiteral("lines")).toArray().first().toString(), QStringLiteral("full-screen"));
    pid = attach(QStringLiteral("my_checkout"), &master);
    seen.clear();
    QVERIFY2(readUntil(master, seen, "full-screen"), seen.constData());
    QVERIFY2(readUntil(master, seen, "\x1b[?1h"), seen.constData());
    QVERIFY(seen.contains("\x1b[?1049h"));
    QVERIFY(seen.contains("\x1b[0;1;33m"));
    QTRY_COMPARE_WITH_TIMEOUT(sessionNamed(c.request(op("list")), QStringLiteral("my_checkout")).value(QStringLiteral("cols")).toInt(), 90,
                              5000);
    // Detaching leaves the terminal as it found it.
    QCOMPARE(::write(master, "\x1c", 1), 1);
    QVERIFY2(readUntil(master, seen, "[detached from my_checkout]"), seen.constData());
    QVERIFY(seen.lastIndexOf("\x1b[?1049l") > seen.lastIndexOf("full-screen"));
    QVERIFY(seen.lastIndexOf("\x1b[?1l") > seen.lastIndexOf("\x1b[?1h"));
    QCOMPARE(exitCode(pid), 0);
    ::close(master);
    c.request(op("write", {{QStringLiteral("name"), QStringLiteral("my_checkout")}, {QStringLiteral("data"), QStringLiteral("\x04")}}));
    c.request(op("write",
                 {{QStringLiteral("name"), QStringLiteral("my_checkout")},
                  {QStringLiteral("data"), QStringLiteral("printf '\\033[?1049l\\033[?1l'; clear; echo typed-42\r")}}));
    QTRY_VERIFY_WITH_TIMEOUT(
        !c.request(op("screen", {{QStringLiteral("name"), QStringLiteral("my_checkout")}})).value(QStringLiteral("altScreen")).toBool(),
        5000);

    // The session ending ends the client.
    pid = attach(QStringLiteral("my_checkout"), &master);
    seen.clear();
    QVERIFY2(readUntil(master, seen, "typed-42"), seen.constData());
    QCOMPARE(::write(master, "exit\r", 5), 5);
    QVERIFY2(readUntil(master, seen, "[session my_checkout ended]"), seen.constData());
    QCOMPARE(exitCode(pid), 0);
    ::close(master);
}

void SessiondTest::directChannel()
{
    QVERIFY(startForeground());
    Client c;
    QVERIFY(c.connectTo(m_socket));
    quint32 id = 0;
    QCOMPARE(shellCommandSession(c, QStringLiteral("dc"), &id), QString());
    auto cols = [&c] {
        return sessionNamed(c.request(op("list")), QStringLiteral("dc")).value(QStringLiteral("cols")).toInt();
    };
    auto rows = [&c] {
        return sessionNamed(c.request(op("list")), QStringLiteral("dc")).value(QStringLiteral("rows")).toInt();
    };
    // Typed before the first prompt, dash (Ubuntu's sh) prints the output on
    // the prompt's line, so wait for the prompt.
    QTRY_VERIFY_WITH_TIMEOUT(!c.request(op("screen", {{QStringLiteral("name"), QStringLiteral("dc")}}))
                                  .value(QStringLiteral("lines"))
                                  .toArray()
                                  .first()
                                  .toString()
                                  .trimmed()
                                  .isEmpty(),
                             5000);
    c.request(
        op("write", {{QStringLiteral("name"), QStringLiteral("dc")}, {QStringLiteral("data"), QStringLiteral("echo before-attach\r")}}));
    QTRY_VERIFY_WITH_TIMEOUT(c.request(op("screen", {{QStringLiteral("name"), QStringLiteral("dc")}}))
                                 .value(QStringLiteral("lines"))
                                 .toArray()
                                 .contains(QStringLiteral("before-attach")),
                             5000);

    // Attaching redraws what the session shows, at the viewer's size; typing
    // before the daemon answered still arrives. The size the view had before
    // its layout ran never reaches the session, whose screen would lose its
    // contents to a single row.
    Viewer v(20, 90);
    v.channel->open(QStringLiteral("dc"), 1, 13, m_socket);
    v.channel->resize(20, 90);
    QVERIFY(v.channel->isOpen());
    v.channel->write("echo early-$((1+1))\r");
    QTRY_VERIFY2_WITH_TIMEOUT(v.shows(QStringLiteral("before-attach")), v.raw.constData(), 5000);
    QTRY_VERIFY2_WITH_TIMEOUT(v.shows(QStringLiteral("early-2")), v.raw.constData(), 5000);
    QVERIFY(v.channel->session() == id);
    QCOMPARE(cols(), 90);
    QCOMPARE(rows(), 20);

    // Live output and typing.
    v.channel->write("echo typed-$((40+2))\r");
    QTRY_VERIFY2_WITH_TIMEOUT(v.shows(QStringLiteral("typed-42")), v.raw.constData(), 5000);

    // The session's size follows the view.
    v.channel->resize(25, 70);
    v.screen.resize(25, 70);
    QTRY_COMPARE_WITH_TIMEOUT(cols(), 70, 5000);
    QCOMPARE(rows(), 25);
    v.channel->write("stty size\r");
    QTRY_VERIFY2_WITH_TIMEOUT(v.shows(QStringLiteral("25 70")), v.raw.constData(), 5000);

    // A second viewer, as with x3ro-deck attach in another terminal: both see
    // the output, and the last size wins.
    Viewer w(10, 50);
    w.channel->open(QStringLiteral("dc"), 10, 50, m_socket);
    QTRY_VERIFY2_WITH_TIMEOUT(w.shows(QStringLiteral("typed-42")), w.raw.constData(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(cols(), 50, 5000);
    w.channel->write("echo both-$((2*3))\r");
    QTRY_VERIFY2_WITH_TIMEOUT(v.raw.contains("both-6"), v.raw.constData(), 5000);
    QTRY_VERIFY2_WITH_TIMEOUT(w.shows(QStringLiteral("both-6")), w.raw.constData(), 5000);

    // A rename while attached changes nothing for the viewers.
    QVERIFY(c.request(op("rename", {{QStringLiteral("name"), QStringLiteral("dc")}, {QStringLiteral("to"), QStringLiteral("dc2")}}))
                .value(QStringLiteral("ok"))
                .toBool());
    v.channel->write("echo renamed\r");
    QTRY_VERIFY2_WITH_TIMEOUT(v.raw.contains("renamed\r\n"), v.raw.constData(), 5000);

    // Detaching one viewer leaves the session and the other viewer.
    w.channel.reset();
    QTest::qWait(100);
    QVERIFY(!sessionNamed(c.request(op("list")), QStringLiteral("dc2")).isEmpty());
    QVERIFY(v.closed.isEmpty());

    // The session ending closes the channel, after a note.
    v.channel->write("exit\r");
    QTRY_COMPARE_WITH_TIMEOUT(v.closed.size(), 1, 5000);
    QVERIFY(!v.channel->isOpen());
    QVERIFY2(v.raw.contains("[session dc2 ended]"), v.raw.constData());
    v.channel->write("ignored");
    QTest::qWait(100);
    QCOMPARE(v.closed.size(), 1);

    // No such session: closed, never attached.
    Viewer missing(10, 40);
    missing.channel->open(QStringLiteral("nope"), 10, 40, m_socket);
    QVERIFY(missing.closed.isEmpty()); // never from inside open()
    QTRY_COMPARE_WITH_TIMEOUT(missing.closed.size(), 1, 5000);
    QVERIFY2(missing.raw.contains("no such session"), missing.raw.constData());
    QCOMPARE(missing.channel->session(), quint32(0));
}

void SessiondTest::directChannelLosesDaemon()
{
    // No daemon at all.
    Viewer none(10, 40);
    none.channel->open(QStringLiteral("x"), 10, 40, m_socket);
    QVERIFY(none.closed.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(none.closed.size(), 1, 5000);
    QVERIFY2(none.raw.contains("no session daemon"), none.raw.constData());

    QVERIFY(startForeground());
    Client c;
    QVERIFY(c.connectTo(m_socket));
    quint32 id = 0;
    QCOMPARE(shellCommandSession(c, QStringLiteral("dl"), &id), QString());
    Viewer v(20, 80);
    v.channel->open(QStringLiteral("dl"), 20, 80, m_socket);
    v.channel->write("echo alive\r");
    QTRY_VERIFY2_WITH_TIMEOUT(v.shows(QStringLiteral("alive")), v.raw.constData(), 5000);

    // The daemon dying (not shutting down, which ends the sessions first).
    m_daemon.kill();
    QVERIFY(m_daemon.waitForFinished(5000));
    QTRY_COMPARE_WITH_TIMEOUT(v.closed.size(), 1, 5000);
    QVERIFY2(v.raw.contains("[lost the session daemon]"), v.raw.constData());
    QVERIFY(!v.channel->isOpen());
    // Its socket stays behind; the next daemon replaces it.
    QFile::remove(m_socket);
}

QTEST_GUILESS_MAIN(SessiondTest)
#include "tst_sessiond.moc"
