#include "Daemon.h"

#include "ProcessInfo.h"
#include "SessionScreen.h"
#include "SessiondProtocol.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <pty.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace
{
// Every session holds a cell grid this big at most.
constexpr int kMaxSize = 1000;
// A client this far behind is stuck; dropping it keeps the daemon's memory bounded.
constexpr qint64 kClientBacklog = 8 * 1024 * 1024;
constexpr qint64 kIdleExitMs = 60 * 1000;
constexpr int kKillGraceMs = 3000;

int g_childPipe[2] = {-1, -1};

void onSigchld(int)
{
    const int saved = errno;
    const char c = 0;
    [[maybe_unused]] const ssize_t n = write(g_childPipe[1], &c, 1);
    errno = saved;
}

// What tmux shows as pane_current_command. Like tmux, it falls back to the
// shell for the moment between a job ending and the shell taking the terminal back.
QString foregroundName(pid_t shell, int fd)
{
    QString name;
    if (const std::optional<ProcessInfo::Process> p = ProcessInfo::process(ProcessInfo::foregroundGroup(fd))) {
        name = ProcessInfo::commandName(*p);
    }
    if (name.isEmpty()) {
        if (const std::optional<ProcessInfo::Process> p = ProcessInfo::process(shell)) {
            name = ProcessInfo::commandName(*p);
        }
    }
    return name;
}

int clampSize(const QJsonValue &v, int fallback)
{
    const int n = v.toInt(fallback);
    return n < 1 ? fallback : qMin(n, kMaxSize);
}

void signalGroup(pid_t pg, int sig)
{
    if (pg > 1) {
        kill(-pg, sig);
    }
}
} // namespace

Daemon::Daemon(const QString &socketPath, QObject *parent)
    : QObject(parent)
    , m_path(socketPath)
    , m_server(new QLocalServer(this))
{
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &Daemon::onNewConnection);

    if (pipe2(g_childPipe, O_CLOEXEC | O_NONBLOCK) == 0) {
        struct sigaction sa{};
        sa.sa_handler = onSigchld;
        sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGCHLD, &sa, nullptr);
        m_childNotifier = new QSocketNotifier(g_childPipe[0], QSocketNotifier::Read, this);
        connect(m_childNotifier, &QSocketNotifier::activated, this, &Daemon::onChildSignal);
    }

    m_idle.setInterval(5000);
    connect(&m_idle, &QTimer::timeout, this, &Daemon::checkIdle);
}

Daemon::~Daemon()
{
    for (Session *s : std::as_const(m_sessions)) {
        ::close(s->fd);
        delete s;
    }
    struct stat st{};
    if (m_socketInode && stat(QFile::encodeName(m_path).constData(), &st) == 0 && st.st_ino == m_socketInode) {
        QFile::remove(m_path);
    }
    if (m_lockFd >= 0) {
        ::close(m_lockFd);
    }
}

