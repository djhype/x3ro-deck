// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GpuTerminalSettings.h"

#include "Config.h"
#include "DetectorUtil.h"
#include "Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace
{
using Pairs = QList<QPair<QString, QString>>;

// The terminals' own defaults, so a config that sets only a font still looks
// like that terminal.
struct Defaults
{
    const char *foreground;
    const char *background;
    std::array<const char *, 16> palette;
    double fontSize;
};

constexpr Defaults kKitty{"#dddddd",
                          "#000000",
                          {"#000000", "#cc0403", "#19cb00", "#cecb00", "#0d73cc", "#cb1ed1", "#0dcdcd", "#dddddd", "#767676", "#f2201f",
                           "#23fd00", "#fffd00", "#1a8fff", "#fd28ff", "#14ffff", "#ffffff"},
                          11.0};
// Ghostty draws with the xterm palette unless a theme says otherwise.
constexpr Defaults kGhostty{"#ffffff",
                            "#282c34",
                            {"#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000ee", "#cd00cd", "#00cdcd", "#e5e5e5", "#7f7f7f", "#ff0000",
                             "#00ff00", "#ffff00", "#5c5cff", "#ff00ff", "#00ffff", "#ffffff"},
                            13.0};
constexpr Defaults kAlacritty{"#d8d8d8",
                              "#181818",
                              {"#181818", "#ac4242", "#90a959", "#f4bf75", "#6a9fb5", "#aa759f", "#75b5aa", "#d8d8d8", "#6b6b6b", "#c55555",
                               "#aabb77", "#feca88", "#82b8c8", "#c28cb8", "#93d3c3", "#f8f8f8"},
                              11.25};

TerminalProfile startProfile(const QString &source, const QString &name, const Defaults &d)
{
    TerminalProfile p;
    p.source = source;
    p.name = name;
    TerminalAppearance &a = p.appearance;
    a = TerminalAppearance::fallback();
    a.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    a.font.setPointSizeF(d.fontSize);
    a.foreground = QColor::fromString(QLatin1String(d.foreground));
    a.background = QColor::fromString(QLatin1String(d.background));
    for (size_t i = 0; i < 16; ++i) {
        a.palette[i] = QColor::fromString(QLatin1String(d.palette[i]));
    }
    a.cursor = a.foreground;
    a.selection = QColor();
    a.padding = 2;
    a.boldIsBright = false;
    return p;
}

void finish(TerminalProfile &p, const QString &fontFamily, double fontSize, const QStringList &shell)
{
    TerminalAppearance &a = p.appearance;
    if (!fontFamily.isEmpty() && fontFamily != QLatin1String("monospace")) {
        a.font.setFamily(fontFamily);
    }
    if (fontSize > 0) {
        a.font.setPointSizeF(fontSize);
    }
    a.font.setStyleHint(QFont::Monospace);
    a.font.setFixedPitch(true);
    a.font.setKerning(false);
    if (!a.selection.isValid()) {
        const QColor &f = a.foreground;
        const QColor &b = a.background;
        a.selection = QColor::fromRgbF(float(b.redF() * 0.7 + f.redF() * 0.3), float(b.greenF() * 0.7 + f.greenF() * 0.3),
                                       float(b.blueF() * 0.7 + f.blueF() * 0.3));
    }
    p.shell = shell;
    if (p.shell.isEmpty() || p.shell.first().isEmpty()) {
        p.shell = {loginShell()};
    }
}

// "#rgb", "#rrggbb", "rrggbb", "0xrrggbb" or an X11 name.
std::optional<QColor> color(QString v)
{
    v = DetectorUtil::unquote(v);
    if (v.startsWith(QLatin1String("0x"))) {
        v = QLatin1Char('#') + v.mid(2);
    } else if (!v.startsWith(QLatin1Char('#')) && QRegularExpression(QStringLiteral("^[0-9a-fA-F]{6}$")).match(v).hasMatch()) {
        v.prepend(QLatin1Char('#'));
    }
    const QColor c = QColor::fromString(v);
    return c.isValid() ? std::optional<QColor>(c) : std::nullopt;
}

QString expandPath(const QString &path, const QString &relativeTo)
{
    QString p = expandVariables(Paths::expandTilde(path.trimmed()), QProcessEnvironment::systemEnvironment());
    return QDir::isAbsolutePath(p) ? QDir::cleanPath(p) : QDir(relativeTo).filePath(p);
}

QStringList splitCommand(const QString &command)
{
    QStringList out;
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &arg : QProcess::splitCommand(command)) {
        out.append(expandVariables(arg, env));
    }
    return out;
}

