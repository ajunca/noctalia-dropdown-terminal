/*
    VTermBridge — libvterm-neovim wrapper for literm Qt6 QML plugin.
    Copyright 2024 — GPL v2+
*/

#include "vtermbridge.h"
#include "ptyiface.h"

#include <QDebug>

VTermBridge::VTermBridge(QObject* parent)
    : QObject(parent)
{
    zeroChar.c = ' ';
    zeroChar.fgColor = qRgb(235, 235, 235);
    zeroChar.bgColor = qRgb(0, 0, 0);
    zeroChar.attrib = TermChar::NoAttributes;
}

VTermBridge::~VTermBridge()
{
    delete m_pty;
    if (m_vt)
        vterm_free(m_vt);
}

void VTermBridge::init(const QString& charset, const QByteArray& termEnv, const QString& command)
{
    int rows = m_termSize.height() > 0 ? m_termSize.height() : 24;
    int cols = m_termSize.width() > 0 ? m_termSize.width() : 80;

    m_vt = vterm_new(rows, cols);
    vterm_set_utf8(m_vt, 1);

    // Output callback: keyboard input → escape sequences → PTY
    vterm_output_set_callback(m_vt, cb_output, this);

    // Screen layer
    m_vtScreen = vterm_obtain_screen(m_vt);
    vterm_screen_enable_altscreen(m_vtScreen, 1);
    vterm_screen_enable_reflow(m_vtScreen, true);
    vterm_screen_set_damage_merge(m_vtScreen, VTERM_DAMAGE_SCROLL);

    static const VTermScreenCallbacks screen_cbs = {
        .damage = cb_damage,
        .moverect = nullptr,
        .movecursor = cb_movecursor,
        .settermprop = cb_settermprop,
        .bell = cb_bell,
        .resize = cb_resize,
        .sb_pushline = cb_sb_pushline,
        .sb_popline = cb_sb_popline,
        .sb_clear = cb_sb_clear,
    };
    vterm_screen_set_callbacks(m_vtScreen, &screen_cbs, this);

    // Default colors
    VTermState* state = vterm_obtain_state(m_vt);
    VTermColor fg, bg;
    vterm_color_rgb(&fg, 235, 235, 235);
    vterm_color_rgb(&bg, 0, 0, 0);
    vterm_state_set_default_colors(state, &fg, &bg);

    vterm_screen_reset(m_vtScreen, 1);

    // Spawn PTY
    m_pty = new PtyIFace(this, charset.isEmpty() ? "UTF-8" : charset,
        termEnv.isEmpty() ? "xterm-256color" : termEnv,
        command, this);

    if (m_pty->failed()) {
        qWarning("VTermBridge: PTY initialization failed");
        return;
    }

    connect(m_pty, &PtyIFace::dataAvailable, this, &VTermBridge::onDataAvailable);
    connect(m_pty, &PtyIFace::hangupReceived, this, &VTermBridge::hangupReceived);
}

// ---------- PTY data → libvterm ----------

void VTermBridge::onDataAvailable()
{
    QByteArray data = m_pty->takeRawData();
    if (!data.isEmpty()) {
        vterm_input_write(m_vt, data.constData(), data.size());
        vterm_screen_flush_damage(m_vtScreen);
        rebuildScreenBuffer();
        emit displayBufferChanged();
    }
}

// ---------- libvterm output → PTY ----------

void VTermBridge::cb_output(const char* s, size_t len, void* user)
{
    auto* self = static_cast<VTermBridge*>(user);
    if (self->m_pty) {
        self->m_pty->writeRawTerm(QByteArray(s, len));
    }
}

// ---------- Screen callbacks ----------

int VTermBridge::cb_damage(VTermRect, void*)
{
    // Damage is handled after flush in onDataAvailable
    return 1;
}

int VTermBridge::cb_movecursor(VTermPos pos, VTermPos, int visible, void* user)
{
    auto* self = static_cast<VTermBridge*>(user);
    self->m_cursorPos = QPoint(pos.col + 1, pos.row + 1); // 1-indexed
    self->m_showCursor = visible;
    return 1;
}

