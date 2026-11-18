#include "MacTerminalSettings.h"

#include "Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace
{
QVariant plistValue(QXmlStreamReader &r);

QVariant plistContainer(QXmlStreamReader &r, bool dict)
{
    QVariantMap map;
    QVariantList list;
    QString key;
    while (r.readNextStartElement()) {
        if (dict && r.name() == QLatin1String("key")) {
            key = r.readElementText();
            continue;
        }
        const QVariant v = plistValue(r);
        if (dict) {
            map.insert(key, v);
        } else {
            list.append(v);
        }
    }
    return dict ? QVariant(map) : QVariant(list);
}

QVariant plistValue(QXmlStreamReader &r)
{
    const QStringView name = r.name();
    if (name == QLatin1String("dict")) {
        return plistContainer(r, true);
    }
    if (name == QLatin1String("array")) {
        return plistContainer(r, false);
    }
    if (name == QLatin1String("true") || name == QLatin1String("false")) {
        const bool value = name == QLatin1String("true");
        r.skipCurrentElement();
        return value;
    }
    const QString text = r.readElementText();
    if (name == QLatin1String("integer")) {
        return text.toLongLong();
    }
    if (name == QLatin1String("real")) {
        return text.toDouble();
    }
    return text; // string, date, data (base64)
}

QByteArray readPlist(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray data = f.readAll();
    if (data.startsWith("bplist")) {
        // Binary plists: macOS's plutil writes them out as XML.
        QProcess p;
        p.start(QStringLiteral("plutil"),
                {QStringLiteral("-convert"), QStringLiteral("xml1"), QStringLiteral("-o"), QStringLiteral("-"), path});
        data = p.waitForFinished(3000) ? p.readAllStandardOutput() : QByteArray();
    }
    return data;
}

// {"Red Component": 0.1, …} in 0–1 floats.
std::optional<QColor> plistColor(const QVariant &v)
{
    const QVariantMap m = v.toMap();
    if (!m.contains(QStringLiteral("Red Component"))) {
        return std::nullopt;
    }
    return QColor::fromRgbF(float(m.value(QStringLiteral("Red Component")).toDouble()),
                            float(m.value(QStringLiteral("Green Component")).toDouble()),
                            float(m.value(QStringLiteral("Blue Component")).toDouble()));
}

// iTerm2's defaults for a new profile ("Dark Background").
constexpr std::array<const char *, 16> kITermPalette{"#000000", "#c91b00", "#00c200", "#c7c400", "#0225c7", "#c930c7",
                                                     "#00c5c7", "#c7c7c7", "#686868", "#ff6e67", "#5ffa68", "#fffc67",
                                                     "#6871ff", "#ff77ff", "#60fdff", "#ffffff"};
} // namespace

ITerm2Settings::Locations ITerm2Settings::Locations::system()
{
    const QString home = QDir::homePath();
    const QString app
        = QFileInfo::exists(QStringLiteral("/Applications/iTerm.app")) ? QStringLiteral("/Applications/iTerm.app") : QString();
    return {home + QStringLiteral("/Library/Preferences/com.googlecode.iterm2.plist"),
            home + QStringLiteral("/Library/Application Support/iTerm2/DynamicProfiles"), app};
}

QVariant ITerm2Settings::parsePlist(const QByteArray &xml)
{
    QXmlStreamReader r(xml);
    while (r.readNextStartElement()) {
        if (r.name() == QLatin1String("plist")) {
            if (r.readNextStartElement()) {
                return plistValue(r);
            }
            return {};
        }
    }
    return {};
}

QFont ITerm2Settings::fontFromName(const QString &iterm)
{
    QFont f;
    const qsizetype space = iterm.lastIndexOf(QLatin1Char(' '));
    bool ok = false;
    const double size = space < 0 ? 0 : iterm.mid(space + 1).toDouble(&ok);
    QString name = ok ? iterm.left(space) : iterm;
    // A PostScript name: MesloLGS-NF-Regular → MesloLGS NF.
    static const QRegularExpression style(QStringLiteral(R"re(-(Regular|Medium|Light|Bold|Italic|Book|Roman|Retina)$)re"));
    name.remove(style);
    name.replace(QLatin1Char('-'), QLatin1Char(' '));
    f.setFamily(name);
    if (ok && size > 0) {
        f.setPointSizeF(size);
    }
    return f;
}

QVariantList ITerm2Settings::allProfiles() const
{
    QVariantList out = parsePlist(readPlist(m_loc.preferences)).toMap().value(QStringLiteral("New Bookmarks")).toList();
    const QDir dynamic(m_loc.dynamicProfiles);
    for (const QString &file : dynamic.entryList(QDir::Files, QDir::Name)) {
        QFile f(dynamic.filePath(file));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        for (const QJsonValue &p : QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("Profiles")).toArray()) {
            out.append(p.toObject().toVariantMap());
        }
    }
    return out;
}

