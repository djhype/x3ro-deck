#include "KonsoleSettings.h"

#include "Config.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <algorithm>

namespace
{
// Konsole's data/color-schemes/Breeze.colorscheme, the scheme of its built-in
// profile and the one it falls back to for an unknown name.
const char kBreeze[] = R"([Background]
Color=35,38,39
[BackgroundIntense]
Color=49,54,59
[Color0]
Color=35,38,39
[Color0Intense]
Color=127,140,141
[Color1]
Color=237,21,21
[Color1Intense]
Color=192,57,43
[Color2]
Color=17,209,22
[Color2Intense]
Color=28,220,154
[Color3]
Color=246,116,0
[Color3Intense]
Color=253,188,75
[Color4]
Color=29,153,243
[Color4Intense]
Color=61,174,233
[Color5]
Color=155,89,182
[Color5Intense]
Color=142,68,173
[Color6]
Color=26,188,156
[Color6Intense]
Color=22,160,133
[Color7]
Color=252,252,252
[Color7Intense]
Color=255,255,255
[Foreground]
Color=252,252,252
[ForegroundIntense]
Color=255,255,255
)";

constexpr int kKonsoleMargin = 1;
constexpr int kKonsoleHistory = 1000;

const QString kGeneral = QStringLiteral("General");
const QString kAppearance = QStringLiteral("Appearance");

// "r,g,b" as KConfig writes colors, or "#rrggbb".
std::optional<QColor> parseColor(const QString &value)
{
    const QString v = value.trimmed();
    if (v.startsWith(QLatin1Char('#'))) {
        const QColor c = QColor::fromString(v);
        return c.isValid() ? std::optional<QColor>(c) : std::nullopt;
    }
    const QStringList parts = v.split(QLatin1Char(','));
    if (parts.size() < 3 || parts.size() > 4) {
        return std::nullopt;
    }
    int rgb[3];
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        rgb[i] = parts.at(i).trimmed().toInt(&ok);
        if (!ok || rgb[i] < 0 || rgb[i] > 255) {
            return std::nullopt;
        }
    }
    return QColor(rgb[0], rgb[1], rgb[2]);
}

void applyScheme(TerminalAppearance &a, const IniGroups &scheme)
{
    auto color = [&scheme](const QString &group) {
        return parseColor(scheme.value(group).value(QStringLiteral("Color")));
    };
    if (auto c = color(QStringLiteral("Foreground"))) {
        a.foreground = *c;
    }
    if (auto c = color(QStringLiteral("Background"))) {
        a.background = *c;
    }
    for (int i = 0; i < 8; ++i) {
        const QString group = QStringLiteral("Color%1").arg(i);
        if (auto c = color(group)) {
            a.palette[size_t(i)] = *c;
            a.palette[size_t(i + 8)] = *c; // until the intense entry says otherwise
        }
        if (auto c = color(group + QStringLiteral("Intense"))) {
            a.palette[size_t(i + 8)] = *c;
        }
    }
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() * (1 - t) + b.redF() * t), float(a.greenF() * (1 - t) + b.greenF() * t),
                            float(a.blueF() * (1 - t) + b.blueF() * t));
}

bool isTrue(const QString &value)
{
    return value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || value == QLatin1String("1");
}
} // namespace

KonsoleSettings::Locations KonsoleSettings::Locations::system()
{
    return {QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
            QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation),
            QStandardPaths::findExecutable(QStringLiteral("konsole"))};
}

KonsoleSettings::KonsoleSettings(Locations locations)
    : m_loc(std::move(locations))
{
}

QStringList KonsoleSettings::desktopEntries() const
{
    return {QStringLiteral("org.kde.konsole"), QStringLiteral("konsole")};
}

QStringList KonsoleSettings::nativeDesktops() const
{
    return {QStringLiteral("KDE")};
}

QStringList KonsoleSettings::konsoleDirs() const
{
    QStringList dirs;
    for (const QString &d : m_loc.dataDirs) {
        const QString dir = QDir(d).filePath(QStringLiteral("konsole"));
        if (!dirs.contains(dir)) {
            dirs.append(dir);
        }
    }
    return dirs;
}

