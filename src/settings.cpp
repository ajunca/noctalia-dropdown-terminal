/*
    Copyright 2026 ajunca — MIT License
*/

#include "settings.h"

#include "ipc.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
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

// Built-in values, used when neither the user file nor a provisioned baseline
// supplies a key. Kept as named constants rather than the members' initialisers
// so that "reset" has something to fall back to after the members have moved.
constexpr double kDefWidth = 0.6;
constexpr double kDefHeight = 0.3;
constexpr double kDefFontSize = 10.5;
constexpr double kDefOpacity = 0.92;
constexpr int kDefRadius = 8;
constexpr int kDefAnimationMs = 180;
const QString kDefFontFamily = QStringLiteral("Hack");
const QString kDefForeground = QStringLiteral("#ebebeb");
const QString kDefBackground = QStringLiteral("#000000");

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

QString Settings::defaultsFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(dir).filePath(QStringLiteral("dropterm/defaults.conf"));
}

QString Settings::themeFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(dir).filePath(QStringLiteral("dropterm/theme.conf"));
}

bool Settings::hasProvisionedDefaults() const
{
    return QFileInfo::exists(defaultsFilePath());
}

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_store(filePath(), QSettings::IniFormat)
    , m_theme(themeFilePath(), QSettings::IniFormat)
    , m_baseline(defaultsFilePath(), QSettings::IniFormat)
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

QVariant Settings::resolve(const char *key, const QVariant &builtin) const
{
    const QString k = QString::fromLatin1(key);
    if (m_store.contains(k)) {
        return m_store.value(k);
    }
    if (m_theme.contains(k)) {
        return m_theme.value(k);
    }
    if (m_baseline.contains(k)) {
        return m_baseline.value(k);
    }
    return builtin;
}

void Settings::load()
{
    m_widthPercent = qBound(kMinWidth, resolve("widthPercent", kDefWidth).toDouble(), kMaxWidth);
    m_heightPercent = qBound(kMinHeight, resolve("heightPercent", kDefHeight).toDouble(), kMaxHeight);
    m_fontFamily = resolve("fontFamily", kDefFontFamily).toString();
    m_fontSize = qBound(kMinFontSize, resolve("fontSize", kDefFontSize).toDouble(), kMaxFontSize);
    m_shellProgram = resolve("shellProgram", QString()).toString();
    m_foreground = normalisedColour(resolve("foreground", kDefForeground).toString(), kDefForeground);
    m_background = normalisedColour(resolve("background", kDefBackground).toString(), kDefBackground);
    m_backgroundOpacity = qBound(0.0, resolve("backgroundOpacity", kDefOpacity).toDouble(), 1.0);
    m_cornerRadius = qBound(0, resolve("cornerRadius", kDefRadius).toInt(), kMaxCornerRadius);
    m_animationMs = qBound(0, resolve("animationMs", kDefAnimationMs).toInt(), kMaxAnimationMs);
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
    ipc::send("reload");
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

    // Every layer load() consults, not just the two that usually move: a
    // rebuild can replace the baseline under a running terminal, and `reload`
    // is the only thing that would ever notice.
    m_store.sync();
    m_theme.sync();
    m_baseline.sync();
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

QString Settings::sourceOf(const QString &key) const
{
    if (m_store.contains(key)) {
        return QStringLiteral("user");
    }
    if (m_theme.contains(key)) {
        return QStringLiteral("theme");
    }
    if (m_baseline.contains(key)) {
        return QStringLiteral("baseline");
    }
    return QStringLiteral("builtin");
}

void Settings::emitAll()
{
    Q_EMIT widthPercentChanged();
    Q_EMIT heightPercentChanged();
    Q_EMIT fontFamilyChanged();
    Q_EMIT fontSizeChanged();
    Q_EMIT shellProgramChanged();
    Q_EMIT foregroundChanged();
    Q_EMIT backgroundChanged();
    Q_EMIT backgroundOpacityChanged();
    Q_EMIT cornerRadiusChanged();
    Q_EMIT animationMsChanged();
}

void Settings::clearOverride(const QString &key)
{
    if (!m_store.contains(key)) {
        return;
    }
    m_dirty.remove(key); // a pending write for this key is now moot
    m_store.remove(key);
    m_store.sync();
    reload();
    // reload() only emits where the value moved, but dropping an override can
    // change a key's *source* without changing its value — the UI has to be
    // told either way.
    emitAll();
    ipc::send("reload");
}

void Settings::resetToDefaults()
{
    // Clearing the overrides rather than writing built-in values back is what
    // makes reset mean "return to the declared baseline" when one exists, and
    // "return to the built-in values" when it does not.
    m_dirty.clear();
    m_flushTimer.stop();
    m_store.clear();
    m_store.sync();
    reload();
    emitAll();
    ipc::send("reload");
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
