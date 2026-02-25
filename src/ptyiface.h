/*
    PtyIFace — PTY master-side interface for spawning and communicating
    with a child shell process.

    Copyright 2026 ajunca — MIT License
*/

#ifndef PTYIFACE_H
#define PTYIFACE_H

#include <QByteArray>
#include <QObject>
#include <QSize>

class QSocketNotifier;
class VTermBridge;

class PtyIFace : public QObject {
    Q_OBJECT
public:
    explicit PtyIFace(VTermBridge* bridge, const QString& charset,
        const QByteArray& termEnv, const QString& command,
        QObject* parent = nullptr);
    ~PtyIFace() override;

    bool failed() const { return m_failed; }

    QByteArray takeRawData()
    {
        QByteArray out;
        out.swap(m_readBuf);
        return out;
    }

    void writeRawTerm(const QByteArray& data);
    void resize(int rows, int columns);

signals:
    void dataAvailable();
    void hangupReceived();

private slots:
    void onReadReady();

private:
    Q_DISABLE_COPY(PtyIFace)

    void spawnShell(const QByteArray& termEnv, const QString& command);
    void cleanup();

    VTermBridge* m_bridge = nullptr;
    int m_masterFd = -1;
    pid_t m_childPid = -1;
    bool m_failed = false;
    bool m_childExited = false;

    QSocketNotifier* m_readNotifier = nullptr;
    QByteArray m_readBuf;
};

#endif // PTYIFACE_H