int VTermBridge::cb_settermprop(VTermProp prop, VTermValue* val, void* user)
{
    auto* self = static_cast<VTermBridge*>(user);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        self->m_showCursor = val->boolean;
        break;
    case VTERM_PROP_TITLE:
        if (val->string.initial)
            self->m_pendingTitle.clear();
        self->m_pendingTitle += QString::fromUtf8(val->string.str, val->string.len);
        if (val->string.final) {
            self->m_title = self->m_pendingTitle;
            emit self->windowTitleChanged(self->m_title);
        }
        break;
    case VTERM_PROP_ALTSCREEN:
        self->m_altScreen = val->boolean;
        if (val->boolean)
            self->resetBackBufferScrollPos();
        break;
    case VTERM_PROP_REVERSE:
        self->m_reverseVideo = val->boolean;
        break;
    default:
        break;
    }
    return 1;
}

int VTermBridge::cb_bell(void* user)
{
    emit static_cast<VTermBridge*>(user)->visualBell();
    return 1;
}

int VTermBridge::cb_resize(int rows, int cols, void* user)
{
    auto* self = static_cast<VTermBridge*>(user);
    self->m_termSize = QSize(cols, rows);
    return 1;
}

// ---------- Scrollback callbacks ----------

int VTermBridge::cb_sb_pushline(int cols, const VTermScreenCell* cells, void* user)
{
    auto* self = static_cast<VTermBridge*>(user);
    self->m_backBuffer.append(self->cellsToLine(cols, cells));

    while (self->m_backBuffer.size() > MAX_SCROLLBACK)
        self->m_backBuffer.removeAt(0);

    return 1;
}

int VTermBridge::cb_sb_popline(int cols, VTermScreenCell* cells, void* user)
{
    auto* self = static_cast<VTermBridge*>(user);
    if (self->m_backBuffer.size() == 0)
        return 0;

    int lastIdx = self->m_backBuffer.size() - 1;
    const TerminalLine& line = self->m_backBuffer.at(lastIdx);

    for (int i = 0; i < cols; i++) {
        memset(&cells[i], 0, sizeof(VTermScreenCell));
        if (i < line.size()) {
            const TermChar& tc = line.at(i);
            cells[i].chars[0] = tc.c.unicode();
            cells[i].width = 1;
            vterm_color_rgb(&cells[i].fg,
                qRed(tc.fgColor), qGreen(tc.fgColor), qBlue(tc.fgColor));
            vterm_color_rgb(&cells[i].bg,
                qRed(tc.bgColor), qGreen(tc.bgColor), qBlue(tc.bgColor));
            cells[i].attrs.bold = (tc.attrib & TermChar::BoldAttribute) ? 1 : 0;
            cells[i].attrs.italic = (tc.attrib & TermChar::ItalicAttribute) ? 1 : 0;
            cells[i].attrs.underline = (tc.attrib & TermChar::UnderlineAttribute) ? 1 : 0;
            cells[i].attrs.reverse = (tc.attrib & TermChar::NegativeAttribute) ? 1 : 0;
            cells[i].attrs.blink = (tc.attrib & TermChar::BlinkAttribute) ? 1 : 0;
        } else {
            cells[i].chars[0] = ' ';
            cells[i].width = 1;
        }
    }

    self->m_backBuffer.removeAt(lastIdx);
    return 1;
}

int VTermBridge::cb_sb_clear(void* user)
{
    auto* self = static_cast<VTermBridge*>(user);
    self->m_backBuffer.clear();
    self->m_backBufferScrollPos = 0;
    return 1;
}

// ---------- Conversions ----------

QRgb VTermBridge::vtermColorToQRgb(VTermColor col) const
{
    if (VTERM_COLOR_IS_DEFAULT_FG(&col))
        return zeroChar.fgColor;
    if (VTERM_COLOR_IS_DEFAULT_BG(&col))
        return zeroChar.bgColor;

    if (VTERM_COLOR_IS_INDEXED(&col) && m_vtScreen)
        vterm_screen_convert_color_to_rgb(m_vtScreen, &col);

    return qRgb(col.rgb.red, col.rgb.green, col.rgb.blue);
}

