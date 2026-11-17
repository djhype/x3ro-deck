#include "AttachClient.h"

#include "SessiondProtocol.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QString>

#include <x3ro-deck-socket.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

namespace
{
// Ctrl+\, as in dtach and abduco; also its CSI u form, which terminals send
// once the session's program turned on extended keys.
constexpr char kDetachKey = 0x1c;
const QByteArray kDetachKeyCsi = QByteArrayLiteral("\x1b[92;5u");

// Leaves the terminal usable whatever the session's program had turned on:
// alternate screen, hidden cursor, mouse reporting, bracketed paste, focus events.
const char kResetModes[] = "\x1b[?1049l\x1b[0m\x1b[?25h\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1006l\x1b[?2004l\x1b[?1004l";

int g_signalPipe[2] = {-1, -1};
termios g_saved{};
bool g_raw = false;

void onSignal(int sig)
{
    const int saved = errno;
    const char c = sig == SIGWINCH ? 'w' : 'q';
    [[maybe_unused]] const ssize_t n = write(g_signalPipe[1], &c, 1);
    errno = saved;
}

bool writeAll(int fd, const char *data, size_t len)
{
    while (len > 0) {
        const ssize_t n = ::write(fd, data, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN) {
                pollfd p{fd, POLLOUT, 0};
                poll(&p, 1, -1);
                continue;
            }
            return false;
        }
        data += n;
        len -= size_t(n);
    }
    return true;
}

bool writeAll(int fd, const QByteArray &bytes)
{
    return writeAll(fd, bytes.constData(), size_t(bytes.size()));
}

struct Connection
{
    int fd = -1;
    QByteArray buffer;

    ~Connection()
    {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    bool open(const std::string &path)
    {
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof addr.sun_path) {
            return false;
        }
        std::memcpy(addr.sun_path, path.c_str(), path.size());
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        return fd >= 0 && ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0;
    }
    bool send(const QByteArray &frame) { return writeAll(fd, frame); }
    // Blocks until data arrives; false once the daemon is gone.
    bool fill()
    {
        char buf[65536];
        ssize_t n = 0;
        do {
            n = ::read(fd, buf, sizeof buf);
        } while (n < 0 && errno == EINTR);
        if (n <= 0) {
            return false;
        }
        buffer.append(buf, n);
        return true;
    }
};

// The same mapping as sessionName() in AppController.cpp.
QString sessionName(QString checkout)
{
    static const QRegularExpression re(QStringLiteral("[.:]"));
    return checkout.replace(re, QStringLiteral("_"));
}

void windowSize(int &cols, int &rows)
{
    winsize ws{};
    for (int fd : {STDIN_FILENO, STDOUT_FILENO}) {
        if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            cols = ws.ws_col;
            rows = ws.ws_row;
            return;
        }
    }
}

void restoreTerminal()
{
    if (!g_raw) {
        return;
    }
    tcsetattr(STDIN_FILENO, TCSADRAIN, &g_saved);
    g_raw = false;
    writeAll(STDOUT_FILENO, kResetModes, sizeof kResetModes - 1);
}

int usage(FILE *out, const char *self, int code)
{
    std::fprintf(out,
                 "usage: %s [--socket PATH] [--no-replay] <checkout>\n"
                 "Attaches this terminal to the checkout's agent session in x3ro-deck-sessiond.\n"
                 "Ctrl+\\ detaches; the agent keeps running.\n",
                 self);
    return code;
}

enum class End {
    Detached,
    Ended,
    Lost,
};
} // namespace

