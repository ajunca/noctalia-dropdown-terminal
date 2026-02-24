/*
    Copyright (C) 2017 Crimson AS <info@crimson.no>
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

#ifndef TEXTRENDER_H
#define TEXTRENDER_H

#include <QQuickItem>

#include "vtermbridge.h"

class TextRender : public QQuickItem
{
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(DragMode dragMode READ dragMode WRITE setDragMode NOTIFY dragModeChanged)
    Q_PROPERTY(QQuickItem* contentItem READ contentItem WRITE setContentItem NOTIFY contentItemChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QSizeF cellSize READ cellSize NOTIFY cellSizeChanged)
    Q_PROPERTY(bool showBufferScrollIndicator READ showBufferScrollIndicator WRITE setShowBufferScrollIndicator NOTIFY showBufferScrollIndicatorChanged)
    Q_PROPERTY(bool allowGestures READ allowGestures WRITE setAllowGestures NOTIFY allowGesturesChanged)
    Q_PROPERTY(QQmlComponent* cellDelegate READ cellDelegate WRITE setCellDelegate NOTIFY cellDelegateChanged)
    Q_PROPERTY(QQmlComponent* cellContentsDelegate READ cellContentsDelegate WRITE setCellContentsDelegate NOTIFY cellContentsDelegateChanged)
    Q_PROPERTY(QQmlComponent* cursorDelegate READ cursorDelegate WRITE setCursorDelegate NOTIFY cursorDelegateChanged)
    Q_PROPERTY(QQmlComponent* selectionDelegate READ selectionDelegate WRITE setSelectionDelegate NOTIFY selectionDelegateChanged)
    Q_PROPERTY(int contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(int visibleHeight READ visibleHeight NOTIFY visibleHeightChanged)
    Q_PROPERTY(int contentY READ contentY NOTIFY contentYChanged)
    Q_PROPERTY(QSize terminalSize READ terminalSize NOTIFY terminalSizeChanged)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectionChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(QString shellProgram READ shellProgram WRITE setShellProgram NOTIFY shellProgramChanged)
    Q_PROPERTY(QString terminalEmulator READ terminalEmulator WRITE setTerminalEmulator NOTIFY terminalEmulatorChanged)
    Q_PROPERTY(QList<int> filterKeys READ filterKeys WRITE setFilterKeys NOTIFY filterKeysChanged)
    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY sessionCountChanged)
    Q_PROPERTY(int activeSession READ activeSession WRITE setActiveSession NOTIFY activeSessionChanged)

    Q_OBJECT
public:
    explicit TextRender(QQuickItem* parent = 0);
    virtual ~TextRender();

    Q_INVOKABLE void putString(QString str);

    bool canPaste() const;
    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void deselect();

    QString selectedText() const;

    int contentHeight() const;
    int visibleHeight() const;
    int contentY() const;

    QString title() const;

    enum DragMode
    {
        DragOff,
        DragGestures,
        DragScroll,
        DragSelect
    };
    Q_ENUMS(DragMode)

    DragMode dragMode() const;
    void setDragMode(DragMode dragMode);

    QQuickItem* contentItem() const { return m_contentItem; }
    void setContentItem(QQuickItem* contentItem);

    QSize terminalSize() const;
    QFont font() const;
    void setFont(const QFont& font);
    bool showBufferScrollIndicator() { return iShowBufferScrollIndicator; }
    void setShowBufferScrollIndicator(bool s)
    {
        if (iShowBufferScrollIndicator != s) {
            iShowBufferScrollIndicator = s;
            emit showBufferScrollIndicatorChanged();
        }
    }

    Q_INVOKABLE QPointF cursorPixelPos();
    QSizeF cellSize();

    bool allowGestures();
    void setAllowGestures(bool allow);

    QQmlComponent* cellDelegate() const;
    void setCellDelegate(QQmlComponent* delegate);
    QQmlComponent* cellContentsDelegate() const;
    void setCellContentsDelegate(QQmlComponent* delegate);
    QQmlComponent* cursorDelegate() const;
    void setCursorDelegate(QQmlComponent* delegate);
    QQmlComponent* selectionDelegate() const;
    void setSelectionDelegate(QQmlComponent* delegate);

    QString shellProgram() const { return m_shellProgram; }
    void setShellProgram(const QString& program) {
        if (m_shellProgram != program) {
            m_shellProgram = program;
            emit shellProgramChanged();
        }
    }
    QString terminalEmulator() const { return m_terminalEmulator; }
    void setTerminalEmulator(const QString& emulator) {
        if (m_terminalEmulator != emulator) {
            m_terminalEmulator = emulator;
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
    void contentItemChanged();
    void fontChanged();
    void cellSizeChanged();
    void showBufferScrollIndicatorChanged();
    void allowGesturesChanged();
    void cellDelegateChanged();
    void cellContentsDelegateChanged();
    void cursorDelegateChanged();
    void selectionDelegateChanged();
    void visualBell();
    void titleChanged();
    void dragModeChanged();
    void contentHeightChanged();
    void visibleHeightChanged();
    void contentYChanged();
    void terminalSizeChanged();
    void clipboardChanged();
    void selectionChanged();
    void shellProgramChanged();
    void terminalEmulatorChanged();
    void filterKeysChanged();
    void displayBufferChanged();
    void panLeft();
    void panRight();
    void panUp();
    void panDown();
    void hangupReceived();
    void sessionCountChanged();
    void activeSessionChanged();
    void sessionTitlesChanged();

public slots:
    void redraw();

protected:
    void updatePolish() override;
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

    void drawBgFragment(QQuickItem* cellContentsDelegate, qreal x, qreal y, int width, TermChar style);
    void drawTextFragment(QQuickItem* cellContentsDelegate, qreal x, qreal y, QString text, TermChar style);
    void paintFromBuffer(const TerminalBuffer& buffer, int from, int to, qreal& y, int& yDelegateIndex);
    QPointF charsToPixels(QPoint pos);
    QPoint pixelToVisual(QPointF pos) const;
    void selectionHelper(QPointF scenePos, bool selectionOngoing);

    qreal fontWidth() { return iFontWidth; }
    qreal fontHeight() { return iFontHeight; }
    qreal fontDescent() { return iFontDescent; }
    int fontPointSize() { return iFont.pointSize(); }

    /**
     * Scroll the back buffer on drag.
     *
     * @param now The current position
     * @param last The last position (or start position)
     * @return The new value for last (modified by any consumed offset)
     **/
    QPointF scrollBackBuffer(QPointF now, QPointF last);

    QQuickItem* fetchFreeCell();
    QQuickItem* fetchFreeCellContent();

    QPointF dragOrigin;
    QPoint m_selectionAnchor;  // absolute coords, set once at click time
    bool m_activeClick;

    QFont iFont;
    qreal iFontWidth;
    qreal iFontHeight;
    qreal iFontDescent;
    bool iShowBufferScrollIndicator;
    bool iAllowGestures;

    QQuickItem* m_contentItem;
    QQuickItem* m_backgroundContainer;
    QQuickItem* m_textContainer;
    QQuickItem* m_overlayContainer;
    QQmlComponent* m_cellDelegate;
    QVector<QQuickItem*> m_cells;
    QVector<QQuickItem*> m_freeCells;
    QQmlComponent* m_cellContentsDelegate;
    QVector<QQuickItem*> m_cellsContent;
    QVector<QQuickItem*> m_freeCellsContent;
    QQmlComponent* m_cursorDelegate;
    QQuickItem* m_cursorDelegateInstance;
    QQmlComponent* m_selectionDelegate;
    QQuickItem* m_topSelectionDelegateInstance;
    QQuickItem* m_middleSelectionDelegateInstance;
    QQuickItem* m_bottomSelectionDelegateInstance;
    DragMode m_dragMode;
    QString m_title;
    QString m_shellProgram;
    QString m_terminalEmulator;
    QList<int> m_filterKeys;
    int m_dispatch_timer;
    QSize m_pendingSize;
    int m_resizeTimer = 0;
    int m_autoScrollTimer = 0;
    int m_autoScrollDirection = 0;  // -1 up, +1 down
    QPointF m_lastDragPos;

    // Session management — static so sessions survive QML component destruction
    VTermBridge* m_terminal = nullptr;
    static QVector<VTermBridge*> s_sessions;
    static int s_activeSession;
};

#endif // TEXTRENDER_H
