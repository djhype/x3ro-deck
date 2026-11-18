#include "WindowsTerminalSettings.h"

#include "Config.h"
#include "DetectorUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>

namespace
{
const char *const kSchemeKeys[16]
    = {"black",       "red",       "green",       "yellow",       "blue",       "purple",       "cyan",       "white",
       "brightBlack", "brightRed", "brightGreen", "brightYellow", "brightBlue", "brightPurple", "brightCyan", "brightWhite"};

// Campbell, Windows Terminal's default scheme (compiled into it).
const QVariantMap &campbell()
{
    static const QVariantMap scheme{{QStringLiteral("name"), QStringLiteral("Campbell")},
                                    {QStringLiteral("foreground"), QStringLiteral("#CCCCCC")},
                                    {QStringLiteral("background"), QStringLiteral("#0C0C0C")},
                                    {QStringLiteral("cursorColor"), QStringLiteral("#FFFFFF")},
                                    {QStringLiteral("selectionBackground"), QStringLiteral("#FFFFFF")},
                                    {QStringLiteral("black"), QStringLiteral("#0C0C0C")},
                                    {QStringLiteral("red"), QStringLiteral("#C50F1F")},
                                    {QStringLiteral("green"), QStringLiteral("#13A10E")},
                                    {QStringLiteral("yellow"), QStringLiteral("#C19C00")},
                                    {QStringLiteral("blue"), QStringLiteral("#0037DA")},
                                    {QStringLiteral("purple"), QStringLiteral("#881798")},
                                    {QStringLiteral("cyan"), QStringLiteral("#3A96DD")},
                                    {QStringLiteral("white"), QStringLiteral("#CCCCCC")},
                                    {QStringLiteral("brightBlack"), QStringLiteral("#767676")},
                                    {QStringLiteral("brightRed"), QStringLiteral("#E74856")},
                                    {QStringLiteral("brightGreen"), QStringLiteral("#16C60C")},
                                    {QStringLiteral("brightYellow"), QStringLiteral("#F9F1A5")},
                                    {QStringLiteral("brightBlue"), QStringLiteral("#3B78FF")},
                                    {QStringLiteral("brightPurple"), QStringLiteral("#B4009E")},
                                    {QStringLiteral("brightCyan"), QStringLiteral("#61D6D6")},
                                    {QStringLiteral("brightWhite"), QStringLiteral("#F2F2F2")}};
    return scheme;
}

QVariantList profileList(const QVariantMap &settings)
{
    const QVariant profiles = settings.value(QStringLiteral("profiles"));
    // Old files keep profiles as a bare array.
    return profiles.typeId() == QMetaType::QVariantList ? profiles.toList() : profiles.toMap().value(QStringLiteral("list")).toList();
}

void finish(TerminalProfile &p)
{
    TerminalAppearance &a = p.appearance;
    a.font.setStyleHint(QFont::Monospace);
    a.font.setFixedPitch(true);
    a.font.setKerning(false);
    if (p.shell.isEmpty() || p.shell.first().isEmpty()) {
        p.shell = {loginShell()};
    }
}
} // namespace

WindowsTerminalSettings::WindowsTerminalSettings(bool preview, QStringList files)
    : m_preview(preview)
    , m_files(std::move(files))
{
    if (m_files.isEmpty()) {
        const QString local = qEnvironmentVariable("LOCALAPPDATA");
        if (!local.isEmpty()) {
            const QString package = preview ? QStringLiteral("Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe")
                                            : QStringLiteral("Microsoft.WindowsTerminal_8wekyb3d8bbwe");
            m_files << local + QStringLiteral("/Packages/") + package + QStringLiteral("/LocalState/settings.json")
                    << local
                    + (preview ? QStringLiteral("/Microsoft/Windows Terminal Preview/settings.json")
                               : QStringLiteral("/Microsoft/Windows Terminal/settings.json"));
        }
    }
}

