/*
    ipc — the one place that knows how dropterm invocations talk to each other.

    A single per-user socket in XDG_RUNTIME_DIR. The terminal listens (with
    QLocalServer); everything else is a client: `dropterm toggle`, and the
    settings process announcing a write.

    The client side is deliberately plain POSIX rather than QLocalSocket, so
    that forwarding a command needs no Qt application object. That is not a
    micro-optimisation: constructing QGuiApplication requires a working
    platform plugin, and Qt calls qFatal() — which aborts — when it cannot
    create one. Building a GUI application just to write six bytes to a socket
    meant `dropterm toggle` dumped core whenever it ran without a usable
    display (over ssh, from a systemd unit, from cron).

    Copyright 2026 ajunca — MIT License
*/

#ifndef IPC_H
#define IPC_H

#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ipc {

// XDG_RUNTIME_DIR is already per-user and mode 0700.
inline std::string socketPath()
{
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    const std::string dir = (runtime && *runtime) ? runtime : "/tmp";
    return dir + "/dropterm.sock";
}

// Returns false when no terminal is listening, which is not an error: the
// caller may be the first invocation, or settings running on its own.
inline bool send(const std::string &command)
{
    const std::string path = socketPath();
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        return false;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(fd); // nothing listening: stale socket, or no terminal running
        return false;
    }

    const char *data = command.data();
    std::size_t left = command.size();
    while (left > 0) {
        const ssize_t n = ::write(fd, data, left);
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        data += n;
        left -= static_cast<std::size_t>(n);
    }

    // Half-close so the peer sees end-of-input and acts before we drop the
    // connection entirely.
    ::shutdown(fd, SHUT_WR);
    ::close(fd);
    return true;
}

} // namespace ipc

#endif // IPC_H