// ── kitty ──

// key value lines in order, includes expanded in place.
void readKitty(const QString &file, Pairs &out, QStringList &read, int depth = 0)
{
    if (depth > 8 || read.contains(file) || !QFileInfo::exists(file)) {
        return;
    }
    read.append(file);
    const QString dir = QFileInfo(file).absolutePath();
    for (const QString &raw : QString::fromUtf8(DetectorUtil::readFile(file)).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const qsizetype space = line.indexOf(QRegularExpression(QStringLiteral("\\s")));
        const QString key = space < 0 ? line : line.left(space);
        const QString value = space < 0 ? QString() : line.mid(space + 1).trimmed();
        if (key == QLatin1String("include")) {
            readKitty(expandPath(value, dir), out, read, depth + 1);
        } else if (key == QLatin1String("globinclude")) {
            const QString pattern = expandPath(value, dir);
            const QDir d(QFileInfo(pattern).absolutePath());
            for (const QString &f : d.entryList({QFileInfo(pattern).fileName()}, QDir::Files, QDir::Name)) {
                readKitty(d.filePath(f), out, read, depth + 1);
            }
        } else {
            out.append({key, value});
        }
    }
}

// ── Ghostty ──

void readGhostty(const QString &file, Pairs &out, QStringList &read, int depth = 0)
{
    if (depth > 8 || read.contains(file) || !QFileInfo::exists(file)) {
        return;
    }
    read.append(file);
    const QString dir = QFileInfo(file).absolutePath();
    QStringList later; // config-file includes load after the file itself
    for (const QString &raw : QString::fromUtf8(DetectorUtil::readFile(file)).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || eq < 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();
        if (value.startsWith(QLatin1Char('"'))) {
            value = DetectorUtil::unquote(value);
        }
        if (key == QLatin1String("config-file")) {
            later.append(expandPath(value.startsWith(QLatin1Char('?')) ? value.mid(1) : value, dir));
        } else {
            out.append({key, value});
        }
    }
    for (const QString &f : std::as_const(later)) {
        readGhostty(f, out, read, depth + 1);
    }
}

// ── Alacritty ──

// TOML key paths ("colors.primary.background") to raw values; inline tables
// are flattened.
void readAlacritty(const QString &file, QList<QPair<QString, QString>> &out, QStringList &read, int depth = 0)
{
    if (depth > 8 || read.contains(file) || !QFileInfo::exists(file)) {
        return;
    }
    read.append(file);
    const QList<DetectorUtil::TomlEntry> entries = DetectorUtil::tomlEntries(DetectorUtil::readFile(file));
    // Imports load first; this file's own values win.
    for (const DetectorUtil::TomlEntry &e : entries) {
        if (e.key == QLatin1String("import") && (e.table.isEmpty() || e.table == QLatin1String("general"))) {
            for (const QJsonValue &v : QJsonDocument::fromJson(e.value.toUtf8()).array()) {
                readAlacritty(expandPath(v.toString(), QFileInfo(file).absolutePath()), out, read, depth + 1);
            }
        }
    }
    static const QRegularExpression inlinePair(QStringLiteral(R"re(([A-Za-z0-9_-]+)\s*=\s*("[^"]*"|'[^']*'|[^,}]+))re"));
    for (const DetectorUtil::TomlEntry &e : entries) {
        const QString key = e.table.isEmpty() ? e.key : e.table + QLatin1Char('.') + e.key;
        if (e.value.startsWith(QLatin1Char('{'))) {
            for (auto it = inlinePair.globalMatch(e.value); it.hasNext();) {
                const QRegularExpressionMatch m = it.next();
                out.append({key + QLatin1Char('.') + m.captured(1), m.captured(2).trimmed()});
            }
        } else {
            out.append({key, e.value});
        }
    }
}

const char *const kAnsiNames[] = {"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"};
} // namespace

PlainConfigSource::Locations PlainConfigSource::Locations::system(const QString &program)
{
    return {QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), QDir::homePath(),
            QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation), QStandardPaths::findExecutable(program)};
}

bool PlainConfigSource::detect() const
{
    return !m_loc.executable.isEmpty() || !configFile().isEmpty();
}

