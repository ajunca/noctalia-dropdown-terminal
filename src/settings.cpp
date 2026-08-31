/*
    Copyright 2026 ajunca — MIT License
*/

#include "settings.h"

#include "ipc.h"

#include <QColor>
#include <QDir>
#include <QStandardPaths>

namespace {

// Bounds live here rather than in the UI, so a hand-edited file cannot produce
// an unusable window either.
constexpr double kMinWidth = 0.2;
constexpr double kMaxWidth = 1.0;
constexpr double kMinHeight = 0.15;
constexpr double kMaxHeight = 1.0;
constexpr double kMinFontSize = 4.0;
constexpr double kMaxFontSize = 72.0;
constexpr int kMaxCornerRadius = 64;
constexpr int kMaxAnimationMs = 2000;

// Long enough to swallow a slider drag, short enough to feel immediate.
constexpr int kFlushDelayMs = 300;

QString normalisedColour(const QString &value, const QString &fallback)
{
    const QColor c(value);
    return c.isValid() ? c.name(QColor::HexRgb) : fallback;
}

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

    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(kFlushDelayMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &Settings::flush);
}

Settings::~Settings()
{
    // Never lose an edit to a debounce window still in flight.
    if (!m_dirty.isEmpty()) {
        flush();
    }
}

void Settings::load()
{
    m_widthPercent = qBound(kMinWidth, m_store.value("widthPercent", m_widthPercent).toDouble(), kMaxWidth);
    m_heightPercent =
        qBound(kMinHeight, m_store.value("heightPercent", m_heightPercent).toDouble(), kMaxHeight);
    m_fontFamily = m_store.value("fontFamily", m_fontFamily).toString();
    m_fontSize = qBound(kMinFontSize, m_store.value("fontSize", m_fontSize).toDouble(), kMaxFontSize);
    m_shellProgram = m_store.value("shellProgram", m_shellProgram).toString();
    m_foreground = normalisedColour(m_store.value("foreground", m_foreground).toString(), m_foreground);
    m_background = normalisedColour(m_store.value("background", m_background).toString(), m_background);
    m_backgroundOpacity = qBound(0.0, m_store.value("backgroundOpacity", m_backgroundOpacity).toDouble(), 1.0);
    m_cornerRadius = qBound(0, m_store.value("cornerRadius", m_cornerRadius).toInt(), kMaxCornerRadius);
    m_animationMs = qBound(0, m_store.value("animationMs", m_animationMs).toInt(), kMaxAnimationMs);
}

void Settings::markDirty(const char *key)
{
    m_dirty.insert(QString::fromLatin1(key));
    m_flushTimer.start(); // restarts: the write lands after the last change
}

void Settings::flush()
{
    if (m_dirty.isEmpty()) {
        return;
    }

    // One coherent write of everything pending. QSettings::sync() replaces the
    // file via a temporary and a rename, so a reader sees either the old file
    // or the new one, never a half-written mixture.
    for (const QString &key : std::as_const(m_dirty)) {
        if (key == QLatin1String("widthPercent")) {
            m_store.setValue(key, m_widthPercent);
        } else if (key == QLatin1String("heightPercent")) {
            m_store.setValue(key, m_heightPercent);
        } else if (key == QLatin1String("fontFamily")) {
            m_store.setValue(key, m_fontFamily);
        } else if (key == QLatin1String("fontSize")) {
            m_store.setValue(key, m_fontSize);
        } else if (key == QLatin1String("shellProgram")) {
            m_store.setValue(key, m_shellProgram);
        } else if (key == QLatin1String("foreground")) {
            m_store.setValue(key, m_foreground);
        } else if (key == QLatin1String("background")) {
            m_store.setValue(key, m_background);
        } else if (key == QLatin1String("backgroundOpacity")) {
            m_store.setValue(key, m_backgroundOpacity);
        } else if (key == QLatin1String("cornerRadius")) {
            m_store.setValue(key, m_cornerRadius);
        } else if (key == QLatin1String("animationMs")) {
            m_store.setValue(key, m_animationMs);
        }
    }
    m_dirty.clear();
    m_store.sync();

    // Tell a running terminal to re-read. Failure just means none is running.
    ipc::send(QByteArrayLiteral("reload"));
}

