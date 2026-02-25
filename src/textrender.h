/*
    TextRender — QPainter-based terminal renderer for
    noctalia-dropdown-terminal.

    Copyright 2026 ajunca — MIT License
*/

#ifndef TEXTRENDER_H
#define TEXTRENDER_H

#include <QQuickPaintedItem>

#include "vtermbridge.h"

class TextRender : public QQuickPaintedItem
{
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(int contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(int visibleHeight READ visibleHeight NOTIFY visibleHeightChanged)
    Q_PROPERTY(int contentY READ contentY NOTIFY contentYChanged)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectionChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(QString shellProgram READ shellProgram WRITE setShellProgram
        NOTIFY shellProgramChanged)
    Q_PROPERTY(QString terminalEmulator READ terminalEmulator
        WRITE setTerminalEmulator NOTIFY terminalEmulatorChanged)
    Q_PROPERTY(QList<int> filterKeys READ filterKeys WRITE setFilterKeys
        NOTIFY filterKeysChanged)
    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY sessionCountChanged)
    Q_PROPERTY(int activeSession READ activeSession WRITE setActiveSession
        NOTIFY activeSessionChanged)

    Q_OBJECT
public:
    explicit TextRender(QQuickItem* parent = nullptr);
    ~TextRender() override;

    void paint(QPainter* painter) override;

    // Clipboard
    bool canPaste() const;
    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void deselect();
    QString selectedText() const;

    // Scroll state (QML scrollbar reads these)
    int contentHeight() const;
    int visibleHeight() const;
    int contentY() const;

    QString title() const;

    QFont font() const;
    void setFont(const QFont& font);

    // Config
    QString shellProgram() const { return m_shellProgram; }
    void setShellProgram(const QString& prog) {
        if (m_shellProgram != prog) {
            m_shellProgram = prog;
            emit shellProgramChanged();
        }
    }
    QString terminalEmulator() const { return m_terminalEmulator; }
    void setTerminalEmulator(const QString& emu) {
        if (m_terminalEmulator != emu) {
            m_terminalEmulator = emu;
            emit terminalEmulatorChanged();
        }
    }
    QList<int> filterKeys() const { return m_filterKeys; }
    void setFilterKeys(const QList<int>& keys) {
        if (m_filterKeys != keys) {
            m_filterKeys = keys;
            emit filterKeysChanged();
        }
    }

    // Session management
    int sessionCount() const;
    int activeSession() const;
    void setActiveSession(int index);
    Q_INVOKABLE int newSession();
    Q_INVOKABLE void closeSession(int index);
    Q_INVOKABLE QString sessionTitle(int index);
    Q_INVOKABLE void scrollBackward(int lines);
    Q_INVOKABLE void scrollForward(int lines);
    Q_INVOKABLE void setScrollPosition(int pos);

signals:
    void fontChanged();
    void titleChanged();
    void contentHeightChanged();
    void visibleHeightChanged();
    void contentYChanged();
    void clipboardChanged();
    void selectionChanged();
    void shellProgramChanged();
    void terminalEmulatorChanged();
    void filterKeysChanged();
    void hangupReceived();
    void sessionCountChanged();
    void activeSessionChanged();
    void sessionTitlesChanged();

public slots:
    void redraw();

protected:
    void geometryChange(const QRectF& newGeo, const QRectF& oldGeo) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void timerEvent(QTimerEvent* event) override;
    void componentComplete() override;

private slots:
    void handleScrollBack(bool reset);
    void handleTitleChanged(const QString& title);

private:
    Q_DISABLE_COPY(TextRender)

    void initTerminal();
    bool m_pendingInit = false;

    void connectTerminal(VTermBridge* t);
    void disconnectTerminal(VTermBridge* t);

    // Coordinate helpers
    QPointF cellToPixel(int col, int row) const;
    QPoint pixelToCell(QPointF pos) const;

    // Selection
    void selectionHelper(QPointF scenePos, bool selectionOngoing);

    // Font metrics
    QFont m_font;
    qreal m_fontWidth = 0;
    qreal m_fontHeight = 0;
    qreal m_fontDescent = 0;
    qreal m_fontAscent = 0;

    // Mouse state
    QPointF m_dragOrigin;
    QPoint m_selectionAnchor;  // absolute buffer coords
    bool m_activeClick = false;
    QPointF m_lastDragPos;

    QString m_title;
    QString m_shellProgram;
    QString m_terminalEmulator;
    QList<int> m_filterKeys;

    // Timers
    int m_dispatchTimer = 0;
    int m_resizeTimer = 0;
    int m_autoScrollTimer = 0;
    int m_autoScrollDirection = 0;  // -1 up, +1 down
    int m_cursorBlinkTimer = 0;
    bool m_cursorVisible = true;

    QSize m_pendingSize;

    // Session management — static so sessions survive QML component destruction
    VTermBridge* m_terminal = nullptr;
    static QVector<VTermBridge*> s_sessions;
    static int s_activeSession;
};

#endif // TEXTRENDER_H