QList<TerminalSettingsSource::ProfileRef> PlainConfigSource::profiles() const
{
    return {{defaultProfile(),
             configFile().isEmpty() ? QStringLiteral("Default") : QStringLiteral("Default (%1)").arg(Paths::tilde(configFile()))}};
}

QStringList PlainConfigSource::watchedPaths(const QString &profile) const
{
    Q_UNUSED(profile)
    QStringList paths = filesRead();
    if (!configFile().isEmpty()) {
        paths.append(QFileInfo(configFile()).absolutePath());
    }
    paths.removeDuplicates();
    return paths;
}

// ───────────────────────── kitty ─────────────────────────

QString KittySettings::configFile() const
{
    const QString env = qEnvironmentVariable("KITTY_CONFIG_DIRECTORY");
    for (const QString &f :
         {env.isEmpty() ? QString() : env + QStringLiteral("/kitty.conf"), m_loc.configDir + QStringLiteral("/kitty/kitty.conf")}) {
        if (!f.isEmpty() && QFileInfo::exists(f)) {
            return f;
        }
    }
    return {};
}

QStringList KittySettings::filesRead() const
{
    Pairs pairs;
    QStringList read;
    readKitty(configFile(), pairs, read);
    return read;
}

std::optional<TerminalProfile> KittySettings::load(const QString &profile) const
{
    if (!profile.isEmpty() && profile != defaultProfile()) {
        return std::nullopt;
    }
    Pairs pairs;
    QStringList read;
    readKitty(configFile(), pairs, read);
    TerminalProfile p = startProfile(id(), QStringLiteral("Default"), kKitty);
    TerminalAppearance &a = p.appearance;
    QString family;
    double size = 0;
    QStringList shell;
    bool cursorSet = false;
    for (const auto &[key, value] : std::as_const(pairs)) {
        if (key == QLatin1String("font_family")) {
            family = value;
        } else if (key == QLatin1String("font_size")) {
            size = value.toDouble();
        } else if (key == QLatin1String("foreground")) {
            a.foreground = color(value).value_or(a.foreground);
        } else if (key == QLatin1String("background")) {
            a.background = color(value).value_or(a.background);
        } else if (key == QLatin1String("cursor")) {
            if (auto c = color(value)) {
                a.cursor = *c;
                cursorSet = true;
            }
        } else if (key == QLatin1String("selection_background")) {
            a.selection = color(value).value_or(a.selection);
        } else if (key.startsWith(QLatin1String("color"))) {
            bool ok = false;
            const int n = key.mid(5).toInt(&ok);
            if (ok && n >= 0 && n < 16) {
                a.palette[size_t(n)] = color(value).value_or(a.palette[size_t(n)]);
            }
        } else if (key == QLatin1String("cursor_shape")) {
            a.cursorShape = value == QLatin1String("beam") ? TerminalAppearance::Bar
                : value == QLatin1String("underline")      ? TerminalAppearance::Underline
                                                           : TerminalAppearance::Block;
        } else if (key == QLatin1String("cursor_blink_interval")) {
            a.cursorBlink = value.section(QLatin1Char(' '), 0, 0).toDouble() != 0;
        } else if (key == QLatin1String("scrollback_lines")) {
            const int lines = value.toInt();
            p.scrollback = lines < 0 ? -1 : lines;
        } else if (key == QLatin1String("window_padding_width")) {
            a.padding = std::clamp(int(value.section(QLatin1Char(' '), 0, 0).toDouble()), 0, 100);
        } else if (key == QLatin1String("shell")) {
            shell = value == QLatin1String(".") ? QStringList() : splitCommand(value);
        }
    }
    if (!cursorSet) {
        a.cursor = QColor(0xcc, 0xcc, 0xcc); // kitty's default cursor
    }
    a.cursorBlink = a.cursorBlink
        || !std::any_of(pairs.cbegin(), pairs.cend(), [](const auto &kv) { return kv.first == QLatin1String("cursor_blink_interval"); });
    finish(p, family, size, shell);
    return p;
}

// ───────────────────────── Ghostty ─────────────────────────

QString GhosttySettings::configFile() const
{
    for (const QString &name : {QStringLiteral("config.ghostty"), QStringLiteral("config")}) {
        const QString f = m_loc.configDir + QStringLiteral("/ghostty/") + name;
        if (QFileInfo::exists(f)) {
            return f;
        }
    }
    return {};
}

