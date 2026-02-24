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

#include <QClipboard>
#include <QCursor>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QTimerEvent>
#include <cmath>

#include "vtermbridge.h"
#include "textrender.h"

// Static session storage — survives QML component destruction/recreation
QVector<VTermBridge*> TextRender::s_sessions;
int TextRender::s_activeSession = 0;

TextRender::TextRender(QQuickItem* parent)
    : QQuickItem(parent)
    , m_activeClick(false)
    , iFontWidth(0)
    , iFontHeight(0)
    , iFontDescent(0)
    , iAllowGestures(true)
    , m_contentItem(0)
    , m_backgroundContainer(0)
    , m_textContainer(0)
    , m_overlayContainer(0)
    , m_cellDelegate(0)
    , m_cellContentsDelegate(0)
    , m_cursorDelegate(0)
    , m_cursorDelegateInstance(0)
    , m_selectionDelegate(0)
    , m_topSelectionDelegateInstance(0)
    , m_middleSelectionDelegateInstance(0)
    , m_bottomSelectionDelegateInstance(0)
    , m_dragMode(DragScroll)
    , m_dispatch_timer(0)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(Qt::IBeamCursor);

    connect(QGuiApplication::clipboard(), SIGNAL(dataChanged()), this, SIGNAL(clipboardChanged()));

    connect(this, SIGNAL(widthChanged()), this, SLOT(redraw()));
    connect(this, SIGNAL(heightChanged()), this, SLOT(redraw()));

    iShowBufferScrollIndicator = false;

    // Create first session if none exist
    if (s_sessions.isEmpty()) {
        s_sessions.append(new VTermBridge());
        s_activeSession = 0;
    }

    m_terminal = s_sessions[s_activeSession];
    connectTerminal(m_terminal);
}

TextRender::~TextRender()
{
    if (m_autoScrollTimer)
        killTimer(m_autoScrollTimer);
    if (m_resizeTimer)
        killTimer(m_resizeTimer);
    if (m_dispatch_timer)
        killTimer(m_dispatch_timer);
    if (m_terminal)
        disconnectTerminal(m_terminal);
    // Don't delete sessions — they persist across panel toggles
}

void TextRender::connectTerminal(VTermBridge* t)
{
    connect(t, SIGNAL(windowTitleChanged(const QString&)), this, SLOT(handleTitleChanged(const QString&)));
    connect(t, SIGNAL(visualBell()), this, SIGNAL(visualBell()));
    connect(t, SIGNAL(hangupReceived()), this, SIGNAL(hangupReceived()));
    connect(t, SIGNAL(displayBufferChanged()), this, SLOT(redraw()));
    connect(t, SIGNAL(displayBufferChanged()), this, SIGNAL(displayBufferChanged()));
    connect(t, SIGNAL(termSizeChanged(int, int)), this, SLOT(redraw()));
    connect(t, SIGNAL(termSizeChanged(int, int)), this, SIGNAL(terminalSizeChanged()));
    connect(t, SIGNAL(selectionChanged()), this, SLOT(redraw()));
    connect(t, SIGNAL(scrollBackBufferAdjusted(bool)), this, SLOT(handleScrollBack(bool)));
    connect(t, SIGNAL(selectionChanged()), this, SIGNAL(selectionChanged()));
    connect(t, SIGNAL(windowTitleChanged(const QString&)), this, SIGNAL(sessionTitlesChanged()));
}

void TextRender::disconnectTerminal(VTermBridge* t)
{
    disconnect(t, nullptr, this, nullptr);
}

// ---------- Session management ----------

int TextRender::sessionCount() const
{
    return s_sessions.size();
}

int TextRender::activeSession() const
{
    return s_activeSession;
}

void TextRender::setActiveSession(int index)
{
    if (index < 0 || index >= s_sessions.size() || index == s_activeSession)
        return;

    // Stop auto-scroll from previous session
    if (m_autoScrollTimer) {
        killTimer(m_autoScrollTimer);
        m_autoScrollTimer = 0;
        m_autoScrollDirection = 0;
    }

    if (m_terminal)
        disconnectTerminal(m_terminal);

    s_activeSession = index;
    m_terminal = s_sessions[s_activeSession];
    connectTerminal(m_terminal);

    // Update title from new session
    m_title = m_terminal->title();
    emit titleChanged();

    // Resize to current dimensions
    if (iFontWidth > 0 && iFontHeight > 0) {
        QSize size(qMax(1, (int)((width() - 4) / iFontWidth)),
                   qMax(1, (int)((height() - 4) / iFontHeight)));
        m_terminal->setTermSize(size);
    }

    emit activeSessionChanged();
    emit sessionTitlesChanged();
    redraw();
}

