/*
    Settings — the persisted configuration, shared by the terminal and the
    settings window.

    Three sources, each with one owner:

      dropterm/dropterm.conf  the user's own overrides — the only file this
                              program ever writes
      dropterm/theme.conf     written by a desktop theme engine (noctalia, or
                              matugen/pywal/a shell script) and refreshed with
                              `dropterm reload`
      dropterm/defaults.conf  read-only baseline, typically a store symlink
                              written by the home-manager module

    Lookup is user -> theme -> defaults -> built-in. Putting the user first is
    the point: choosing a colour by hand deliberately stops that key following
    the desktop palette, and "reset" clears the user file so it follows again.
    Writing the baseline or theme file from here would fight their owners.

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

#include <array>

#include <QObject>
#include <QSet>
#include <QSettings>
#include <QVariant>
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
    // Which layer each colour currently comes from, for the UI to show. Tied to
    // the colour's own signal: clearing an override always re-emits, so these
    // stay correct even when clearing does not change the resulting value.
    Q_PROPERTY(QString foregroundSource READ foregroundSource NOTIFY foregroundChanged)
    Q_PROPERTY(QString backgroundSource READ backgroundSource NOTIFY backgroundChanged)

public:
    explicit Settings(QObject *parent = nullptr);
    ~Settings() override;

    // The writable user file.
    [[nodiscard]] static QString filePath();
    // The read-only baseline, if one has been provisioned.
    [[nodiscard]] static QString defaultsFilePath();
    // Written by a theme engine; absent unless something provisions it.
    [[nodiscard]] static QString themeFilePath();

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
    [[nodiscard]] QString foregroundSource() const { return sourceOf(QStringLiteral("foreground")); }
    [[nodiscard]] QString backgroundSource() const { return sourceOf(QStringLiteral("background")); }

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

    // Drop every user override, falling back to the provisioned baseline and
    // then to the built-in values.
    Q_INVOKABLE void resetToDefaults();

    // True when a baseline file exists, so the UI can say what reset means.
    [[nodiscard]] Q_INVOKABLE bool hasProvisionedDefaults() const;

    // Which layer a key's current value comes from: "user", "theme",
    // "baseline" or "builtin". Lets the UI show why a value is what it is,
    // rather than leaving the layering invisible.
    [[nodiscard]] Q_INVOKABLE QString sourceOf(const QString &key) const;

    // Drop this key's user override so it follows the lower layers again.
    // This is how "stop customising, follow the desktop theme" is expressed —
    // there is no separate follow/don't-follow flag to keep in sync.
    Q_INVOKABLE void clearOverride(const QString &key);

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
    // user override -> theme -> provisioned baseline -> built-in.
    // The typed variants skip a layer whose value will not parse, so a
    // half-written file cannot win with a garbage value.
    [[nodiscard]] std::array<const QSettings *, 3> layers() const { return {&m_store, &m_theme, &m_baseline}; }
    [[nodiscard]] QVariant resolve(const char *key, const QVariant &builtin) const;
    [[nodiscard]] double resolveDouble(const char *key, double builtin) const;
    [[nodiscard]] int resolveInt(const char *key, int builtin) const;
    [[nodiscard]] QString resolveColour(const char *key, const QString &builtin) const;
    void load();
    void emitAll();
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

    QSettings m_store;      // user overrides, writable
    QSettings m_theme;      // theme engine output, read-only
    QSettings m_baseline;   // provisioned defaults, read-only
    QSet<QString> m_dirty;
    QTimer m_flushTimer;
};

#endif // SETTINGS_H
