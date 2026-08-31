/*
    Settings — the persisted configuration, shared by the terminal and the
    settings window.

    Backed by QSettings (INI) at $XDG_CONFIG_HOME/dropterm/dropterm.conf.
    Writing a property persists it immediately and emits its change signal, so
    QML bindings update live. The terminal watches the file, so edits made in
    the settings process reach a running terminal without a restart.

    Copyright 2026 ajunca — MIT License
*/

#ifndef SETTINGS_H
#define SETTINGS_H

#include <QFileSystemWatcher>
#include <QObject>
#include <QSettings>

class Settings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double widthPercent READ widthPercent WRITE setWidthPercent NOTIFY widthPercentChanged)
    Q_PROPERTY(double heightPercent READ heightPercent WRITE setHeightPercent NOTIFY heightPercentChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(double fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(QString shellProgram READ shellProgram WRITE setShellProgram NOTIFY shellProgramChanged)
    Q_PROPERTY(double backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY
                   backgroundOpacityChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)
    Q_PROPERTY(int animationMs READ animationMs WRITE setAnimationMs NOTIFY animationMsChanged)

public:
    explicit Settings(QObject *parent = nullptr);

    [[nodiscard]] static QString filePath();

    [[nodiscard]] double widthPercent() const { return m_widthPercent; }
    [[nodiscard]] double heightPercent() const { return m_heightPercent; }
    [[nodiscard]] QString fontFamily() const { return m_fontFamily; }
    [[nodiscard]] double fontSize() const { return m_fontSize; }
    [[nodiscard]] QString shellProgram() const { return m_shellProgram; }
    [[nodiscard]] double backgroundOpacity() const { return m_backgroundOpacity; }
    [[nodiscard]] int cornerRadius() const { return m_cornerRadius; }
    [[nodiscard]] int animationMs() const { return m_animationMs; }

    void setWidthPercent(double v);
    void setHeightPercent(double v);
    void setFontFamily(const QString &v);
    void setFontSize(double v);
    void setShellProgram(const QString &v);
    void setBackgroundOpacity(double v);
    void setCornerRadius(int v);
    void setAnimationMs(int v);

    // Re-read the file and emit whatever changed. Called by the watcher.
    void reload();

Q_SIGNALS:
    void widthPercentChanged();
    void heightPercentChanged();
    void fontFamilyChanged();
    void fontSizeChanged();
    void shellProgramChanged();
    void backgroundOpacityChanged();
    void cornerRadiusChanged();
    void animationMsChanged();

private:
    void load();
    void store(const char *key, const QVariant &value);
    void watch();

    double m_widthPercent = 0.6;
    double m_heightPercent = 0.3;
    QString m_fontFamily = QStringLiteral("Hack");
    double m_fontSize = 10.5;
    QString m_shellProgram;
    double m_backgroundOpacity = 0.92;
    int m_cornerRadius = 8;
    int m_animationMs = 180;

    QSettings m_store;
    QFileSystemWatcher m_watcher;
};

#endif // SETTINGS_H