int TextRender::newSession()
{
    auto* session = new VTermBridge();
    s_sessions.append(session);

    int index = s_sessions.size() - 1;

    // Init immediately if we know the terminal size
    if (iFontWidth > 0 && iFontHeight > 0) {
        QSize size(qMax(1, (int)((width() - 4) / iFontWidth)),
                   qMax(1, (int)((height() - 4) / iFontHeight)));
        session->setTermSize(size);
        session->init("UTF-8",
            m_terminalEmulator.isEmpty() ? "xterm-256color" : m_terminalEmulator.toUtf8(),
            m_shellProgram);
    }

    emit sessionCountChanged();
    setActiveSession(index);
    return index;
}

void TextRender::closeSession(int index)
{
    if (index < 0 || index >= s_sessions.size() || s_sessions.size() <= 1)
        return;

    if (index == s_activeSession)
        disconnectTerminal(s_sessions[index]);

    delete s_sessions[index];
    s_sessions.removeAt(index);

    if (s_activeSession >= s_sessions.size())
        s_activeSession = s_sessions.size() - 1;

    m_terminal = s_sessions[s_activeSession];
    connectTerminal(m_terminal);

    m_title = m_terminal->title();
    emit titleChanged();
    emit sessionCountChanged();
    emit activeSessionChanged();
    emit sessionTitlesChanged();
    redraw();
}

QString TextRender::sessionTitle(int index)
{
    if (index < 0 || index >= s_sessions.size())
        return QString();
    QString title = s_sessions[index]->title();
    if (title.isEmpty())
        return QString("%1: Shell").arg(index + 1);
    return QString("%1: %2").arg(index + 1).arg(title);
}

void TextRender::scrollBackward(int lines)
{
    m_terminal->scrollBackBufferBack(lines);
    redraw();
}

void TextRender::scrollForward(int lines)
{
    m_terminal->scrollBackBufferFwd(lines);
    redraw();
}

void TextRender::setScrollPosition(int pos)
{
    // pos is contentY: 0 = scrolled to top, backBuffer.size() = at bottom
    // backBufferScrollPos = backBuffer.size() - pos
    int bbSize = m_terminal->backBuffer().size();
    int bbPos = qBound(0, bbSize - pos, bbSize);
    m_terminal->setBackBufferScrollPos(bbPos);
    redraw();
}

void TextRender::putString(QString str)
{
    m_terminal->putString(str);
}

void TextRender::componentComplete()
{
    QQuickItem::componentComplete();
    m_pendingInit = true;
}

void TextRender::initTerminal()
{
    m_pendingInit = false;
    if (!m_terminal->isInitialized()) {
        m_terminal->init("UTF-8",
                        m_terminalEmulator.isEmpty() ? "xterm-256color" : m_terminalEmulator.toUtf8(),
                        m_shellProgram);
    }
}

void TextRender::copy()
{
    QClipboard* cb = QGuiApplication::clipboard();
    cb->clear();
    cb->setText(selectedText());
}

void TextRender::paste()
{
    QClipboard* cb = QGuiApplication::clipboard();
    QString cbText = cb->text();
    m_terminal->paste(cbText);
}

bool TextRender::canPaste() const
{
    QClipboard* cb = QGuiApplication::clipboard();

    return !cb->text().isEmpty();
}

void TextRender::deselect()
{
    m_terminal->clearSelection();
}

QString TextRender::selectedText() const
{
    return m_terminal->selectedText();
}

QSize TextRender::terminalSize() const
{
    return QSize(m_terminal->columns(), m_terminal->rows());
}

QString TextRender::title() const
{
    return m_title;
}

void TextRender::handleTitleChanged(const QString& newTitle)
{
    if (m_title == newTitle)
        return;

    m_title = newTitle;
    emit titleChanged();
}

TextRender::DragMode TextRender::dragMode() const
{
    return m_dragMode;
}

void TextRender::setDragMode(DragMode dragMode)
{
    if (m_dragMode == dragMode)
        return;

    m_dragMode = dragMode;
    emit dragModeChanged();
}

