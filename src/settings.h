/*
    Settings — the persisted configuration, shared by the terminal and the
    settings window.

    Backed by QSettings (INI) at $XDG_CONFIG_HOME/dropterm/dropterm.conf.

    Writing a property updates memory and emits immediately, so the UI stays
    live, but the disk write is debounced and then flushed as a single atomic
    sync of every pending key. That matters twice over: a slider emits
    continuously while dragged, so per-key writes would mean hundreds of file
    writes per drag; and a partial write would let the terminal observe an
    incoherent set (new width, old height).

    After a flush the terminal is told over the local socket. There is
    deliberately no QFileSystemWatcher: QSettings replaces the file by rename on
    every sync, which drops a watch, so watching correctly means also watching
    the directory and re-adding the file — fiddly, and unnecessary when we
    already have an IPC channel.

    Copyright 2026 ajunca — MIT License
*/

#ifndef SETTINGS_H
#define SETTINGS_H

#include <QObject>
#include <QSet>
#include <QSettings>
#include <QTimer>

class Settings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double widthPercent READ widthPercent WRITE setWidthPercent NOTIFY widthPercentChanged)
    Q_PROPERTY(double heightPercent READ heightPercent WRITE setHeightPercent NOTIFY heightPercentChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(double fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(QString shellProgram READ shellProgram WRITE setShellProgram NOTIFY shellProgramChanged)
    Q_PROPERTY(QString foreground READ foreground WRITE setForeground NOTIFY foregroundChanged)
    Q_PROPERTY(QString background READ background WRITE setBackground NOTIFY backgroundChanged)
    Q_PROPERTY(double backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY
                   backgroundOpacityChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)
    Q_PROPERTY(int animationMs READ animationMs WRITE setAnimationMs NOTIFY animationMsChanged)

public:
    explicit Settings(QObject *parent = nullptr);
    ~Settings() override;

    [[nodiscard]] static QString filePath();

    [[nodiscard]] double widthPercent() const { return m_widthPercent; }
    [[nodiscard]] double heightPercent() const { return m_heightPercent; }
    [[nodiscard]] QString fontFamily() const { return m_fontFamily; }
    [[nodiscard]] double fontSize() const { return m_fontSize; }
    [[nodiscard]] QString shellProgram() const { return m_shellProgram; }
    [[nodiscard]] QString foreground() const { return m_foreground; }
    [[nodiscard]] QString background() const { return m_background; }
    [[nodiscard]] double backgroundOpacity() const { return m_backgroundOpacity; }
    [[nodiscard]] int cornerRadius() const { return m_cornerRadius; }
    [[nodiscard]] int animationMs() const { return m_animationMs; }

    void setWidthPercent(double v);
    void setHeightPercent(double v);
    void setFontFamily(const QString &v);
    void setFontSize(double v);
    void setShellProgram(const QString &v);
    void setForeground(const QString &v);
    void setBackground(const QString &v);
    void setBackgroundOpacity(double v);
    void setCornerRadius(int v);
    void setAnimationMs(int v);

    // Re-read from disk and emit whatever changed. The terminal calls this when
    // the settings process reports a write.
    Q_INVOKABLE void reload();

    // Restore every value to its built-in default.
    Q_INVOKABLE void resetToDefaults();

Q_SIGNALS:
    void widthPercentChanged();
    void heightPercentChanged();
    void fontFamilyChanged();
    void fontSizeChanged();
    void shellProgramChanged();
    void foregroundChanged();
    void backgroundChanged();
    void backgroundOpacityChanged();
    void cornerRadiusChanged();
    void animationMsChanged();

private:
    void load();
    void markDirty(const char *key);
    void flush();

    double m_widthPercent = 0.6;
    double m_heightPercent = 0.3;
    QString m_fontFamily = QStringLiteral("Hack");
    double m_fontSize = 10.5;
    QString m_shellProgram;
    QString m_foreground = QStringLiteral("#ebebeb");
    QString m_background = QStringLiteral("#000000");
    double m_backgroundOpacity = 0.92;
    int m_cornerRadius = 8;
    int m_animationMs = 180;

    QSettings m_store;
    QSet<QString> m_dirty;
    QTimer m_flushTimer;
};

#endif // SETTINGS_H
