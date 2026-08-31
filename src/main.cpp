/*
    dropterm — standalone Yakuake-style dropdown terminal.

    Owns its own wlr-layer-shell surface via LayerShellQt instead of being
    embedded in a shell's panel slot. The terminal itself (TextRender,
    VTermBridge, PtyIFace) is unchanged from the noctalia plugin — only the
    window ownership moved here.

    Two run modes, deliberately separate processes:
      terminal  — the layer-shell dropdown (default)
      settings  — an ordinary window (`dropterm settings`)
    See selectShellIntegration() for why they cannot share a process.

    Copyright 2026 ajunca — MIT License
*/

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQuickView>
#include <QSurfaceFormat>

#include <LayerShellQt/window.h>

#include "settings.h"
#include "windowcontroller.h"

namespace {

// One socket per user; XDG_RUNTIME_DIR is already per-user and mode 0700.
QString socketPath()
{
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty()) {
        runtime = QDir::tempPath();
    }
    return QDir(runtime).filePath(QStringLiteral("dropterm.sock"));
}

// The command has to be known before QGuiApplication exists, because the
// Wayland shell integration is chosen while the platform plugin is built.
QByteArray peekCommand(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (!arg.startsWith('-')) {
            return arg;
        }
    }
    return QByteArrayLiteral("toggle");
}

// LayerShellQt's integration returns a layer surface for *every* window in the
// process, with no xdg-shell fallback, so this is a whole-process decision --
// which is exactly why settings is a separate invocation rather than a second
// window here.
//
// Terminal: LayerShellQt::Shell::useLayerShell() is deprecated as "not needed
// since Qt 6.5", but that is wrong. Without the variable the process starts,
// loads its QML and runs while never mapping a surface at all, and reports no
// error. An explicit override from the environment still wins.
//
// Settings: clear it, so the value inherited from the terminal that spawned us
// cannot turn an ordinary window into a layer surface.
void selectShellIntegration(bool wantsLayerShell)
{
    if (wantsLayerShell) {
        if (!qEnvironmentVariableIsSet("QT_WAYLAND_SHELL_INTEGRATION")) {
            qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");
        }
    } else {
        qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
    }
}

// Hand the command to an already-running terminal, if there is one.
bool sendToRunningInstance(const QByteArray &command)
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

int runSettings(QGuiApplication &app)
{
    Settings settings;

    QQuickView view;
    view.setTitle(QStringLiteral("dropterm settings"));
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setInitialProperties({
        {QStringLiteral("settings"), QVariant::fromValue(static_cast<QObject *>(&settings))},
    });
    view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/dropterm/SettingsWindow.qml")));
    if (view.status() == QQuickView::Error) {
        qCritical("dropterm: failed to load settings QML");
        return 1;
    }
    view.setMinimumSize(QSize(380, 340));
    view.resize(460, 430);
    view.show();
    return app.exec();
}

