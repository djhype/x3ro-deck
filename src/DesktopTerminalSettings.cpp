#include "DesktopTerminalSettings.h"

#include "Config.h"
#include "DconfTerminalSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QXmlStreamReader>

namespace
{
QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

TerminalProfile start(const QString &source, const QString &name, const char *fg, const char *bg,
                      const std::array<const char *, 16> &palette)
{
    TerminalProfile p;
    p.source = source;
    p.name = name;
    TerminalAppearance &a = p.appearance;
    a = TerminalAppearance::fallback();
    a.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    a.foreground = QColor::fromString(QLatin1String(fg));
    a.background = QColor::fromString(QLatin1String(bg));
    for (size_t i = 0; i < 16; ++i) {
        a.palette[i] = QColor::fromString(QLatin1String(palette[i]));
    }
    a.cursor = a.foreground;
    a.boldIsBright = false;
    return p;
}

void finish(TerminalProfile &p, const QString &command)
{
    TerminalAppearance &a = p.appearance;
    a.font.setStyleHint(QFont::Monospace);
    a.font.setFixedPitch(true);
    a.font.setKerning(false);
    const QColor &f = a.foreground;
    const QColor &b = a.background;
    a.selection = QColor::fromRgbF(float(b.redF() * 0.7 + f.redF() * 0.3), float(b.greenF() * 0.7 + f.greenF() * 0.3),
                                   float(b.blueF() * 0.7 + f.blueF() * 0.3));
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &arg : QProcess::splitCommand(command)) {
        p.shell.append(expandVariables(arg, env));
    }
    if (p.shell.isEmpty() || p.shell.first().isEmpty()) {
        p.shell = {loginShell()};
    }
}

// "ColorBoldIsBright" → "color-bold-is-bright"
QString kebab(const QString &camel)
{
    QString out;
    for (const QChar c : camel) {
        if (c.isUpper() && !out.isEmpty()) {
            out += QLatin1Char('-');
        }
        out += c.toLower();
    }
    return out;
}

constexpr std::array<const char *, 16> kXfcePalette{"#000000", "#aa0000", "#00aa00", "#aa5500", "#0000aa", "#aa00aa", "#00aaaa", "#aaaaaa",
                                                    "#555555", "#ff5555", "#55ff55", "#ffff55", "#5555ff", "#ff55ff", "#55ffff", "#ffffff"};
// COSMIC Terminal's built-in schemes, which live in its binary.
constexpr std::array<const char *, 16> kCosmicDark{"#1b1b1b", "#f16161", "#7cb987", "#ddc74c", "#6296be", "#be6dee", "#49bac8", "#bebebe",
                                                   "#808080", "#ff8985", "#97d5a0", "#fae365", "#7db4e1", "#d48cff", "#63d3e3", "#e3e3e3"};
constexpr std::array<const char *, 16> kCosmicLight{"#292929", "#a52e2e", "#1d7c3b", "#8a6c00", "#2e5f99", "#8e3aab", "#0b7a8c", "#b0b0b0",
                                                    "#5c5c5c", "#c43c3c", "#2a9850", "#a88400", "#3a73b8", "#a84ec8", "#1192a6", "#ffffff"};
} // namespace

// ───────────────────────── xfce4-terminal ─────────────────────────

XfceTerminalSettings::Locations XfceTerminalSettings::Locations::system()
{
    Query query = [] {
        if (QStandardPaths::findExecutable(QStringLiteral("xfconf-query")).isEmpty()) {
            return QByteArray();
        }
        QProcess p;
        p.start(QStringLiteral("xfconf-query"), {QStringLiteral("-c"), QStringLiteral("xfce4-terminal"), QStringLiteral("-lv")});
        if (!p.waitForFinished(2000) || p.exitCode() != 0) {
            p.kill();
            return QByteArray();
        }
        return p.readAllStandardOutput();
    };
    return {query, QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
            QStandardPaths::findExecutable(QStringLiteral("xfce4-terminal"))};
}

QString XfceTerminalSettings::xmlFile() const
{
    return m_loc.configDir + QStringLiteral("/xfce4/xfconf/xfce-perchannel-xml/xfce4-terminal.xml");
}

QString XfceTerminalSettings::rcFile() const
{
    return m_loc.configDir + QStringLiteral("/xfce4/terminal/terminalrc");
}

