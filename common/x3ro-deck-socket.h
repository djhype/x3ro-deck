#pragma once

// The hook socket's path, shared by the app (AgentBus) and x3ro-deck-hook,
// and the session daemon's (x3ro-deck-sessiond). Plain C++ and libc only,
// because the hook links nothing else.
//
// Resolution:
//   1. $X3RO_DECK_SOCKET, so a test instance and its hooks can pair up
//      without touching a running app's socket.
//   2. Linux: $XDG_RUNTIME_DIR/x3ro-deck.sock.
//      macOS: $TMPDIR/x3ro-deck.sock; $TMPDIR is per-user but can be long.
//   3. Otherwise /tmp/x3ro-deck-<uid>/x3ro-deck.sock. The app creates that
//      directory 0700 and refuses one owned by another user.

#include <cstddef>
#include <cstdlib>
#include <string>

#include <sys/un.h>
#include <unistd.h>

namespace x3ro_deck
{

// sun_path is 108 bytes on Linux and 104 on macOS, NUL included.
constexpr std::size_t kMaxSocketPath = sizeof(sockaddr_un::sun_path) - 1;

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
    r.privateDir = "/tmp/x3ro-deck-" + std::to_string(getuid());
    r.path = r.privateDir + "/" + file;
    return r;
}

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

} // namespace x3ro_deck
