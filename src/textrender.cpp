/*
    TextRender — QPainter-based terminal renderer for
    noctalia-dropdown-terminal.

    Copyright 2026 ajunca — MIT License
*/

#include "textrender.h"
#include "vtermbridge.h"

#include <QClipboard>
#include <QCursor>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QTimerEvent>
#include <cmath>

// Static session storage — survives QML component destruction/recreation
QVector<VTermBridge*> TextRender::s_sessions;
int TextRender::s_activeSession = 0;

TextRender::TextRender(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(QCursor(Qt::IBeamCursor));
    setFillColor(Qt::transparent);

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
            this, &TextRender::clipboardChanged);

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
    if (m_cursorBlinkTimer)
        killTimer(m_cursorBlinkTimer);
    if (m_autoScrollTimer)
        killTimer(m_autoScrollTimer);
    if (m_resizeTimer)
        killTimer(m_resizeTimer);
    if (m_dispatchTimer)
        killTimer(m_dispatchTimer);
    if (m_terminal)
        disconnectTerminal(m_terminal);
    // Don't delete sessions — they persist across panel toggles
}

void TextRender::connectTerminal(VTermBridge* t)
{
    connect(t, &VTermBridge::windowTitleChanged,
            this, &TextRender::handleTitleChanged);
    connect(t, &VTermBridge::hangupReceived,
            this, &TextRender::hangupReceived);
    connect(t, &VTermBridge::displayBufferChanged,
            this, &TextRender::redraw);
    connect(t, &VTermBridge::termSizeChanged,
            this, &TextRender::redraw);
    connect(t, &VTermBridge::selectionChanged,
            this, &TextRender::redraw);
    connect(t, &VTermBridge::scrollBackBufferAdjusted,
            this, &TextRender::handleScrollBack);
    connect(t, &VTermBridge::selectionChanged,
            this, &TextRender::selectionChanged);
    connect(t, &VTermBridge::windowTitleChanged,
            this, &TextRender::sessionTitlesChanged);
}

void TextRender::disconnectTerminal(VTermBridge* t)
{
    disconnect(t, nullptr, this, nullptr);
}

