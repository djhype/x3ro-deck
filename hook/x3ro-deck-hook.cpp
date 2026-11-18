// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// x3ro-deck-hook — agent hook that forwards the hook payload to x3ro-deck.
//
// Claude Code (~/.claude/settings.json) and Codex (~/.codex/hooks.json) both
// run it with the same Claude-style JSON on stdin. It writes that JSON,
// unchanged, to the x3ro-deck Unix socket, or on Windows its named pipe
// (see common/x3ro-deck-socket.h).
// One connection carries one event; closing the connection marks the end of
// the payload.
//
// It always exits 0 and prints nothing, so either agent treats it as a no-op
// hook whether or not x3ro-deck is running.

#include <x3ro-deck-socket.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>

int main()
{
    std::string payload;
    char buf[65536];
    for (;;) {
        const size_t n = std::fread(buf, 1, sizeof buf, stdin);
        if (n == 0) {
            break;
        }
        payload.append(buf, n);
    }
    if (payload.empty()) {
        return 0;
    }
    const std::string &path = x3ro_deck::hookSocketPath().path;
    const std::wstring name(path.begin(), path.end());
    // A busy pipe gets a short wait; a missing one (the app is not running)
    // ends the hook at once.
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 2; ++attempt) {
        pipe = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(name.c_str(), 500)) {
            break;
        }
    }
    if (pipe == INVALID_HANDLE_VALUE) {
        return 0;
    }
    const char *p = payload.data();
    size_t left = payload.size();
    while (left > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe, p, DWORD(left), &written, nullptr) || written == 0) {
            break;
        }
        p += written;
        left -= written;
    }
    CloseHandle(pipe);
    return 0;
}
#else
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

    // An over-long path cannot be connected to; the app logs it.
    const x3ro_deck::SocketPath sock = x3ro_deck::hookSocketPath();
    if (sock.tooLong()) {
        return 0;
    }
    const std::string &path = sock.path;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    int fd = x3ro_deck::unixStreamSocket();
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
            ssize_t n = send(fd, p, left, x3ro_deck::kSendNoSignal);
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
#endif
