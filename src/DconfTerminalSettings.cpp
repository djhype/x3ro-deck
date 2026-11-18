#include "DconfTerminalSettings.h"

#include "Config.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
// GNOME's palette, which GNOME Terminal, Console and Ptyxis default to.
constexpr std::array<const char *, 16> kGnomePalette{"#171421", "#c01c28", "#26a269", "#a2734c", "#12488b", "#a347ba",
                                                     "#2aa1b3", "#d0cfcc", "#5e5c64", "#f66151", "#33da7a", "#e9ad0c",
                                                     "#2a7bde", "#c061cb", "#33c7de", "#ffffff"};

// The GNOME palette on libadwaita's dark or light view colors.
TerminalProfile gnomeProfile(const QString &source, const QString &name, bool dark, const QFont &font)
{
    TerminalProfile p;
    p.source = source;
    p.name = name;
    TerminalAppearance &a = p.appearance;
    a = TerminalAppearance::fallback();
    a.font = font;
    for (size_t i = 0; i < 16; ++i) {
        a.palette[i] = QColor::fromString(QLatin1String(kGnomePalette[i]));
    }
    a.background = dark ? QColor(0x1e, 0x1e, 0x1e) : QColor(0xff, 0xff, 0xff);
    a.foreground = dark ? QColor(0xff, 0xff, 0xff) : QColor(0x17, 0x14, 0x21);
    a.cursor = a.foreground;
    a.boldIsBright = false;
    a.padding = 4;
    return p;
}

void finish(TerminalProfile &p, const QStringList &shell)
{
    TerminalAppearance &a = p.appearance;
    a.font.setStyleHint(QFont::Monospace);
    a.font.setFixedPitch(true);
    a.font.setKerning(false);
    const QColor &f = a.foreground;
    const QColor &b = a.background;
    a.selection = QColor::fromRgbF(float(b.redF() * 0.7 + f.redF() * 0.3), float(b.greenF() * 0.7 + f.greenF() * 0.3),
                                   float(b.blueF() * 0.7 + f.blueF() * 0.3));
    p.shell = shell;
    if (p.shell.isEmpty() || p.shell.first().isEmpty()) {
        p.shell = {loginShell()};
    }
}

QStringList command(const QHash<QString, QString> &keys)
{
    if (keys.value(QStringLiteral("use-custom-command")) != QLatin1String("true")) {
        return {};
    }
    QStringList out;
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &arg : QProcess::splitCommand(DconfSource::gString(keys.value(QStringLiteral("custom-command"))))) {
        out.append(expandVariables(arg, env));
    }
    return out;
}

// "uint32 5000" or "int64 -1" → the number.
int gInt(const QString &value, int fallback)
{
    bool ok = false;
    const int n = value.section(QLatin1Char(' '), -1).toInt(&ok);
    return ok ? n : fallback;
}

void scrollback(TerminalProfile &p, const QHash<QString, QString> &keys, const QString &unlimitedKey, bool unlimitedWhenTrue)
{
    if (keys.contains(unlimitedKey)) {
        const bool flag = keys.value(unlimitedKey) == QLatin1String("true");
        if (flag == unlimitedWhenTrue) {
            p.scrollback = -1;
            return;
        }
    }
    if (keys.contains(QStringLiteral("scrollback-lines"))) {
        p.scrollback = std::max(0, gInt(keys.value(QStringLiteral("scrollback-lines")), 10000));
    }
}

// GNOME and MATE Terminal store a profile in the same shape.
void applyVteProfile(TerminalProfile &p, const QHash<QString, QString> &keys, const QFont &systemFont, bool systemBlink)
{
    TerminalAppearance &a = p.appearance;
    if (keys.value(QStringLiteral("use-system-font"), QStringLiteral("true")) == QLatin1String("false")
        && keys.contains(QStringLiteral("font"))) {
        a.font = DconfSource::pangoFont(DconfSource::gString(keys.value(QStringLiteral("font"))));
    } else {
        a.font = systemFont;
    }
    if (keys.value(QStringLiteral("use-theme-colors"), QStringLiteral("true")) == QLatin1String("false")) {
        if (auto c = DconfSource::gColor(keys.value(QStringLiteral("foreground-color")))) {
            a.foreground = *c;
        }
        if (auto c = DconfSource::gColor(keys.value(QStringLiteral("background-color")))) {
            a.background = *c;
        }
    }
    // A list (GNOME) or one colon-separated string (MATE).
    QStringList palette = DconfSource::gList(keys.value(QStringLiteral("palette")));
    if (palette.size() == 1) {
        palette = palette.first().split(QLatin1Char(':'));
    }
    for (int i = 0; i < std::min<int>(16, int(palette.size())); ++i) {
        if (auto c = DconfSource::gColor(palette.at(i))) {
            a.palette[size_t(i)] = *c;
        }
    }
    a.cursor = a.foreground;
    if (keys.value(QStringLiteral("cursor-colors-set")) == QLatin1String("true")) {
        if (auto c = DconfSource::gColor(keys.value(QStringLiteral("cursor-background-color")))) {
            a.cursor = *c;
        }
    }
    const QString shape = DconfSource::gString(keys.value(QStringLiteral("cursor-shape")));
    a.cursorShape = shape == QLatin1String("ibeam") ? TerminalAppearance::Bar
        : shape == QLatin1String("underline")       ? TerminalAppearance::Underline
                                                    : TerminalAppearance::Block;
    const QString blink = DconfSource::gString(keys.value(QStringLiteral("cursor-blink-mode")));
    a.cursorBlink = blink == QLatin1String("on") || (blink != QLatin1String("off") && systemBlink);
    a.boldIsBright = keys.value(QStringLiteral("bold-is-bright")) == QLatin1String("true");
}
} // namespace

