/*
    WindowController — the QML's handle on the layer-shell window.

    The QML decides *when* something should be dismissed (a click landed on the
    backdrop); this decides *what that means*. Exposed as a typed object rather
    than wiring a QML signal with a string-based connect.

    Copyright 2026 ajunca — MIT License
*/

#ifndef WINDOWCONTROLLER_H
#define WINDOWCONTROLLER_H

#include <QObject>
#include <QQuickView>

class WindowController : public QObject
{
    Q_OBJECT

public:
    explicit WindowController(QQuickView *view, QObject *parent = nullptr)
        : QObject(parent)
        , m_view(view)
    {
    }

    Q_INVOKABLE void hide() { m_view->hide(); }

    Q_INVOKABLE void show()
    {
        m_view->show();
        m_view->requestActivate();
    }

    Q_INVOKABLE void toggle()
    {
        m_view->isVisible() ? hide() : show();
    }

private:
    QQuickView *const m_view;
};

#endif // WINDOWCONTROLLER_H