Daemon::ListenResult Daemon::listen(QString *error)
{
    // One daemon per socket. The lock outlives a crash only as long as the
    // process, so a stale socket file never blocks a new daemon.
    const QByteArray lockPath = QFile::encodeName(m_path + QStringLiteral(".lock"));
    m_lockFd = ::open(lockPath.constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (m_lockFd < 0) {
        *error = QStringLiteral("cannot open %1: %2").arg(QString::fromLocal8Bit(lockPath), QString::fromLocal8Bit(strerror(errno)));
        return ListenResult::Failed;
    }
    if (flock(m_lockFd, LOCK_EX | LOCK_NB) != 0) {
        return ListenResult::Running;
    }
    QLocalServer::removeServer(m_path);
    if (!m_server->listen(m_path)) {
        *error = m_server->errorString();
        return ListenResult::Failed;
    }
    struct stat st{};
    if (stat(QFile::encodeName(m_path).constData(), &st) == 0) {
        m_socketInode = st.st_ino;
    }
    m_idleFor.start();
    m_idle.start();
    return ListenResult::Listening;
}

void Daemon::onNewConnection()
{
    while (QLocalSocket *sock = m_server->nextPendingConnection()) {
        // The socket is 0700 already; the peer check also covers a directory
        // with looser permissions.
        struct ucred cred{};
        socklen_t len = sizeof cred;
        if (getsockopt(int(sock->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0 || cred.uid != getuid()) {
            sock->abort();
            sock->deleteLater();
            continue;
        }
        m_clients.insert(sock, Client{});
        connect(sock, &QLocalSocket::readyRead, this, [this, sock] { onClientData(sock); });
        connect(sock, &QLocalSocket::disconnected, this, [this, sock] {
            m_clients.remove(sock);
            sock->deleteLater();
        });
    }
}

void Daemon::send(QLocalSocket *sock, const QByteArray &frame)
{
    if (sock->state() != QLocalSocket::ConnectedState) {
        return;
    }
    sock->write(frame);
    if (sock->bytesToWrite() > kClientBacklog) {
        qWarning("x3ro-deck-sessiond: dropping a client that stopped reading");
        QMetaObject::invokeMethod(sock, &QLocalSocket::abort, Qt::QueuedConnection);
    }
}

void Daemon::onClientData(QLocalSocket *sock)
{
    auto it = m_clients.find(sock);
    if (it == m_clients.end()) {
        return;
    }
    it->buffer.append(sock->readAll());
    Sessiond::Frame frame;
    bool bad = false;
    // handle() may touch m_clients, so look the client up for every frame.
    while (m_clients.contains(sock) && Sessiond::takeFrame(m_clients[sock].buffer, frame, &bad)) {
        if (frame.type == Sessiond::Control) {
            handle(sock, Sessiond::parseControl(frame.payload));
        } else if (frame.type == Sessiond::Input) {
            quint32 id = 0;
            QByteArray bytes;
            if (Sessiond::splitData(frame.payload, id, bytes)) {
                if (Session *s = m_sessions.value(id)) {
                    writePty(s, bytes);
                }
            }
        }
    }
    if (bad) {
        qWarning("x3ro-deck-sessiond: dropping a client that sent an oversized frame");
        sock->abort();
    }
}

Daemon::Session *Daemon::find(const QJsonObject &req) const
{
    if (req.contains(QStringLiteral("session"))) {
        return m_sessions.value(quint32(req.value(QStringLiteral("session")).toInt()));
    }
    const QString name = req.value(QStringLiteral("name")).toString();
    for (Session *s : m_sessions) {
        if (s->name == name) {
            return s;
        }
    }
    return nullptr;
}

void Daemon::handle(QLocalSocket *sock, const QJsonObject &req)
{
    const QString op = req.value(QStringLiteral("op")).toString();
    QJsonObject reply;
    QString error;
    std::function<void()> afterReply;
    Session *s = find(req);
    const QString noSession = QStringLiteral("no such session");

    if (op == QLatin1String("hello")) {
        reply.insert(QStringLiteral("version"), Sessiond::kProtocolVersion);
        reply.insert(QStringLiteral("pid"), qint64(getpid()));
    } else if (op == QLatin1String("create")) {
        reply = create(req, &error);
    } else if (op == QLatin1String("list")) {
        // The whole foreground job only on request: it walks the session's
        // process tree, which the pane poll wants and a lookup does not.
        const bool processes = req.value(QStringLiteral("processes")).toBool();
        QJsonArray list;
        for (Session *x : std::as_const(m_sessions)) {
            QJsonObject entry{{QStringLiteral("id"), qint64(x->id)},
                              {QStringLiteral("name"), x->name},
                              {QStringLiteral("cwd"), x->cwd},
                              {QStringLiteral("env"), x->env},
                              {QStringLiteral("shell"), x->shell},
                              {QStringLiteral("pid"), qint64(x->pid)},
                              {QStringLiteral("cols"), x->cols},
                              {QStringLiteral("rows"), x->rows},
                              {QStringLiteral("foreground"), foregroundName(x->pid, x->fd)}};
            if (processes) {
                entry.insert(QStringLiteral("foregroundGroup"), QJsonArray::fromStringList(ProcessInfo::foregroundNames(x->pid, x->fd)));
            }
            list.append(entry);
        }
        reply.insert(QStringLiteral("sessions"), list);
    } else if (op == QLatin1String("write")) {
        if (s) {
            writePty(s, req.value(QStringLiteral("data")).toString().toUtf8());
        } else {
            error = noSession;
        }
    } else if (op == QLatin1String("resize")) {
        if (s) {
            resize(s, clampSize(req.value(QStringLiteral("cols")), s->cols), clampSize(req.value(QStringLiteral("rows")), s->rows));
        } else {
            error = noSession;
        }
    } else if (op == QLatin1String("rename")) {
        const QString to = req.value(QStringLiteral("to")).toString();
        QJsonObject byName{{QStringLiteral("name"), to}};
        if (!s) {
            error = noSession;
        } else if (to.isEmpty() || (find(byName) && find(byName) != s)) {
            error = QStringLiteral("a session named %1 exists").arg(to);
        } else {
            s->name = to;
        }
    } else if (op == QLatin1String("kill")) {
        if (s) {
            killSession(s);
        } else {
            error = noSession;
        }
    } else if (op == QLatin1String("attach")) {
        if (s) {
            const quint32 id = s->id;
            m_clients[sock].attached.insert(id);
            const bool redraw = req.value(QStringLiteral("replay")).toBool(true);
            const int cols = clampSize(req.value(QStringLiteral("cols")), s->cols);
            const int rows = clampSize(req.value(QStringLiteral("rows")), s->rows);
            reply.insert(QStringLiteral("session"), qint64(id));
            reply.insert(QStringLiteral("cols"), cols);
            reply.insert(QStringLiteral("rows"), rows);
            afterReply = [this, sock, id, redraw, cols, rows] {
                Session *x = m_sessions.value(id);
                if (!x) {
                    return;
                }
                // Resized first, so the redraw already has the client's size;
                // the kernel tells the program when the size changed.
                resize(x, cols, rows);
                if (redraw) {
                    send(sock, Sessiond::encodeData(Sessiond::Output, id, x->screen->redraw()));
                } else {
                    // Nothing to show until the program repaints, which a TUI
                    // does on SIGWINCH even when the size stayed the same.
                    signalGroup(tcgetpgrp(x->fd), SIGWINCH);
                }
            };
        } else {
            error = noSession;
        }
    } else if (op == QLatin1String("processes")) {
        if (s) {
            QJsonArray list;
            for (const ProcessInfo::Process &p : ProcessInfo::tree(s->pid)) {
                list.append(QJsonObject{{QStringLiteral("pid"), p.pid},
                                        {QStringLiteral("ppid"), p.ppid},
                                        {QStringLiteral("pgid"), p.pgid},
                                        {QStringLiteral("name"), ProcessInfo::commandName(p)},
                                        {QStringLiteral("args"), QJsonArray::fromStringList(p.args)}});
            }
            reply.insert(QStringLiteral("foreground"), ProcessInfo::foregroundGroup(s->fd));
            reply.insert(QStringLiteral("processes"), list);
        } else {
            error = noSession;
        }
    } else if (op == QLatin1String("screen")) {
        if (s) {
            QJsonArray lines;
            for (int row = 0; row < s->screen->rows(); ++row) {
                lines.append(s->screen->rowText(row));
            }
            reply.insert(QStringLiteral("cols"), s->screen->cols());
            reply.insert(QStringLiteral("rows"), s->screen->rows());
            reply.insert(QStringLiteral("cursor"),
                         QJsonObject{{QStringLiteral("row"), s->screen->cursorRow()}, {QStringLiteral("col"), s->screen->cursorCol()}});
            reply.insert(QStringLiteral("cursorVisible"), s->screen->cursorVisible());
            reply.insert(QStringLiteral("altScreen"), s->screen->altScreen());
            reply.insert(QStringLiteral("title"), s->screen->title());
            reply.insert(QStringLiteral("lines"), lines);
        } else {
            error = noSession;
        }
    } else if (op == QLatin1String("detach")) {
        m_clients[sock].attached.remove(quint32(req.value(QStringLiteral("session")).toInt()));
    } else if (op == QLatin1String("shutdown")) {
        afterReply = [this, sock] {
            sock->flush();
            shutdown();
        };
    } else {
        error = QStringLiteral("unknown op: %1").arg(op);
    }

    reply.insert(QStringLiteral("re"), req.value(QStringLiteral("id")));
    reply.insert(QStringLiteral("ok"), error.isEmpty());
    if (!error.isEmpty()) {
        reply.insert(QStringLiteral("error"), error);
    }
    send(sock, Sessiond::encodeControl(reply));
    if (afterReply) {
        afterReply();
    }
}

QJsonObject Daemon::create(const QJsonObject &req, QString *error)
{
    const QString name = req.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        *error = QStringLiteral("a session needs a name");
        return {};
    }
    if (find(QJsonObject{{QStringLiteral("name"), name}})) {
        *error = QStringLiteral("a session named %1 exists").arg(name);
        return {};
    }
    const QString cwd = req.value(QStringLiteral("cwd")).toString();
    const QJsonObject env = req.value(QStringLiteral("env")).toObject();
    const int cols = clampSize(req.value(QStringLiteral("cols")), 200);
    const int rows = clampSize(req.value(QStringLiteral("rows")), 50);

    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.remove(QStringLiteral("TMUX"));
    pe.remove(QStringLiteral("TMUX_PANE"));
    pe.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    pe.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    for (auto it = env.begin(); it != env.end(); ++it) {
        pe.insert(it.key(), it.value().toString());
    }

    QStringList command;
    for (const QJsonValue &v : req.value(QStringLiteral("command")).toArray()) {
        command.append(v.toString());
    }
    QByteArray program;
    QList<QByteArray> args;
    if (command.isEmpty()) {
        // A login shell, as tmux starts one.
        QString shell = pe.value(QStringLiteral("SHELL"));
        if (shell.isEmpty() || !QFileInfo(shell).isExecutable()) {
            const passwd *pw = getpwuid(getuid());
            shell = pw && pw->pw_shell && *pw->pw_shell ? QString::fromLocal8Bit(pw->pw_shell) : QStringLiteral("/bin/sh");
        }
        program = QFile::encodeName(shell);
        args.append('-' + QFile::encodeName(QFileInfo(shell).fileName()));
    } else {
        QString exe = command.first();
        if (!exe.contains(QLatin1Char('/'))) {
            exe = QStandardPaths::findExecutable(exe, pe.value(QStringLiteral("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts));
        }
        if (exe.isEmpty()) {
            *error = QStringLiteral("command not found: %1").arg(command.first());
            return {};
        }
        program = QFile::encodeName(exe);
        for (const QString &a : std::as_const(command)) {
            args.append(a.toLocal8Bit());
        }
        if (req.value(QStringLiteral("login")).toBool()) {
            args[0] = '-' + QFile::encodeName(QFileInfo(exe).fileName());
        }
    }

    // Everything the child touches is built before fork: between fork and
    // exec only async-signal-safe calls are allowed.
    QList<QByteArray> envStrings;
    for (const QString &kv : pe.toStringList()) {
        envStrings.append(kv.toLocal8Bit());
    }
    std::vector<char *> argv;
    for (QByteArray &a : args) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    std::vector<char *> envp;
    for (QByteArray &e : envStrings) {
        envp.push_back(e.data());
    }
    envp.push_back(nullptr);
    const QByteArray dir = QFile::encodeName(cwd);
    const QByteArray home = QFile::encodeName(pe.value(QStringLiteral("HOME")));

    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    int fd = -1;
    const pid_t pid = forkpty(&fd, nullptr, nullptr, &ws);
    if (pid < 0) {
        *error = QStringLiteral("forkpty: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return {};
    }
    if (pid == 0) {
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        for (int sig : {SIGCHLD, SIGPIPE, SIGHUP, SIGINT, SIGQUIT, SIGTERM}) {
            signal(sig, SIG_DFL);
        }
        if (dir.isEmpty() || chdir(dir.constData()) != 0) {
            if (home.isEmpty() || chdir(home.constData()) != 0) {
                [[maybe_unused]] const int r = chdir("/");
            }
        }
        execve(program.constData(), argv.data(), envp.data());
        static const char msg[] = "x3ro-deck-sessiond: cannot run the session command\r\n";
        [[maybe_unused]] const ssize_t n = write(STDOUT_FILENO, msg, sizeof msg - 1);
        _exit(127);
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

    auto *s = new Session;
    s->id = m_nextId++;
    s->name = name;
    s->cwd = cwd;
    s->env = env;
    s->shell = req.value(QStringLiteral("shell")).toArray();
    s->pid = pid;
    s->fd = fd;
    s->cols = cols;
    s->rows = rows;
    s->screen = std::make_unique<SessionScreen>(rows, cols);
    const quint32 id = s->id;
    s->reader = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(s->reader, &QSocketNotifier::activated, this, [this, id] {
        if (Session *x = m_sessions.value(id)) {
            readPty(x);
        }
    });
    s->writer = new QSocketNotifier(fd, QSocketNotifier::Write, this);
    s->writer->setEnabled(false);
    connect(s->writer, &QSocketNotifier::activated, this, [this, id] {
        if (Session *x = m_sessions.value(id)) {
            flushPty(x);
        }
    });
    m_sessions.insert(id, s);
    return {{QStringLiteral("session"), qint64(id)}};
}

qsizetype Daemon::readPty(Session *s)
{
    char buf[65536];
    const ssize_t n = ::read(s->fd, buf, sizeof buf);
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
        return 0;
    }
    if (n <= 0) {
        // Every slave end is closed (EIO). The session ends when its
        // process is reaped; stop the notifier spinning until then.
        s->reader->setEnabled(false);
        return 0;
    }
    const QByteArray chunk(buf, n);
    s->screen->feed(chunk);
    const QByteArray frame = Sessiond::encodeData(Sessiond::Output, s->id, chunk);
    const QList<QLocalSocket *> socks = m_clients.keys();
    for (QLocalSocket *sock : socks) {
        auto it = m_clients.constFind(sock);
        if (it != m_clients.cend() && it->attached.contains(s->id)) {
            send(sock, frame);
        }
    }
    return n;
}

void Daemon::writePty(Session *s, const QByteArray &bytes)
{
    s->pending.append(bytes);
    flushPty(s);
}

void Daemon::flushPty(Session *s)
{
    while (!s->pending.isEmpty()) {
        const ssize_t n = ::write(s->fd, s->pending.constData(), size_t(s->pending.size()));
        if (n > 0) {
            s->pending.remove(0, n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && errno == EAGAIN) {
            s->writer->setEnabled(true);
            return;
        } else {
            s->pending.clear();
        }
    }
    s->writer->setEnabled(false);
}

void Daemon::resize(Session *s, int cols, int rows)
{
    if (cols == s->cols && rows == s->rows) {
        return;
    }
    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    // The kernel sends SIGWINCH to the foreground process group.
    if (ioctl(s->fd, TIOCSWINSZ, &ws) == 0) {
        s->cols = cols;
        s->rows = rows;
        s->screen->resize(rows, cols);
    }
}

void Daemon::onChildSignal()
{
    char buf[64];
    while (::read(g_childPipe[0], buf, sizeof buf) > 0) {
    }
    int status = 0;
    pid_t pid = 0;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (Session *s : std::as_const(m_sessions)) {
            if (s->pid != pid) {
                continue;
            }
            // Pass on what the process wrote last before the PTY closes.
            for (int i = 0; i < 64 && s->reader->isEnabled(); ++i) {
                if (readPty(s) == 0) {
                    break;
                }
            }
            endSession(s, WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
            break;
        }
    }
}

void Daemon::endSession(Session *s, int status)
{
    const QJsonObject event{{QStringLiteral("event"), QStringLiteral("exited")},
                            {QStringLiteral("session"), qint64(s->id)},
                            {QStringLiteral("name"), s->name},
                            {QStringLiteral("status"), status}};
    const QByteArray frame = Sessiond::encodeControl(event);
    const QList<QLocalSocket *> socks = m_clients.keys();
    for (QLocalSocket *sock : socks) {
        auto it = m_clients.find(sock);
        if (it != m_clients.end() && it->attached.remove(s->id)) {
            send(sock, frame);
        }
    }
    m_sessions.remove(s->id);
    delete s->reader;
    delete s->writer;
    ::close(s->fd);
    delete s;
}

void Daemon::killSession(Session *s)
{
    // Closing the master hangs up the terminal, which sends SIGHUP to the
    // shell and the foreground job; tell both directly as well, and kill
    // whatever is left after a grace period.
    const QList<pid_t> groups{s->pid, tcgetpgrp(s->fd)};
    for (pid_t pg : groups) {
        signalGroup(pg, SIGHUP);
    }
    endSession(s, -1);
    QTimer::singleShot(kKillGraceMs, [groups] {
        for (pid_t pg : groups) {
            if (pg > 1 && kill(-pg, 0) == 0) {
                kill(-pg, SIGKILL);
            }
        }
    });
}

void Daemon::shutdown()
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;
    // Stop listening and release the lock now, so a new daemon can start
    // while this one waits out the kill grace period.
    m_server->close();
    struct stat st{};
    if (m_socketInode && stat(QFile::encodeName(m_path).constData(), &st) == 0 && st.st_ino == m_socketInode) {
        QFile::remove(m_path);
    }
    m_socketInode = 0;
    if (m_lockFd >= 0) {
        ::close(m_lockFd);
        m_lockFd = -1;
    }
    const QList<Session *> sessions = m_sessions.values();
    for (Session *s : sessions) {
        killSession(s);
    }
    QTimer::singleShot(sessions.isEmpty() ? 0 : kKillGraceMs + 100, qApp, &QCoreApplication::quit);
}

void Daemon::checkIdle()
{
    if (m_shuttingDown) {
        return;
    }
    // A daemon whose socket was removed (say, with its runtime directory)
    // can't be reached again, so its sessions are lost either way.
    struct stat st{};
    if (stat(QFile::encodeName(m_path).constData(), &st) != 0 || st.st_ino != m_socketInode) {
        qWarning("x3ro-deck-sessiond: %s is gone, shutting down", qPrintable(m_path));
        shutdown();
        return;
    }
    // Like a tmux server after its last session, but it waits a while so a
    // GUI between restarts finds it.
    if (!m_sessions.isEmpty() || !m_clients.isEmpty()) {
        m_idleFor.restart();
    } else if (m_idleFor.elapsed() > kIdleExitMs) {
        shutdown();
    }
}