DconfSource::Locations DconfSource::Locations::system(const QString &program)
{
    Dumper dump = [](const QString &dir) {
        QProcess p;
        p.start(QStringLiteral("dconf"), {QStringLiteral("dump"), dir});
        if (!p.waitForFinished(2000)) {
            p.kill();
            p.waitForFinished(500);
            return QByteArray();
        }
        return p.readAllStandardOutput();
    };
    return {dump, QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/dconf/user"),
            QStandardPaths::findExecutable(program)};
}

QStringList DconfSource::watchedPaths(const QString &profile) const
{
    Q_UNUSED(profile)
    // dconf rewrites its database file on every change.
    return {m_loc.dconfDb, QFileInfo(m_loc.dconfDb).absolutePath()};
}

IniGroups DconfSource::dump(const QString &dir) const
{
    return m_loc.dump ? parseIni(m_loc.dump(dir)) : IniGroups();
}

QString DconfSource::gString(const QString &value)
{
    QString v = value.trimmed();
    if (v.size() >= 2 && (v.front() == QLatin1Char('\'') || v.front() == QLatin1Char('"')) && v.back() == v.front()) {
        v = v.mid(1, v.size() - 2);
        v.replace(QStringLiteral("\\'"), QStringLiteral("'"));
        v.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    }
    return v;
}

