/*
    dropterm — standalone Yakuake-style dropdown terminal.

    Owns its own wlr-layer-shell surface via LayerShellQt instead of being
    embedded in a shell's panel slot. The terminal itself (TextRender,
    VTermBridge, PtyIFace) is unchanged from the noctalia plugin — only the
    window ownership moved here.

    Copyright 2026 ajunca — MIT License
*/

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQuickView>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QSurfaceFormat>

#include <LayerShellQt/window.h>

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

struct Config {
    double widthPercent = 0.6;
    double heightPercent = 0.3;
    QString fontFamily = QStringLiteral("Hack");
    double fontSize = 10.5;
    QString shellProgram;   // empty -> PtyIFace falls back to the passwd shell
    double backgroundOpacity = 0.92;
    int cornerRadius = 8;
};

Config loadConfig()
{
    Config c;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    const QSettings s(QDir(dir).filePath(QStringLiteral("dropterm/dropterm.conf")),
                      QSettings::IniFormat);

    c.widthPercent = s.value(QStringLiteral("widthPercent"), c.widthPercent).toDouble();
    c.heightPercent = s.value(QStringLiteral("heightPercent"), c.heightPercent).toDouble();
    c.fontFamily = s.value(QStringLiteral("fontFamily"), c.fontFamily).toString();
    c.fontSize = s.value(QStringLiteral("fontSize"), c.fontSize).toDouble();
    c.shellProgram = s.value(QStringLiteral("shellProgram"), c.shellProgram).toString();
    c.backgroundOpacity =
        s.value(QStringLiteral("backgroundOpacity"), c.backgroundOpacity).toDouble();
    c.cornerRadius = s.value(QStringLiteral("cornerRadius"), c.cornerRadius).toInt();

    // Same bounds the plugin QML used to enforce.
    c.widthPercent = qBound(0.2, c.widthPercent, 1.0);
    c.heightPercent = qBound(0.15, c.heightPercent, 1.0);
    c.backgroundOpacity = qBound(0.0, c.backgroundOpacity, 1.0);
    return c;
}

// Hand the command to an already-running instance, if there is one.
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

} // namespace

int main(int argc, char *argv[])
{
    // Select the layer-shell integration before QGuiApplication exists.
    // LayerShellQt::Shell::useLayerShell() is deprecated as "not needed since
    // Qt 6.5", but that is wrong for this use: the Wayland shell integration is
    // picked once, while the platform plugin is constructed, so it has to be
    // chosen before the application object. Without it the process starts,
    // loads its QML and runs happily while never mapping a surface at all —
    // no window, no layer, no error. Verified against Qt 6.10 / LayerShellQt
    // 6.6. An explicit override from the environment still wins.
    if (!qEnvironmentVariableIsSet("QT_WAYLAND_SHELL_INTEGRATION")) {
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");
    }

    // The terminal background is translucent; the alpha channel has to be
    // requested before any window exists or the compositor gets an opaque
    // surface and the rounded corners come out black.
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dropterm"));
    app.setApplicationVersion(QStringLiteral("2.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Yakuake-style dropdown terminal on wlr-layer-shell."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("command"),
        QStringLiteral("toggle | show | hide (default: toggle)"));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.size() > 1) {
        qCritical("dropterm: expected at most one command");
        return 2;
    }
    const QByteArray command = args.isEmpty() ? QByteArray("toggle") : args.first().toUtf8();
    if (command != "toggle" && command != "show" && command != "hide") {
        qCritical("dropterm: unknown command '%s' (expected toggle, show or hide)",
                  command.constData());
        return 2;
    }

    // With an instance already running this invocation is only a remote control.
    if (sendToRunningInstance(command)) {
        return 0;
    }
    if (command == "hide") {
        return 0; // nothing running, nothing to hide
    }

    const Config cfg = loadConfig();

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setColor(Qt::transparent);

    WindowController controller(&view);
    // Typed root-object properties rather than context properties, so the QML
    // stays lint-checkable and self-documenting.
    view.setInitialProperties({
        {QStringLiteral("termFontFamily"), cfg.fontFamily},
        {QStringLiteral("termFontSize"), cfg.fontSize},
        {QStringLiteral("shellProgram"), cfg.shellProgram},
        {QStringLiteral("widthPercent"), cfg.widthPercent},
        {QStringLiteral("heightPercent"), cfg.heightPercent},
        {QStringLiteral("controller"), QVariant::fromValue(static_cast<QObject *>(&controller))},
        {QStringLiteral("backgroundOpacity"), cfg.backgroundOpacity},
        {QStringLiteral("cornerRadius"), cfg.cornerRadius},
    });

    // ── Layer surface ───────────────────────────────────────────────
    // The surface spans the whole usable output rather than just the terminal.
    // That is deliberate and it is what the host shell used to do for us (its
    // own full-output backdrop layer): a surface that only covers the terminal
    // cannot see a click that lands anywhere else, so outside-click dismissal
    // is impossible to implement from inside it. The QML draws the terminal at
    // the top and leaves the remainder transparent, catching stray clicks.
    //
    // Anchoring all four edges with exclusiveZone(0) makes the compositor size
    // us to the output minus everyone else's exclusive zones — so we start
    // directly below the bar, flush against it, with no hardcoded bar height.
    //
    // Exclusive keyboard interactivity is the other half of the fix. With
    // on-demand, focus-follows-mouse hands the keyboard to whatever the pointer
    // drifts over, so typing would silently go elsewhere the moment the mouse
    // moved. Exclusive keeps the keyboard here for as long as the terminal is
    // mapped, independent of the pointer, which is the behaviour a dropdown
    // terminal needs. Compositor keybinds (the toggle) still take priority.
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

    // ── Show / hide ─────────────────────────────────────────────────
    // Dismissal is driven by an actual click on the transparent area outside
    // the terminal, reported by the QML as dismissRequested. Nothing here keys
    // off focus: under focus-follows-mouse the keyboard leaves the moment the
    // pointer does, so a focus-based rule cannot tell "moved the mouse" from
    // "clicked away" — it fires on both, which is what made the panel vanish
    // on mouse movement.
    const auto showTerminal = [&controller] { controller.show(); };
    const auto hideTerminal = [&controller] { controller.hide(); };

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
            QObject::connect(conn, &QLocalSocket::readyRead, conn,
                             [conn, &view, showTerminal, hideTerminal] {
                                 const QByteArray cmd = conn->readAll().trimmed();
                                 if (cmd == "show") {
                                     showTerminal();
                                 } else if (cmd == "hide") {
                                     hideTerminal();
                                 } else if (cmd == "toggle") {
                                     view.isVisible() ? hideTerminal() : showTerminal();
                                 }
                                 conn->disconnectFromServer();
                             });
            QObject::connect(conn, &QLocalSocket::disconnected, conn, &QObject::deleteLater);
        }
    });

    showTerminal();
    return app.exec();
}
