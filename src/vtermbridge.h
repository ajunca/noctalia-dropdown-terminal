/*
    VTermBridge — libvterm-neovim wrapper for literm Qt6 QML plugin.
    Replaces the old Terminal + Parser classes with a complete terminal
    emulator backed by libvterm (the same library used by neovim).

    Copyright 2024 — GPL v2+
*/

#ifndef VTERMBRIDGE_H
#define VTERMBRIDGE_H

#include <QChar>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QRgb>
#include <QSize>
#include <QVector>

extern "C" {
#include <vterm.h>
}

// ---------- Data types (kept for TextRender compatibility) ----------

struct TermChar {
    enum TextAttributes {
        NoAttributes = 0x00,
        BoldAttribute = 0x01,
        ItalicAttribute = 0x02,
        UnderlineAttribute = 0x04,
        NegativeAttribute = 0x08,
        BlinkAttribute = 0x10
    };

    QChar c;
    QRgb fgColor = qRgb(235, 235, 235);
    QRgb bgColor = qRgb(0, 0, 0);
    int attrib = NoAttributes;

    bool operator==(const TermChar& other) const
    {
        return c == other.c && fgColor == other.fgColor
            && bgColor == other.bgColor && attrib == other.attrib;
    }
    bool operator!=(const TermChar& other) const { return !(*this == other); }
};

class TerminalLine {
public:
    void append(const TermChar& tc) { m_contents.append(tc); }
    int size() const { return m_contents.size(); }
    const TermChar& at(int i) const { return m_contents.at(i); }
    TermChar& operator[](int i) { return m_contents[i]; }
    void clear() { m_contents.clear(); }

private:
    QVector<TermChar> m_contents;
};

class TerminalBuffer {
public:
    void append(const TerminalLine& line) { m_buffer.append(line); }
    int size() const { return m_buffer.size(); }
    const TerminalLine& at(int i) const { return m_buffer.at(i); }
    TerminalLine& operator[](int i) { return m_buffer[i]; }
    void removeAt(int i) { m_buffer.removeAt(i); }
    void clear() { m_buffer.clear(); }

private:
    QVector<TerminalLine> m_buffer;
};

// ---------- VTermBridge ----------

class PtyIFace;

class VTermBridge : public QObject {
    Q_OBJECT
public:
    explicit VTermBridge(QObject* parent = nullptr);
    ~VTermBridge();

    // Initialization
    void init(const QString& charset = "UTF-8",
        const QByteArray& termEnv = "xterm-256color",
        const QString& command = "");

    // Size
    void setTermSize(QSize size);
    int rows() const { return m_termSize.height(); }
    int columns() const { return m_termSize.width(); }

    // Buffer access (TextRender reads these)
    TerminalBuffer& buffer() { return m_screenBuffer; }
    const TerminalBuffer& buffer() const { return m_screenBuffer; }
    TerminalBuffer& backBuffer() { return m_backBuffer; }
    const TerminalBuffer& backBuffer() const { return m_backBuffer; }

    // Cursor
    QPoint cursorPos() const { return m_cursorPos; }
    bool showCursor() const { return m_showCursor; }

    // State
    bool isInitialized() const { return m_vt != nullptr; }
    bool inverseVideoMode() const { return m_reverseVideo; }
    bool useAltScreenBuffer() const { return m_altScreen; }
    QString title() const { return m_title; }

    // Scrollback
    int backBufferScrollPos() const { return m_backBufferScrollPos; }
    void scrollBackBufferFwd(int lines);
    void scrollBackBufferBack(int lines);
    void resetBackBufferScrollPos();
    void setBackBufferScrollPos(int pos);

    // Input
    void keyPress(int key, int modifiers, const QString& text = "");
    void paste(const QString& text);
    void putString(QString str);

    // Selection
    QString selectedText() const;
    void setSelection(QPoint start, QPoint end, bool selectionOngoing);
    QRect selection() const { return m_selection; }
    void clearSelection();

    // Misc
    const QStringList printableLinesFromCursor(int lines);
    const QStringList grabURLsFromBuffer();

    TermChar zeroChar;

signals:
    void displayBufferChanged();
    void cursorPosChanged(QPoint newPos);
    void termSizeChanged(int rows, int columns);
    void selectionChanged();
    void scrollBackBufferAdjusted(bool reset);
    void windowTitleChanged(const QString& title);
    void visualBell();
    void hangupReceived();

private slots:
    void onDataAvailable();

private:
    // libvterm
    VTerm* m_vt = nullptr;
    VTermScreen* m_vtScreen = nullptr;

    // PTY
    PtyIFace* m_pty = nullptr;

    // Terminal state
    QSize m_termSize;
    bool m_showCursor = true;
    bool m_altScreen = false;
    bool m_reverseVideo = false;
    QPoint m_cursorPos = QPoint(1, 1); // 1-indexed

    // Buffers
    TerminalBuffer m_screenBuffer;
    TerminalBuffer m_backBuffer;
    int m_backBufferScrollPos = 0;
    static const int MAX_SCROLLBACK = 5000;

    // Selection
    QRect m_selection;
    bool m_selectionOngoing = false;

    // Title
    QString m_title;
    QString m_pendingTitle;

    // Conversions
    QRgb vtermColorToQRgb(VTermColor col) const;
    TermChar cellToTermChar(const VTermScreenCell& cell) const;
    TerminalLine cellsToLine(int cols, const VTermScreenCell* cells) const;

    // Rebuild screen buffer from libvterm
    void rebuildScreenBuffer();

    // Static libvterm callbacks
    static int cb_damage(VTermRect rect, void* user);
    static int cb_movecursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int cb_settermprop(VTermProp prop, VTermValue* val, void* user);
    static int cb_bell(void* user);
    static int cb_resize(int rows, int cols, void* user);
    static int cb_sb_pushline(int cols, const VTermScreenCell* cells, void* user);
    static int cb_sb_popline(int cols, VTermScreenCell* cells, void* user);
    static int cb_sb_clear(void* user);
    static void cb_output(const char* s, size_t len, void* user);

    friend class TextRender;
};

#endif // VTERMBRIDGE_H