QStringList DconfSource::gList(const QString &value)
{
    QString v = value.trimmed();
    if (v.startsWith(QLatin1Char('@'))) {
        v = v.section(QLatin1Char(' '), 1); // "@as []"
    }
    if (!v.startsWith(QLatin1Char('['))) {
        return v.isEmpty() ? QStringList() : QStringList{gString(v)};
    }
    static const QRegularExpression item(QStringLiteral(R"re('((?:[^'\\]|\\.)*)'|"((?:[^"\\]|\\.)*)")re"));
    QStringList out;
    for (auto it = item.globalMatch(v); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out.append(m.captured(1) + m.captured(2));
    }
    return out;
}

std::optional<QColor> DconfSource::gColor(const QString &value)
{
    const QString v = gString(value).trimmed();
    static const QRegularExpression rgb(QStringLiteral(R"re(^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+))re"));
    if (const QRegularExpressionMatch m = rgb.match(v); m.hasMatch()) {
        return QColor(std::clamp(m.captured(1).toInt(), 0, 255), std::clamp(m.captured(2).toInt(), 0, 255),
                      std::clamp(m.captured(3).toInt(), 0, 255));
    }
    const QColor c = QColor::fromString(v);
    return c.isValid() ? std::optional<QColor>(c) : std::nullopt;
}

QFont DconfSource::pangoFont(const QString &description)
{
    QFont font;
    QStringList words = description.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    bool ok = false;
    const double size = words.isEmpty() ? 0 : words.last().toDouble(&ok);
    if (ok) {
        words.removeLast();
        font.setPointSizeF(size);
    }
    // Style words at the end are not part of the family.
    static const QStringList styles{QStringLiteral("Bold"),  QStringLiteral("Italic"),    QStringLiteral("Oblique"),
                                    QStringLiteral("Light"), QStringLiteral("Medium"),    QStringLiteral("Regular"),
                                    QStringLiteral("Book"),  QStringLiteral("Semi-Bold"), QStringLiteral("Heavy")};
    while (words.size() > 1 && styles.contains(words.last())) {
        if (words.last() == QLatin1String("Bold")) {
            font.setBold(true);
        } else if (words.last() == QLatin1String("Italic") || words.last() == QLatin1String("Oblique")) {
            font.setItalic(true);
        }
        words.removeLast();
    }
    font.setFamily(words.join(QLatin1Char(' ')));
    return font;
}

QFont DconfSource::systemMonospace() const
{
    const QString name = gString(
        dump(QStringLiteral("/org/gnome/desktop/interface/")).value(QStringLiteral("/")).value(QStringLiteral("monospace-font-name")));
    if (name.isEmpty()) {
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        return f;
    }
    return pangoFont(name);
}

bool DconfSource::prefersDark() const
{
    return gString(dump(QStringLiteral("/org/gnome/desktop/interface/")).value(QStringLiteral("/")).value(QStringLiteral("color-scheme")))
        == QLatin1String("prefer-dark");
}

// ───────────────────────── GNOME Console ─────────────────────────

bool GnomeConsoleSettings::detect() const
{
    return !m_loc.executable.isEmpty() || !dump(QStringLiteral("/org/gnome/Console/")).isEmpty();
}

QList<TerminalSettingsSource::ProfileRef> GnomeConsoleSettings::profiles() const
{
    return {{defaultProfile(), QStringLiteral("Default")}};
}

std::optional<TerminalProfile> GnomeConsoleSettings::load(const QString &profile) const
{
    if (!profile.isEmpty() && profile != defaultProfile()) {
        return std::nullopt;
    }
    const QHash<QString, QString> keys = dump(QStringLiteral("/org/gnome/Console/")).value(QStringLiteral("/"));
    const QString theme = gString(keys.value(QStringLiteral("theme"), QStringLiteral("'auto'")));
    const bool dark = theme == QLatin1String("night") || (theme != QLatin1String("day") && prefersDark());
    QFont font = systemMonospace();
    if (keys.value(QStringLiteral("use-system-font"), QStringLiteral("true")) == QLatin1String("false")
        && keys.contains(QStringLiteral("custom-font"))) {
        font = pangoFont(gString(keys.value(QStringLiteral("custom-font"))));
    }
    TerminalProfile p = gnomeProfile(id(), QStringLiteral("Default"), dark, font);
    scrollback(p, keys, QStringLiteral("ignore-scrollback-limit"), true);
    finish(p, {});
    return p;
}

// ───────────────────────── GNOME Terminal ─────────────────────────

namespace
{
const QString kGnomeTerminal = QStringLiteral("/org/gnome/terminal/legacy/profiles:/");
}

bool GnomeTerminalSettings::detect() const
{
    return !m_loc.executable.isEmpty() || !dump(kGnomeTerminal).isEmpty();
}

QList<TerminalSettingsSource::ProfileRef> GnomeTerminalSettings::profiles() const
{
    const IniGroups groups = dump(kGnomeTerminal);
    QStringList ids = gList(groups.value(QStringLiteral("/")).value(QStringLiteral("list")));
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        if (it.key().startsWith(QLatin1Char(':')) && !ids.contains(it.key().mid(1))) {
            ids.append(it.key().mid(1));
        }
    }
    QList<ProfileRef> out;
    for (const QString &uuid : std::as_const(ids)) {
        const QString name = gString(groups.value(QLatin1Char(':') + uuid).value(QStringLiteral("visible-name")));
        out.append({uuid, name.isEmpty() ? uuid.left(8) : name});
    }
    return out;
}

QString GnomeTerminalSettings::defaultProfile() const
{
    const QString def = gString(dump(kGnomeTerminal).value(QStringLiteral("/")).value(QStringLiteral("default")));
    if (!def.isEmpty()) {
        return def;
    }
    const QList<ProfileRef> all = profiles();
    return all.isEmpty() ? QString() : all.first().id;
}

std::optional<TerminalProfile> GnomeTerminalSettings::load(const QString &profile) const
{
    const QString uuid = profile.isEmpty() ? defaultProfile() : profile;
    const IniGroups groups = dump(kGnomeTerminal);
    const QHash<QString, QString> keys = groups.value(QLatin1Char(':') + uuid);
    // No profile written yet means GNOME Terminal's built-in defaults.
    if (keys.isEmpty() && !profile.isEmpty()) {
        return std::nullopt;
    }
    const QString name = gString(keys.value(QStringLiteral("visible-name")));
    TerminalProfile p = gnomeProfile(id(), name.isEmpty() ? QStringLiteral("Default") : name, prefersDark(), systemMonospace());
    const bool systemBlink = dump(QStringLiteral("/org/gnome/desktop/interface/"))
                                 .value(QStringLiteral("/"))
                                 .value(QStringLiteral("cursor-blink"), QStringLiteral("true"))
        == QLatin1String("true");
    applyVteProfile(p, keys, p.appearance.font, systemBlink);
    scrollback(p, keys, QStringLiteral("scrollback-unlimited"), true);
    finish(p, command(keys));
    return p;
}

// ───────────────────────── MATE Terminal ─────────────────────────

namespace
{
const QString kMate = QStringLiteral("/org/mate/terminal/");
}

bool MateTerminalSettings::detect() const
{
    return !m_loc.executable.isEmpty() || !dump(kMate).isEmpty();
}

QList<TerminalSettingsSource::ProfileRef> MateTerminalSettings::profiles() const
{
    const IniGroups groups = dump(kMate);
    QStringList ids = gList(groups.value(QStringLiteral("global")).value(QStringLiteral("profile-list")));
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("profiles/")) && !ids.contains(it.key().mid(9))) {
            ids.append(it.key().mid(9));
        }
    }
    if (ids.isEmpty()) {
        ids.append(QStringLiteral("default"));
    }
    QList<ProfileRef> out;
    for (const QString &pid : std::as_const(ids)) {
        const QString name = gString(groups.value(QStringLiteral("profiles/") + pid).value(QStringLiteral("visible-name")));
        out.append({pid, name.isEmpty() ? pid : name});
    }
    return out;
}