QString GhosttySettings::themeFile(const QString &name) const
{
    if (name.isEmpty()) {
        return {};
    }
    if (QDir::isAbsolutePath(Paths::expandTilde(name))) {
        return QFileInfo::exists(Paths::expandTilde(name)) ? Paths::expandTilde(name) : QString();
    }
    QStringList dirs{m_loc.configDir + QStringLiteral("/ghostty/themes")};
    for (const QString &d : m_loc.dataDirs) {
        dirs.append(d + QStringLiteral("/ghostty/themes"));
    }
    if (!m_loc.executable.isEmpty()) {
        // An install under a prefix keeps them in <prefix>/share.
        dirs.append(QFileInfo(m_loc.executable).absolutePath() + QStringLiteral("/../share/ghostty/themes"));
    }
    for (const QString &d : std::as_const(dirs)) {
        if (QFileInfo::exists(d + QLatin1Char('/') + name)) {
            return QDir::cleanPath(d + QLatin1Char('/') + name);
        }
    }
    return {};
}

namespace
{
// "light:A,dark:B" → B; a single name stays.
QString darkTheme(const QString &value)
{
    if (!value.contains(QLatin1Char(':'))) {
        return value;
    }
    for (const QString &part : value.split(QLatin1Char(','))) {
        if (part.trimmed().startsWith(QLatin1String("dark:"))) {
            return part.trimmed().mid(5);
        }
    }
    return value.section(QLatin1Char(','), 0, 0).section(QLatin1Char(':'), 1);
}
} // namespace

QStringList GhosttySettings::filesRead() const
{
    Pairs pairs;
    QStringList read;
    readGhostty(configFile(), pairs, read);
    for (const auto &[key, value] : std::as_const(pairs)) {
        if (key == QLatin1String("theme")) {
            if (const QString t = themeFile(darkTheme(value)); !t.isEmpty()) {
                read.append(t);
            }
        }
    }
    return read;
}

std::optional<TerminalProfile> GhosttySettings::load(const QString &profile) const
{
    if (!profile.isEmpty() && profile != defaultProfile()) {
        return std::nullopt;
    }
    Pairs config;
    QStringList read;
    readGhostty(configFile(), config, read);
    // The theme sets defaults; the config's own keys win over it.
    Pairs pairs;
    for (const auto &[key, value] : std::as_const(config)) {
        if (key == QLatin1String("theme")) {
            QStringList themeRead;
            Pairs theme;
            readGhostty(themeFile(darkTheme(value)), theme, themeRead);
            pairs = theme + pairs;
        }
    }
    pairs += config;
    TerminalProfile p = startProfile(id(), QStringLiteral("Default"), kGhostty);
    TerminalAppearance &a = p.appearance;
    QString family;
    double size = 0;
    QStringList shell;
    bool cursorSet = false;
    for (const auto &[key, value] : std::as_const(pairs)) {
        if (key == QLatin1String("font-family")) {
            if (family.isEmpty() || value.isEmpty()) {
                family = value; // the first wins; an empty value resets the list
            }
        } else if (key == QLatin1String("font-size")) {
            size = value.toDouble();
        } else if (key == QLatin1String("foreground")) {
            a.foreground = color(value).value_or(a.foreground);
        } else if (key == QLatin1String("background")) {
            a.background = color(value).value_or(a.background);
        } else if (key == QLatin1String("cursor-color")) {
            if (auto c = color(value)) {
                a.cursor = *c;
                cursorSet = true;
            }
        } else if (key == QLatin1String("selection-background")) {
            a.selection = color(value).value_or(a.selection);
        } else if (key == QLatin1String("palette")) {
            const int n = value.section(QLatin1Char('='), 0, 0).trimmed().toInt();
            if (n >= 0 && n < 16) {
                a.palette[size_t(n)] = color(value.section(QLatin1Char('='), 1)).value_or(a.palette[size_t(n)]);
            }
        } else if (key == QLatin1String("cursor-style")) {
            a.cursorShape = value == QLatin1String("bar") ? TerminalAppearance::Bar
                : value == QLatin1String("underline")     ? TerminalAppearance::Underline
                                                          : TerminalAppearance::Block;
        } else if (key == QLatin1String("cursor-style-blink")) {
            a.cursorBlink = value != QLatin1String("false");
        } else if (key == QLatin1String("window-padding-x")) {
            a.padding = std::clamp(value.section(QLatin1Char(','), 0, 0).trimmed().toInt(), 0, 100);
        } else if (key == QLatin1String("command")) {
            shell = splitCommand(value);
        }
    }
    if (!cursorSet) {
        a.cursor = a.foreground;
    }
    finish(p, family, size, shell);
    return p;
}