void TextRender::setContentItem(QQuickItem* contentItem)
{
    Q_ASSERT(!m_contentItem); // changing this requires work
    m_contentItem = contentItem;
    m_contentItem->setParentItem(this);
    m_backgroundContainer = new QQuickItem(m_contentItem);
    m_backgroundContainer->setClip(true);
    m_textContainer = new QQuickItem(m_contentItem);
    m_textContainer->setClip(true);
    m_overlayContainer = new QQuickItem(m_contentItem);
    m_overlayContainer->setClip(true);
    polish();
}

void TextRender::setFont(const QFont& font)
{
    if (iFont == font)
        return;

    iFont = font;
    QFontMetricsF fontMetrics(iFont);
    iFontHeight = fontMetrics.height();

    // Font should be consistent in spacing with all characters,
    // otherwise it's all going to break horribly.
    iFontWidth = fontMetrics.horizontalAdvance(' ');

    iFontDescent = fontMetrics.descent();

    polish();
    emit fontChanged();
    emit cellSizeChanged();
}

QFont TextRender::font() const
{
    return iFont;
}

QQuickItem* TextRender::fetchFreeCell()
{
    QQuickItem* it = nullptr;
    if (!m_freeCells.isEmpty()) {
        it = m_freeCells.takeFirst();
    } else {
        it = qobject_cast<QQuickItem*>(m_cellDelegate->create(qmlContext(this)));
    }

    it->setParentItem(m_backgroundContainer);
    m_cells.append(it);
    return it;
}

QQuickItem* TextRender::fetchFreeCellContent()
{
    QQuickItem* it = nullptr;
    if (!m_freeCellsContent.isEmpty()) {
        it = m_freeCellsContent.takeFirst();
    } else {
        it = qobject_cast<QQuickItem*>(m_cellContentsDelegate->create(qmlContext(this)));
    }

    it->setParentItem(m_textContainer);
    m_cellsContent.append(it);
    return it;
}