QString WindowsTerminalSettings::file() const
{
    for (const QString &f : m_files) {
        if (QFileInfo::exists(f)) {
            return f;
        }
    }
    return {};
}

QVariantMap WindowsTerminalSettings::settings() const
{
    const QString f = file();
    return f.isEmpty() ? QVariantMap()
                       : QJsonDocument::fromJson(DetectorUtil::stripJsonc(DetectorUtil::readFile(f))).object().toVariantMap();
}

bool WindowsTerminalSettings::detect() const
{
    return !file().isEmpty();
}

QList<TerminalSettingsSource::ProfileRef> WindowsTerminalSettings::profiles() const
{
    QList<ProfileRef> out;
    for (const QVariant &v : profileList(settings())) {
        const QVariantMap p = v.toMap();
        if (!p.value(QStringLiteral("hidden")).toBool()) {
            out.append({p.value(QStringLiteral("guid")).toString(), p.value(QStringLiteral("name")).toString()});
        }
    }
    return out;
}

QString WindowsTerminalSettings::defaultProfile() const
{
    const QString def = settings().value(QStringLiteral("defaultProfile")).toString();
    if (!def.isEmpty()) {
        return def;
    }
    const QList<ProfileRef> all = profiles();
    return all.isEmpty() ? QString() : all.first().id;
}

std::optional<TerminalProfile> WindowsTerminalSettings::load(const QString &profile) const
{
    const QVariantMap root = settings();
    const QString guid = profile.isEmpty() ? defaultProfile() : profile;
    QVariantMap merged = root.value(QStringLiteral("profiles")).toMap().value(QStringLiteral("defaults")).toMap();
    bool found = false;
    for (const QVariant &v : profileList(root)) {
        const QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("guid")).toString().compare(guid, Qt::CaseInsensitive) == 0) {
            for (auto it = p.cbegin(); it != p.cend(); ++it) {
                merged.insert(it.key(), it.value());
            }
            found = true;
        }
    }
    if (!found && !profile.isEmpty()) {
        return std::nullopt;
    }
    TerminalProfile out;
    out.source = id();
    out.name = merged.value(QStringLiteral("name"), QStringLiteral("Default")).toString();
    TerminalAppearance &a = out.appearance;
    a = TerminalAppearance::fallback();
    // "font": {"face", "size"}, or the older fontFace/fontSize.
    const QVariantMap font = merged.value(QStringLiteral("font")).toMap();
    a.font
        = QFont(font.value(QStringLiteral("face"), merged.value(QStringLiteral("fontFace"), QStringLiteral("Cascadia Mono"))).toString());
    a.font.setPointSizeF(font.value(QStringLiteral("size"), merged.value(QStringLiteral("fontSize"), 12)).toDouble());

    QVariantMap scheme = campbell();
    const QString schemeName = merged.value(QStringLiteral("colorScheme"), QStringLiteral("Campbell")).toString();
    for (const QVariant &v : root.value(QStringLiteral("schemes")).toList()) {
        if (v.toMap().value(QStringLiteral("name")).toString() == schemeName) {
            for (auto it = v.toMap().cbegin(); it != v.toMap().cend(); ++it) {
                scheme.insert(it.key(), it.value());
            }
        }
    }
    // A profile may override single colors.
    for (const char *key : {"foreground", "background", "cursorColor", "selectionBackground"}) {
        if (merged.contains(QLatin1String(key))) {
            scheme.insert(QLatin1String(key), merged.value(QLatin1String(key)));
        }
    }
    auto color = [&scheme](const char *key, const QColor &fallback) {
        const QColor c = QColor::fromString(scheme.value(QLatin1String(key)).toString());
        return c.isValid() ? c : fallback;
    };
    a.foreground = color("foreground", a.foreground);
    a.background = color("background", a.background);
    a.cursor = color("cursorColor", a.foreground);
    a.selection = color("selectionBackground", a.selection);
    for (size_t i = 0; i < 16; ++i) {
        a.palette[i] = color(kSchemeKeys[i], a.palette[i]);
    }
    const QString shape = merged.value(QStringLiteral("cursorShape"), QStringLiteral("bar")).toString();
    a.cursorShape = shape == QLatin1String("filledBox") || shape == QLatin1String("emptyBox") ? TerminalAppearance::Block
        : shape == QLatin1String("underscore") || shape == QLatin1String("doubleUnderscore") || shape == QLatin1String("vintage")
        ? TerminalAppearance::Underline
        : TerminalAppearance::Bar;
    if (merged.contains(QStringLiteral("historySize"))) {
        out.scrollback = std::max(0, merged.value(QStringLiteral("historySize")).toInt());
    }
    if (const QString cmd = merged.value(QStringLiteral("commandline")).toString(); !cmd.isEmpty()) {
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString &arg : QProcess::splitCommand(cmd)) {
            out.shell.append(expandVariables(arg, env));
        }
    }
    finish(out);
    return out;
}

