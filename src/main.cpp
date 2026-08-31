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

#include <QDir>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQuickView>
#include <QSurfaceFormat>

#include <cstdio>
#include <cstring>

#include <LayerShellQt/window.h>

#include "ipc.h"
#include "settings.h"
#include "windowcontroller.h"

namespace {

// Argument handling is done by hand, before any Qt object exists.
// QCommandLineParser would need a QCoreApplication, and the whole point is to
// reach the socket without constructing an application at all.
std::string peekCommand(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            return argv[i];
        }
    }
    return "toggle";
}

bool hasFlag(int argc, char *argv[], const char *shortFlag, const char *longFlag)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], shortFlag) == 0 || std::strcmp(argv[i], longFlag) == 0) {
            return true;
        }
    }
    return false;
}

void printUsage()
{
    std::puts("Yakuake-style dropdown terminal on wlr-layer-shell.\n"
              "\n"
              "Usage: dropterm [command]\n"
              "\n"
              "Commands:\n"
              "  toggle     show the terminal, or hide it if visible (default)\n"
              "  show       show the terminal\n"
              "  hide       hide the terminal\n"
              "  settings   open the settings window\n"
              "\n"
              "Options:\n"
              "  -h, --help     show this help\n"
              "  -v, --version  show version information\n"
              "\n"
              "The first invocation starts the terminal; later ones are forwarded to it\n"
              "over a socket in $XDG_RUNTIME_DIR.");
}

bool isKnownCommand(const std::string &c)
{
    return c == "toggle" || c == "show" || c == "hide" || c == "settings";
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

int runTerminal(QGuiApplication &app)
{
    // The window is hidden rather than closed when dismissed, but a compositor
    // config reload can dismiss the surface outright. Without this the daemon
    // would quit at that point and the keybind would silently stop working.
    app.setQuitOnLastWindowClosed(false);

    Settings settings;

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setColor(Qt::transparent);

    WindowController controller(&view, &settings);
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
    const QString sockPath = QString::fromStdString(ipc::socketPath());
    QLocalServer::removeServer(sockPath);
    QLocalServer server;
    if (!server.listen(sockPath)) {
        qCritical("dropterm: cannot listen on %s: %s", qPrintable(sockPath),
                  qPrintable(server.errorString()));
        return 1;
    }
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        while (QLocalSocket *conn = server.nextPendingConnection()) {
            QObject::connect(conn, &QLocalSocket::readyRead, conn, [conn, &controller, &settings] {
                const QByteArray cmd = conn->readAll().trimmed();
                if (cmd == "reload") {
                    // Sent by the settings process after it flushes a write.
                    settings.reload();
                } else if (cmd == "show") {
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
    const std::string command = peekCommand(argc, argv);

    if (hasFlag(argc, argv, "-h", "--help")) {
        printUsage();
        return 0;
    }
    if (hasFlag(argc, argv, "-v", "--version")) {
        std::puts("dropterm 2.0.0");
        return 0;
    }
    if (!isKnownCommand(command)) {
        std::fprintf(stderr, "dropterm: unknown command '%s' (expected toggle, show, hide or settings)\n",
                     command.c_str());
        return 2;
    }

    // ── Client path: no Qt, deliberately ────────────────────────────
    // If a terminal is already running this invocation is only a remote
    // control, and it must reach the socket without constructing an
    // application: QGuiApplication's constructor calls qFatal() (abort) when it
    // cannot create a platform plugin, so doing this the other way round meant
    // `dropterm toggle` dumped core whenever it ran without a usable display.
    // It is also simply faster — no Wayland connection, no EGL, no QML engine.
    if (command != "settings") {
        if (ipc::send(command)) {
            return 0;
        }
        if (command == "hide") {
            return 0; // nothing running, nothing to hide
        }
    }

    // ── From here we are the process that owns a window ─────────────
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

    return isSettings ? runSettings(app) : runTerminal(app);
}
