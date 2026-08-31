/*
    WindowController — the QML's handle on the layer-shell window.

    Hiding is deferred so the close animation can play: requestHide() clears
    `opened` (the QML animates the panel back up) and the window is unmapped
    once the animation's duration has elapsed.

    The delay is owned here rather than driven by a callback from the QML
    animation. A callback only fires if the animation actually runs, and it
    silently does not when the animated value happens not to change — which
    leaves `opened` false while the window stays mapped, and the dropdown then
    never closes. Timing it here has one code path and cannot get stuck.

    Copyright 2026 ajunca — MIT License
*/

#ifndef WINDOWCONTROLLER_H
#define WINDOWCONTROLLER_H

#include <QCoreApplication>
#include <QObject>
#include <QProcess>
#include <QQuickView>
#include <QTimer>

#include "settings.h"

class WindowController : public QObject
{
    Q_OBJECT

    // Intent, not visibility: true from the moment we start opening until the
    // moment we start closing. The QML animates on this.
    Q_PROPERTY(bool opened READ opened NOTIFY openedChanged)

public:
    WindowController(QQuickView *view, Settings *settings, QObject *parent = nullptr)
        : QObject(parent)
        , m_view(view)
        , m_settings(settings)
    {
        m_hideTimer.setSingleShot(true);
        connect(&m_hideTimer, &QTimer::timeout, this, [this] { m_view->hide(); });
    }

    [[nodiscard]] bool opened() const { return m_opened; }

    Q_INVOKABLE void show()
    {
        m_hideTimer.stop(); // reopening mid-close must not be unmapped later
        m_view->show();
        m_view->requestActivate();
        setOpened(true);
    }

    Q_INVOKABLE void requestHide()
    {
        if (!m_opened) {
            m_hideTimer.stop();
            m_view->hide();
            return;
        }
        setOpened(false);
        // Small margin so the last animation frame is presented before the
        // surface goes away.
        m_hideTimer.start(m_settings->animationMs() + 30);
    }

    // Visibility is not consulted: `opened` is the authority, and the two
    // differ for as long as the close animation is playing.
    Q_INVOKABLE void toggle()
    {
        if (m_opened) {
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
    Settings *const m_settings;
    QTimer m_hideTimer;
    bool m_opened = false;
};

#endif // WINDOWCONTROLLER_H