void Settings::reload()
{
    const double oldWidth = m_widthPercent;
    const double oldHeight = m_heightPercent;
    const QString oldFamily = m_fontFamily;
    const double oldSize = m_fontSize;
    const QString oldShell = m_shellProgram;
    const QString oldFg = m_foreground;
    const QString oldBg = m_background;
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
    if (oldFg != m_foreground) {
        Q_EMIT foregroundChanged();
    }
    if (oldBg != m_background) {
        Q_EMIT backgroundChanged();
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

void Settings::resetToDefaults()
{
    setWidthPercent(0.6);
    setHeightPercent(0.3);
    setFontFamily(QStringLiteral("Hack"));
    setFontSize(10.5);
    setShellProgram(QString());
    setForeground(QStringLiteral("#ebebeb"));
    setBackground(QStringLiteral("#000000"));
    setBackgroundOpacity(0.92);
    setCornerRadius(8);
    setAnimationMs(180);
}

void Settings::setWidthPercent(double v)
{
    v = qBound(kMinWidth, v, kMaxWidth);
    if (qFuzzyCompare(m_widthPercent, v)) {
        return;
    }
    m_widthPercent = v;
    markDirty("widthPercent");
    Q_EMIT widthPercentChanged();
}

void Settings::setHeightPercent(double v)
{
    v = qBound(kMinHeight, v, kMaxHeight);
    if (qFuzzyCompare(m_heightPercent, v)) {
        return;
    }
    m_heightPercent = v;
    markDirty("heightPercent");
    Q_EMIT heightPercentChanged();
}

void Settings::setFontFamily(const QString &v)
{
    if (m_fontFamily == v || v.isEmpty()) {
        return;
    }
    m_fontFamily = v;
    markDirty("fontFamily");
    Q_EMIT fontFamilyChanged();
}

void Settings::setFontSize(double v)
{
    v = qBound(kMinFontSize, v, kMaxFontSize);
    if (qFuzzyCompare(m_fontSize, v)) {
        return;
    }
    m_fontSize = v;
    markDirty("fontSize");
    Q_EMIT fontSizeChanged();
}

void Settings::setShellProgram(const QString &v)
{
    if (m_shellProgram == v) {
        return;
    }
    m_shellProgram = v;
    markDirty("shellProgram");
    Q_EMIT shellProgramChanged();
}

void Settings::setForeground(const QString &v)
{
    const QString normalised = normalisedColour(v, m_foreground);
    if (m_foreground == normalised) {
        return;
    }
    m_foreground = normalised;
    markDirty("foreground");
    Q_EMIT foregroundChanged();
}

void Settings::setBackground(const QString &v)
{
    const QString normalised = normalisedColour(v, m_background);
    if (m_background == normalised) {
        return;
    }
    m_background = normalised;
    markDirty("background");
    Q_EMIT backgroundChanged();
}

void Settings::setBackgroundOpacity(double v)
{
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_backgroundOpacity, v)) {
        return;
    }
    m_backgroundOpacity = v;
    markDirty("backgroundOpacity");
    Q_EMIT backgroundOpacityChanged();
}

void Settings::setCornerRadius(int v)
{
    v = qBound(0, v, kMaxCornerRadius);
    if (m_cornerRadius == v) {
        return;
    }
    m_cornerRadius = v;
    markDirty("cornerRadius");
    Q_EMIT cornerRadiusChanged();
}

void Settings::setAnimationMs(int v)
{
    v = qBound(0, v, kMaxAnimationMs);
    if (m_animationMs == v) {
        return;
    }
    m_animationMs = v;
    markDirty("animationMs");
    Q_EMIT animationMsChanged();
}
