/*
    PtyIFace — PTY master-side interface for spawning and communicating
    with a child shell process.

    Copyright 2026 ajunca — MIT License
*/

#include "ptyiface.h"
#include "vtermbridge.h"

#include <QDebug>
#include <QSocketNotifier>

extern "C" {
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <pwd.h>
#if defined(Q_OS_LINUX)
#include <pty.h>
#elif defined(Q_OS_MACOS)
#include <util.h>
#endif
}

PtyIFace::PtyIFace(VTermBridge* bridge, const QString& /*charset*/,
    const QByteArray& termEnv, const QString& command,
    QObject* parent)
    : QObject(parent)
    , m_bridge(bridge)
{
    spawnShell(termEnv, command);
}

PtyIFace::~PtyIFace()
{
    cleanup();
}

void PtyIFace::spawnShell(const QByteArray& termEnv, const QString& command)
{
    // Determine what to execute
    QString shell = command;
    if (shell.isEmpty()) {
        struct passwd* pw = getpwuid(getuid());
        if (!pw || !pw->pw_shell) {
            qWarning("PtyIFace: cannot determine user shell");
            m_failed = true;
            return;
        }
        shell = QString::fromLocal8Bit(pw->pw_shell) + " --login";
    }

    QStringList parts = shell.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        m_failed = true;
        return;
    }

    // Build null-terminated argv
    QVector<QByteArray> argBytes;
    argBytes.reserve(parts.size());
    for (const QString& p : parts)
        argBytes.append(p.toLocal8Bit());

    QVector<char*> argv(argBytes.size() + 1);
    for (int i = 0; i < argBytes.size(); ++i)
        argv[i] = argBytes[i].data();
    argv[argBytes.size()] = nullptr;

    // Allocate PTY pair
    int master = -1, slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, nullptr) < 0) {
        qWarning("PtyIFace: openpty: %s", strerror(errno));
        m_failed = true;
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        qWarning("PtyIFace: fork: %s", strerror(errno));
        close(master);
        close(slave);
        m_failed = true;
        return;
    }

    if (pid == 0) {
        // ── Child process ──
        close(master);

        // New session + controlling terminal
        setsid();
        ioctl(slave, TIOCSCTTY, 0);

        // Redirect stdio to slave PTY
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO)
            close(slave);

        // Environment
        setenv("TERM", termEnv.isEmpty() ? "xterm-256color" : termEnv.constData(), 1);

        // Start in home directory
        const char* home = getenv("HOME");
        if (home)
            (void)chdir(home);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    // ── Parent process ──
    close(slave);
    m_masterFd = master;
    m_childPid = pid;

    // Non-blocking reads
    fcntl(m_masterFd, F_SETFL, fcntl(m_masterFd, F_GETFL) | O_NONBLOCK);

    // Set initial terminal size
    if (m_bridge)
        resize(m_bridge->rows(), m_bridge->columns());

    // Watch for data from the child
    m_readNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &PtyIFace::onReadReady);
}

void PtyIFace::onReadReady()
{
    if (m_childExited)
        return;

    // Drain the kernel buffer in one notifier fire (standard non-blocking
    // I/O idiom). BATCH_CAP keeps the GUI thread responsive under heavy
    // output — when more data is pending, the level-triggered
    // QSocketNotifier fires again on the next event loop iteration,
    // naturally yielding for paint/input between batches.
    constexpr int BATCH_CAP = 256 * 1024;  // 256 KiB
    char buf[16384];
    bool gotData = false;
    int batchSize = 0;

    while (batchSize < BATCH_CAP) {
        ssize_t n = read(m_masterFd, buf, sizeof(buf));
        if (n > 0) {
            m_readBuf.append(buf, static_cast<int>(n));
            batchSize += n;
            gotData = true;
        } else if (n == 0) {
            // EOF — child closed the PTY
            if (m_readNotifier)
                m_readNotifier->setEnabled(false);
            if (m_childPid > 0) {
                int status = 0;
                waitpid(m_childPid, &status, 0);
                m_childPid = -1;
            }
            m_childExited = true;
            if (gotData)
                emit dataAvailable();
            emit hangupReceived();
            return;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Kernel buffer drained — wait for next notifier fire
            break;
        } else if (errno == EINTR) {
            // Interrupted by signal, retry the read
            continue;
        } else {
            // Real error
            qWarning("PtyIFace: read: %s", strerror(errno));
            if (m_readNotifier)
                m_readNotifier->setEnabled(false);
            m_childExited = true;
            if (gotData)
                emit dataAvailable();
            emit hangupReceived();
            return;
        }
    }

    if (gotData)
        emit dataAvailable();
}

void PtyIFace::writeRawTerm(const QByteArray& data)
{
    if (m_masterFd < 0 || m_childExited || data.isEmpty())
        return;

    ssize_t written = write(m_masterFd, data.constData(), data.size());
    if (written < 0 && errno != EAGAIN)
        qWarning("PtyIFace: write: %s", strerror(errno));
}

void PtyIFace::resize(int rows, int columns)
{
    if (m_masterFd < 0 || m_childExited)
        return;

    struct winsize ws = {};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(columns);
    ioctl(m_masterFd, TIOCSWINSZ, &ws);
}

void PtyIFace::cleanup()
{
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        delete m_readNotifier;
        m_readNotifier = nullptr;
    }

    if (m_masterFd >= 0) {
        close(m_masterFd);
        m_masterFd = -1;
    }

    // Signal child to exit if still running
    if (m_childPid > 0 && !m_childExited) {
        kill(m_childPid, SIGHUP);
        // Brief wait, then force
        int status = 0;
        if (waitpid(m_childPid, &status, WNOHANG) == 0) {
            kill(m_childPid, SIGTERM);
            waitpid(m_childPid, &status, 0);
        }
        m_childPid = -1;
    }
}