int runTerminal(QGuiApplication &app, const QByteArray &command)
{
    // With a terminal already running this invocation is only a remote control.
    if (sendToRunningInstance(command)) {
        return 0;
    }
    if (command == "hide") {
        return 0; // nothing running, nothing to hide
    }

    // The window is hidden rather than closed when dismissed, but a compositor
    // config reload can dismiss the surface outright. Without this the daemon
    // would quit at that point and the keybind would silently stop working.
    app.setQuitOnLastWindowClosed(false);

    Settings settings;

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setColor(Qt::transparent);

    WindowController controller(&view);
    view.setInitialProperties({
        {QStringLiteral("settings"), QVariant::fromValue(static_cast<QObject *>(&settings))},
        {QStringLiteral("controller"), QVariant::fromValue(static_cast<QObject *>(&controller))},
    });

    // ── Layer surface ───────────────────────────────────────────────
    // The surface spans the whole usable output rather than just the terminal.
    // That is deliberate, and it is what the host shell used to do for us with
    // its own full-output backdrop layer: a surface covering only the terminal
    // is never told about clicks landing elsewhere, so outside-click dismissal
    // cannot be implemented from inside it.
    //
    // Anchoring all four edges with exclusiveZone(0) makes the compositor size
    // us to the output minus everyone else's exclusive zones, so we begin
    // exactly at the bar's lower edge with no hardcoded bar height. The QML
    // animates the panel down from above its own top edge; because a surface is
    // clipped to its own bounds, the panel emerges from under the bar and can
    // never draw over it.
    //
    // Exclusive keyboard interactivity is the other half. With on-demand,
    // focus-follows-mouse hands the keyboard to whatever the pointer drifts
    // over, so typing would silently go elsewhere. Exclusive keeps it here
    // while the terminal is mapped; compositor keybinds still take priority.
    LayerShellQt::Window *layer = LayerShellQt::Window::get(&view);
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    LayerShellQt::Window::Anchors anchors;
    anchors.setFlag(LayerShellQt::Window::AnchorTop);
    anchors.setFlag(LayerShellQt::Window::AnchorBottom);
    anchors.setFlag(LayerShellQt::Window::AnchorLeft);
    anchors.setFlag(LayerShellQt::Window::AnchorRight);
    layer->setAnchors(anchors);
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    layer->setScope(QStringLiteral("dropterm"));
    layer->setActivateOnShow(true);
    // Follow the focused output, and outlive that output disappearing (a
    // virtual display being torn down) instead of being closed with it.
    layer->setWantsToBeOnActiveScreen(true);
    layer->setCloseOnDismissed(false);

    view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/dropterm/Panel.qml")));
    if (view.status() == QQuickView::Error) {
        qCritical("dropterm: failed to load QML");
        return 1;
    }

    // ── Toggle IPC ──────────────────────────────────────────────────
    // Safe to clear: we only get here after failing to reach a live instance.
    QLocalServer::removeServer(socketPath());
    QLocalServer server;
    if (!server.listen(socketPath())) {
        qCritical("dropterm: cannot listen on %s: %s", qPrintable(socketPath()),
                  qPrintable(server.errorString()));
        return 1;
    }
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        while (QLocalSocket *conn = server.nextPendingConnection()) {
            QObject::connect(conn, &QLocalSocket::readyRead, conn, [conn, &controller] {
                const QByteArray cmd = conn->readAll().trimmed();
                if (cmd == "show") {
                    controller.show();
                } else if (cmd == "hide") {
                    controller.requestHide();
                } else if (cmd == "toggle") {
                    controller.toggle();
                } else if (cmd == "settings") {
                    controller.openSettings();
                }
                conn->disconnectFromServer();
            });
            QObject::connect(conn, &QLocalSocket::disconnected, conn, &QObject::deleteLater);
        }
    });

    controller.show();
    return app.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    const QByteArray command = peekCommand(argc, argv);
    const bool isSettings = (command == "settings");
    selectShellIntegration(!isSettings);

    // The terminal background is translucent; the alpha channel has to be
    // requested before any window exists or the compositor gets an opaque
    // surface and the rounded corners come out black.
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dropterm"));
    app.setApplicationVersion(QStringLiteral("2.0.0"));
    app.setDesktopFileName(QStringLiteral("dropterm"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Yakuake-style dropdown terminal on wlr-layer-shell."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("command"),
        QStringLiteral("toggle | show | hide | settings (default: toggle)"));
    parser.process(app);

    if (parser.positionalArguments().size() > 1) {
        qCritical("dropterm: expected at most one command");
        return 2;
    }
    if (command != "toggle" && command != "show" && command != "hide" && command != "settings") {
        qCritical("dropterm: unknown command '%s' (expected toggle, show, hide or settings)",
                  command.constData());
        return 2;
    }

    return isSettings ? runSettings(app) : runTerminal(app, command);
}
