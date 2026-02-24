/*
    Copyright (C) 2017 Robin Burchell <robin+git@viroteck.net>
    Copyright 2011-2012 Heikki Holstila <heikki.holstila@gmail.com>

    This work is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This work is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this work.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <QDebug>
#include <QTimer>

extern "C" {
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#if defined(Q_OS_LINUX)
#include <pty.h>
#elif defined(Q_OS_MAC)
#include <util.h>
#endif
#include <pwd.h>
#include <stdlib.h>
}

#include <vector>

#include "ptyiface.h"
#include "vtermbridge.h"

void PtyIFace::checkChildStatus()
{
    if (m_childProcessQuit || m_childProcessPid <= 0)
        return;

    int status = 0;
    int ret = waitpid(m_childProcessPid, &status, WNOHANG);
    if (ret == m_childProcessPid) {
        m_childProcessQuit = true;
        m_childProcessPid = 0;

        if (iReadNotifier)
            iReadNotifier->setEnabled(false);

        emit hangupReceived();
    }
}

PtyIFace::PtyIFace(VTermBridge* bridge, const QString& charset,
    const QByteArray& termEnv, const QString& commandOverride,
    QObject* parent)
    : QObject(parent)
    , m_bridge(bridge)
    , iFailed(false)
    , m_childProcessQuit(false)
    , m_childProcessPid(0)
    , iReadNotifier(0)
{
    Q_UNUSED(charset); // libvterm handles UTF-8 internally

    m_childCheckTimer = new QTimer(this);
    m_childCheckTimer->setInterval(500);
    connect(m_childCheckTimer, &QTimer::timeout, this, &PtyIFace::checkChildStatus);

    // Build the command to exec
    QString execCmd = commandOverride;
    if (execCmd.isEmpty()) {
        passwd* pwdstruct = getpwuid(getuid());
        execCmd = QString(pwdstruct->pw_shell);
        execCmd.append(" --login");
    }

    QStringList execParts = execCmd.split(' ', Qt::SkipEmptyParts);
    if (execParts.length() == 0) {
        iFailed = true;
        return;
    }

    // Build argv
    std::vector<std::vector<char>> argStorage(execParts.length());
    std::vector<char*> argv(execParts.length() + 1);
    for (int i = 0; i < execParts.length(); i++) {
        QByteArray ba = execParts.at(i).toLatin1();
        argStorage[i].assign(ba.data(), ba.data() + ba.length() + 1);
        argv[i] = argStorage[i].data();
    }
    argv[execParts.length()] = nullptr;

    // Create PTY pair
    int masterFd, slaveFd;
    if (openpty(&masterFd, &slaveFd, NULL, NULL, NULL) == -1) {
        qWarning("openpty failed");
        iFailed = true;
        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        qWarning("fork failed: %s", strerror(errno));
        close(masterFd);
        close(slaveFd);
        iFailed = true;
        return;
    }

    if (pid == 0) {
        // Child
        close(masterFd);
        setsid();
        ioctl(slaveFd, TIOCSCTTY, 0);  // set controlling terminal (needed for sudo, ssh, etc.)
        dup2(slaveFd, 0);
        dup2(slaveFd, 1);
        dup2(slaveFd, 2);
        if (slaveFd > 2)
            close(slaveFd);
        setenv("TERM", termEnv.constData(), 1);
        const char* home = getenv("HOME");
        if (home && chdir(home) != 0)
            {} // best-effort, fall through to exec in inherited cwd
        execvp(argv[0], argv.data());
        _exit(127);
    }

    // Parent
    close(slaveFd);

    m_childProcessPid = pid;
    iMasterFd = masterFd;

    if (!m_bridge || m_childProcessQuit) {
        iFailed = true;
        qWarning("PtyIFace: null bridge pointer");
        return;
    }

    resize(m_bridge->rows(), m_bridge->columns());

    iReadNotifier = new QSocketNotifier(iMasterFd, QSocketNotifier::Read, this);
    connect(iReadNotifier, &QSocketNotifier::activated, this, &PtyIFace::readActivated);

    fcntl(iMasterFd, F_SETFL, O_NONBLOCK);

    m_childCheckTimer->start();
}

PtyIFace::~PtyIFace()
{
    if (iReadNotifier) {
        iReadNotifier->setEnabled(false);
        delete iReadNotifier;
        iReadNotifier = nullptr;
    }

    if (iMasterFd >= 0) {
        close(iMasterFd);
        iMasterFd = -1;
    }

    if (!m_childProcessQuit && m_childProcessPid > 0) {
        kill(m_childProcessPid, SIGHUP);
        kill(m_childProcessPid, SIGTERM);
    }
}

void PtyIFace::readActivated()
{
    if (m_childProcessQuit)
        return;

    char buf[4096];
    int ret = read(iMasterFd, buf, sizeof(buf));
    if (ret > 0) {
        m_pendingRawData.append(buf, ret);
        emit dataAvailable();
    } else if (ret == 0 || (ret == -1 && errno != EAGAIN && errno != EINTR)) {
        if (iReadNotifier)
            iReadNotifier->setEnabled(false);
        checkChildStatus();
    }
}

void PtyIFace::resize(int rows, int columns)
{
    if (m_childProcessQuit)
        return;

    winsize winp;
    winp.ws_col = columns;
    winp.ws_row = rows;
    ioctl(iMasterFd, TIOCSWINSZ, &winp);
}

void PtyIFace::writeRawTerm(const QByteArray& data)
{
    if (m_childProcessQuit)
        return;

    int ret = write(iMasterFd, data.constData(), data.size());
    if (ret != data.size())
        qDebug() << "write error!";
}