void TextRender::updatePolish()
{
    // ### these should be handled more carefully
    emit contentYChanged();
    emit visibleHeightChanged();
    emit contentHeightChanged();

    if (iFontWidth <= 0 || iFontHeight <= 0)
        return;

    int cols = qMax(1, (int)((width() - 4) / iFontWidth));
    int rows = qMax(1, (int)((height() - 4) / iFontHeight));

    // Ignore tiny sizes during panel open/close animation
    if (cols < 4 || rows < 2)
        return;

    QSize size(cols, rows);

    // Debounce all resizes (including first open) — wait for animation to settle
    if (size != QSize(m_terminal->columns(), m_terminal->rows()) || m_pendingInit) {
        m_pendingSize = size;
        if (m_resizeTimer)
            killTimer(m_resizeTimer);
        m_resizeTimer = startTimer(150);
    }

    if (!m_contentItem || m_terminal->rows() == 0 || m_terminal->columns() == 0)
        return;

    m_contentItem->setWidth(width());
    m_contentItem->setHeight(height());
    m_backgroundContainer->setWidth(width());
    m_backgroundContainer->setHeight(height());
    m_textContainer->setWidth(width());
    m_textContainer->setHeight(height());
    m_overlayContainer->setWidth(width());
    m_overlayContainer->setHeight(height());

    // Push everything back to the free list.
    m_freeCells += m_cells;
    m_freeCellsContent += m_cellsContent;

    m_cells.clear();
    m_cellsContent.clear();

    qreal y = 0;
    int yDelegateIndex = 0;
    if (m_terminal->backBufferScrollPos() != 0 && m_terminal->backBuffer().size() > 0) {
        int from = m_terminal->backBuffer().size() - m_terminal->backBufferScrollPos();
        if (from < 0)
            from = 0;
        int to = m_terminal->backBuffer().size();
        if (to - from > m_terminal->rows())
            to = from + m_terminal->rows();
        paintFromBuffer(m_terminal->backBuffer(), from, to, y, yDelegateIndex);
        if (to - from < m_terminal->rows() && m_terminal->buffer().size() > 0) {
            int to2 = m_terminal->rows() - (to - from);
            if (to2 > m_terminal->buffer().size())
                to2 = m_terminal->buffer().size();
            paintFromBuffer(m_terminal->buffer(), 0, to2, y, yDelegateIndex);
        }
    } else {
        int count = qMin(m_terminal->rows(), m_terminal->buffer().size());
        paintFromBuffer(m_terminal->buffer(), 0, count, y, yDelegateIndex);
    }

    // any remaining items in the free lists are unused
    for (QQuickItem* it : m_freeCells) {
        it->setVisible(false);
    }
    for (QQuickItem* it : m_freeCellsContent) {
        it->setVisible(false);
    }

    // cursor
    if (m_terminal->showCursor()) {
        if (!m_cursorDelegateInstance) {
            m_cursorDelegateInstance = qobject_cast<QQuickItem*>(m_cursorDelegate->create(qmlContext(this)));
            m_cursorDelegateInstance->setVisible(false);
            m_cursorDelegateInstance->setParentItem(m_overlayContainer);
        }

        m_cursorDelegateInstance->setVisible(true);
        QPointF cursor = cursorPixelPos();
        QSizeF csize = cellSize();
        m_cursorDelegateInstance->setX(cursor.x());
        m_cursorDelegateInstance->setY(cursor.y());
        m_cursorDelegateInstance->setWidth(csize.width());
        m_cursorDelegateInstance->setHeight(csize.height());
        m_cursorDelegateInstance->setProperty("color", m_terminal->zeroChar.fgColor);
    } else if (m_cursorDelegateInstance) {
        m_cursorDelegateInstance->setVisible(false);
    }

    QRect selection = m_terminal->selection();
    if (!selection.isNull()) {
        if (!m_topSelectionDelegateInstance) {
            m_topSelectionDelegateInstance = qobject_cast<QQuickItem*>(m_selectionDelegate->create(qmlContext(this)));
            m_topSelectionDelegateInstance->setVisible(false);
            m_topSelectionDelegateInstance->setParentItem(m_overlayContainer);

            m_middleSelectionDelegateInstance = qobject_cast<QQuickItem*>(m_selectionDelegate->create(qmlContext(this)));
            m_middleSelectionDelegateInstance->setVisible(false);
            m_middleSelectionDelegateInstance->setParentItem(m_overlayContainer);

            m_bottomSelectionDelegateInstance = qobject_cast<QQuickItem*>(m_selectionDelegate->create(qmlContext(this)));
            m_bottomSelectionDelegateInstance->setVisible(false);
            m_bottomSelectionDelegateInstance->setParentItem(m_overlayContainer);
        }

        if (selection.top() == selection.bottom()) {
            QPointF start = charsToPixels(selection.topLeft());
            QPointF end = charsToPixels(selection.bottomRight());
            m_topSelectionDelegateInstance->setVisible(false);
            m_bottomSelectionDelegateInstance->setVisible(false);
            m_middleSelectionDelegateInstance->setVisible(true);
            m_middleSelectionDelegateInstance->setX(start.x());
            m_middleSelectionDelegateInstance->setY(start.y());
            m_middleSelectionDelegateInstance->setWidth(end.x() - start.x() + fontWidth());
            m_middleSelectionDelegateInstance->setHeight(end.y() - start.y() + fontHeight());
        } else {
            m_topSelectionDelegateInstance->setVisible(true);
            m_bottomSelectionDelegateInstance->setVisible(true);
            m_middleSelectionDelegateInstance->setVisible(true);

            QPointF start = charsToPixels(selection.topLeft());
            QPointF end = charsToPixels(QPoint(m_terminal->columns(), selection.top()));
            m_topSelectionDelegateInstance->setX(start.x());
            m_topSelectionDelegateInstance->setY(start.y());
            m_topSelectionDelegateInstance->setWidth(end.x() - start.x() + fontWidth());
            m_topSelectionDelegateInstance->setHeight(end.y() - start.y() + fontHeight());

            start = charsToPixels(QPoint(1, selection.top() + 1));
            end = charsToPixels(QPoint(m_terminal->columns(), selection.bottom() - 1));

            m_middleSelectionDelegateInstance->setX(start.x());
            m_middleSelectionDelegateInstance->setY(start.y());
            m_middleSelectionDelegateInstance->setWidth(end.x() - start.x() + fontWidth());
            m_middleSelectionDelegateInstance->setHeight(end.y() - start.y() + fontHeight());

            start = charsToPixels(QPoint(1, selection.bottom()));
            end = charsToPixels(selection.bottomRight());

            m_bottomSelectionDelegateInstance->setX(start.x());
            m_bottomSelectionDelegateInstance->setY(start.y());
            m_bottomSelectionDelegateInstance->setWidth(end.x() - start.x() + fontWidth());
            m_bottomSelectionDelegateInstance->setHeight(end.y() - start.y() + fontHeight());
        }
    } else if (m_topSelectionDelegateInstance) {
        m_topSelectionDelegateInstance->setVisible(false);
        m_bottomSelectionDelegateInstance->setVisible(false);
        m_middleSelectionDelegateInstance->setVisible(false);
    }
}

