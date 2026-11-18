// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// `x3ro-deck attach` on Windows: the console in VT mode, the daemon's named
// pipe through QLocalSocket, and a thread reading console input. Ctrl+\
// detaches, as on Unix.

#include "AttachClient.h"

#include "SessiondProtocol.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMutex>
#include <QRegularExpression>
#include <QThread>

#include <x3ro-deck-socket.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include <windows.h>

namespace
{
constexpr char kDetachKey = 0x1c;

QString sessionName(QString checkout)
{
    static const QRegularExpression re(QStringLiteral("[.:]"));
    return checkout.replace(re, QStringLiteral("_"));
}

void consoleSize(int &cols, int &rows)
{
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        cols = info.srWindow.Right - info.srWindow.Left + 1;
        rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
}

void writeOut(const QByteArray &bytes)
{
    DWORD n = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.constData(), DWORD(bytes.size()), &n, nullptr);
}

// Console input, read on its own thread; VT input mode turns keys into bytes.
class InputReader : public QThread
{
public:
    QMutex lock;
    QByteArray pending;
    std::atomic<bool> closed{false};

protected:
    void run() override
    {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf, sizeof buf, &n, nullptr) && n > 0) {
            QMutexLocker guard(&lock);
            pending.append(buf, qsizetype(n));
        }
        closed = true;
    }
};
} // namespace

int runAttach(int argc, char **argv)
{
    std::string socketPath = x3ro_deck::sessiondSocketPath().path;
    bool replay = true;
    QString checkout;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socketPath = argv[++i];
        } else if (std::strcmp(argv[i], "--no-replay") == 0) {
            replay = false;
        } else if (argv[i][0] == '-' || !checkout.isEmpty()) {
            std::fprintf(stderr, "usage: x3ro-deck attach [--socket PATH] [--no-replay] <checkout>\n");
            return 2;
        } else {
            checkout = QString::fromLocal8Bit(argv[i]);
        }
    }
    if (checkout.isEmpty()) {
        std::fprintf(stderr, "usage: x3ro-deck attach [--socket PATH] [--no-replay] <checkout>\n");
        return 2;
    }
    QLocalSocket sock;
    sock.connectToServer(QString::fromStdString(socketPath));
    if (!sock.waitForConnected(2000)) {
        std::fprintf(stderr, "x3ro-deck attach: no session daemon is listening on %s\n", socketPath.c_str());
        return 1;
    }
    int cols = 80;
    int rows = 24;
    consoleSize(cols, rows);
    sock.write(Sessiond::encodeControl({{QStringLiteral("op"), QStringLiteral("hello")},
                                        {QStringLiteral("id"), 1},
                                        {QStringLiteral("version"), Sessiond::kProtocolVersion}}));
    sock.write(Sessiond::encodeControl({{QStringLiteral("op"), QStringLiteral("attach")},
                                        {QStringLiteral("id"), 2},
                                        {QStringLiteral("name"), sessionName(checkout)},
                                        {QStringLiteral("cols"), cols},
                                        {QStringLiteral("rows"), rows},
                                        {QStringLiteral("replay"), replay}}));
    sock.flush();

    // VT in both directions; restored on the way out.
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD inMode = 0, outMode = 0;
    const bool console = GetConsoleMode(in, &inMode) && GetConsoleMode(out, &outMode);
    if (console) {
        SetConsoleMode(in, ENABLE_VIRTUAL_TERMINAL_INPUT);
        SetConsoleMode(out, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
    }
    InputReader reader;
    reader.start();

    QByteArray buffer;
    quint32 session = 0;
    int result = 0;
    bool running = true;
    QElapsedTimer resizeCheck;
    resizeCheck.start();
    while (running) {
        if (sock.waitForReadyRead(20)) {
            buffer.append(sock.readAll());
        }
        if (sock.state() != QLocalSocket::ConnectedState && buffer.isEmpty()) {
            std::fprintf(stderr, "\r\nx3ro-deck attach: lost the session daemon\r\n");
            result = 1;
            break;
        }
        Sessiond::Frame f;
        bool bad = false;
        while (Sessiond::takeFrame(buffer, f, &bad)) {
            if (f.type == Sessiond::Output) {
                quint32 id = 0;
                QByteArray bytes;
                if (Sessiond::splitData(f.payload, id, bytes) && (session == 0 || id == session)) {
                    writeOut(bytes);
                }
            } else if (f.type == Sessiond::Control) {
                const QJsonObject msg = Sessiond::parseControl(f.payload);
                if (msg.value(QStringLiteral("re")).toInt() == 2) {
                    if (!msg.value(QStringLiteral("ok")).toBool()) {
                        std::fprintf(stderr, "x3ro-deck attach: %s\n", qPrintable(msg.value(QStringLiteral("error")).toString()));
                        result = 1;
                        running = false;
                    }
                    session = quint32(msg.value(QStringLiteral("session")).toInt());
                } else if (msg.value(QStringLiteral("event")).toString() == QLatin1String("exited")) {
                    std::fprintf(stderr, "\r\n[the session ended]\r\n");
                    running = false;
                }
            }
        }
        QByteArray input;
        {
            QMutexLocker guard(&reader.lock);
            input.swap(reader.pending);
        }
        if (const qsizetype detach = input.indexOf(kDetachKey); detach >= 0) {
            input.truncate(detach);
            running = false;
            std::fprintf(stderr, "\r\n[detached]\r\n");
        }
        if (!input.isEmpty() && session) {
            sock.write(Sessiond::encodeData(Sessiond::Input, session, input));
        }
        if (reader.closed && running) {
            running = false;
        }
        // Windows has no SIGWINCH; poll the size a few times a second.
        if (resizeCheck.elapsed() > 250 && session) {
            resizeCheck.restart();
            int c = cols, r = rows;
            consoleSize(c, r);
            if (c != cols || r != rows) {
                cols = c;
                rows = r;
                sock.write(Sessiond::encodeControl({{QStringLiteral("op"), QStringLiteral("resize")},
                                                    {QStringLiteral("id"), 3},
                                                    {QStringLiteral("session"), qint64(session)},
                                                    {QStringLiteral("cols"), cols},
                                                    {QStringLiteral("rows"), rows}}));
            }
        }
        sock.flush();
    }
    if (console) {
        SetConsoleMode(in, inMode);
        SetConsoleMode(out, outMode);
    }
    // The reader blocks in ReadFile; it ends with the process.
    reader.terminate();
    reader.wait(500);
    return result;
}