// ── Session management ──────────────────────────────────────────────

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
    if (m_fontWidth > 0 && m_fontHeight > 0) {
        QSize size(qMax(1, (int)((width() - 4) / m_fontWidth)),
                   qMax(1, (int)((height() - 4) / m_fontHeight)));
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
    if (m_fontWidth > 0 && m_fontHeight > 0) {
        QSize size(qMax(1, (int)((width() - 4) / m_fontWidth)),
                   qMax(1, (int)((height() - 4) / m_fontHeight)));
        session->setTermSize(size);
        session->init("UTF-8",
            m_terminalEmulator.isEmpty() ? "xterm-256color"
                                         : m_terminalEmulator.toUtf8(),
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

// ── Scroll API ──────────────────────────────────────────────────────

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
    int bbSize = m_terminal->backBuffer().size();
    int bbPos = qBound(0, bbSize - pos, bbSize);
    m_terminal->setBackBufferScrollPos(bbPos);
    redraw();
}

int TextRender::contentHeight() const
{
    if (m_terminal->useAltScreenBuffer())
        return m_terminal->buffer().size();
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
    return m_terminal->backBuffer().size() - m_terminal->backBufferScrollPos();
}

// ── Clipboard ───────────────────────────────────────────────────────

void TextRender::copy()
{
    QClipboard* cb = QGuiApplication::clipboard();
    cb->clear();
    cb->setText(selectedText());
}

void TextRender::paste()
{
    QClipboard* cb = QGuiApplication::clipboard();
    m_terminal->paste(cb->text());
}

bool TextRender::canPaste() const
{
    return !QGuiApplication::clipboard()->text().isEmpty();
}

void TextRender::deselect()
{
    m_terminal->clearSelection();
}

QString TextRender::selectedText() const
{
    return m_terminal->selectedText();
}

// ── Title ───────────────────────────────────────────────────────────

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

// ── Font ────────────────────────────────────────────────────────────

QFont TextRender::font() const
{
    return m_font;
}

void TextRender::setFont(const QFont& font)
{
    if (m_font == font)
        return;

    m_font = font;
    QFontMetricsF fm(m_font);
    m_fontHeight = fm.height();
    m_fontWidth = fm.horizontalAdvance(' ');
    m_fontDescent = fm.descent();
    m_fontAscent = fm.ascent();

    emit fontChanged();
    update();
}

// ── Lifecycle ───────────────────────────────────────────────────────

void TextRender::componentComplete()
{
    QQuickPaintedItem::componentComplete();
    m_pendingInit = true;
    m_cursorBlinkTimer = startTimer(500);
}

void TextRender::initTerminal()
{
    m_pendingInit = false;
    if (!m_terminal->isInitialized()) {
        m_terminal->init("UTF-8",
            m_terminalEmulator.isEmpty() ? "xterm-256color"
                                         : m_terminalEmulator.toUtf8(),
            m_shellProgram);
    }
}

// ── Coordinate helpers ──────────────────────────────────────────────

QPointF TextRender::cellToPixel(int col, int row) const
{
    // col, row are 1-indexed
    const qreal leftMargin = 2.0;
    return QPointF(leftMargin + (col - 1) * m_fontWidth,
                   (row - 1) * m_fontHeight);
}

QPoint TextRender::pixelToCell(QPointF pos) const
{
    // Returns 1-indexed (col, row)
    const qreal leftMargin = 2.0;
    int col = qMax(1, static_cast<int>((pos.x() - leftMargin) / m_fontWidth) + 1);
    int row = qMax(1, static_cast<int>(pos.y() / m_fontHeight) + 1);
    return QPoint(col, row);
}

// ── Painting ────────────────────────────────────────────────────────

void TextRender::paint(QPainter* painter)
{
    if (!m_terminal || m_fontWidth <= 0 || m_fontHeight <= 0)
        return;

    const int termRows = m_terminal->rows();
    const int termCols = m_terminal->columns();
    if (termRows == 0 || termCols == 0)
        return;

    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const TermChar& zero = m_terminal->zeroChar;
    const bool inverseVideo = m_terminal->inverseVideoMode();

    // ── Gather visible rows ─────────────────────────────────────────

    struct RowRef { const TerminalBuffer* buf; int idx; };
    QVector<RowRef> rows;
    rows.reserve(termRows);

    const int bbScroll = m_terminal->backBufferScrollPos();
    const TerminalBuffer& backBuf = m_terminal->backBuffer();
    const TerminalBuffer& screenBuf = m_terminal->buffer();

    if (bbScroll > 0 && !backBuf.isEmpty()) {
        int from = qMax(0, backBuf.size() - bbScroll);
        int to = backBuf.size();
        if (to - from > termRows)
            to = from + termRows;

        for (int i = from; i < to; ++i)
            rows.append({&backBuf, i});

        // Fill remaining with screen buffer
        int remaining = termRows - rows.size();
        int screenTo = qMin(remaining, screenBuf.size());
        for (int i = 0; i < screenTo; ++i)
            rows.append({&screenBuf, i});
    } else {
        int count = qMin(termRows, screenBuf.size());
        for (int i = 0; i < count; ++i)
            rows.append({&screenBuf, i});
    }

    // ── Draw rows (background + text) ───────────────────────────────

    const qreal leftMargin = 2.0;

    for (int r = 0; r < rows.size(); ++r) {
        const TerminalLine& line = rows[r].buf->at(rows[r].idx);
        const int xcount = qMin(line.size(), termCols);
        const qreal rowY = r * m_fontHeight;

        // Background pass — batch cells with same resolved bg color
        int col = 0;
        while (col < xcount) {
            const TermChar& first = line.at(col);
            QRgb bg = first.bgColor;
            QRgb fg = first.fgColor;
            if (first.attrib & TermChar::NegativeAttribute)
                std::swap(bg, fg);

            int end = col + 1;
            while (end < xcount) {
                const TermChar& next = line.at(end);
                QRgb nbg = next.bgColor;
                QRgb nfg = next.fgColor;
                if (next.attrib & TermChar::NegativeAttribute)
                    std::swap(nbg, nfg);
                if (nbg != bg)
                    break;
                ++end;
            }

            // Resolve color
            QColor bgColor;
            if (inverseVideo && bg == zero.bgColor)
                bgColor = QColor::fromRgba(zero.fgColor);
            else if (bg == zero.bgColor)
                bgColor = Qt::transparent;
            else
                bgColor = QColor::fromRgba(bg);

            if (bgColor != Qt::transparent) {
                qreal x = leftMargin + col * m_fontWidth;
                qreal w = (end - col) * m_fontWidth;
                painter->fillRect(QRectF(x, rowY, std::ceil(w), m_fontHeight),
                                  bgColor);
            }

            col = end;
        }

        // Text pass — batch cells with same style
        col = 0;
        while (col < xcount) {
            const TermChar& first = line.at(col);
            int attrib = first.attrib;
            QRgb fg = first.fgColor;
            QRgb bg = first.bgColor;
            if (attrib & TermChar::NegativeAttribute)
                std::swap(fg, bg);

            QString text;
            text += first.c;
            int end = col + 1;

            while (end < xcount) {
                const TermChar& next = line.at(end);
                QRgb nfg = next.fgColor;
                QRgb nbg = next.bgColor;
                if (next.attrib & TermChar::NegativeAttribute)
                    std::swap(nfg, nbg);

                if (next.attrib != attrib || nfg != fg || nbg != bg)
                    break;
                text += next.c;
                ++end;
            }

            // Blink: hide text during off phase
            bool visible = true;
            if ((attrib & TermChar::BlinkAttribute) && !m_cursorVisible)
                visible = false;

            if (visible) {
                // Resolve foreground
                QColor fgColor;
                if (inverseVideo && fg == zero.fgColor)
                    fgColor = QColor::fromRgba(zero.bgColor);
                else
                    fgColor = QColor::fromRgba(fg);

                QFont batchFont = m_font;
                if (attrib & TermChar::BoldAttribute)
                    batchFont.setBold(true);
                if (attrib & TermChar::ItalicAttribute)
                    batchFont.setItalic(true);
                if (attrib & TermChar::UnderlineAttribute)
                    batchFont.setUnderline(true);

                painter->setFont(batchFont);
                painter->setPen(fgColor);

                qreal x = leftMargin + col * m_fontWidth;
                painter->drawText(QPointF(x, rowY + m_fontAscent), text);
            }

            col = end;
        }
    }

    // ── Selection overlay ───────────────────────────────────────────

    QRect sel = m_terminal->selection();
    if (!sel.isNull()) {
        QColor selColor(0x88, 0xc0, 0xd0, 102);  // #88c0d0 @ 40%

        if (sel.top() == sel.bottom()) {
            // Single-line selection
            QPointF start = cellToPixel(sel.left(), sel.top());
            qreal w = (sel.right() - sel.left() + 1) * m_fontWidth;
            painter->fillRect(QRectF(start.x(), start.y(), w, m_fontHeight),
                              selColor);
        } else {
            // Top line: from sel.left() to end of line
            QPointF topStart = cellToPixel(sel.left(), sel.top());
            qreal topW = (termCols - sel.left() + 1) * m_fontWidth;
            painter->fillRect(
                QRectF(topStart.x(), topStart.y(), topW, m_fontHeight),
                selColor);

            // Middle lines (full width)
            for (int r = sel.top() + 1; r < sel.bottom(); ++r) {
                QPointF p = cellToPixel(1, r);
                painter->fillRect(
                    QRectF(p.x(), p.y(), termCols * m_fontWidth, m_fontHeight),
                    selColor);
            }

            // Bottom line: from column 1 to sel.right()
            QPointF botStart = cellToPixel(1, sel.bottom());
            qreal botW = sel.right() * m_fontWidth;
            painter->fillRect(
                QRectF(botStart.x(), botStart.y(), botW, m_fontHeight),
                selColor);
        }
    }

    // ── Cursor ──────────────────────────────────────────────────────

    if (m_terminal->showCursor() && m_cursorVisible && bbScroll == 0) {
        QPoint cpos = m_terminal->cursorPos();  // 1-indexed
        QPointF px = cellToPixel(cpos.x(), cpos.y());
        QColor cursorColor = QColor::fromRgba(zero.fgColor);
        cursorColor.setAlpha(180);
        painter->fillRect(
            QRectF(px.x(), px.y(), m_fontWidth, m_fontHeight), cursorColor);
    }
}

// ── Redraw / timers ─────────────────────────────────────────────────

void TextRender::redraw()
{
    // Emit scroll state changes for QML scrollbar bindings
    emit contentYChanged();
    emit visibleHeightChanged();
    emit contentHeightChanged();

    if (m_dispatchTimer)
        return;

    update();

    // Throttle: wait 3ms before allowing another repaint
    m_dispatchTimer = startTimer(3);
}

void TextRender::timerEvent(QTimerEvent* event)
{
    int id = event->timerId();

    if (id == m_dispatchTimer) {
        killTimer(m_dispatchTimer);
        m_dispatchTimer = 0;
        update();
    } else if (id == m_cursorBlinkTimer) {
        m_cursorVisible = !m_cursorVisible;
        update();
    } else if (id == m_autoScrollTimer) {
        if (m_autoScrollDirection < 0)
            m_terminal->scrollBackBufferBack(1);
        else if (m_autoScrollDirection > 0)
            m_terminal->scrollBackBufferFwd(1);
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

// ── Resize ──────────────────────────────────────────────────────────

void TextRender::geometryChange(const QRectF& newGeo, const QRectF& oldGeo)
{
    QQuickPaintedItem::geometryChange(newGeo, oldGeo);

    if (newGeo.size() == oldGeo.size())
        return;
    if (m_fontWidth <= 0 || m_fontHeight <= 0)
        return;

    int cols = qMax(1, static_cast<int>((newGeo.width() - 4) / m_fontWidth));
    int rows = qMax(1, static_cast<int>((newGeo.height() - 4) / m_fontHeight));

    // Ignore tiny sizes during panel open/close animation
    if (cols < 4 || rows < 2)
        return;

    QSize size(cols, rows);

    if (size != QSize(m_terminal->columns(), m_terminal->rows())
        || m_pendingInit) {
        m_pendingSize = size;
        if (m_resizeTimer)
            killTimer(m_resizeTimer);
        m_resizeTimer = startTimer(150);
    }

    update();
}

// ── Scroll back handler ─────────────────────────────────────────────

void TextRender::handleScrollBack(bool /*reset*/)
{
    redraw();
}

// ── Mouse events (always-select) ────────────────────────────────────

void TextRender::mousePressEvent(QMouseEvent* event)
{
    m_activeClick = true;
    m_dragOrigin = event->position();

    m_terminal->clearSelection();
    QPoint vis = pixelToCell(m_dragOrigin);
    m_selectionAnchor = QPoint(vis.x(),
                               m_terminal->visualRowToAbsolute(vis.y()));
}

void TextRender::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_activeClick)
        return;

    QPointF eventPos = event->position();
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

void TextRender::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_activeClick)
        return;

    if (m_autoScrollTimer) {
        killTimer(m_autoScrollTimer);
        m_autoScrollTimer = 0;
        m_autoScrollDirection = 0;
    }

    selectionHelper(event->position(), false);
    m_activeClick = false;
}

void TextRender::selectionHelper(QPointF scenePos, bool selectionOngoing)
{
    // Anchor is in absolute coords (set at click time).
    // Convert moving end from visual → absolute.
    QPoint visEnd = pixelToCell(scenePos);

    int termRows = m_terminal->rows();
    if (visEnd.y() > termRows)
        visEnd.setY(termRows);

    // Past top → column 1 (full line); past bottom → last column
    if (scenePos.y() < 0)
        visEnd.setX(1);
    else if (scenePos.y() > height())
        visEnd.setX(m_terminal->columns());

    visEnd.setX(qBound(1, visEnd.x(), m_terminal->columns()));

    QPoint absEnd(visEnd.x(), m_terminal->visualRowToAbsolute(visEnd.y()));

    if (m_selectionAnchor != absEnd)
        m_terminal->setSelection(m_selectionAnchor, absEnd, selectionOngoing);
}

// ── Keyboard ────────────────────────────────────────────────────────

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
            if (s_sessions.size() > 1)
                closeSession(s_activeSession);
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
    if (!event->pixelDelta().isNull())
        delta = event->pixelDelta().y();
    else
        delta = event->angleDelta().y();

    int lines = qAbs(delta) / 40;
    if (lines < 1)
        lines = 1;

    if (delta > 0)
        m_terminal->scrollBackBufferBack(lines);
    else if (delta < 0)
        m_terminal->scrollBackBufferFwd(lines);

    redraw();
    event->accept();
}
