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

#ifndef PTYIFACE_H
#define PTYIFACE_H

#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QSocketNotifier>
#include <QTimer>

class VTermBridge;

class PtyIFace : public QObject {
    Q_OBJECT
public:
    explicit PtyIFace(VTermBridge* bridge, const QString& charset,
        const QByteArray& terminalEnv, const QString& commandOverride,
        QObject* parent);
    virtual ~PtyIFace();

    bool failed() { return iFailed; }

    // Raw byte interface (for libvterm)
    QByteArray takeRawData()
    {
        QByteArray tmp;
        tmp.swap(m_pendingRawData);
        return tmp;
    }

    void writeRawTerm(const QByteArray& data);

    // Resize PTY
    void resize(int rows, int columns);

private slots:
    void readActivated();
    void checkChildStatus();

signals:
    void dataAvailable();
    void hangupReceived();

private:
    Q_DISABLE_COPY(PtyIFace)

    VTermBridge* m_bridge;
    int iMasterFd;
    bool iFailed;
    bool m_childProcessQuit;
    int m_childProcessPid;

    QSocketNotifier* iReadNotifier;
    QTimer* m_childCheckTimer;

    QByteArray m_pendingRawData;
};

#endif // PTYIFACE_H
