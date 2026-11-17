#include "PtyChannel.h"

#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

PtyChannel::PtyChannel(QObject *parent)
    : TerminalChannel(parent)
{
}

PtyChannel::~PtyChannel()
{
    hangUp();
}

bool PtyChannel::start(const QStringList &argv, const QString &dir, int rows, int cols, const QMap<QString, QString> &env, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (m_fd >= 0) {
        return fail(QStringLiteral("already running"));
    }
    if (argv.isEmpty()) {
        return fail(QStringLiteral("no command"));
    }

    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    pe.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    // Konsole's markers would make programs expect Konsole's own extensions.
    pe.remove(QStringLiteral("KONSOLE_DBUS_SESSION"));
    pe.remove(QStringLiteral("KONSOLE_VERSION"));
    for (auto it = env.cbegin(); it != env.cend(); ++it) {
        pe.insert(it.key(), it.value());
    }

    QString exe = argv.first();
    if (!exe.contains(QLatin1Char('/'))) {
        exe = QStandardPaths::findExecutable(exe, pe.value(QStringLiteral("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts));
    }
    if (exe.isEmpty() || !QFileInfo(exe).isExecutable()) {
        return fail(QStringLiteral("command not found: %1").arg(argv.first()));
    }

    // Everything the child touches is built before fork: between fork and
    // exec only async-signal-safe calls are allowed.
    const QByteArray program = QFile::encodeName(exe);
    QList<QByteArray> args;
    for (const QString &a : argv) {
        args.append(a.toLocal8Bit());
    }
    QList<QByteArray> envStrings;
    for (const QString &kv : pe.toStringList()) {
        envStrings.append(kv.toLocal8Bit());
    }
    std::vector<char *> cargv;
    for (QByteArray &a : args) {
        cargv.push_back(a.data());
    }
    cargv.push_back(nullptr);
    std::vector<char *> cenvp;
    for (QByteArray &e : envStrings) {
        cenvp.push_back(e.data());
    }
    cenvp.push_back(nullptr);
    const QByteArray cdir = QFile::encodeName(dir);
    const QByteArray home = QFile::encodeName(pe.value(QStringLiteral("HOME")));

    struct winsize ws{};
    ws.ws_row = static_cast<unsigned short>(std::max(1, rows));
    ws.ws_col = static_cast<unsigned short>(std::max(1, cols));
    int fd = -1;
    const pid_t pid = forkpty(&fd, nullptr, nullptr, &ws);
    if (pid < 0) {
        return fail(QStringLiteral("forkpty: %1").arg(QString::fromLocal8Bit(strerror(errno))));
    }
    if (pid == 0) {
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        for (int sig : {SIGCHLD, SIGPIPE, SIGHUP, SIGINT, SIGQUIT, SIGTERM}) {
            signal(sig, SIG_DFL);
        }
        if (cdir.isEmpty() || chdir(cdir.constData()) != 0) {
            if (home.isEmpty() || chdir(home.constData()) != 0) {
                [[maybe_unused]] const int r = chdir("/");
            }
        }
        execve(program.constData(), cargv.data(), cenvp.data());
        static const char msg[] = "x3ro-deck: cannot run the terminal command\r\n";
        [[maybe_unused]] const ssize_t n = ::write(STDOUT_FILENO, msg, sizeof msg - 1);
        _exit(127);
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    m_fd = fd;
    m_pid = pid;
    m_exitCode = -1;
    m_pending.clear();

    m_readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &PtyChannel::onReadable);
    m_writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &PtyChannel::onWritable);
    return true;
}

void PtyChannel::onReadable()
{
    char buf[16384];
    QByteArray data;
    for (;;) {
        const ssize_t n = ::read(m_fd, buf, sizeof buf);
        if (n > 0) {
            data.append(buf, n);
            if (data.size() >= 256 * 1024) {
                break; // let the view paint between large bursts
            }
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        // EOF, or EIO once every holder of the slave side has closed it.
        if (!data.isEmpty()) {
            Q_EMIT received(data);
        }
        hangUp();
        Q_EMIT closed();
        return;
    }
    if (!data.isEmpty()) {
        Q_EMIT received(data);
    }
}

void PtyChannel::write(const QByteArray &bytes)
{
    if (m_fd < 0 || bytes.isEmpty()) {
        return;
    }
    m_pending.append(bytes);
    onWritable();
}

void PtyChannel::onWritable()
{
    while (m_fd >= 0 && !m_pending.isEmpty()) {
        const ssize_t n = ::write(m_fd, m_pending.constData(), static_cast<size_t>(m_pending.size()));
        if (n > 0) {
            m_pending.remove(0, n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break; // EAGAIN: the program is not reading; wait for room
    }
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(!m_pending.isEmpty());
    }
}

void PtyChannel::resize(int rows, int cols)
{
    if (m_fd < 0) {
        return;
    }
    struct winsize ws{};
    ws.ws_row = static_cast<unsigned short>(std::max(1, rows));
    ws.ws_col = static_cast<unsigned short>(std::max(1, cols));
    ioctl(m_fd, TIOCSWINSZ, &ws);
}

QString PtyChannel::foregroundProcessName() const
{
    if (m_fd < 0) {
        return {};
    }
    const pid_t pgrp = tcgetpgrp(m_fd);
    if (pgrp <= 0) {
        return {};
    }
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pgrp));
    if (!comm.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLocal8Bit(comm.readAll()).trimmed();
}

void PtyChannel::hangUp()
{
    delete m_readNotifier;
    m_readNotifier = nullptr;
    delete m_writeNotifier;
    m_writeNotifier = nullptr;
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    if (m_pid > 0) {
        // Closing the master hangs the session up; SIGHUP also reaches a
        // program that ignores the terminal. Attach clients detach on it.
        kill(static_cast<pid_t>(m_pid), SIGHUP);
        reap();
    }
}

void PtyChannel::reap()
{
    if (m_pid <= 0) {
        return;
    }
    // A program usually exits right after the hangup or after closing the
    // terminal, so wait a moment for its status before going asynchronous.
    for (int i = 0; i < 20; ++i) {
        int status = 0;
        const pid_t r = waitpid(static_cast<pid_t>(m_pid), &status, WNOHANG);
        if (r == static_cast<pid_t>(m_pid)) {
            m_exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            m_pid = -1;
            return;
        }
        if (r < 0) {
            m_pid = -1; // reaped elsewhere
            return;
        }
        usleep(1000);
    }
    // Still exiting: collect it shortly so no zombie stays behind.
    const qint64 pid = m_pid;
    m_pid = -1;
    auto *timer = new QTimer;
    timer->setInterval(100);
    auto tries = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, pid, tries] {
        int st = 0;
        const pid_t done = waitpid(static_cast<pid_t>(pid), &st, WNOHANG);
        if (done != 0 || ++*tries > 50) {
            if (done == 0) {
                kill(static_cast<pid_t>(pid), SIGKILL);
                waitpid(static_cast<pid_t>(pid), &st, 0);
            }
            timer->deleteLater();
        }
    });
    timer->start();
}