void TextRender::paintFromBuffer(const TerminalBuffer& buffer, int from, int to, qreal& y, int& yDelegateIndex)
{
    const int leftmargin = 2;
    int cutAfter = property("cutAfter").toInt() + iFontDescent;

    TermChar tmp = m_terminal->zeroChar;
    TermChar nextAttrib = m_terminal->zeroChar;
    TermChar currAttrib = m_terminal->zeroChar;
    qreal currentX = leftmargin;

    for (int i = from; i < to; i++, yDelegateIndex++) {
        y += iFontHeight;

        qreal opacity = 1.0;
        if (y >= cutAfter)
            opacity = 0.3;

        const auto& lineBuffer = buffer.at(i);
        int xcount = qMin(lineBuffer.size(), m_terminal->columns());

        // background for the current line
        currentX = leftmargin;
        qreal fragWidth = 0;
        for (int j = 0; j < xcount; j++) {
            fragWidth += iFontWidth;
            if (j == 0) {
                tmp = lineBuffer.at(j);
                currAttrib = tmp;
                nextAttrib = tmp;
            } else if (j < xcount - 1) {
                nextAttrib = lineBuffer.at(j + 1);
            }

            if (currAttrib.attrib != nextAttrib.attrib || currAttrib.bgColor != nextAttrib.bgColor || currAttrib.fgColor != nextAttrib.fgColor || j == xcount - 1) {
                QQuickItem* backgroundRectangle = fetchFreeCell();
                drawBgFragment(backgroundRectangle, currentX, y - iFontHeight + iFontDescent, std::ceil(fragWidth), currAttrib);
                backgroundRectangle->setOpacity(opacity);
                currentX += fragWidth;
                fragWidth = 0;
                currAttrib.attrib = nextAttrib.attrib;
                currAttrib.bgColor = nextAttrib.bgColor;
                currAttrib.fgColor = nextAttrib.fgColor;
            }
        }

        // text for the current line
        QString line;
        currentX = leftmargin;
        for (int j = 0; j < xcount; j++) {
            tmp = lineBuffer.at(j);
            line += tmp.c;
            if (j == 0) {
                currAttrib = tmp;
                nextAttrib = tmp;
            } else if (j < xcount - 1) {
                nextAttrib = lineBuffer.at(j + 1);
            }

            if (currAttrib.attrib != nextAttrib.attrib || currAttrib.bgColor != nextAttrib.bgColor || currAttrib.fgColor != nextAttrib.fgColor || j == xcount - 1) {
                QQuickItem* foregroundText = fetchFreeCellContent();
                drawTextFragment(foregroundText, currentX, y - iFontHeight + iFontDescent, line, currAttrib);
                foregroundText->setOpacity(opacity);
                currentX += iFontWidth * line.length();
                line.clear();
                currAttrib.attrib = nextAttrib.attrib;
                currAttrib.bgColor = nextAttrib.bgColor;
                currAttrib.fgColor = nextAttrib.fgColor;
            }
        }
    }
}

void TextRender::drawBgFragment(QQuickItem* cellDelegate, qreal x, qreal y, int width, TermChar style)
{
    if (style.attrib & TermChar::NegativeAttribute) {
        QRgb c = style.fgColor;
        style.fgColor = style.bgColor;
        style.bgColor = c;
    }

    QColor qtColor;

    if (m_terminal->inverseVideoMode() && style.bgColor == m_terminal->zeroChar.bgColor) {
        qtColor = m_terminal->zeroChar.fgColor;
    } else if (style.bgColor == m_terminal->zeroChar.bgColor) {
        qtColor = Qt::transparent;
    } else {
        qtColor = style.bgColor;
    }

    cellDelegate->setX(x);
    cellDelegate->setY(y);
    cellDelegate->setWidth(width);
    cellDelegate->setHeight(iFontHeight);
    cellDelegate->setProperty("color", qtColor);
    cellDelegate->setVisible(true);
}

