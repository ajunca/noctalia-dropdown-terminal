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
#include <spawn.h>
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

#include <string>
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

    // Build argv for posix_spawn
    std::vector<std::vector<char>> argStorage(execParts.length());
    std::vector<char*> argv(execParts.length() + 1);
    for (int i = 0; i < execParts.length(); i++) {
        QByteArray ba = execParts.at(i).toLatin1();
        argStorage[i].assign(ba.data(), ba.data() + ba.length() + 1);
        argv[i] = argStorage[i].data();
    }
    argv[execParts.length()] = nullptr;

    // Build environment with TERM set
    extern char** environ;
    std::vector<std::string> envStorage;
    std::vector<char*> envp;
    bool termSet = false;
    for (char** e = environ; *e; ++e) {
        if (strncmp(*e, "TERM=", 5) == 0) {
            envStorage.push_back(std::string("TERM=") + termEnv.constData());
            envp.push_back(const_cast<char*>(envStorage.back().c_str()));
            termSet = true;
        } else {
            envp.push_back(*e);
        }
    }
    if (!termSet) {
        envStorage.push_back(std::string("TERM=") + termEnv.constData());
        envp.push_back(const_cast<char*>(envStorage.back().c_str()));
    }
    envp.push_back(nullptr);

    // Create PTY pair using openpty (no fork)
    int masterFd, slaveFd;
    if (openpty(&masterFd, &slaveFd, NULL, NULL, NULL) == -1) {
        qWarning("openpty failed");
        iFailed = true;
        return;
    }

    // Get slave device path — reopening it after setsid() sets controlling terminal
    const char* slaveName = ptsname(masterFd);
    if (!slaveName) {
        qWarning("ptsname failed");
        close(masterFd);
        close(slaveFd);
        iFailed = true;
        return;
    }
    std::string slaveNameStr(slaveName); // copy before slaveFd is closed

    // Use posix_spawn to avoid fork() in NVIDIA/Wayland context
    // File actions run AFTER setsid (POSIX_SPAWN_SETSID), so opening
    // the slave by name makes it the controlling terminal automatically.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, masterFd);
    posix_spawn_file_actions_addclose(&actions, slaveFd);
    posix_spawn_file_actions_addopen(&actions, 0, slaveNameStr.c_str(), O_RDWR, 0);
    posix_spawn_file_actions_adddup2(&actions, 0, 1);
    posix_spawn_file_actions_adddup2(&actions, 0, 2);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

    pid_t pid;
    int spawnRet = posix_spawnp(&pid, argv[0], &actions, &attr, argv.data(), envp.data());

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(slaveFd);

    if (spawnRet != 0) {
        qWarning("posix_spawn failed: %s", strerror(spawnRet));
        close(masterFd);
        iFailed = true;
        return;
    }

    iPid = pid;
    iMasterFd = masterFd;
    m_childProcessPid = iPid;

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

    if (!m_childProcessQuit && iPid > 0) {
        kill(iPid, SIGHUP);
        kill(iPid, SIGTERM);
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