bool KonsoleSettings::detect() const
{
    if (!m_loc.executable.isEmpty() || QFileInfo::exists(QDir(m_loc.configDir).filePath(QStringLiteral("konsolerc")))) {
        return true;
    }
    for (const QString &dir : konsoleDirs()) {
        if (!QDir(dir).entryList({QStringLiteral("*.profile")}, QDir::Files).isEmpty()) {
            return true;
        }
    }
    return false;
}

QList<TerminalSettingsSource::ProfileRef> KonsoleSettings::profiles() const
{
    QList<ProfileRef> list;
    QStringList seen;
    for (const QString &dir : konsoleDirs()) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.profile")}, QDir::Files, QDir::Name);
        for (const QFileInfo &file : files) {
            if (seen.contains(file.fileName())) {
                continue; // a user profile hides the system one of the same name
            }
            seen.append(file.fileName());
            QString name = readIni(file.filePath()).value(kGeneral).value(QStringLiteral("Name"));
            list.append({file.fileName(), name.isEmpty() ? file.completeBaseName() : name});
        }
    }
    std::sort(list.begin(), list.end(),
              [](const ProfileRef &a, const ProfileRef &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    list.prepend({builtinProfile(), QStringLiteral("Built-in")});
    return list;
}

QString KonsoleSettings::defaultProfile() const
{
    const QString name = readIni(QDir(m_loc.configDir).filePath(QStringLiteral("konsolerc")))
                             .value(QStringLiteral("Desktop Entry"))
                             .value(QStringLiteral("DefaultProfile"));
    const QString path = profilePath(name);
    return path.isEmpty() ? builtinProfile() : QFileInfo(path).fileName();
}

// A profile by file name, path (Parent= stores one) or display name.
QString KonsoleSettings::profilePath(const QString &ref) const
{
    if (ref.isEmpty() || ref == builtinProfile()) {
        return {};
    }
    if (QDir::isAbsolutePath(ref) && QFileInfo(ref).isFile()) {
        return ref;
    }
    QString file = QFileInfo(ref).fileName();
    if (!file.endsWith(QLatin1String(".profile"))) {
        file += QLatin1String(".profile");
    }
    const QStringList dirs = konsoleDirs();
    for (const QString &dir : dirs) {
        const QString path = QDir(dir).filePath(file);
        if (QFileInfo(path).isFile()) {
            return path;
        }
    }
    for (const QString &dir : dirs) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.profile")}, QDir::Files, QDir::Name);
        for (const QFileInfo &f : files) {
            if (readIni(f.filePath()).value(kGeneral).value(QStringLiteral("Name")) == ref) {
                return f.filePath();
            }
        }
    }
    return {};
}

QStringList KonsoleSettings::profileChain(const QString &profile) const
{
    QStringList chain;
    QString path = profilePath(profile);
    while (!path.isEmpty() && !chain.contains(path) && chain.size() < 16) {
        chain.append(path);
        path = profilePath(readIni(path).value(kGeneral).value(QStringLiteral("Parent")));
    }
    return chain;
}

QString KonsoleSettings::colorSchemePath(const QString &name) const
{
    if (name.isEmpty()) {
        return {};
    }
    for (const QString &dir : konsoleDirs()) {
        const QString path = QDir(dir).filePath(name + QStringLiteral(".colorscheme"));
        if (QFileInfo(path).isFile()) {
            return path;
        }
    }
    return {};
}