QString MateTerminalSettings::defaultProfile() const
{
    const QString def = gString(dump(kMate).value(QStringLiteral("global")).value(QStringLiteral("default-profile")));
    return def.isEmpty() ? QStringLiteral("default") : def;
}

std::optional<TerminalProfile> MateTerminalSettings::load(const QString &profile) const
{
    const QString pid = profile.isEmpty() ? defaultProfile() : profile;
    const QHash<QString, QString> keys = dump(kMate).value(QStringLiteral("profiles/") + pid);
    if (keys.isEmpty() && !profile.isEmpty() && profile != QLatin1String("default")) {
        return std::nullopt;
    }
    const QString font
        = gString(dump(QStringLiteral("/org/mate/interface/")).value(QStringLiteral("/")).value(QStringLiteral("monospace-font-name")));
    const QString name = gString(keys.value(QStringLiteral("visible-name")));
    TerminalProfile p = gnomeProfile(id(), name.isEmpty() ? pid : name, true, font.isEmpty() ? systemMonospace() : pangoFont(font));
    applyVteProfile(p, keys, p.appearance.font, true);
    scrollback(p, keys, QStringLiteral("scrollback-unlimited"), true);
    finish(p, command(keys));
    return p;
}

// ───────────────────────── Ptyxis ─────────────────────────

namespace
{
const QString kPtyxis = QStringLiteral("/org/gnome/Ptyxis/");
}

bool PtyxisSettings::detect() const
{
    return !m_loc.executable.isEmpty() || !dump(kPtyxis).isEmpty();
}

QList<TerminalSettingsSource::ProfileRef> PtyxisSettings::profiles() const
{
    const IniGroups groups = dump(kPtyxis);
    QStringList ids = gList(groups.value(QStringLiteral("/")).value(QStringLiteral("profile-uuids")));
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("Profiles/")) && !ids.contains(it.key().mid(9))) {
            ids.append(it.key().mid(9));
        }
    }
    QList<ProfileRef> out;
    for (const QString &uuid : std::as_const(ids)) {
        const QString label = gString(groups.value(QStringLiteral("Profiles/") + uuid).value(QStringLiteral("label")));
        out.append({uuid, label.isEmpty() ? uuid.left(8) : label});
    }
    return out;
}

QString PtyxisSettings::defaultProfile() const
{
    const QString def = gString(dump(kPtyxis).value(QStringLiteral("/")).value(QStringLiteral("default-profile-uuid")));
    if (!def.isEmpty()) {
        return def;
    }
    const QList<ProfileRef> all = profiles();
    return all.isEmpty() ? QString() : all.first().id;
}

std::optional<TerminalProfile> PtyxisSettings::load(const QString &profile) const
{
    const QString uuid = profile.isEmpty() ? defaultProfile() : profile;
    const IniGroups groups = dump(kPtyxis);
    const QHash<QString, QString> root = groups.value(QStringLiteral("/"));
    const QHash<QString, QString> keys = groups.value(QStringLiteral("Profiles/") + uuid);
    if (keys.isEmpty() && !profile.isEmpty()) {
        return std::nullopt;
    }
    const QString style = gString(root.value(QStringLiteral("interface-style"), QStringLiteral("'system'")));
    const bool dark = style == QLatin1String("dark") || (style != QLatin1String("light") && prefersDark());
    QFont font = systemMonospace();
    if (root.value(QStringLiteral("use-system-font"), QStringLiteral("true")) == QLatin1String("false")
        && root.contains(QStringLiteral("font-name"))) {
        font = pangoFont(gString(root.value(QStringLiteral("font-name"))));
    }
    const QString label = gString(keys.value(QStringLiteral("label")));
    TerminalProfile p = gnomeProfile(id(), label.isEmpty() ? QStringLiteral("Default") : label, dark, font);
    if (dark) {
        p.appearance.background = QColor(0x24, 0x24, 0x24); // Ptyxis's own dark GNOME palette
    }
    scrollback(p, keys, QStringLiteral("limit-scrollback"), false);
    finish(p, command(keys));
    return p;
}