void TextRender::drawTextFragment(QQuickItem* cellContentsDelegate, qreal x, qreal y, QString text, TermChar style)
{
    if (style.attrib & TermChar::NegativeAttribute) {
        QRgb c = style.fgColor;
        style.fgColor = style.bgColor;
        style.bgColor = c;
    }
    if (style.attrib & TermChar::BoldAttribute) {
        iFont.setBold(true);
    } else if (iFont.bold()) {
        iFont.setBold(false);
    }
    if (style.attrib & TermChar::UnderlineAttribute) {
        iFont.setUnderline(true);
    } else if (iFont.underline()) {
        iFont.setUnderline(false);
    }
    if (style.attrib & TermChar::ItalicAttribute) {
        iFont.setItalic(true);
    } else if (iFont.italic()) {
        iFont.setItalic(false);
    }

    QColor qtColor;

    if (m_terminal->inverseVideoMode() && style.fgColor == m_terminal->zeroChar.fgColor) {
        qtColor = m_terminal->zeroChar.bgColor;
    } else {
        qtColor = style.fgColor;
    }

    cellContentsDelegate->setX(x);
    cellContentsDelegate->setY(y);
    cellContentsDelegate->setHeight(iFontHeight);
    cellContentsDelegate->setProperty("color", qtColor);
    cellContentsDelegate->setProperty("text", text);
    cellContentsDelegate->setProperty("font", iFont);

    if (style.attrib & TermChar::BlinkAttribute) {
        cellContentsDelegate->setProperty("blinking", true);
    } else {
        cellContentsDelegate->setProperty("blinking", false);
    }

    cellContentsDelegate->setVisible(true);
}

void TextRender::redraw()
{
    if (m_dispatch_timer)
        return;

    // instantly polish
    polish();

    // ... but now, wait a while, so we don't constantly re-polish.
    m_dispatch_timer = startTimer(3);
}

void TextRender::timerEvent(QTimerEvent* event)
{
    int id = event->timerId();
    if (id == m_dispatch_timer) {
        killTimer(m_dispatch_timer);
        m_dispatch_timer = 0;
        polish();
    } else if (id == m_autoScrollTimer) {
        if (m_autoScrollDirection < 0) {
            m_terminal->scrollBackBufferBack(1);
        } else if (m_autoScrollDirection > 0) {
            m_terminal->scrollBackBufferFwd(1);
        }
        selectionHelper(m_lastDragPos, true);
        redraw();
    } else if (id == m_resizeTimer) {
        killTimer(m_resizeTimer);
        m_resizeTimer = 0;
        if (!m_pendingSize.isEmpty()) {
            m_terminal->setTermSize(m_pendingSize);
            if (m_pendingInit) {
                m_pendingInit = false;
                if (!m_terminal->isInitialized())
                    initTerminal();
            }
            m_pendingSize = QSize();
            redraw();
        }
    }
}

QPoint TextRender::pixelToVisual(QPointF pos) const
{
    int yCorr = iFontDescent;
    return QPoint(qMax(1, qRound((pos.x() + 2) / iFontWidth)),
                  qMax(1, qRound((pos.y() + yCorr) / iFontHeight)));
}

void TextRender::mousePressEvent(QMouseEvent* event)
{
    if (!allowGestures())
        return;

    m_activeClick = true;
    dragOrigin = event->position();

    if (m_dragMode == DragSelect) {
        m_terminal->clearSelection();
        // Store anchor in absolute coords so it survives scrolling
        QPoint vis = pixelToVisual(dragOrigin);
        m_selectionAnchor = QPoint(vis.x(), m_terminal->visualRowToAbsolute(vis.y()));
    }
}

void TextRender::mouseMoveEvent(QMouseEvent* event)
{
    if (!allowGestures() || !m_activeClick)
        return;

    QPointF eventPos = event->position();

    if (m_dragMode == DragScroll) {
        dragOrigin = scrollBackBuffer(eventPos, dragOrigin);
    } else if (m_dragMode == DragSelect) {
        m_lastDragPos = eventPos;
        selectionHelper(eventPos, true);

        // Auto-scroll when dragging past top/bottom edges
        if (eventPos.y() < 0) {
            if (m_autoScrollDirection != -1) {
                if (m_autoScrollTimer)
                    killTimer(m_autoScrollTimer);
                m_autoScrollDirection = -1;
                m_autoScrollTimer = startTimer(50);
            }
        } else if (eventPos.y() > height()) {
            if (m_autoScrollDirection != 1) {
                if (m_autoScrollTimer)
                    killTimer(m_autoScrollTimer);
                m_autoScrollDirection = 1;
                m_autoScrollTimer = startTimer(50);
            }
        } else if (m_autoScrollTimer) {
            killTimer(m_autoScrollTimer);
            m_autoScrollTimer = 0;
            m_autoScrollDirection = 0;
        }
    }
}