QHash<QString, QString> XfceTerminalSettings::properties() const
{
    QHash<QString, QString> out;
    // Live values first: "/color-background   #1e1e1e"
    if (const QByteArray listed = m_loc.query ? m_loc.query() : QByteArray(); !listed.isEmpty()) {
        static const QRegularExpression line(QStringLiteral(R"re(^/(\S+)\s+(.*)$)re"));
        for (const QString &l : QString::fromUtf8(listed).split(QLatin1Char('\n'))) {
            if (const QRegularExpressionMatch m = line.match(l.trimmed()); m.hasMatch()) {
                out.insert(m.captured(1), m.captured(2).trimmed());
            }
        }
        return out;
    }
    if (const QByteArray xml = readFile(xmlFile()); !xml.isEmpty()) {
        QXmlStreamReader r(xml);
        while (!r.atEnd() && !r.hasError()) {
            r.readNext();
            if (r.isStartElement() && r.name() == QLatin1String("property")) {
                out.insert(r.attributes().value(QLatin1String("name")).toString(), r.attributes().value(QLatin1String("value")).toString());
            }
        }
        if (!out.isEmpty()) {
            return out;
        }
    }
    const IniGroups rc = readIni(rcFile());
    const QHash<QString, QString> config = rc.value(QStringLiteral("Configuration"));
    for (auto it = config.cbegin(); it != config.cend(); ++it) {
        out.insert(kebab(it.key()), it.value());
    }
    return out;
}

bool XfceTerminalSettings::detect() const
{
    return !m_loc.executable.isEmpty() || QFileInfo::exists(xmlFile()) || QFileInfo::exists(rcFile());
}