int runAttach(int argc, char **argv)
{
    const char *self = argc > 0 ? argv[0] : "attach";
    std::string socketPath = x3ro_deck::sessiondSocketPath().path;
    bool replay = true;
    QString checkout;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strcmp(a, "--socket") == 0 && i + 1 < argc) {
            socketPath = argv[++i];
        } else if (std::strcmp(a, "--no-replay") == 0) {
            replay = false;
        } else if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            return usage(stdout, self, 0);
        } else if (a[0] == '-' || !checkout.isEmpty()) {
            return usage(stderr, self, 2);
        } else {
            checkout = QString::fromLocal8Bit(a);
        }
    }
    if (checkout.isEmpty()) {
        return usage(stderr, self, 2);
    }
    const QString name = sessionName(checkout);

    Connection conn;
    if (!conn.open(socketPath)) {
        std::fprintf(stderr,
                     "x3ro-deck attach: no session daemon is listening on %s\n"
                     "X3RO Deck starts it when config.json has \"sessionBackend\": \"daemon\".\n",
                     socketPath.c_str());
        return 1;
    }
    int cols = 80;
    int rows = 24;
    windowSize(cols, rows);
    conn.send(Sessiond::encodeControl({{QStringLiteral("op"), QStringLiteral("hello")},
                                       {QStringLiteral("id"), 1},
                                       {QStringLiteral("version"), Sessiond::kProtocolVersion}}));
    conn.send(Sessiond::encodeControl({{QStringLiteral("op"), QStringLiteral("attach")},
                                       {QStringLiteral("id"), 2},
                                       {QStringLiteral("name"), name},
                                       {QStringLiteral("cols"), cols},
                                       {QStringLiteral("rows"), rows},
                                       {QStringLiteral("replay"), replay}}));

    // Wait for the attach reply; output frames after it stay buffered.
    quint32 session = 0;
    for (bool attached = false; !attached;) {
        Sessiond::Frame f;
        bool bad = false;
        while (!attached && Sessiond::takeFrame(conn.buffer, f, &bad)) {
            if (f.type != Sessiond::Control) {
                continue;
            }
            const QJsonObject reply = Sessiond::parseControl(f.payload);
            if (reply.value(QStringLiteral("re")).toInt() != 2) {
                continue;
            }
            if (!reply.value(QStringLiteral("ok")).toBool()) {
                std::fprintf(stderr, "x3ro-deck attach: %s: %s\n", qPrintable(name),
                             qPrintable(reply.value(QStringLiteral("error")).toString()));
                return 1;
            }
            session = quint32(reply.value(QStringLiteral("session")).toInt());
            attached = true;
        }
        if (!attached && (bad || !conn.fill())) {
            std::fprintf(stderr, "x3ro-deck attach: the session daemon closed the connection\n");
            return 1;
        }
    }

    if (pipe2(g_signalPipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        std::perror("x3ro-deck attach: pipe");
        return 1;
    }
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGWINCH, SIGTERM, SIGHUP, SIGINT}) {
        sigaction(sig, &sa, nullptr);
    }
    signal(SIGPIPE, SIG_IGN);

    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_saved) == 0) {
        termios raw = g_saved;
        cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSADRAIN, &raw);
        g_raw = true;
        // A clean screen for the replayed output.
        writeAll(STDOUT_FILENO, QByteArrayLiteral("\x1b[H\x1b[2J"));
    }

    // Writes the session's output; returns true when the session ended.
    auto drain = [&conn, session](bool &bad) {
        Sessiond::Frame f;
        while (Sessiond::takeFrame(conn.buffer, f, &bad)) {
            if (f.type == Sessiond::Output) {
                quint32 id = 0;
                QByteArray bytes;
                if (Sessiond::splitData(f.payload, id, bytes) && id == session) {
                    writeAll(STDOUT_FILENO, bytes);
                }
            } else if (f.type == Sessiond::Control) {
                const QJsonObject event = Sessiond::parseControl(f.payload);
                if (event.value(QStringLiteral("event")).toString() == QLatin1String("exited")
                    && quint32(event.value(QStringLiteral("session")).toInt()) == session) {
                    return true;
                }
            }
        }
        return false;
    };

    End end = End::Detached;
    bool stdinOpen = true;
    for (bool running = true; running;) {
        bool bad = false;
        if (drain(bad)) {
            end = End::Ended;
            break;
        }
        if (bad) {
            end = End::Lost;
            break;
        }
        pollfd fds[3] = {{stdinOpen ? STDIN_FILENO : -1, POLLIN, 0}, {conn.fd, POLLIN, 0}, {g_signalPipe[0], POLLIN, 0}};
        if (poll(fds, 3, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            end = End::Lost;
            break;
        }
        if (fds[2].revents & POLLIN) {
            char sigs[64];
            const ssize_t n = ::read(g_signalPipe[0], sigs, sizeof sigs);
            const QByteArray got(sigs, n > 0 ? n : 0);
            if (got.contains('q')) {
                break;
            }
            if (got.contains('w')) {
                windowSize(cols, rows);
                conn.send(Sessiond::encodeControl({{QStringLiteral("op"), QStringLiteral("resize")},
                                                   {QStringLiteral("id"), 3},
                                                   {QStringLiteral("session"), qint64(session)},
                                                   {QStringLiteral("cols"), cols},
                                                   {QStringLiteral("rows"), rows}}));
            }
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            char buf[16384];
            const ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) {
                    continue;
                }
                // Without a terminal there is nothing left to forward; with
                // input from a pipe, keep showing output until the session ends.
                stdinOpen = false;
                if (g_raw || n < 0) {
                    break;
                }
            } else {
                QByteArray input(buf, n);
                qsizetype cut = input.indexOf(kDetachKey);
                const qsizetype csi = input.indexOf(kDetachKeyCsi);
                if (csi >= 0 && (cut < 0 || csi < cut)) {
                    cut = csi;
                }
                if (cut >= 0) {
                    input.truncate(cut);
                    running = false;
                }
                if (!input.isEmpty()) {
                    conn.send(Sessiond::encodeData(Sessiond::Input, session, input));
                }
            }
        }
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (!conn.fill()) {
                end = End::Lost;
                break;
            }
        }
    }

    restoreTerminal();
    switch (end) {
    case End::Detached:
        std::printf("[detached from %s]\n", qPrintable(name));
        return 0;
    case End::Ended:
        std::printf("[session %s ended]\n", qPrintable(name));
        return 0;
    case End::Lost:
        std::printf("[lost the session daemon]\n");
        return 1;
    }
    return 0;
}
