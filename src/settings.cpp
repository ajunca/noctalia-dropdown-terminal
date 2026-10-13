/*
    Copyright 2026 ajunca — MIT License
*/

#include "settings.h"

#include "ipc.h"

#include <QColor>

#include <optional>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

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

// A layer only counts if it holds a *usable* value for this key. Containing it
// is not enough: a half-rendered template leaves entries like
// "backgroundOpacity=", which QVariant::toDouble() happily reports as 0.0 —
// clamped to a fully transparent terminal that has simply vanished. An
// unparseable entry must fall through to the next layer instead of winning.
std::optional<double> layerDouble(const SettingsLayer &s, const QString &k)
{
    if (!s.contains(k)) {
        return std::nullopt;
    }
    bool ok = false;
    const double v = s.value(k).toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}

std::optional<int> layerInt(const SettingsLayer &s, const QString &k)
{
    if (!s.contains(k)) {
        return std::nullopt;
    }
    bool ok = false;
    const int v = s.value(k).toInt(&ok);
    return ok ? std::optional<int>(v) : std::nullopt;
}

std::optional<QString> layerColour(const SettingsLayer &s, const QString &k)
{
    if (!s.contains(k)) {
        return std::nullopt;
    }
    const QColor c(s.value(k).toString());
    return c.isValid() ? std::optional<QString>(c.name(QColor::HexRgb)) : std::nullopt;
}

QString ansiKey(int index)
{
    return QStringLiteral("color%1").arg(index);
}

bool isColourKey(const QString &key)
{
    static const QRegularExpression ansi(QStringLiteral("^color([0-9]|1[0-5])$"));
    return key == QLatin1String("foreground") || key == QLatin1String("background") || ansi.match(key).hasMatch();
}

QStringList colorSchemeDirs()
{
    QStringList dirs{QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                         .filePath(QStringLiteral("dropterm/colorschemes"))};
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        dirs.append(QDir(dir).filePath(QStringLiteral("konsole")));
    }
    return dirs;
}

// Konsole writes "Color=r,g,b" (optionally ",a"); KConfig also accepts the
// usual colour names and #rrggbb, so take those too.
QColor parseSchemeColour(const QString &value)
{
    const QStringList parts = value.split(QLatin1Char(','));
    if (parts.size() < 3) {
        return QColor(value);
    }
    int rgb[3];
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        rgb[i] = parts[i].trimmed().toInt(&ok);
        if (!ok || rgb[i] < 0 || rgb[i] > 255) {
            return {};
        }
    }
    return QColor(rgb[0], rgb[1], rgb[2]);
}

// The dropterm key a .colorscheme group sets, or empty for one with no
// equivalent. Konsole's ColorN/ColorNIntense are the normal and bright halves
// of the ANSI palette. Faint variants, the intense and faint foreground and
// background (Konsole's bold/dim default text) and the colour randomisation
// have nothing to map to and are ignored.
QString schemeKey(const QString &group)
{
    if (group == QLatin1String("Foreground")) {
        return QStringLiteral("foreground");
    }
    if (group == QLatin1String("Background")) {
        return QStringLiteral("background");
    }
    static const QRegularExpression ansi(QStringLiteral("^Color([0-7])(Intense)?$"));
    const QRegularExpressionMatch m = ansi.match(group);
    if (!m.hasMatch()) {
        return {};
    }
    return ansiKey(m.captured(1).toInt() + (m.hasCaptured(2) ? 8 : 0));
}

// A Konsole .colorscheme — what Yakuake uses too — translated into dropterm's
// keys. It is KConfig INI, but read by hand: QSettings does not understand the
// "[Color0] # Black" comments stock schemes put after group names.
QVariantHash readColorScheme(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QVariantHash out;
    QString group;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            const int close = line.indexOf(QLatin1Char(']'));
            group = close > 0 ? line.mid(1, close - 1).trimmed() : QString();
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        // Left as text: resolution parses and validates every layer alike.
        if (group == QLatin1String("General") && key == QLatin1String("Opacity")) {
            out.insert(QStringLiteral("backgroundOpacity"), value);
            continue;
        }
        if (key != QLatin1String("Color")) {
            continue;
        }
        const QString target = schemeKey(group);
        const QColor colour = parseSchemeColour(value);
        if (!target.isEmpty() && colour.isValid()) {
            out.insert(target, colour.name(QColor::HexRgb));
        }
    }
    return out;
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

QString Settings::locateColorScheme(const QString &name)
{
    QString n = name.trimmed();
    if (n.isEmpty()) {
        return {};
    }
    if (n.startsWith(QLatin1String("~/"))) {
        n = QDir::homePath() + n.mid(1);
    }
    if (n.contains(QLatin1Char('/'))) {
        return QFileInfo(n).isFile() ? n : QString();
    }
    if (!n.endsWith(QLatin1String(".colorscheme"))) {
        n += QLatin1String(".colorscheme");
    }
    for (const QString &dir : colorSchemeDirs()) {
        const QString path = QDir(dir).filePath(n);
        if (QFileInfo(path).isFile()) {
            return path;
        }
    }
    return {};
}