std::optional<TerminalProfile> KonsoleSettings::load(const QString &profile) const
{
    const QString id = profile.isEmpty() ? defaultProfile() : profile;
    const QStringList chain = profileChain(id);
    if (chain.isEmpty() && id != builtinProfile()) {
        return std::nullopt;
    }
    // Each profile overrides its parent, key by key.
    IniGroups merged;
    for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
        const IniGroups groups = readIni(*it);
        for (auto g = groups.begin(); g != groups.end(); ++g) {
            for (auto k = g->begin(); k != g->end(); ++k) {
                merged[g.key()].insert(k.key(), k.value());
            }
        }
    }
    auto value = [&merged](const QString &group, const QString &key) {
        return merged.value(group).value(key);
    };
    // Konsole has moved a few keys between groups over the years.
    auto anyGroup = [&merged](const QString &key) {
        for (const QString &g : {kGeneral, kAppearance}) {
            if (merged.value(g).contains(key)) {
                return merged.value(g).value(key);
            }
        }
        return QString();
    };

    TerminalProfile p;
    p.source = this->id();
    p.name = chain.isEmpty() ? QStringLiteral("Built-in") : value(kGeneral, QStringLiteral("Name"));
    if (p.name.isEmpty()) {
        p.name = QFileInfo(chain.first()).completeBaseName();
    }

    TerminalAppearance &a = p.appearance;
    a = TerminalAppearance::fallback();
    a.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QString font = value(kAppearance, QStringLiteral("Font"));
    if (QFont f; !font.isEmpty() && f.fromString(font)) {
        a.font = f;
    }
    a.font.setStyleHint(QFont::Monospace);
    a.font.setFixedPitch(true);
    a.font.setKerning(false);

    applyScheme(a, parseIni(QByteArray(kBreeze)));
    if (const QString scheme = colorSchemePath(value(kAppearance, QStringLiteral("ColorScheme"))); !scheme.isEmpty()) {
        applyScheme(a, readIni(scheme));
    }
    a.cursor = a.foreground;
    if (isTrue(value(QStringLiteral("Cursor Options"), QStringLiteral("UseCustomCursorColor")))) {
        if (auto c = parseColor(value(QStringLiteral("Cursor Options"), QStringLiteral("CustomCursorColor")))) {
            a.cursor = *c;
        }
    }
    // Konsole inverts selected cells; a wash keeps colored text readable.
    a.selection = mix(a.background, a.foreground, 0.3);

    switch (value(QStringLiteral("Cursor Options"), QStringLiteral("CursorShape")).toInt()) {
    case 1:
        a.cursorShape = TerminalAppearance::Bar; // Konsole's I-beam
        break;
    case 2:
        a.cursorShape = TerminalAppearance::Underline;
        break;
    default:
        a.cursorShape = TerminalAppearance::Block;
    }
    a.cursorBlink = isTrue(value(QStringLiteral("Terminal Features"), QStringLiteral("BlinkingCursorEnabled")));

    bool ok = false;
    const int margin = anyGroup(QStringLiteral("TerminalMargin")).toInt(&ok);
    a.padding = ok ? std::clamp(margin, 0, 100) : kKonsoleMargin;
    a.lineSpacing = std::clamp(anyGroup(QStringLiteral("LineSpacing")).toInt(), 0, 100);
    const QString bold = value(kAppearance, QStringLiteral("BoldIntense"));
    a.boldIsBright = bold.isEmpty() || isTrue(bold);

    const QHash<QString, QString> scrolling = merged.value(QStringLiteral("Scrolling"));
    if (scrolling.contains(QStringLiteral("HistoryMode")) || scrolling.contains(QStringLiteral("HistorySize"))) {
        switch (scrolling.value(QStringLiteral("HistoryMode"), QStringLiteral("1")).toInt()) {
        case 0:
            p.scrollback = 0;
            break;
        case 2:
            p.scrollback = -1;
            break;
        default:
            p.scrollback = std::max(0, scrolling.value(QStringLiteral("HistorySize"), QString::number(kKonsoleHistory)).toInt());
        }
    }

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &arg : QProcess::splitCommand(value(kGeneral, QStringLiteral("Command")))) {
        p.shell.append(expandVariables(arg, env));
    }
    if (p.shell.isEmpty() || p.shell.first().isEmpty()) {
        p.shell = {loginShell()};
    }
    return p;
}

QStringList KonsoleSettings::watchedPaths(const QString &profile) const
{
    QStringList paths{QDir(m_loc.configDir).filePath(QStringLiteral("konsolerc"))};
    paths += konsoleDirs();
    const QString id = profile.isEmpty() ? defaultProfile() : profile;
    const QStringList chain = profileChain(id);
    paths += chain;
    QString scheme;
    for (const QString &file : chain) {
        const QString s = readIni(file).value(kAppearance).value(QStringLiteral("ColorScheme"));
        if (!s.isEmpty()) {
            scheme = s;
            break; // the leaf-most profile that names one wins
        }
    }
    if (const QString path = colorSchemePath(scheme); !path.isEmpty()) {
        paths.append(path);
    }
    return paths;
}