TermChar VTermBridge::cellToTermChar(const VTermScreenCell& cell) const
{
    TermChar tc;
    tc.c = (cell.chars[0] != 0) ? QChar(cell.chars[0]) : QChar(' ');
    tc.fgColor = vtermColorToQRgb(cell.fg);
    tc.bgColor = vtermColorToQRgb(cell.bg);

    tc.attrib = TermChar::NoAttributes;
    if (cell.attrs.bold)
        tc.attrib |= TermChar::BoldAttribute;
    if (cell.attrs.italic)
        tc.attrib |= TermChar::ItalicAttribute;
    if (cell.attrs.underline)
        tc.attrib |= TermChar::UnderlineAttribute;
    if (cell.attrs.reverse)
        tc.attrib |= TermChar::NegativeAttribute;
    if (cell.attrs.blink)
        tc.attrib |= TermChar::BlinkAttribute;

    return tc;
}

TerminalLine VTermBridge::cellsToLine(int cols, const VTermScreenCell* cells) const
{
    TerminalLine line;
    for (int i = 0; i < cols; i++)
        line.append(cellToTermChar(cells[i]));
    return line;
}

void VTermBridge::rebuildScreenBuffer()
{
    int rows = m_termSize.height();
    int cols = m_termSize.width();

    m_screenBuffer.clear();

    for (int row = 0; row < rows; row++) {
        TerminalLine line;
        for (int col = 0; col < cols; col++) {
            VTermScreenCell cell;
            VTermPos pos = { .row = row, .col = col };
            vterm_screen_get_cell(m_vtScreen, pos, &cell);
            line.append(cellToTermChar(cell));
        }
        m_screenBuffer.append(line);
    }
}

// ---------- Size ----------

void VTermBridge::setTermSize(QSize size)
{
    if (m_termSize == size)
        return;
    m_termSize = size;

    if (m_vt) {
        vterm_set_size(m_vt, size.height(), size.width());
        vterm_screen_flush_damage(m_vtScreen);
        rebuildScreenBuffer();
    }

    if (m_pty)
        m_pty->resize(size.height(), size.width());

    emit termSizeChanged(size.height(), size.width());
}

// ---------- Keyboard input ----------

void VTermBridge::keyPress(int key, int modifiers, const QString& text)
{
    if (!m_vt)
        return;

    // Ignore modifier-only key presses (don't clear selection)
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt
        || key == Qt::Key_Meta || key == Qt::Key_Super_L || key == Qt::Key_Super_R)
        return;

    VTermModifier mod = VTERM_MOD_NONE;
    if (modifiers & Qt::ShiftModifier)
        mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
    if (modifiers & Qt::AltModifier)
        mod = (VTermModifier)(mod | VTERM_MOD_ALT);
    if (modifiers & Qt::ControlModifier)
        mod = (VTermModifier)(mod | VTERM_MOD_CTRL);

    VTermKey vtKey = VTERM_KEY_NONE;

    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        vtKey = VTERM_KEY_ENTER;
        break;
    case Qt::Key_Tab:
        vtKey = VTERM_KEY_TAB;
        break;
    case Qt::Key_Backtab:
        vtKey = VTERM_KEY_TAB;
        mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
        break;
    case Qt::Key_Backspace:
        vtKey = VTERM_KEY_BACKSPACE;
        mod = (VTermModifier)(mod & ~VTERM_MOD_SHIFT);
        break;
    case Qt::Key_Escape:
        vtKey = VTERM_KEY_ESCAPE;
        break;
    case Qt::Key_Up:
        vtKey = VTERM_KEY_UP;
        break;
    case Qt::Key_Down:
        vtKey = VTERM_KEY_DOWN;
        break;
    case Qt::Key_Left:
        vtKey = VTERM_KEY_LEFT;
        break;
    case Qt::Key_Right:
        vtKey = VTERM_KEY_RIGHT;
        break;
    case Qt::Key_Insert:
        vtKey = VTERM_KEY_INS;
        break;
    case Qt::Key_Delete:
        vtKey = VTERM_KEY_DEL;
        break;
    case Qt::Key_Home:
        vtKey = VTERM_KEY_HOME;
        break;
    case Qt::Key_End:
        vtKey = VTERM_KEY_END;
        break;
    case Qt::Key_PageUp:
        vtKey = VTERM_KEY_PAGEUP;
        break;
    case Qt::Key_PageDown:
        vtKey = VTERM_KEY_PAGEDOWN;
        break;
    default:
        if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
            vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
        break;
    }

    resetBackBufferScrollPos();
    clearSelection();

    if (vtKey != VTERM_KEY_NONE) {
        vterm_keyboard_key(m_vt, vtKey, mod);
    } else if ((modifiers & Qt::ControlModifier) && key >= Qt::Key_A && key <= Qt::Key_Z) {
        // Ctrl+letter: pass base letter, libvterm applies Ctrl internally
        vterm_keyboard_unichar(m_vt, 'a' + (key - Qt::Key_A), mod);
    } else if (!text.isEmpty()) {
        for (const QChar& ch : text) {
            vterm_keyboard_unichar(m_vt, ch.unicode(), mod);
        }
    }
}