QStringList WindowsTerminalSettings::watchedPaths(const QString &profile) const
{
    Q_UNUSED(profile)
    const QString f = file();
    return f.isEmpty() ? QStringList() : QStringList{f, QFileInfo(f).absolutePath()};
}

// ───────────────────────── Console Host ─────────────────────────

ConsoleHostSettings::ConsoleHostSettings(Reader read)
    : m_read(std::move(read))
{
    if (!m_read) {
        m_read = [] {
#if defined(Q_OS_WIN)
            QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Console"), QSettings::NativeFormat);
            QVariantMap out;
            for (const QString &key : reg.childKeys()) {
                out.insert(key, reg.value(key));
            }
            return out;
#else
            return QVariantMap();
#endif
        };
    }
}

bool ConsoleHostSettings::detect() const
{
    return !m_read().isEmpty();
}

std::optional<TerminalProfile> ConsoleHostSettings::load(const QString &profile) const
{
    if (!profile.isEmpty() && profile != defaultProfile()) {
        return std::nullopt;
    }
    const QVariantMap reg = m_read();
    TerminalProfile out;
    out.source = id();
    out.name = QStringLiteral("Default");
    TerminalAppearance &a = out.appearance;
    a = TerminalAppearance::fallback();
    a.font = QFont(reg.value(QStringLiteral("FaceName"), QStringLiteral("Consolas")).toString());
    // FontSize keeps the cell height in pixels in its high word.
    const uint size = reg.value(QStringLiteral("FontSize")).toUInt();
    a.font.setPixelSize(size >> 16 ? int(size >> 16) : 16);
    // ColorTableNN is 0x00BBGGRR.
    for (int i = 0; i < 16; ++i) {
        const QString key = QStringLiteral("ColorTable%1").arg(i, 2, 10, QLatin1Char('0'));
        if (reg.contains(key)) {
            const uint v = reg.value(key).toUInt();
            a.palette[size_t(i)] = QColor(int(v & 0xff), int((v >> 8) & 0xff), int((v >> 16) & 0xff));
        }
    }
    // ScreenColors: foreground index in the low nibble, background in the next.
    const uint screen = reg.value(QStringLiteral("ScreenColors"), 0x07).toUInt();
    a.foreground = a.palette[screen & 0xf];
    a.background = a.palette[(screen >> 4) & 0xf];
    a.cursor = a.foreground;
    const int cursor = reg.value(QStringLiteral("CursorSize"), 25).toInt();
    a.cursorShape = cursor >= 100 ? TerminalAppearance::Block : TerminalAppearance::Underline;
    finish(out);
    return out;
}

QStringList ConsoleHostSettings::watchedPaths(const QString &profile) const
{
    Q_UNUSED(profile)
    return {}; // the registry has no file to watch; changes apply on the next start
}
