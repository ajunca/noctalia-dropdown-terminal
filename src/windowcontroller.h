/*
    WindowController — the QML's handle on the layer-shell window.

    The QML owns the open/close animation, so hiding is a two-step handshake:
    requestHide() clears `opened` (the QML animates out) and the QML calls
    hideCompleted() when the animation has finished, which actually unmaps the
    window. Hiding directly would cut the animation off.

    Copyright 2026 ajunca — MIT License
*/

#ifndef WINDOWCONTROLLER_H
#define WINDOWCONTROLLER_H

#include <QCoreApplication>
#include <QObject>
#include <QProcess>
#include <QQuickView>

class WindowController : public QObject
{
    Q_OBJECT

    // Intent, not visibility: true from the moment we start opening until the
    // moment we start closing. The QML animates on this.
    Q_PROPERTY(bool opened READ opened NOTIFY openedChanged)

public:
    explicit WindowController(QQuickView *view, QObject *parent = nullptr)
        : QObject(parent)
        , m_view(view)
    {
    }

    [[nodiscard]] bool opened() const { return m_opened; }

    Q_INVOKABLE void show()
    {
        m_view->show();
        m_view->requestActivate();
        setOpened(true);
    }

    Q_INVOKABLE void requestHide()
    {
        if (!m_opened) {
            m_view->hide(); // already closed, or never animated in
            return;
        }
        setOpened(false);
    }

    Q_INVOKABLE void hideCompleted()
    {
        if (!m_opened) {
            m_view->hide();
        }
    }

    Q_INVOKABLE void toggle()
    {
        if (m_view->isVisible() && m_opened) {
            requestHide();
        } else {
            show();
        }
    }

    // Settings runs as a separate invocation on purpose: this process has
    // QT_WAYLAND_SHELL_INTEGRATION=layer-shell set, and LayerShellQt turns
    // *every* window in the process into a layer surface with no fallback, so
    // an in-process settings window could never be an ordinary one.
    Q_INVOKABLE void openSettings()
    {
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                {QStringLiteral("settings")});
        requestHide();
    }

Q_SIGNALS:
    void openedChanged();

private:
    void setOpened(bool opened)
    {
        if (m_opened == opened) {
            return;
        }
        m_opened = opened;
        Q_EMIT openedChanged();
    }

    QQuickView *const m_view;
    bool m_opened = false;
};

#endif // WINDOWCONTROLLER_H