void VTermBridge::paste(const QString& text)
{
    if (!m_pty || text.isEmpty())
        return;

    resetBackBufferScrollPos();
    clearSelection();

    m_pty->writeRawTerm(text.toUtf8());
}

void VTermBridge::putString(QString str)
{
    if (m_pty)
        m_pty->writeRawTerm(str.toUtf8());
}

// ---------- Scrollback navigation ----------

void VTermBridge::scrollBackBufferFwd(int lines)
{
    if (lines <= 0)
        return;
    if (m_altScreen)
        return;

    m_backBufferScrollPos -= lines;
    if (m_backBufferScrollPos < 0)
        m_backBufferScrollPos = 0;
    emit scrollBackBufferAdjusted(m_backBufferScrollPos == 0);
}

void VTermBridge::scrollBackBufferBack(int lines)
{
    if (lines <= 0)
        return;
    if (m_altScreen)
        return;

    m_backBufferScrollPos += lines;
    if (m_backBufferScrollPos > m_backBuffer.size())
        m_backBufferScrollPos = m_backBuffer.size();
    emit scrollBackBufferAdjusted(false);
}

void VTermBridge::resetBackBufferScrollPos()
{
    if (m_backBufferScrollPos != 0) {
        m_backBufferScrollPos = 0;
        emit scrollBackBufferAdjusted(true);
    }
}

void VTermBridge::setBackBufferScrollPos(int pos)
{
    if (m_altScreen)
        return;
    pos = qBound(0, pos, m_backBuffer.size());
    if (m_backBufferScrollPos != pos) {
        m_backBufferScrollPos = pos;
        emit scrollBackBufferAdjusted(pos == 0);
    }
}

// ---------- Selection ----------

void VTermBridge::setSelection(QPoint start, QPoint end, bool selectionOngoing)
{
    m_selectionOngoing = selectionOngoing;

    // Normalize so start is before end
    if (start.y() > end.y() || (start.y() == end.y() && start.x() > end.x()))
        std::swap(start, end);

    QRect newSel(start, end);
    if (m_selection != newSel) {
        m_selection = newSel;
        emit selectionChanged();
    }
}

void VTermBridge::clearSelection()
{
    if (!m_selection.isNull()) {
        m_selection = QRect();
        emit selectionChanged();
    }
}

QString VTermBridge::selectedText() const
{
    if (m_selection.isNull() || !m_vtScreen)
        return QString();

    // Convert from 1-based selection coords to 0-based libvterm coords
    int startRow = qMax(m_selection.top(), 1) - 1;
    int endRow = qMax(m_selection.bottom(), 1) - 1;
    int startCol = qMax(m_selection.left(), 1) - 1;
    int endCol = qMax(m_selection.right(), 1); // exclusive for VTermRect

    int cols = m_termSize.width();
    QString text;
    char buf[8192];

    for (int row = startRow; row <= endRow; row++) {
        int cs = (row == startRow) ? startCol : 0;
        int ce = (row == endRow) ? endCol : cols;

        VTermRect rect = { row, row + 1, cs, ce };
        size_t len = vterm_screen_get_text(m_vtScreen, buf, sizeof(buf) - 1, rect);

        QString line = QString::fromUtf8(buf, (int)len);

        // Trim trailing whitespace
        while (!line.isEmpty() && line.back() == ' ')
            line.chop(1);

        text += line;
        if (row < endRow)
            text += '\n';
    }

    return text;
}

