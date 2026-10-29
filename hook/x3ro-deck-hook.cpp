// x3ro-deck-hook — agent hook that forwards the hook payload to x3ro-deck.
//
// Claude Code (~/.claude/settings.json) and Codex (~/.codex/hooks.json) both
// run it with the same Claude-style JSON on stdin. It writes that JSON,
// unchanged, to the x3ro-deck Unix socket ($XDG_RUNTIME_DIR/x3ro-deck.sock).
// One connection carries one event; closing the connection marks the end of
// the payload.
//
// It always exits 0 and prints nothing, so either agent treats it as a no-op
// hook whether or not x3ro-deck is running.

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main()
{
    std::string payload;
    char buf[65536];
    for (;;) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
        if (n > 0) {
            payload.append(buf, static_cast<size_t>(n));
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    if (payload.empty()) {
        return 0;
    }

    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    std::string path = runtime && *runtime ? std::string(runtime) : "/run/user/" + std::to_string(getuid());
    path += "/x3ro-deck.sock";

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) {
        return 0;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return 0;
    }
    // A stalled x3ro-deck must never stall the agent.
    timeval tv{0, 500000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0) {
        const char *p = payload.data();
        size_t left = payload.size();
        while (left > 0) {
            ssize_t n = send(fd, p, left, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                break;
            }
            p += n;
            left -= static_cast<size_t>(n);
        }
    }
    close(fd);
    return 0;
}
