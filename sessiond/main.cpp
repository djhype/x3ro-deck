// x3ro-deck-sessiond: keeps agent sessions alive outside the GUI. See
// Daemon.h for what it does and SessiondProtocol.h for how to talk to it.
//
//   x3ro-deck-sessiond [--socket PATH] [--detach]
//
// --detach forks into the background and returns once the daemon listens
// (exit 0, also when one is already running), so callers can start it and
// connect right away.

#include "Daemon.h"

#include <QCoreApplication>

#include <x3ro-deck-socket.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
int usage(FILE *out, int code)
{
    std::fputs("usage: x3ro-deck-sessiond [--socket PATH] [--detach]\n", out);
    return code;
}

void report(int fd, char c)
{
    if (fd >= 0) {
        [[maybe_unused]] const ssize_t n = write(fd, &c, 1);
        close(fd);
    }
}
} // namespace

int main(int argc, char *argv[])
{
    bool detach = false;
    x3ro_deck::SocketPath sock = x3ro_deck::sessiondSocketPath();
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--detach") == 0) {
            detach = true;
        } else if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            sock = x3ro_deck::SocketPath{argv[++i], {}};
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            return usage(stdout, 0);
        } else {
            return usage(stderr, 2);
        }
    }
    if (sock.tooLong()) {
        std::fprintf(stderr, "x3ro-deck-sessiond: socket path %s is over the platform limit of %zu bytes\n", sock.path.c_str(),
                     x3ro_deck::kMaxSocketPath);
        return 1;
    }
    if (!sock.privateDir.empty()) {
        // /tmp is shared: a directory someone else made could hand them our sessions.
        const char *dir = sock.privateDir.c_str();
        mkdir(dir, 0700);
        struct stat st{};
        if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 077) != 0) {
            std::fprintf(stderr, "x3ro-deck-sessiond: %s is not a private directory owned by this user\n", dir);
            return 1;
        }
    }

    // The ready pipe tells the waiting parent: '1' listening, 'a' another
    // daemon owns the socket; EOF means it failed.
    int ready = -1;
    if (detach) {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) != 0) {
            std::perror("x3ro-deck-sessiond: pipe");
            return 1;
        }
        const pid_t child = fork();
        if (child < 0) {
            std::perror("x3ro-deck-sessiond: fork");
            return 1;
        }
        if (child > 0) {
            close(fds[1]);
            char c = 0;
            ssize_t n = 0;
            do {
                n = read(fds[0], &c, 1);
            } while (n < 0 && errno == EINTR);
            waitpid(child, nullptr, 0);
            return n == 1 && (c == '1' || c == 'a') ? 0 : 1;
        }
        close(fds[0]);
        ready = fds[1];
        // A new session without a controlling terminal, and a second fork so
        // the daemon can never acquire one: it outlives the GUI and the
        // terminal that started it.
        setsid();
        const pid_t daemon = fork();
        if (daemon != 0) {
            _exit(daemon < 0 ? 1 : 0);
        }
        [[maybe_unused]] const int r = chdir("/");
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("x3ro-deck-sessiond"));
    Daemon daemon(QString::fromStdString(sock.path));
    QString error;
    switch (daemon.listen(&error)) {
    case Daemon::ListenResult::Running:
        report(ready, 'a');
        std::fprintf(stderr, "x3ro-deck-sessiond: already running on %s\n", sock.path.c_str());
        return 0;
    case Daemon::ListenResult::Failed:
        std::fprintf(stderr, "x3ro-deck-sessiond: cannot listen on %s: %s\n", sock.path.c_str(), qPrintable(error));
        return 1;
    case Daemon::ListenResult::Listening:
        break;
    }
    if (detach) {
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, STDIN_FILENO);
            dup2(null, STDOUT_FILENO);
            dup2(null, STDERR_FILENO);
            if (null > STDERR_FILENO) {
                close(null);
            }
        }
        report(ready, '1');
    }
    return app.exec();
}