void TextRender::mouseReleaseEvent(QMouseEvent* event)
{
    if (!allowGestures() || !m_activeClick)
        return;

    // Stop auto-scroll if active
    if (m_autoScrollTimer) {
        killTimer(m_autoScrollTimer);
        m_autoScrollTimer = 0;
        m_autoScrollDirection = 0;
    }

    QPointF eventPos = event->position();
    const int reqDragLength = 140;

    if (m_dragMode == DragGestures) {
        int xdist = qAbs(eventPos.x() - dragOrigin.x());
        int ydist = qAbs(eventPos.y() - dragOrigin.y());
        if (eventPos.x() < dragOrigin.x() - reqDragLength && xdist > ydist * 2)
            emit panLeft();
        else if (eventPos.x() > dragOrigin.x() + reqDragLength && xdist > ydist * 2)
            emit panRight();
        else if (eventPos.y() > dragOrigin.y() + reqDragLength && ydist > xdist * 2)
            emit panDown();
        else if (eventPos.y() < dragOrigin.y() - reqDragLength && ydist > xdist * 2)
            emit panUp();
    } else if (m_dragMode == DragScroll) {
        scrollBackBuffer(eventPos, dragOrigin);
    } else if (m_dragMode == DragSelect) {
        selectionHelper(eventPos, false);
    }
}

void TextRender::keyPressEvent(QKeyEvent* event)
{
    // Filter keys that should be ignored (e.g. the toggle hotkey)
    if (m_filterKeys.contains(event->key())) {
        event->ignore();
        return;
    }
    auto mods = event->modifiers();
    int key = event->key();

    // Ctrl+Shift shortcuts
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
        if (key == Qt::Key_C) { copy(); return; }
        if (key == Qt::Key_V) { paste(); return; }
        if (key == Qt::Key_T || key == Qt::Key_N) { newSession(); return; }
        if (key == Qt::Key_W) {
            if (s_sessions.size() > 1) closeSession(s_activeSession);
            return;
        }
    }

    // Shift+Insert — paste
    if ((mods & Qt::ShiftModifier) && key == Qt::Key_Insert) {
        paste();
        return;
    }

    // Ctrl+Tab / Ctrl+Shift+Tab — switch tabs
    if ((mods & Qt::ControlModifier) && key == Qt::Key_Tab) {
        int next = (s_activeSession + 1) % s_sessions.size();
        setActiveSession(next);
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_Backtab) {
        int prev = (s_activeSession - 1 + s_sessions.size()) % s_sessions.size();
        setActiveSession(prev);
        return;
    }

    m_terminal->keyPress(key, event->modifiers(), event->text());
}

void TextRender::wheelEvent(QWheelEvent* event)
{
    int delta = 0;
    if (!event->pixelDelta().isNull()) {
        delta = event->pixelDelta().y();
    } else {
        delta = event->angleDelta().y();
    }

    int lines = qAbs(delta) / 40;
    if (lines < 1)
        lines = 1;

    if (delta > 0) {
        m_terminal->scrollBackBufferBack(lines);
    } else if (delta < 0) {
        m_terminal->scrollBackBufferFwd(lines);
    }
    redraw();
    event->accept();
}

void TextRender::selectionHelper(QPointF scenePos, bool selectionOngoing)
{
    // Anchor is already in absolute coords (set at click time).
    // Convert only the moving end from visual → absolute.
    QPoint visEnd = pixelToVisual(scenePos);

    // Clamp visual row to visible range
    int rows = m_terminal->rows();
    if (visEnd.y() > rows)
        visEnd.setY(rows);

    // When past top edge, select from column 1 (full line);
    // when past bottom edge, select to last column (full line)
    if (scenePos.y() < 0)
        visEnd.setX(1);
    else if (scenePos.y() > height())
        visEnd.setX(m_terminal->columns());

    // Clamp column to valid range
    visEnd.setX(qBound(1, visEnd.x(), m_terminal->columns()));

    QPoint absEnd(visEnd.x(), m_terminal->visualRowToAbsolute(visEnd.y()));

    if (m_selectionAnchor != absEnd) {
        m_terminal->setSelection(m_selectionAnchor, absEnd, selectionOngoing);
    }
}