std::optional<TerminalProfile> XfceTerminalSettings::load(const QString &profile) const
{
    if (!profile.isEmpty() && profile != defaultProfile()) {
        return std::nullopt;
    }
    QHash<QString, QString> props = properties();
    // terminalrc writes TRUE and FALSE.
    for (QString &v : props) {
        if (v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || v.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
            v = v.toLower();
        }
    }
    TerminalProfile p = start(id(), QStringLiteral("Default"), "#ffffff", "#000000", kXfcePalette);
    TerminalAppearance &a = p.appearance;
    a.font = DconfSource::pangoFont(QStringLiteral("Monospace 12"));
    if (props.value(QStringLiteral("font-use-system")) != QLatin1String("true") && props.contains(QStringLiteral("font-name"))) {
        a.font = DconfSource::pangoFont(props.value(QStringLiteral("font-name")));
    } else if (props.value(QStringLiteral("font-use-system")) == QLatin1String("true")) {
        a.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    auto color = [&props](const QString &key, QColor &into) {
        if (auto c = DconfSource::gColor(props.value(key))) {
            into = *c;
            return true;
        }
        return false;
    };
    color(QStringLiteral("color-foreground"), a.foreground);
    color(QStringLiteral("color-background"), a.background);
    a.cursor = a.foreground;
    if (props.value(QStringLiteral("color-cursor-use-default")) != QLatin1String("true")) {
        color(QStringLiteral("color-cursor"), a.cursor);
    }
    const QStringList palette = props.value(QStringLiteral("color-palette")).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (int i = 0; i < std::min<int>(16, int(palette.size())); ++i) {
        if (auto c = DconfSource::gColor(palette.at(i))) {
            a.palette[size_t(i)] = *c;
        }
    }
    const QString shape = props.value(QStringLiteral("misc-cursor-shape"));
    a.cursorShape = shape.endsWith(QLatin1String("IBEAM")) ? TerminalAppearance::Bar
        : shape.endsWith(QLatin1String("UNDERLINE"))       ? TerminalAppearance::Underline
                                                           : TerminalAppearance::Block;
    a.cursorBlink = props.value(QStringLiteral("misc-cursor-blinks")) == QLatin1String("true");
    a.boldIsBright = props.value(QStringLiteral("color-bold-is-bright"), QStringLiteral("true")) == QLatin1String("true");
    if (props.value(QStringLiteral("scrolling-unlimited")) == QLatin1String("true")) {
        p.scrollback = -1;
    } else if (props.contains(QStringLiteral("scrolling-lines"))) {
        p.scrollback = std::max(0, props.value(QStringLiteral("scrolling-lines")).toInt());
    }
    finish(p, QString());
    return p;
}

QStringList XfceTerminalSettings::watchedPaths(const QString &profile) const
{
    Q_UNUSED(profile)
    return {xmlFile(), QFileInfo(xmlFile()).absolutePath(), rcFile(), QFileInfo(rcFile()).absolutePath()};
}

// ───────────────────────── RON ─────────────────────────

namespace
{
class Ron
{
public:
    explicit Ron(const QByteArray &in)
        : m_in(in)
    {
    }
    QVariant value()
    {
        skip();
        const char c = peek();
        if (c == '"') {
            return string();
        }
        if (c == '(') {
            return group(')');
        }
        if (c == '[') {
            return group(']');
        }
        if (c == '{') {
            return map();
        }
        // A number, bool, identifier, or a named struct/tuple: Name(…).
        const QString word = token();
        skip();
        if (peek() == '(') {
            const QVariant inner = group(')');
            if (word == QLatin1String("Some")) {
                const QVariantList l = inner.toList();
                return l.size() == 1 ? l.first() : inner;
            }
            // Keep "ProfileId(0)" readable as a map key.
            if (inner.typeId() == QMetaType::QVariantList) {
                QStringList parts;
                for (const QVariant &v : inner.toList()) {
                    parts.append(v.toString());
                }
                return QStringLiteral("%1(%2)").arg(word, parts.join(QStringLiteral(", ")));
            }
            return inner;
        }
        if (word == QLatin1String("None") || word.isEmpty()) {
            return {};
        }
        if (word == QLatin1String("true") || word == QLatin1String("false")) {
            return word == QLatin1String("true");
        }
        bool ok = false;
        const double n = word.toDouble(&ok);
        return ok ? QVariant(n) : QVariant(word);
    }

private:
    char peek() const { return m_pos < m_in.size() ? m_in.at(m_pos) : '\0'; }
    void skip()
    {
        while (m_pos < m_in.size()) {
            const char c = m_in.at(m_pos);
            if (c == '/' && m_pos + 1 < m_in.size() && m_in.at(m_pos + 1) == '/') {
                while (m_pos < m_in.size() && m_in.at(m_pos) != '\n') {
                    ++m_pos;
                }
            } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            } else {
                break;
            }
        }
    }
    QString string()
    {
        ++m_pos;
        QByteArray out;
        while (m_pos < m_in.size() && m_in.at(m_pos) != '"') {
            if (m_in.at(m_pos) == '\\' && m_pos + 1 < m_in.size()) {
                ++m_pos;
            }
            out += m_in.at(m_pos++);
        }
        ++m_pos;
        return QString::fromUtf8(out);
    }
    QString token()
    {
        const qsizetype start = m_pos;
        while (m_pos < m_in.size() && QByteArrayView("(){}[],: \t\r\n\"").indexOf(m_in.at(m_pos)) < 0) {
            ++m_pos;
        }
        return QString::fromUtf8(m_in.mid(start, m_pos - start));
    }
    // ( a: 1, b: 2 ) is a struct; ( 1, 2 ) and [ … ] are lists.
    QVariant group(char close)
    {
        ++m_pos;
        QVariantMap fields;
        QVariantList items;
        while (true) {
            skip();
            if (peek() == close || peek() == '\0') {
                ++m_pos;
                break;
            }
            const qsizetype save = m_pos;
            const QString name = close == ')' ? token() : QString();
            skip();
            if (!name.isEmpty() && peek() == ':') {
                ++m_pos;
                fields.insert(name, value());
            } else {
                m_pos = save;
                items.append(value());
            }
            skip();
            if (peek() == ',') {
                ++m_pos;
            }
        }
        if (!fields.isEmpty()) {
            return fields;
        }
        return items;
    }
    QVariant map()
    {
        ++m_pos;
        QVariantMap out;
        while (true) {
            skip();
            if (peek() == '}' || peek() == '\0') {
                ++m_pos;
                break;
            }
            const QString key = value().toString();
            skip();
            if (peek() == ':') {
                ++m_pos;
            }
            out.insert(key, value());
            skip();
            if (peek() == ',') {
                ++m_pos;
            }
        }
        return out;
    }

    const QByteArray &m_in;
    qsizetype m_pos = 0;
};
} // namespace

QVariant CosmicTerminalSettings::parseRon(const QByteArray &ron)
{
    return Ron(ron).value();
}

// ───────────────────────── COSMIC Terminal ─────────────────────────

