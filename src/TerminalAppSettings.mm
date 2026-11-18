// Terminal.app's profiles ("Window Settings" in com.apple.Terminal). Colors
// and fonts are NSKeyedArchiver blobs of NSColor and NSFont, so this one is
// Objective-C++ and macOS only.

#include "Config.h"
#include "MacTerminalSettings.h"

#include <QDir>
#include <QFontDatabase>
#include <QProcess>
#include <QProcessEnvironment>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace
{
NSDictionary *preferences()
{
    return [[NSUserDefaults standardUserDefaults] persistentDomainForName:@"com.apple.Terminal"];
}

NSDictionary *windowSettings()
{
    id all = preferences()[@"Window Settings"];
    return [all isKindOfClass:[NSDictionary class]] ? all : nil;
}

std::optional<QColor> archivedColor(id data)
{
    if (![data isKindOfClass:[NSData class]]) {
        return std::nullopt;
    }
    NSError *error = nil;
    NSColor *color = [NSKeyedUnarchiver unarchivedObjectOfClass:[NSColor class] fromData:data error:&error];
    NSColor *rgb = [color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!rgb) {
        return std::nullopt;
    }
    return QColor::fromRgbF(float(rgb.redComponent), float(rgb.greenComponent), float(rgb.blueComponent));
}

// Terminal.app's "Basic" profile colors.
constexpr std::array<const char *, 16> kTerminalPalette{"#000000", "#990000", "#00a600", "#999900", "#0000b2", "#b200b2",
                                                       "#00a6b2", "#bfbfbf", "#666666", "#e50000", "#00d900", "#e5e500",
                                                       "#0000ff", "#e500e5", "#00e5e5", "#e5e5e5"};
NSString *const kAnsiKeys[16] = {@"ANSIBlackColor",       @"ANSIRedColor",          @"ANSIGreenColor",       @"ANSIYellowColor",
                                 @"ANSIBlueColor",        @"ANSIMagentaColor",      @"ANSICyanColor",        @"ANSIWhiteColor",
                                 @"ANSIBrightBlackColor", @"ANSIBrightRedColor",    @"ANSIBrightGreenColor", @"ANSIBrightYellowColor",
                                 @"ANSIBrightBlueColor",  @"ANSIBrightMagentaColor", @"ANSIBrightCyanColor",  @"ANSIBrightWhiteColor"};
} // namespace

bool TerminalAppSettings::detect() const
{
    return windowSettings() != nil || [[NSFileManager defaultManager] fileExistsAtPath:@"/System/Applications/Utilities/Terminal.app"];
}

QList<TerminalSettingsSource::ProfileRef> TerminalAppSettings::profiles() const
{
    QList<ProfileRef> out;
    for (NSString *name in windowSettings()) {
        out.append({QString::fromNSString(name), QString::fromNSString(name)});
    }
    std::sort(out.begin(), out.end(), [](const ProfileRef &a, const ProfileRef &b) { return a.name < b.name; });
    return out;
}

QString TerminalAppSettings::defaultProfile() const
{
    id name = preferences()[@"Default Window Settings"];
    return [name isKindOfClass:[NSString class]] ? QString::fromNSString(name) : QStringLiteral("Basic");
}

std::optional<TerminalProfile> TerminalAppSettings::load(const QString &profile) const
{
    const QString name = profile.isEmpty() ? defaultProfile() : profile;
    NSDictionary *settings = windowSettings()[name.toNSString()];
    if (!settings && !profile.isEmpty() && windowSettings()) {
        return std::nullopt;
    }
    TerminalProfile p;
    p.source = id();
    p.name = name;
    TerminalAppearance &a = p.appearance;
    a = TerminalAppearance::fallback();
    a.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (id fontData = settings[@"Font"]; [fontData isKindOfClass:[NSData class]]) {
        NSError *error = nil;
        NSFont *font = [NSKeyedUnarchiver unarchivedObjectOfClass:[NSFont class] fromData:fontData error:&error];
        if (font) {
            a.font = QFont(QString::fromNSString(font.familyName));
            a.font.setPointSizeF(font.pointSize);
        }
    }
    a.font.setStyleHint(QFont::Monospace);
    a.font.setFixedPitch(true);
    a.font.setKerning(false);
    a.foreground = archivedColor(settings[@"TextColor"]).value_or(QColor(Qt::black));
    a.background = archivedColor(settings[@"BackgroundColor"]).value_or(QColor(Qt::white));
    a.cursor = archivedColor(settings[@"CursorColor"]).value_or(a.foreground);
    a.selection = archivedColor(settings[@"SelectionColor"]).value_or(QColor(0xb5, 0xd5, 0xff));
    for (int i = 0; i < 16; ++i) {
        a.palette[size_t(i)] = archivedColor(settings[kAnsiKeys[i]]).value_or(QColor::fromString(QLatin1String(kTerminalPalette[size_t(i)])));
    }
    const int cursor = [settings[@"CursorType"] intValue];
    a.cursorShape = cursor == 1 ? TerminalAppearance::Underline : cursor == 2 ? TerminalAppearance::Bar : TerminalAppearance::Block;
    a.cursorBlink = [settings[@"CursorBlink"] boolValue];
    a.boldIsBright = [settings[@"UseBrightBold"] boolValue];
    if (settings[@"ShouldLimitScrollback"] && [settings[@"ShouldLimitScrollback"] intValue] == 0) {
        p.scrollback = -1;
    } else if (settings[@"ScrollbackLines"]) {
        p.scrollback = std::max(0, [settings[@"ScrollbackLines"] intValue]);
    }
    if (id command = settings[@"CommandString"]; [command isKindOfClass:[NSString class]] && [settings[@"RunCommandAsShell"] boolValue]) {
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString &arg : QProcess::splitCommand(QString::fromNSString(command))) {
            p.shell.append(expandVariables(arg, env));
        }
    }
    if (p.shell.isEmpty()) {
        p.shell = {loginShell()};
    }
    return p;
}

QStringList TerminalAppSettings::watchedPaths(const QString &profile) const
{
    Q_UNUSED(profile)
    const QString prefs = QDir::homePath() + QStringLiteral("/Library/Preferences");
    return {prefs + QStringLiteral("/com.apple.Terminal.plist"), prefs};
}
