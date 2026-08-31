/*
    ipc — the one place that knows how the two dropterm processes talk.

    A single per-user socket in XDG_RUNTIME_DIR. The terminal listens; every
    other invocation (`dropterm toggle`, and the settings process announcing a
    write) is a client.

    Copyright 2026 ajunca — MIT License
*/

#ifndef IPC_H
#define IPC_H

#include <QDir>
#include <QLocalSocket>
#include <QString>

namespace ipc {

// XDG_RUNTIME_DIR is already per-user and mode 0700.
inline QString socketPath()
{
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty()) {
        runtime = QDir::tempPath();
    }
    return QDir(runtime).filePath(QStringLiteral("dropterm.sock"));
}

// Returns false when no terminal is listening, which is not an error: the
// caller may simply be the first invocation, or settings running on its own.
inline bool send(const QByteArray &command)
{
    QLocalSocket sock;
    sock.connectToServer(socketPath());
    if (!sock.waitForConnected(300)) {
        return false;
    }
    sock.write(command);
    sock.flush();
    sock.waitForBytesWritten(300);
    sock.disconnectFromServer();
    return true;
}

} // namespace ipc

#endif // IPC_H