CosmicTerminalSettings::Locations CosmicTerminalSettings::Locations::system()
{
    return {QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
            QStandardPaths::findExecutable(QStringLiteral("cosmic-term"))};
}

QString CosmicTerminalSettings::dir() const
{
    return m_loc.configDir + QStringLiteral("/cosmic/com.system76.CosmicTerm/v1");
}

QVariant CosmicTerminalSettings::key(const QString &name) const
{
    const QByteArray ron = readFile(dir() + QLatin1Char('/') + name);
    return ron.isEmpty() ? QVariant() : parseRon(ron);
}

bool CosmicTerminalSettings::detect() const
{
    return !m_loc.executable.isEmpty() || QFileInfo(dir()).isDir();
}

QList<TerminalSettingsSource::ProfileRef> CosmicTerminalSettings::profiles() const
{
    const QVariantMap all = key(QStringLiteral("profiles")).toMap();
    QList<ProfileRef> out;
    for (auto it = all.cbegin(); it != all.cend(); ++it) {
        const QString name = it.value().toMap().value(QStringLiteral("name")).toString();
        out.append({it.key(), name.isEmpty() ? it.key() : name});
    }
    if (out.isEmpty()) {
        out.append({QStringLiteral("default"), QStringLiteral("Default")});
    }
    return out;
}

QString CosmicTerminalSettings::defaultProfile() const
{
    const QString def = key(QStringLiteral("default_profile")).toString();
    return def.isEmpty() ? profiles().first().id : def;
}

std::optional<TerminalProfile> CosmicTerminalSettings::load(const QString &profile) const
{
    const QString pid = profile.isEmpty() ? defaultProfile() : profile;
    const QVariantMap profiles = key(QStringLiteral("profiles")).toMap();
    if (!profiles.isEmpty() && !profiles.contains(pid) && !profile.isEmpty()) {
        return std::nullopt;
    }
    const QVariantMap prof = profiles.value(pid).toMap();
    const QString appTheme = key(QStringLiteral("app_theme")).toString();
    // System follows the desktop; COSMIC's own default is dark.
    const bool dark = appTheme != QLatin1String("Light");
    const QString variant = dark ? QStringLiteral("dark") : QStringLiteral("light");
    QString scheme = prof.value(QStringLiteral("syntax_theme_") + variant).toString();
    if (scheme.isEmpty()) {
        scheme = key(QStringLiteral("syntax_theme_") + variant).toString();
    }
    const QString name = prof.value(QStringLiteral("name"), QStringLiteral("Default")).toString();
    TerminalProfile p = dark ? start(id(), name, "#c4c4c4", "#1b1b1b", kCosmicDark) : start(id(), name, "#292929", "#fbfbfb", kCosmicLight);
    TerminalAppearance &a = p.appearance;
    // A custom scheme with that name replaces the built-in colors.
    const QVariantMap custom = key(QStringLiteral("color_schemes_") + variant).toMap();
    for (const QVariant &v : custom) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("name")).toString() != scheme) {
            continue;
        }
        auto set = [](const QVariant &value, QColor &into) {
            if (const QColor c = QColor::fromString(value.toString()); c.isValid()) {
                into = c;
            }
        };
        set(s.value(QStringLiteral("foreground")), a.foreground);
        set(s.value(QStringLiteral("background")), a.background);
        a.cursor = a.foreground;
        set(s.value(QStringLiteral("cursor")), a.cursor);
        static const char *const names[] = {"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"};
        for (size_t i = 0; i < 8; ++i) {
            set(s.value(QStringLiteral("normal")).toMap().value(QLatin1String(names[i])), a.palette[i]);
            set(s.value(QStringLiteral("bright")).toMap().value(QLatin1String(names[i])), a.palette[i + 8]);
        }
    }
    const QString family = key(QStringLiteral("font_name")).toString();
    a.font.setFamily(family.isEmpty() ? QStringLiteral("Fira Mono") : family);
    const double size = key(QStringLiteral("font_size")).toDouble();
    a.font.setPointSizeF(size > 0 ? size : 14);
    finish(p, prof.value(QStringLiteral("command")).toString());
    return p;
}

QStringList CosmicTerminalSettings::watchedPaths(const QString &profile) const
{
    Q_UNUSED(profile)
    QStringList out{dir()};
    for (const QString &f : QDir(dir()).entryList(QDir::Files)) {
        out.append(dir() + QLatin1Char('/') + f);
    }
    return out;
}