void TextRender::handleScrollBack(bool reset)
{
    if (reset) {
        setShowBufferScrollIndicator(false);
    } else {
        setShowBufferScrollIndicator(m_terminal->backBufferScrollPos() != 0);
    }
    redraw();
}

QPointF TextRender::cursorPixelPos()
{
    return charsToPixels(m_terminal->cursorPos());
}

QPointF TextRender::charsToPixels(QPoint pos)
{
    qreal x = 2;                     // left margin
    x += iFontWidth * (pos.x() - 1); // 0 indexed, so -1

    qreal y = iFontHeight * (pos.y() - 1) + iFontDescent + 1;

    return QPointF(x, y);
}

QSizeF TextRender::cellSize()
{
    return QSizeF(iFontWidth, iFontHeight);
}

bool TextRender::allowGestures()
{
    return iAllowGestures;
}

void TextRender::setAllowGestures(bool allow)
{
    if (iAllowGestures != allow) {
        iAllowGestures = allow;
        emit allowGesturesChanged();
    }

    if (!allow) {
        m_activeClick = false;
        if (m_autoScrollTimer) {
            killTimer(m_autoScrollTimer);
            m_autoScrollTimer = 0;
            m_autoScrollDirection = 0;
        }
    }
}

QQmlComponent* TextRender::cellDelegate() const
{
    return m_cellDelegate;
}

void TextRender::setCellDelegate(QQmlComponent* component)
{
    if (m_cellDelegate == component)
        return;

    qDeleteAll(m_cells);
    qDeleteAll(m_freeCells);
    m_cells.clear();
    m_freeCells.clear();
    m_cellDelegate = component;
    emit cellDelegateChanged();
    polish();
}

QQmlComponent* TextRender::cellContentsDelegate() const
{
    return m_cellContentsDelegate;
}

void TextRender::setCellContentsDelegate(QQmlComponent* component)
{
    if (m_cellContentsDelegate == component)
        return;

    qDeleteAll(m_cellsContent);
    qDeleteAll(m_freeCellsContent);
    m_cellsContent.clear();
    m_freeCellsContent.clear();
    m_cellContentsDelegate = component;
    emit cellContentsDelegateChanged();
    polish();
}

QQmlComponent* TextRender::cursorDelegate() const
{
    return m_cursorDelegate;
}

void TextRender::setCursorDelegate(QQmlComponent* component)
{
    if (m_cursorDelegate == component)
        return;

    delete m_cursorDelegateInstance;
    m_cursorDelegateInstance = 0;
    m_cursorDelegate = component;

    emit cursorDelegateChanged();
    polish();
}

QQmlComponent* TextRender::selectionDelegate() const
{
    return m_selectionDelegate;
}

void TextRender::setSelectionDelegate(QQmlComponent* component)
{
    if (m_selectionDelegate == component)
        return;

    delete m_topSelectionDelegateInstance;
    delete m_middleSelectionDelegateInstance;
    delete m_bottomSelectionDelegateInstance;
    m_topSelectionDelegateInstance = 0;
    m_middleSelectionDelegateInstance = 0;
    m_bottomSelectionDelegateInstance = 0;

    m_selectionDelegate = component;

    emit selectionDelegateChanged();
    polish();
}

int TextRender::contentHeight() const
{
    if (m_terminal->useAltScreenBuffer())
        return m_terminal->buffer().size();
    else
        return m_terminal->buffer().size() + m_terminal->backBuffer().size();
}

int TextRender::visibleHeight() const
{
    return m_terminal->buffer().size();
}

int TextRender::contentY() const
{
    if (m_terminal->useAltScreenBuffer())
        return 0;

    int scrollPos = m_terminal->backBuffer().size() - m_terminal->backBufferScrollPos();
    return scrollPos;
}

QPointF TextRender::scrollBackBuffer(QPointF now, QPointF last)
{
    int xdist = qAbs(now.x() - last.x());
    int ydist = qAbs(now.y() - last.y());
    int fontSize = fontPointSize();

    int lines = ydist / fontSize;

    if (lines > 0 && now.y() < last.y() && xdist < ydist * 2) {
        m_terminal->scrollBackBufferFwd(lines);
        last = QPointF(now.x(), last.y() - lines * fontSize);
    } else if (lines > 0 && now.y() > last.y() && xdist < ydist * 2) {
        m_terminal->scrollBackBufferBack(lines);
        last = QPointF(now.x(), last.y() + lines * fontSize);
    }

    return last;
}