bool ITerm2Settings::detect() const
{
    return !m_loc.app.isEmpty() || QFileInfo::exists(m_loc.preferences);
}

QList<TerminalSettingsSource::ProfileRef> ITerm2Settings::profiles() const
{
    QList<ProfileRef> out;
    for (const QVariant &v : allProfiles()) {
        const QVariantMap p = v.toMap();
        out.append({p.value(QStringLiteral("Guid")).toString(), p.value(QStringLiteral("Name")).toString()});
    }
    return out;
}

QString ITerm2Settings::defaultProfile() const
{
    const QString guid = parsePlist(readPlist(m_loc.preferences)).toMap().value(QStringLiteral("Default Bookmark Guid")).toString();
    if (!guid.isEmpty()) {
        return guid;
    }
    const QList<ProfileRef> all = profiles();
    return all.isEmpty() ? QString() : all.first().id;
}

std::optional<TerminalProfile> ITerm2Settings::load(const QString &profile) const
{
    const QString guid = profile.isEmpty() ? defaultProfile() : profile;
    const QVariantList all = allProfiles();
    QVariantMap p;
    for (const QVariant &v : all) {
        if (v.toMap().value(QStringLiteral("Guid")).toString() == guid) {
            p = v.toMap();
        }
    }
    if (p.isEmpty() && !all.isEmpty()) {
        return std::nullopt;
    }
    // A dynamic profile inherits from its parent by name.
    if (const QString parent = p.value(QStringLiteral("Dynamic Profile Parent Name")).toString(); !parent.isEmpty()) {
        for (const QVariant &v : all) {
            if (v.toMap().value(QStringLiteral("Name")).toString() == parent) {
                QVariantMap merged = v.toMap();
                for (auto it = p.cbegin(); it != p.cend(); ++it) {
                    merged.insert(it.key(), it.value());
                }
                p = merged;
            }
        }
    }
    TerminalProfile out;
    out.source = id();
    out.name = p.value(QStringLiteral("Name"), QStringLiteral("Default")).toString();
    TerminalAppearance &a = out.appearance;
    a = TerminalAppearance::fallback();
    a.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (const QString font = p.value(QStringLiteral("Normal Font")).toString(); !font.isEmpty()) {
        a.font = fontFromName(font);
    }
    a.font.setStyleHint(QFont::Monospace);
    a.font.setFixedPitch(true);
    a.font.setKerning(false);
    a.foreground = QColor(0xc7, 0xc7, 0xc7);
    a.background = QColor(Qt::black);
    for (size_t i = 0; i < 16; ++i) {
        a.palette[i] = QColor::fromString(QLatin1String(kITermPalette[i]));
        if (auto c = plistColor(p.value(QStringLiteral("Ansi %1 Color").arg(i)))) {
            a.palette[i] = *c;
        }
    }
    a.foreground = plistColor(p.value(QStringLiteral("Foreground Color"))).value_or(a.foreground);
    a.background = plistColor(p.value(QStringLiteral("Background Color"))).value_or(a.background);
    a.cursor = plistColor(p.value(QStringLiteral("Cursor Color"))).value_or(a.foreground);
    a.selection = plistColor(p.value(QStringLiteral("Selection Color"))).value_or(QColor(0xb5, 0xd5, 0xff));
    switch (p.value(QStringLiteral("Cursor Type"), 2).toInt()) {
    case 0:
        a.cursorShape = TerminalAppearance::Underline;
        break;
    case 1:
        a.cursorShape = TerminalAppearance::Bar;
        break;
    default:
        a.cursorShape = TerminalAppearance::Block;
    }
    a.cursorBlink = p.value(QStringLiteral("Blinking Cursor")).toBool();
    a.boldIsBright = p.value(QStringLiteral("Use Bright Bold"), true).toBool();
    if (p.value(QStringLiteral("Unlimited Scrollback")).toBool()) {
        out.scrollback = -1;
    } else if (p.contains(QStringLiteral("Scrollback Lines"))) {
        out.scrollback = std::max(0, p.value(QStringLiteral("Scrollback Lines")).toInt());
    }
    if (p.value(QStringLiteral("Custom Command")).toString() == QLatin1String("Yes")) {
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString &arg : QProcess::splitCommand(p.value(QStringLiteral("Command")).toString())) {
            out.shell.append(expandVariables(arg, env));
        }
    }
    if (out.shell.isEmpty()) {
        out.shell = {loginShell()};
    }
    return out;
}

QStringList ITerm2Settings::watchedPaths(const QString &profile) const
{
    Q_UNUSED(profile)
    return {m_loc.preferences, QFileInfo(m_loc.preferences).absolutePath(), m_loc.dynamicProfiles};
}
