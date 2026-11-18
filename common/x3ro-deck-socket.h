// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The hook socket's path, shared by the app (AgentBus) and x3ro-deck-hook,
// the session daemon's (x3ro-deck-sessiond) and the app's single-instance one. Plain C++ and libc only,
// because the hook links nothing else.
//
// Resolution:
//   1. $X3RO_DECK_SOCKET, so a test instance and its hooks can pair up
//      without touching a running app's socket.
//   2. Linux: $XDG_RUNTIME_DIR/x3ro-deck.sock.
//      macOS: $TMPDIR/x3ro-deck.sock; $TMPDIR is per-user but can be long.
//   3. Otherwise /tmp/x3ro-deck-<uid>/x3ro-deck.sock. The app creates that
//      directory 0700 and refuses one owned by another user.
//   Windows: the named pipe \\.\pipe\x3ro-deck-<USERNAME> (and so on).

#include <cstddef>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <cstdlib>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace x3ro_deck
{

#if defined(_WIN32)
// Named pipes take names up to 256 characters.
constexpr std::size_t kMaxSocketPath = 256;
#else
// sun_path is 108 bytes on Linux and 104 on macOS, NUL included.
constexpr std::size_t kMaxSocketPath = sizeof(sockaddr_un::sun_path) - 1;
#endif

struct SocketPath
{
    std::string path;
    // Set when `path` lives in the /tmp fallback directory, which the app
    // creates and checks before listening.
    std::string privateDir;
    bool tooLong() const { return path.size() > kMaxSocketPath; }
};

inline std::string envVar(const char *name)
{
    const char *v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// `file` lives in the per-user runtime directory unless `overrideVar` names
// a path.
inline SocketPath socketPathFor(const char *overrideVar, const std::string &file)
{
    SocketPath r;
    if (std::string over = envVar(overrideVar); !over.empty()) {
        r.path = over;
        return r;
    }
#if defined(_WIN32)
    // A named pipe per user; QLocalServer and QLocalSocket take the full name.
    std::string user = envVar("USERNAME");
    if (user.empty()) {
        user = "user";
    }
    const std::string base = file.substr(0, file.rfind(".sock"));
    r.path = "\\\\.\\pipe\\" + base + "-" + user;
    return r;
#endif
#if defined(__APPLE__)
    std::string dir = envVar("TMPDIR");
#else
    std::string dir = envVar("XDG_RUNTIME_DIR");
#endif
    while (dir.size() > 1 && dir.back() == '/') {
        dir.pop_back();
    }
    if (!dir.empty() && dir.size() + 1 + file.size() <= kMaxSocketPath) {
        r.path = dir + "/" + file;
        return r;
    }
#if !defined(_WIN32)
    r.privateDir = "/tmp/x3ro-deck-" + std::to_string(getuid());
    r.path = r.privateDir + "/" + file;
#endif
    return r;
}

#if !defined(_WIN32)
// pipe2, SOCK_CLOEXEC and MSG_NOSIGNAL are Linux (and BSD) only; macOS
// gets the same effect with fcntl and SO_NOSIGPIPE.
inline int cloexecPipe(int fds[2], bool nonblock)
{
#if defined(__linux__)
    return pipe2(fds, O_CLOEXEC | (nonblock ? O_NONBLOCK : 0));
#else
    if (pipe(fds) != 0) {
        return -1;
    }
    for (int i = 0; i < 2; ++i) {
        fcntl(fds[i], F_SETFD, FD_CLOEXEC);
        if (nonblock) {
            fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
        }
    }
    return 0;
#endif
}

// A Unix stream socket that is closed on exec and never raises SIGPIPE.
inline int unixStreamSocket()
{
#if defined(SOCK_CLOEXEC)
    return socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        fcntl(fd, F_SETFD, FD_CLOEXEC);
#if defined(SO_NOSIGPIPE)
        const int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    }
    return fd;
#endif
}

#if defined(MSG_NOSIGNAL)
constexpr int kSendNoSignal = MSG_NOSIGNAL;
#else
constexpr int kSendNoSignal = 0; // SO_NOSIGPIPE is set on the socket instead
#endif
#endif

inline SocketPath hookSocketPath()
{
    return socketPathFor("X3RO_DECK_SOCKET", "x3ro-deck.sock");
}

// x3ro-deck-sessiond's socket, resolved the same way with its own override,
// $X3RO_DECK_SESSIOND_SOCKET.
inline SocketPath sessiondSocketPath()
{
    return socketPathFor("X3RO_DECK_SESSIOND_SOCKET", "x3ro-deck-sessiond.sock");
}

// The running app's single-instance socket (SingleInstance), with
// $X3RO_DECK_INSTANCE_SOCKET as its override.
inline SocketPath instanceSocketPath()
{
    return socketPathFor("X3RO_DECK_INSTANCE_SOCKET", "x3ro-deck-instance.sock");
}

} // namespace x3ro_deck
