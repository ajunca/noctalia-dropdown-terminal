/*
    Copyright 2026 ajunca — MIT License
*/

#include "settings.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
// Bounds are enforced here rather than in the UI so a hand-edited file cannot
// produce an unusable window.
constexpr double kMinWidth = 0.2;
constexpr double kMaxWidth = 1.0;
constexpr double kMinHeight = 0.15;
constexpr double kMaxHeight = 1.0;
constexpr double kMinFontSize = 4.0;
constexpr double kMaxFontSize = 72.0;
constexpr int kMaxCornerRadius = 64;
constexpr int kMaxAnimationMs = 2000;
} // namespace

QString Settings::filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(dir).filePath(QStringLiteral("dropterm/dropterm.conf"));
}

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_store(filePath(), QSettings::IniFormat)
{
    load();
    watch();
}

void Settings::load()
{
    m_widthPercent = qBound(kMinWidth, m_store.value("widthPercent", m_widthPercent).toDouble(), kMaxWidth);
    m_heightPercent =
        qBound(kMinHeight, m_store.value("heightPercent", m_heightPercent).toDouble(), kMaxHeight);
    m_fontFamily = m_store.value("fontFamily", m_fontFamily).toString();
    m_fontSize = qBound(kMinFontSize, m_store.value("fontSize", m_fontSize).toDouble(), kMaxFontSize);
    m_shellProgram = m_store.value("shellProgram", m_shellProgram).toString();
    m_backgroundOpacity = qBound(0.0, m_store.value("backgroundOpacity", m_backgroundOpacity).toDouble(), 1.0);
    m_cornerRadius = qBound(0, m_store.value("cornerRadius", m_cornerRadius).toInt(), kMaxCornerRadius);
    m_animationMs = qBound(0, m_store.value("animationMs", m_animationMs).toInt(), kMaxAnimationMs);
}

void Settings::watch()
{
    const QString path = filePath();
    // QSettings only creates the file on first write, and editors replace it
    // rather than modifying in place, so watch the directory too and re-add the
    // file whenever it reappears.
    const QString dir = QFileInfo(path).absolutePath();
    QDir().mkpath(dir);
    m_watcher.addPath(dir);
    if (QFileInfo::exists(path)) {
        m_watcher.addPath(path);
    }

    const auto rescan = [this, path] {
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path)) {
            m_watcher.addPath(path);
        }
        reload();
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, rescan);
}

void Settings::reload()
{
    const double oldWidth = m_widthPercent;
    const double oldHeight = m_heightPercent;
    const QString oldFamily = m_fontFamily;
    const double oldSize = m_fontSize;
    const QString oldShell = m_shellProgram;
    const double oldOpacity = m_backgroundOpacity;
    const int oldRadius = m_cornerRadius;
    const int oldAnim = m_animationMs;

    m_store.sync();
    load();

    if (!qFuzzyCompare(oldWidth, m_widthPercent)) {
        Q_EMIT widthPercentChanged();
    }
    if (!qFuzzyCompare(oldHeight, m_heightPercent)) {
        Q_EMIT heightPercentChanged();
    }
    if (oldFamily != m_fontFamily) {
        Q_EMIT fontFamilyChanged();
    }
    if (!qFuzzyCompare(oldSize, m_fontSize)) {
        Q_EMIT fontSizeChanged();
    }
    if (oldShell != m_shellProgram) {
        Q_EMIT shellProgramChanged();
    }
    if (!qFuzzyCompare(oldOpacity, m_backgroundOpacity)) {
        Q_EMIT backgroundOpacityChanged();
    }
    if (oldRadius != m_cornerRadius) {
        Q_EMIT cornerRadiusChanged();
    }
    if (oldAnim != m_animationMs) {
        Q_EMIT animationMsChanged();
    }
}

void Settings::store(const char *key, const QVariant &value)
{
    m_store.setValue(QLatin1String(key), value);
    m_store.sync();
}

void Settings::setWidthPercent(double v)
{
    v = qBound(kMinWidth, v, kMaxWidth);
    if (qFuzzyCompare(m_widthPercent, v)) {
        return;
    }
    m_widthPercent = v;
    store("widthPercent", v);
    Q_EMIT widthPercentChanged();
}

void Settings::setHeightPercent(double v)
{
    v = qBound(kMinHeight, v, kMaxHeight);
    if (qFuzzyCompare(m_heightPercent, v)) {
        return;
    }
    m_heightPercent = v;
    store("heightPercent", v);
    Q_EMIT heightPercentChanged();
}

void Settings::setFontFamily(const QString &v)
{
    if (m_fontFamily == v || v.isEmpty()) {
        return;
    }
    m_fontFamily = v;
    store("fontFamily", v);
    Q_EMIT fontFamilyChanged();
}

void Settings::setFontSize(double v)
{
    v = qBound(kMinFontSize, v, kMaxFontSize);
    if (qFuzzyCompare(m_fontSize, v)) {
        return;
    }
    m_fontSize = v;
    store("fontSize", v);
    Q_EMIT fontSizeChanged();
}

void Settings::setShellProgram(const QString &v)
{
    if (m_shellProgram == v) {
        return;
    }
    m_shellProgram = v;
    store("shellProgram", v);
    Q_EMIT shellProgramChanged();
}

void Settings::setBackgroundOpacity(double v)
{
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_backgroundOpacity, v)) {
        return;
    }
    m_backgroundOpacity = v;
    store("backgroundOpacity", v);
    Q_EMIT backgroundOpacityChanged();
}

void Settings::setCornerRadius(int v)
{
    v = qBound(0, v, kMaxCornerRadius);
    if (m_cornerRadius == v) {
        return;
    }
    m_cornerRadius = v;
    store("cornerRadius", v);
    Q_EMIT cornerRadiusChanged();
}

void Settings::setAnimationMs(int v)
{
    v = qBound(0, v, kMaxAnimationMs);
    if (m_animationMs == v) {
        return;
    }
    m_animationMs = v;
    store("animationMs", v);
    Q_EMIT animationMsChanged();
}