QStringList Settings::availableColorSchemes()
{
    QStringList names;
    for (const QString &dir : colorSchemeDirs()) {
        const QStringList files =
            QDir(dir).entryList({QStringLiteral("*.colorscheme")}, QDir::Files | QDir::Readable);
        for (const QString &file : files) {
            names.append(file.chopped(int(sizeof(".colorscheme") - 1)));
        }
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

std::array<SettingsLayer, 3> Settings::fileLayers() const
{
    return {{{"user", &m_store}, {"theme", &m_theme}, {"baseline", &m_baseline}}};
}

QVarLengthArray<SettingsLayer, 4> Settings::layers() const
{
    const std::array<SettingsLayer, 3> files = fileLayers();
    QVarLengthArray<SettingsLayer, 4> out;
    for (int i = 0; i < int(files.size()); ++i) {
        out.append(files[i]);
        if (i == m_schemeOwner && !m_scheme.isEmpty()) {
            out.append({"scheme", nullptr, &m_scheme});
        }
    }
    return out;
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
    for (const SettingsLayer &s : layers()) {
        if (s.contains(k)) {
            return s.value(k);
        }
    }
    return builtin;
}

double Settings::resolveDouble(const char *key, double builtin) const
{
    const QString k = QString::fromLatin1(key);
    for (const SettingsLayer &s : layers()) {
        if (const auto v = layerDouble(s, k)) {
            return *v;
        }
    }
    return builtin;
}

int Settings::resolveInt(const char *key, int builtin) const
{
    const QString k = QString::fromLatin1(key);
    for (const SettingsLayer &s : layers()) {
        if (const auto v = layerInt(s, k)) {
            return *v;
        }
    }
    return builtin;
}

QString Settings::resolveColour(const QString &key, const QString &builtin) const
{
    for (const SettingsLayer &s : layers()) {
        if (const auto v = layerColour(s, key)) {
            return *v;
        }
    }
    return builtin;
}

void Settings::load()
{
    // The scheme first, since where it sits in the lookup depends on which
    // file named it. Found by presence, like shellProgram: an empty value in
    // the user file is how "no scheme" overrides one declared lower down.
    m_colorScheme.clear();
    m_schemeOwner = -1;
    const std::array<SettingsLayer, 3> files = fileLayers();
    for (int i = 0; i < int(files.size()); ++i) {
        if (files[i].contains(QStringLiteral("colorScheme"))) {
            m_colorScheme = files[i].value(QStringLiteral("colorScheme")).toString().trimmed();
            m_schemeOwner = i;
            break;
        }
    }
    m_scheme.clear();
    if (!m_colorScheme.isEmpty()) {
        const QString path = locateColorScheme(m_colorScheme);
        if (path.isEmpty()) {
            qWarning("dropterm: colour scheme \"%s\" not found", qUtf8Printable(m_colorScheme));
        } else {
            m_scheme = readColorScheme(path);
        }
    }

    m_widthPercent = qBound(kMinWidth, resolveDouble("widthPercent", kDefWidth), kMaxWidth);
    m_heightPercent = qBound(kMinHeight, resolveDouble("heightPercent", kDefHeight), kMaxHeight);
    m_fontFamily = resolve("fontFamily", kDefFontFamily).toString();
    m_fontSize = qBound(kMinFontSize, resolveDouble("fontSize", kDefFontSize), kMaxFontSize);
    // Empty is meaningful here (use the passwd shell), so no validity filter.
    m_shellProgram = resolve("shellProgram", QString()).toString();
    m_foreground = resolveColour(QStringLiteral("foreground"), kDefForeground);
    m_background = resolveColour(QStringLiteral("background"), kDefBackground);
    m_ansiColors.clear();
    for (int i = 0; i < 16; ++i) {
        m_ansiColors.append(resolveColour(ansiKey(i), QString()));
    }
    m_backgroundOpacity = qBound(0.0, resolveDouble("backgroundOpacity", kDefOpacity), 1.0);
    m_cornerRadius = qBound(0, resolveInt("cornerRadius", kDefRadius), kMaxCornerRadius);
    m_animationMs = qBound(0, resolveInt("animationMs", kDefAnimationMs), kMaxAnimationMs);
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
        } else if (key == QLatin1String("colorScheme")) {
            m_store.setValue(key, m_colorScheme);
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
    const QString oldScheme = m_colorScheme;
    const QStringList oldAnsi = m_ansiColors;

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
    if (oldScheme != m_colorScheme) {
        Q_EMIT colorSchemeChanged();
    }
    if (oldAnsi != m_ansiColors) {
        Q_EMIT ansiColorsChanged();
    }
}

QString Settings::sourceOf(const QString &key) const
{
    // m_dirty as well as m_store: writes are debounced, so a key the user just
    // set is not on disk yet. Reporting it as still coming from the theme would
    // leave the UI claiming a value follows the palette when it no longer does,
    // and — because nothing emits when the flush eventually lands — the tag
    // would never correct itself.
    // Colour keys are the ones the UI shows, and they are filtered on validity
    // when resolved — so report the layer that actually supplied the value, not
    // merely one that mentions the key.
    const bool isColour = isColourKey(key);
    const auto holds = [&](const SettingsLayer &s) {
        return isColour ? layerColour(s, key).has_value() : s.contains(key);
    };
    if (m_dirty.contains(key)) {
        return QStringLiteral("user");
    }
    for (const SettingsLayer &s : layers()) {
        if (holds(s)) {
            return QString::fromLatin1(s.name);
        }
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
    Q_EMIT colorSchemeChanged();
    Q_EMIT ansiColorsChanged();
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

void Settings::setColorScheme(const QString &v)
{
    const QString name = v.trimmed();
    if (m_colorScheme == name) {
        return;
    }
    m_colorScheme = name;
    markDirty("colorScheme");

    // Unlike every other key, this one moves a whole layer, so it cannot be
    // applied in memory alone: write it now, along with anything else
    // pending so the re-read cannot lose it, and resolve everything again.
    m_flushTimer.stop();
    flush();
    reload();
    // As in clearOverride(): sources can change without the values changing.
    emitAll();
}