// ───────────────────────── Alacritty ─────────────────────────

QString AlacrittySettings::configFile() const
{
    for (const QString &f :
         {m_loc.configDir + QStringLiteral("/alacritty/alacritty.toml"), m_loc.configDir + QStringLiteral("/alacritty.toml"),
          m_loc.home + QStringLiteral("/.config/alacritty/alacritty.toml"), m_loc.home + QStringLiteral("/.alacritty.toml")}) {
        if (QFileInfo::exists(f)) {
            return f;
        }
    }
    return {};
}

QStringList AlacrittySettings::filesRead() const
{
    QList<QPair<QString, QString>> pairs;
    QStringList read;
    readAlacritty(configFile(), pairs, read);
    return read;
}

std::optional<TerminalProfile> AlacrittySettings::load(const QString &profile) const
{
    if (!profile.isEmpty() && profile != defaultProfile()) {
        return std::nullopt;
    }
    QList<QPair<QString, QString>> pairs;
    QStringList read;
    readAlacritty(configFile(), pairs, read);
    TerminalProfile p = startProfile(id(), QStringLiteral("Default"), kAlacritty);
    TerminalAppearance &a = p.appearance;
    QString family;
    double size = 0;
    QString program;
    QStringList args;
    bool cursorSet = false;
    for (const auto &[key, raw] : std::as_const(pairs)) {
        const QString value = DetectorUtil::unquote(raw);
        if (key == QLatin1String("font.normal.family")) {
            family = value;
        } else if (key == QLatin1String("font.size")) {
            size = value.toDouble();
        } else if (key == QLatin1String("colors.primary.foreground")) {
            a.foreground = color(value).value_or(a.foreground);
        } else if (key == QLatin1String("colors.primary.background")) {
            a.background = color(value).value_or(a.background);
        } else if (key == QLatin1String("colors.cursor.cursor")) {
            if (auto c = color(value)) {
                a.cursor = *c;
                cursorSet = true;
            }
        } else if (key == QLatin1String("colors.selection.background")) {
            a.selection = color(value).value_or(a.selection);
        } else if (key.startsWith(QLatin1String("colors.normal.")) || key.startsWith(QLatin1String("colors.bright."))) {
            const bool bright = key.startsWith(QLatin1String("colors.bright."));
            const QString name = key.section(QLatin1Char('.'), 2);
            for (size_t i = 0; i < 8; ++i) {
                if (name == QLatin1String(kAnsiNames[i])) {
                    a.palette[i + (bright ? 8 : 0)] = color(value).value_or(a.palette[i + (bright ? 8 : 0)]);
                }
            }
        } else if (key == QLatin1String("cursor.style.shape") || key == QLatin1String("cursor.style")) {
            const QString shape = value.toLower();
            a.cursorShape = shape == QLatin1String("beam") ? TerminalAppearance::Bar
                : shape == QLatin1String("underline")      ? TerminalAppearance::Underline
                                                           : TerminalAppearance::Block;
        } else if (key == QLatin1String("cursor.style.blinking")) {
            a.cursorBlink = value == QLatin1String("On") || value == QLatin1String("Always");
        } else if (key == QLatin1String("scrolling.history")) {
            p.scrollback = std::max(0, value.toInt());
        } else if (key == QLatin1String("window.padding.x")) {
            a.padding = std::clamp(value.toInt(), 0, 100);
        } else if (key == QLatin1String("terminal.shell.program") || key == QLatin1String("shell.program")
                   || key == QLatin1String("terminal.shell")) {
            program = value;
        } else if (key == QLatin1String("terminal.shell.args") || key == QLatin1String("shell.args")) {
            args.clear();
            for (const QJsonValue &v : QJsonDocument::fromJson(raw.toUtf8()).array()) {
                args.append(v.toString());
            }
        }
    }
    if (!cursorSet) {
        a.cursor = a.foreground;
    }
    finish(p, family, size, program.isEmpty() ? QStringList() : QStringList{program} + args);
    return p;
}
