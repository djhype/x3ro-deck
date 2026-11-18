#pragma once

#include "TerminalSettingsSource.h"

#include <QVariant>

// iTerm2: ~/Library/Preferences/com.googlecode.iterm2.plist ("New Bookmarks",
// "Default Bookmark Guid") plus Application Support/iTerm2/DynamicProfiles/
// *.json. Binary plists go through `plutil -convert xml1` first; the XML
// reader itself is portable, so fixtures run everywhere.
class ITerm2Settings : public TerminalSettingsSource
{
public:
    struct Locations
    {
        QString preferences; // the plist file
        QString dynamicProfiles; // the DynamicProfiles directory
        QString app; // iTerm.app, empty when not installed
        static Locations system();
    };
    explicit ITerm2Settings(Locations locations = Locations::system())
        : m_loc(std::move(locations))
    {
    }
    QString id() const override { return QStringLiteral("iterm2"); }
    QString displayName() const override { return QStringLiteral("iTerm2"); }
    QStringList desktopEntries() const override { return {QStringLiteral("com.googlecode.iterm2"), QStringLiteral("iterm2")}; }
    bool detect() const override;
    QList<ProfileRef> profiles() const override;
    QString defaultProfile() const override;
    std::optional<TerminalProfile> load(const QString &profile) const override;
    QStringList watchedPaths(const QString &profile) const override;

    // An XML property list as QVariant: dict → QVariantMap, array → QVariantList.
    static QVariant parsePlist(const QByteArray &xml);
    // "MesloLGS-NF-Regular 13" → family and point size.
    static QFont fontFromName(const QString &iterm);

private:
    QVariantList allProfiles() const;
    Locations m_loc;
};

#if defined(Q_OS_MACOS)
// Terminal.app: com.apple.Terminal's "Window Settings", whose colors and
// fonts are archived NSColor/NSFont objects (decoded in Objective-C++).
class TerminalAppSettings : public TerminalSettingsSource
{
public:
    QString id() const override { return QStringLiteral("terminal-app"); }
    QString displayName() const override { return QStringLiteral("Terminal"); }
    QStringList desktopEntries() const override { return {QStringLiteral("com.apple.terminal")}; }
    bool detect() const override;
    QList<ProfileRef> profiles() const override;
    QString defaultProfile() const override;
    std::optional<TerminalProfile> load(const QString &profile) const override;
    QStringList watchedPaths(const QString &profile) const override;
};
#endif
